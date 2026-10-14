/* Archtop: a sampled archtop guitar for MPC OS (mpc_engine, see mpc-vst-plugins wrapper/engine.h).
 *
 * Sound: Karoryfer's Shinyguitar (CC0), packed by tools/pack_samples.py into engine/archtop.smp:
 * each take is a stereo frame pair, L = magnetic pickup, R = microphone, so BLEND mixes them per sample.
 *
 * Memory: the essential sample set is stored losslessly compressed (~20 MB on disk) and decoded into RAM
 * on a loader thread when the plugin is first inserted (~70 MB, shared by every instance in the process).
 *
 * Playing model: six strings. Each note-on batch (a chord arrives in one block) is assigned to strings
 * (open strings, capo, drop tuning, a hand position that follows the music, or a fixed POSITION).
 * One sound per string: a new note on a string chokes the old one, or with LEGATO and the old key still
 * held, slurs into it (hammer-on / pull-off). STRUM spreads a chord low string to high.
 * ENGINE layers a plucked-string model (Karplus-Strong) on every note. SPACE is a small stereo reverb. */
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "engine.h"

#define SR 44100.0f
#define NSTR 6
#define MAX_V 32
#define MAX_RR 8
#define KS_LEN 2048
#define KS_MASK (KS_LEN - 1)
#define MAX_EV 256
#define PI_F 3.14159265f

enum { ZT_NOTE = 0, ZT_RELEASE = 1, ZT_NOISE = 2 };

/* ---------- the shared sample bank ---------- */
typedef struct { uint32_t off, frames; } ent_t;
typedef struct __attribute__((packed)) { uint8_t type, lo, hi, root, lv, hv, nrr, pad; uint16_t rr[MAX_RR]; } zone_t;

enum { BANK_NONE, BANK_LOADING, BANK_READY, BANK_FAILED };

typedef struct {
    int refs, state, stop;
    char path[1024];
    uint8_t *map; size_t maplen;
    uint32_t nsamp, nzone;
    const ent_t *ent; const zone_t *zone;
    int16_t *head; uint32_t *hoff, *hlen;     /* all samples, decoded */
    pthread_t loader; int have_loader, own_tables;
} bank_t;

static bank_t g_bank;
static pthread_mutex_t g_bank_mx = PTHREAD_MUTEX_INITIALIZER;

static int bank_state(void) { return __atomic_load_n(&g_bank.state, __ATOMIC_ACQUIRE); }



/* ---------- sample file (written by tools/pack_samples.py) ----------
 * header: "ATSP", u32 version (2), u32 samples, u32 zones, u32 data offset
 * then per sample { u32 byte offset into data, u32 frames }, then the zones (zone_t), then the data.
 * Each sample: u32 len0, u32 len1, then two bitstreams (pickup, mic). A stream is 1024-frame blocks: a header byte
 * (order << 5 | k), then one Rice code per frame (a run of q zeros, a 1, k low bits; 24 zeros = a raw 20-bit value)
 * of the zigzagged residual of a fixed order-0..3 predictor that runs across blocks. Lossless 16-bit. */
typedef struct { const uint8_t *p; size_t n, pos; uint64_t cache; int avail; } br_t;

static inline void br_fill(br_t *b) {
    while (b->avail <= 56) {
        uint64_t byte = b->pos < b->n ? b->p[b->pos] : 0;
        b->pos++;
        b->cache |= byte << (56 - b->avail);
        b->avail += 8;
    }
}
static inline uint32_t br_take(br_t *b, int k) {   /* k <= 32, after br_fill */
    if (!k) return 0;
    uint32_t v = (uint32_t)(b->cache >> (64 - k));
    b->cache <<= k; b->avail -= k;
    return v;
}

static int decode_channel(const uint8_t *src, size_t len, int16_t *dst, uint32_t frames) {
    br_t b = { src, len, 0, 0, 0 };
    int32_t h1 = 0, h2 = 0, h3 = 0;
    int order = 0, k = 0;
    for (uint32_t i = 0; i < frames; i++) {
        br_fill(&b);
        if ((i & 1023) == 0) {                      /* block header */
            uint32_t hd = br_take(&b, 8);
            order = (int)(hd >> 5); k = (int)(hd & 31);
            if (order > 3 || k > 20) return -1;
            br_fill(&b);
        }
        int z = b.cache ? __builtin_clzll(b.cache) : 64;
        uint32_t u;
        if (z >= 24) { br_take(&b, 24); u = br_take(&b, 20); }
        else { br_take(&b, z + 1); u = ((uint32_t)z << k) | br_take(&b, k); }
        int32_t r = (int32_t)(u >> 1) ^ -(int32_t)(u & 1);
        int32_t x;
        switch (order) {
        case 0: x = r; break;
        case 1: x = r + h1; break;
        case 2: x = r + 2 * h1 - h2; break;
        default: x = r + 3 * h1 - 3 * h2 + h3; break;
        }
        if (x < -32768 || x > 32767) return -1;
        dst[2 * (size_t)i] = (int16_t)x;
        h3 = h2; h2 = h1; h1 = x;
    }
    return b.pos <= len + 8 ? 0 : -1;
}

static void *loader_main(void *arg) {
    (void)arg;
    bank_t *b = &g_bank;
    FILE *f = fopen(b->path, "rb");
    long size = 0;
    if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) < 20 || fseek(f, 0, SEEK_SET)) goto fail;
    b->maplen = (size_t)size;
    b->map = malloc(b->maplen);
    if (!b->map || fread(b->map, 1, b->maplen, f) != b->maplen) goto fail;
    fclose(f); f = NULL;
    {
        const uint8_t *m = b->map;
        uint32_t ver, ns, nz, doff;
        if (memcmp(m, "ATSP", 4)) goto fail;
        memcpy(&ver, m + 4, 4); memcpy(&ns, m + 8, 4); memcpy(&nz, m + 12, 4); memcpy(&doff, m + 16, 4);
        size_t need = 20 + (size_t)ns * sizeof(ent_t) + (size_t)nz * sizeof(zone_t);
        if (ver != 2 || ns == 0 || ns > 65535 || nz == 0 || nz > 1024 || doff != need || doff > b->maplen) goto fail;
        b->nsamp = ns; b->nzone = nz;
        b->ent = (const ent_t *)(m + 20);
        b->zone = (const zone_t *)(m + 20 + ns * sizeof(ent_t));
        for (uint32_t z = 0; z < nz; z++)
            for (int k = 0; k < b->zone[z].nrr && k < MAX_RR; k++)
                if (b->zone[z].rr[k] >= ns) goto fail;
        size_t total = 0, dlen = b->maplen - doff;
        for (uint32_t i = 0; i < ns; i++) {
            if (b->ent[i].frames < 8 || b->ent[i].frames > 60 * 44100 || (size_t)b->ent[i].off + 8 > dlen) goto fail;
            total += b->ent[i].frames;
        }
        b->head = malloc(total * 4);
        b->hoff = malloc(ns * sizeof(uint32_t));
        b->hlen = malloc(ns * sizeof(uint32_t));
        if (!b->head || !b->hoff || !b->hlen) goto fail;
        size_t at = 0;
        for (uint32_t i = 0; i < ns && !b->stop; i++) {
            const uint8_t *src = m + doff + b->ent[i].off;
            uint32_t l0, l1;
            memcpy(&l0, src, 4); memcpy(&l1, src + 4, 4);
            if ((size_t)b->ent[i].off + 8 + (size_t)l0 + l1 > dlen) goto fail;
            int16_t *dst = b->head + at * 2;
            if (decode_channel(src + 8, l0, dst, b->ent[i].frames) ||
                decode_channel(src + 8 + l0, l1, dst + 1, b->ent[i].frames)) goto fail;
            b->hoff[i] = (uint32_t)at; b->hlen[i] = b->ent[i].frames;
            at += b->ent[i].frames;
        }
        if (b->stop) goto fail;
    }
    {   /* keep only the small tables and the decoded audio; drop the compressed file */
        ent_t *e = malloc(b->nsamp * sizeof(ent_t));
        zone_t *z = malloc(b->nzone * sizeof(zone_t));
        if (!e || !z) { free(e); free(z); goto fail; }
        memcpy(e, b->ent, b->nsamp * sizeof(ent_t)); memcpy(z, b->zone, b->nzone * sizeof(zone_t));
        b->ent = e; b->zone = z;
        free(b->map); b->map = NULL;
        b->own_tables = 1;
    }
    __atomic_store_n(&b->state, BANK_READY, __ATOMIC_RELEASE);
    return NULL;
fail:
    if (f) fclose(f);
    __atomic_store_n(&b->state, BANK_FAILED, __ATOMIC_RELEASE);
    return NULL;
}

static void bank_acquire(const char *data_dir) {
    pthread_mutex_lock(&g_bank_mx);
    if (g_bank.refs++ == 0) {
        memset(&g_bank, 0, sizeof g_bank);
        g_bank.refs = 1;
        snprintf(g_bank.path, sizeof g_bank.path, "%s/archtop.smp", data_dir ? data_dir : ".");
        g_bank.state = BANK_LOADING;
        g_bank.have_loader = !pthread_create(&g_bank.loader, NULL, loader_main, NULL);
        if (!g_bank.have_loader) g_bank.state = BANK_FAILED;
    }
    pthread_mutex_unlock(&g_bank_mx);
}

static void bank_release(void) {
    pthread_mutex_lock(&g_bank_mx);
    if (--g_bank.refs == 0) {
        g_bank.stop = 1;
        if (g_bank.have_loader) pthread_join(g_bank.loader, NULL);
        free(g_bank.map);
        if (g_bank.own_tables) { free((void *)g_bank.ent); free((void *)g_bank.zone); }
        free(g_bank.head); free(g_bank.hoff); free(g_bank.hlen);
        memset(&g_bank, 0, sizeof g_bank);
    }
    pthread_mutex_unlock(&g_bank_mx);
}

static inline const int16_t *frame_ptr(const bank_t *b, uint32_t s, uint32_t i) {
    return b->head + 2 * ((size_t)b->hoff[s] + i);
}

/* ---------- parameters ---------- */
enum {
    P_BLEND, P_TONE, P_DAMP, P_VIBRATO, P_POSITION, P_CAPO, P_STRUM_SPEED, P_NOISE,
    P_ARTIC, P_LEGATO, P_STRUM, P_DROP, P_ENGINE,
    P_ENG_MIX, P_ENG_COLOR, P_ENG_SUSTAIN, P_ENG_DETUNE, P_SPACE, P_VOLUME, P_THEME, NP
};
static const char *PKEY[NP] = {
    "blend", "tone", "damp", "vibrato", "position", "capo", "strum_speed", "noise",
    "artic", "legato", "strum", "drop", "engine",
    "eng_mix", "eng_color", "eng_sustain", "eng_detune", "space", "volume", "theme"
};
static const float PDEF[NP] = {
    0.35f, 1.0f, 0.0f, 0.0f, 0, 0, 0.35f, 0.55f,
    0, 1, 1, 0, 0,
    0.25f, 0.55f, 0.60f, 0.25f, 0.0f, 0.80f, 0
};
static const int OPEN[NSTR] = { 40, 45, 50, 55, 59, 64 };
static const char *NOTE_NAME[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

/* ---------- voices ---------- */
enum { VK_NOTE, VK_RELEASE, VK_NOISE };

typedef struct {
    int on, kind, str, key, held, samp_done;
    uint32_t samp, age;
    double pos;
    float semis;           /* key - root: pitch shift from the sample's own pitch */
    float gain, pl, pr;
    float t;               /* seconds since the note started sounding */
    int delay;             /* frames before it starts (strum) */
    float att, att_step;
    float denv, dk, hold;  /* damping: hold, then denv *= dk per frame */
    float rel, rel_k; int releasing;
    float fade, fade_step;
    float lp, lp_k_scale;
    int palm;
    /* plucked-string layer */
    float *ks; int ks_on, ks_w; float ks_prev, ks_gain, ks_level, ks_hz;
} voice_t;

typedef struct { uint8_t st, d1, d2; } ev_t;
typedef struct { int key, vel, off, str, fret; } pend_t;

typedef struct {
    float p[NP];
    voice_t v[MAX_V];
    float *ksmem;
    int str_voice[NSTR], str_key[NSTR], str_fret[NSTR];
    float hand;
    uint32_t rng, age;
    uint8_t rr_last[1024];
    ev_t ev[MAX_EV]; int nev;
    uint8_t key_down[128];
    float bend, modwheel, vib_ph;
    /* reverb */
    float *rv; int rv_comb[8], rv_len_c[8], rv_ap[4], rv_len_a[4]; float rv_store[8];
    float rv_buf_total;
    char chord[32];
    int rev, last_state;
} inst_t;

static uint32_t rnd(inst_t *s) { s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5; return s->rng; }
static float frnd(inst_t *s) { return (rnd(s) >> 8) * (1.0f / 16777216.0f); }

static int open_note(const inst_t *s, int str) {
    int n = OPEN[str] + (int)lroundf(s->p[P_CAPO]);
    if (str == 0 && s->p[P_DROP] > 0.5f) n -= 3;
    return n;
}

/* ---------- reverb (Freeverb-style, 4 combs + 2 allpasses per side) ---------- */
static const int COMB_L[4] = { 1116, 1188, 1277, 1356 };
static const int AP_L[2] = { 556, 441 };

static int reverb_init(inst_t *s) {
    int total = 0;
    for (int c = 0; c < 8; c++) { s->rv_len_c[c] = COMB_L[c & 3] + (c >= 4 ? 23 : 0); total += s->rv_len_c[c]; }
    for (int a = 0; a < 4; a++) { s->rv_len_a[a] = AP_L[a & 1] + (a >= 2 ? 23 : 0); total += s->rv_len_a[a]; }
    s->rv = calloc(total, sizeof(float));
    return s->rv != NULL;
}

static void reverb_frame(inst_t *s, float in, float *ol, float *orr) {
    float *buf = s->rv;
    float fb = 0.80f, damp = 0.35f;
    float out[2] = { 0, 0 };
    int base = 0;
    for (int c = 0; c < 8; c++) {
        float *d = buf + base;
        int i = s->rv_comb[c];
        float y = d[i];
        s->rv_store[c] = y * (1 - damp) + s->rv_store[c] * damp;
        d[i] = in + s->rv_store[c] * fb;
        if (++i >= s->rv_len_c[c]) i = 0;
        s->rv_comb[c] = i;
        out[c >> 2] += y;
        base += s->rv_len_c[c];
    }
    for (int a = 0; a < 4; a++) {
        float *d = buf + base;
        int i = s->rv_ap[a];
        float bo = d[i], x = out[a >> 1];
        d[i] = x + bo * 0.5f;
        out[a >> 1] = bo - x;
        if (++i >= s->rv_len_a[a]) i = 0;
        s->rv_ap[a] = i;
        base += s->rv_len_a[a];
    }
    *ol = out[0]; *orr = out[1];
}

/* ---------- zones ---------- */
static const zone_t *find_zone(const bank_t *b, int type, int key, int vel) {
    const zone_t *best = NULL;
    int bd = 1000;
    for (uint32_t z = 0; z < b->nzone; z++) {
        const zone_t *zz = &b->zone[z];
        if (zz->type != type || !zz->nrr) continue;
        if (type == ZT_NOTE && (vel < zz->lv || vel > zz->hv)) continue;
        if (key >= zz->lo && key <= zz->hi) return zz;
        if (type == ZT_NOISE) continue;
        int d = abs(key - zz->root);
        if (d < bd) { bd = d; best = zz; }
    }
    return best;
}

static uint32_t pick_rr(inst_t *s, const bank_t *b, const zone_t *z) {
    int zi = (int)(z - b->zone);
    int n = z->nrr, k = (int)(rnd(s) % (uint32_t)n);
    if (n > 1 && zi < 1024 && k == s->rr_last[zi]) k = (k + 1 + (int)(rnd(s) % (uint32_t)(n - 1))) % n;
    if (zi < 1024) s->rr_last[zi] = (uint8_t)k;
    return z->rr[k];
}

static voice_t *alloc_voice(inst_t *s) {
    voice_t *best = NULL;
    for (int i = 0; i < MAX_V; i++) if (!s->v[i].on) return &s->v[i];
    /* steal: the oldest releasing voice, else the oldest */
    for (int pass = 0; pass < 2 && !best; pass++)
        for (int i = 0; i < MAX_V; i++) {
            voice_t *v = &s->v[i];
            if (pass == 0 && !v->releasing && v->kind == VK_NOTE) continue;
            if (!best || v->age < best->age) best = v;
        }
    for (int i = 0; i < NSTR; i++) if (s->str_voice[i] == (int)(best - s->v)) s->str_voice[i] = -1;
    return best;
}

static void choke(voice_t *v, float ms) {
    if (!v->on) return;
    float st = 1.0f / (ms * 0.001f * SR);
    if (v->fade_step < st) v->fade_step = st;
}

static void pan_for(voice_t *v, int str) {
    float pan = str < 0 ? 0 : -0.22f + 0.088f * str;          /* low strings a little left */
    float a = (pan + 1) * 0.25f * PI_F;
    v->pl = cosf(a); v->pr = sinf(a);
}

static void voice_common(inst_t *s, voice_t *v, int kind, int str, int key, uint32_t samp, float semis, float gain) {
    float *ks = v->ks;
    memset(v, 0, sizeof *v);
    v->ks = ks;
    v->on = 1; v->kind = kind; v->str = str; v->key = key; v->samp = samp; v->semis = semis; v->gain = gain;
    v->age = ++s->age; v->att_step = 1.0f / (0.0004f * SR);
    v->denv = 1; v->dk = 1; v->hold = 1e9f; v->rel = 1; v->rel_k = 1; v->fade = 1;
    v->lp_k_scale = 1;
    pan_for(v, str);
}

static void start_release_noise(inst_t *s, const voice_t *nv) {
    const bank_t *b = &g_bank;
    float amt = s->p[P_NOISE];
    if (amt < 0.01f) return;
    const zone_t *z = find_zone(b, ZT_RELEASE, nv->key, 64);
    if (!z) return;
    float level = nv->denv * nv->rel * nv->fade;
    if (level < 0.05f) return;
    voice_t *v = alloc_voice(s);
    voice_common(s, v, VK_RELEASE, nv->str, nv->key, pick_rr(s, b, z), (float)(nv->key - z->root),
                 amt * amt * 0.9f * (level > 0.7f ? 1.0f : level / 0.7f));
    v->releasing = 1;                 /* a one-shot: never "held" */
}

static void release_voice(inst_t *s, voice_t *v) {
    if (!v->on || v->releasing) return;
    v->held = 0;
    v->releasing = 1;
    float sec = v->palm ? 0.05f : 0.18f;
    v->rel_k = expf(-1.0f / (sec * SR));
    if (v->kind == VK_NOTE) start_release_noise(s, v);
}

static void excite_ks(inst_t *s, voice_t *v, int key, int vel) {
    float det = s->p[P_ENG_DETUNE] * 30.0f;
    float hz = 440.0f * powf(2.0f, (key - 69) / 12.0f + det / 1200.0f);
    float len = SR / hz;
    if (len > KS_LEN - 4) len = KS_LEN - 4;
    if (len < 2) len = 2;
    float c = 0.15f + 0.8f * s->p[P_ENG_COLOR], lp = 0;
    int n = (int)len + 2;
    for (int i = 0; i < KS_LEN; i++) v->ks[i] = 0;
    for (int i = 0; i < n; i++) {
        float x = frnd(s) * 2 - 1;
        lp += c * (x - lp);
        v->ks[i] = lp;
    }
    v->ks_w = n; v->ks_prev = 0; v->ks_on = 1; v->ks_hz = hz;
    v->ks_gain = 0.25f + 0.75f * (vel / 127.0f);
    v->ks_level = 1;
}

/* A string note: choke what the string was playing (or slur from it), then start the sample (+ the pluck). */
static void start_note(inst_t *s, int key, int vel, int str, int fret, int delay, int allow_legato) {
    const bank_t *b = &g_bank;
    const zone_t *z = find_zone(b, ZT_NOTE, key, vel);
    if (!z) return;
    int legato = 0;
    int old = str >= 0 ? s->str_voice[str] : -1;
    if (old >= 0 && s->v[old].on) {
        voice_t *ov = &s->v[old];
        legato = allow_legato && s->p[P_LEGATO] > 0.5f && ov->held && !ov->releasing && ov->kind == VK_NOTE && abs(key - ov->key) <= 5;
        ov->held = 0;
        choke(ov, legato ? 12.0f : 6.0f);
    }
    voice_t *v = alloc_voice(s);
    float span = (float)(z->hv - z->lv);
    float g = 0.6f + 0.4f * (span > 0 ? (vel - z->lv) / span : 1.0f);
    if (vel < 40) g *= 0.25f + 0.75f * vel / 40.0f;      /* soft playing gets properly soft, not just the soft layer */
    voice_common(s, v, VK_NOTE, str, key, pick_rr(s, b, z), (float)(key - z->root), g);
    v->held = 1;
    v->delay = delay;
    int palm = s->p[P_ARTIC] > 0.5f;
    v->palm = palm;
    if (legato) {
        v->pos = 0.045 * SR;        /* past the pick attack: the finger, not the pick, sounds the note */
        v->gain *= 0.78f;
        v->att_step = 1.0f / (0.006f * SR);
    }
    float d = s->p[P_DAMP];
    if (palm) { v->hold = 0.012f; v->dk = expf(-1.0f / (0.09f * SR)); v->lp_k_scale = 0.38f; }
    else if (d > 0.005f) {
        v->hold = 0.02f + 0.35f * (1 - d);
        float tau = 0.12f + 6.0f * (1 - d) * (1 - d);
        v->dk = expf(-1.0f / (tau * SR));
    }
    if (s->p[P_ENGINE] > 0.5f && v->ks) excite_ks(s, v, key, vel);
    if (str >= 0) {
        s->str_voice[str] = (int)(v - s->v);
        s->str_key[str] = key;
        s->str_fret[str] = fret;
    }
}

static void start_noise(inst_t *s, int key, int vel) {
    const bank_t *b = &g_bank;
    const zone_t *z = find_zone(b, ZT_NOISE, key, vel);
    if (!z) return;
    voice_t *v = alloc_voice(s);
    voice_common(s, v, VK_NOISE, -1, key, pick_rr(s, b, z), 0, 0.3f + 0.7f * vel / 127.0f);
    v->releasing = 1;
    if (key >= 12 && key <= 17) pan_for(v, key - 12);
}

/* ---------- chord naming (for the readout) ---------- */
static void name_chord(inst_t *s) {
    int pcs = 0, low = 128, n = 0;
    for (int k = 28; k < 128; k++) if (s->key_down[k]) { pcs |= 1 << (k % 12); if (k < low) low = k; n++; }
    if (!n) return;
    if (n == 1 || __builtin_popcount(pcs) == 1) { snprintf(s->chord, sizeof s->chord, "%s", NOTE_NAME[low % 12]); s->rev++; return; }
    static const struct { int mask; const char *name; } T[] = {
        { 0x091, "MAJOR" }, { 0x089, "MINOR" }, { 0x491, "7" }, { 0x891, "MAJ7" }, { 0x489, "MIN7" },
        { 0x085, "SUS2" }, { 0x0A1, "SUS4" }, { 0x049, "DIM" }, { 0x111, "AUG" }, { 0x081, "5" }, { 0x291, "6" },
        { 0x289, "MIN6" }, { 0x249, "DIM7" }, { 0x449, "MIN7b5" }, { 0x495, "9" }, { 0x48D, "MIN9" }
    };
    for (int pass = 0; pass < 2; pass++)
        for (int r = 0; r < 12; r++) {
            int root = pass == 0 ? low % 12 : r;
            int rot = ((pcs >> root) | (pcs << (12 - root))) & 0xFFF;
            for (unsigned t = 0; t < sizeof T / sizeof T[0]; t++)
                if (rot == T[t].mask) {
                    snprintf(s->chord, sizeof s->chord, "%s %s", NOTE_NAME[root], T[t].name);
                    s->rev++;
                    return;
                }
            if (pass == 0) break;
        }
    snprintf(s->chord, sizeof s->chord, "%s ...", NOTE_NAME[low % 12]);
    s->rev++;
}

/* ---------- note batches: string assignment + strum ---------- */
static int cmp_key(const void *a, const void *b) { return ((const pend_t *)a)->key - ((const pend_t *)b)->key; }
static int cmp_str(const void *a, const void *b) { return ((const pend_t *)a)->str - ((const pend_t *)b)->str; }

static void play_batch(inst_t *s, pend_t *pd, int n) {
    if (!n) return;
    qsort(pd, n, sizeof *pd, cmp_key);
    int used = 0;
    int pos = (int)lroundf(s->p[P_POSITION]);
    float target = pos > 0 ? (float)pos : s->hand;
    float fsum = 0; int fcount = 0;
    for (int i = 0; i < n; i++) {
        int best = -1, bfret = 0;
        float bs = 1e9f;
        for (int st = 0; st < NSTR; st++) {
            if (used & (1 << st)) continue;
            int fr = pd[i].key - open_note(s, st);
            if (fr < 0 || fr > (st == NSTR - 1 ? 24 : 17)) continue;
            float sc = fabsf(fr - target);
            if (fr == 0) sc = fminf(sc, pos > 0 ? 3.0f : 0.6f);
            int ov = s->str_voice[st];
            if (ov >= 0 && s->v[ov].on && s->v[ov].held) sc += 6.0f;      /* don't steal a string still held */
            sc += st * 0.01f;
            if (sc < bs) { bs = sc; best = st; bfret = fr; }
        }
        if (best < 0) {   /* out of the neck's range: nearest string, still plays */
            best = pd[i].key < open_note(s, 0) ? 0 : NSTR - 1;
            bfret = pd[i].key - open_note(s, best);
        }
        used |= 1 << best;
        pd[i].str = best; pd[i].fret = bfret;
        if (bfret > 0) { fsum += bfret; fcount++; }
    }
    if (fcount && pos == 0) {
        float m = fsum / fcount;
        s->hand = 0.6f * s->hand + 0.4f * (m > 1 ? m - 1 : m);   /* the hand sits about a fret below the notes */
    }
    int strum = n > 1 && s->p[P_STRUM] > 0.5f;
    if (strum) qsort(pd, n, sizeof *pd, cmp_str);
    float gap = (0.004f + 0.036f * s->p[P_STRUM_SPEED]) * SR;
    for (int i = 0; i < n; i++) {
        int delay = strum ? (int)(i * gap) : 0;
        int vel = pd[i].vel;
        if (strum) { vel -= i * 3; if (vel < 1) vel = 1; }
        start_note(s, pd[i].key, vel, pd[i].str, pd[i].fret, delay, n == 1);
        if (pd[i].off) {
            int vi = s->str_voice[pd[i].str];
            if (vi >= 0) release_voice(s, &s->v[vi]);
        }
    }
    name_chord(s);
}

/* ---------- engine API ---------- */
static void *eng_create(const char *data_dir) {
    inst_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    for (int i = 0; i < NP; i++) s->p[i] = PDEF[i];
    s->ksmem = calloc((size_t)MAX_V * KS_LEN, sizeof(float));
    if (!s->ksmem || !reverb_init(s)) { free(s->ksmem); free(s->rv); free(s); return NULL; }
    for (int i = 0; i < MAX_V; i++) s->v[i].ks = s->ksmem + (size_t)i * KS_LEN;
    for (int i = 0; i < NSTR; i++) { s->str_voice[i] = -1; s->str_key[i] = -1; s->str_fret[i] = -1; }
    s->hand = 2;
    s->rng = 0x9E3779B9u ^ (uint32_t)(uintptr_t)s;
    s->last_state = -1;
    snprintf(s->chord, sizeof s->chord, "-");
    bank_acquire(data_dir);
    return s;
}

static void eng_destroy(void *inst) {
    inst_t *s = inst;
    bank_release();
    free(s->ksmem); free(s->rv); free(s);
}

static void eng_midi(void *inst, const uint8_t *msg, int len) {
    inst_t *s = inst;
    if (len < 1 || s->nev >= MAX_EV) return;
    ev_t e = { msg[0], len > 1 ? msg[1] : 0, len > 2 ? msg[2] : 0 };
    s->ev[s->nev++] = e;
}

static int find_key(const char *key) {
    for (int i = 0; i < NP; i++) if (!strcmp(key, PKEY[i])) return i;
    return -1;
}

static void eng_set_param(void *inst, const char *key, const char *val) {
    inst_t *s = inst;
    int i = find_key(key);
    if (i < 0) return;
    float x = (float)atof(val);
    if (x != x) return;
    s->p[i] = x;
}

static int fmt_display(inst_t *s, int i, char *buf, int n) {
    float v = s->p[i];
    switch (i) {
    case P_BLEND:
        if (v < 0.02f) return snprintf(buf, n, "PICKUP");
        if (v > 0.98f) return snprintf(buf, n, "MIC");
        return snprintf(buf, n, "%d%% MIC", (int)lroundf(v * 100));
    case P_POSITION: { int k = (int)lroundf(v); return k ? snprintf(buf, n, "FRET %d", k) : snprintf(buf, n, "AUTO"); }
    case P_CAPO: { int k = (int)lroundf(v); return k ? snprintf(buf, n, "FRET %d", k) : snprintf(buf, n, "OFF"); }
    case P_STRUM_SPEED: return snprintf(buf, n, "%d ms", (int)lroundf(4 + 36 * v));
    case P_NOISE: return v < 0.01f ? snprintf(buf, n, "OFF") : snprintf(buf, n, "%.0f dB", 40 * log10f(v));
    case P_TONE: return v >= 0.99f ? snprintf(buf, n, "OPEN") : snprintf(buf, n, "%.1f kHz", 0.6f * powf(2.0f, v * 5.0f));
    case P_ENG_COLOR: return snprintf(buf, n, "%.1f kHz", 0.4f + 7.6f * v);
    case P_ENG_DETUNE: return snprintf(buf, n, "+%d ct", (int)lroundf(v * 30));
    case P_VOLUME: return v < 0.001f ? snprintf(buf, n, "-inf dB") : snprintf(buf, n, "%.1f dB", 20 * log10f(v / 0.9f));
    default: return snprintf(buf, n, "%d%%", (int)lroundf(v * 100));
    }
}

static int eng_get_param(void *inst, const char *key, char *buf, int n) {
    inst_t *s = inst;
    if (n <= 0) return 0;
    int st = bank_state();
    if (st != s->last_state) { s->last_state = st; s->rev++; }
    if (!strcmp(key, "display_rev")) return snprintf(buf, n, "%d", s->rev);
    if (!strcmp(key, "status"))
        return snprintf(buf, n, "%s", st == BANK_READY ? "READY" : st == BANK_FAILED ? "SAMPLES MISSING" : "LOADING...");
    if (!strcmp(key, "chord")) return snprintf(buf, n, "%s", s->chord);
    int i = find_key(key);
    if (i >= 0) return snprintf(buf, n, "%g", s->p[i]);
    size_t kl = strlen(key);
    if (kl > 8 && !strcmp(key + kl - 8, "_display")) {
        char k2[64];
        if (kl - 8 >= sizeof k2) return 0;
        memcpy(k2, key, kl - 8); k2[kl - 8] = 0;
        i = find_key(k2);
        if (i >= 0) return fmt_display(s, i, buf, n);
    }
    return 0;
}

static void all_off(inst_t *s, int hard) {
    for (int i = 0; i < MAX_V; i++) {
        if (!s->v[i].on) continue;
        if (hard) choke(&s->v[i], 5); else release_voice(s, &s->v[i]);
    }
    memset(s->key_down, 0, sizeof s->key_down);
}

static void handle_events(inst_t *s) {
    pend_t pd[NSTR * 4];
    int np = 0, ready = bank_state() == BANK_READY;
    for (int e = 0; e < s->nev; e++) {
        uint8_t st = s->ev[e].st & 0xF0, d1 = s->ev[e].d1 & 0x7F, d2 = s->ev[e].d2 & 0x7F;
        if (st == 0x90 && d2 > 0) {
            s->key_down[d1] = 1;
            if (!ready) continue;
            if (d1 == 22) { s->p[P_ARTIC] = 0; s->rev++; continue; }          /* keyswitches: A#0 sustain, B0 palm mute */
            if (d1 == 23) { s->p[P_ARTIC] = 1; s->rev++; continue; }
            if (d1 < 28) { start_noise(s, d1, d2); continue; }                 /* dead strings and body knocks */
            if (np < (int)(sizeof pd / sizeof pd[0])) { pend_t p = { d1, d2, 0, -1, 0 }; pd[np++] = p; }
        } else if (st == 0x80 || (st == 0x90 && d2 == 0)) {
            s->key_down[d1] = 0;
            int found = 0;
            for (int i = 0; i < np; i++) if (pd[i].key == d1 && !pd[i].off) { pd[i].off = 1; found = 1; break; }
            if (found) continue;
            for (int i = 0; i < MAX_V; i++) {
                voice_t *v = &s->v[i];
                if (v->on && v->kind == VK_NOTE && v->key == d1 && v->held) release_voice(s, v);
            }
        } else if (st == 0xB0) {
            if (d1 == 1) s->modwheel = d2 / 127.0f;
            else if (d1 == 120) all_off(s, 1);
            else if (d1 == 123) all_off(s, 0);
        } else if (st == 0xE0) {
            int b = ((int)d2 << 7 | d1) - 8192;
            s->bend = b / 8192.0f * 2.0f;
        }
    }
    s->nev = 0;
    if (np) play_batch(s, pd, np);
}

static inline float hermite(float xm1, float x0, float x1, float x2, float t) {
    float c = (x1 - xm1) * 0.5f, v = x0 - x1, w = c + v, a = w + v + (x2 - x0) * 0.5f, bn = w + a;
    return ((((a * t) - bn) * t + c) * t + x0);
}

static void eng_render(void *inst, int16_t *out, int frames) {
    inst_t *s = inst;
    handle_events(s);
    float mixL[128], mixR[128], send[128];
    if (frames > 128) frames = 128;
    memset(mixL, 0, sizeof mixL); memset(mixR, 0, sizeof mixR); memset(send, 0, sizeof send);
    const bank_t *b = &g_bank;
    int ready = bank_state() == BANK_READY;

    float blend = s->p[P_BLEND];
    float tone_hz = 600.0f * powf(2.0f, s->p[P_TONE] * 5.0f);
    int tone_open = s->p[P_TONE] >= 0.99f;           /* fully open: no filter at all, the raw recording */
    float vib_depth = (s->p[P_VIBRATO] + s->modwheel) * 0.30f;       /* semitones, up to 30 cents each */
    s->vib_ph += 2 * PI_F * 5.2f * frames / SR;
    if (s->vib_ph > 2 * PI_F) s->vib_ph -= 2 * PI_F;
    float vib_sin = sinf(s->vib_ph);
    float eng_on = s->p[P_ENGINE] > 0.5f, eng_mix = s->p[P_ENG_MIX];
    float ks_fb = 0.94f + 0.0595f * sqrtf(s->p[P_ENG_SUSTAIN]);
    float ks_c = 0.15f + 0.8f * s->p[P_ENG_COLOR];

    for (int vi = 0; vi < MAX_V && ready; vi++) {
        voice_t *v = &s->v[vi];
        if (!v->on) continue;
        const ent_t *e = &b->ent[v->samp];
        float vib = 0;
        if (v->kind == VK_NOTE && vib_depth > 0) {
            float fin = (v->t - 0.25f) / 0.5f;
            fin = fin < 0 ? 0 : fin > 1 ? 1 : fin;
            vib = vib_depth * fin * vib_sin;
        }
        float bend = v->kind == VK_NOTE ? s->bend : 0;
        double rate = pow(2.0, (v->semis + bend + vib) / 12.0);
        float lpk = (tone_open && v->lp_k_scale >= 1.0f) ? 1.0f
                  : 1.0f - expf(-2 * PI_F * fminf(tone_hz * v->lp_k_scale, 18000.0f) / SR);
        float ks_len = 0;
        int use_ks = v->ks_on && eng_on;
        if (use_ks) {
            ks_len = SR / (v->ks_hz * powf(2.0f, (bend + vib) / 12.0f));
            if (ks_len > KS_LEN - 4) ks_len = KS_LEN - 4;
        }
        float vfb = ks_fb * (v->releasing ? 0.995f : 1.0f);
        if (v->palm) vfb *= 0.985f;
        float ks_amp = eng_mix * 1.0f * v->ks_gain;
        float ks_peak = 0;
        int start = 0;
        if (v->delay > 0) {
            start = v->delay < frames ? v->delay : frames;
            v->delay -= start;
        }
        for (int i = start; i < frames; i++) {
            /* envelope */
            if (v->att < 1) { v->att += v->att_step; if (v->att > 1) v->att = 1; }
            if (v->t > v->hold) v->denv *= v->dk;
            v->rel *= v->rel_k;
            if (v->fade_step > 0) { v->fade -= v->fade_step; if (v->fade <= 0) { v->fade = 0; } }
            float env = v->att * v->denv * v->rel * v->fade;
            float smp = 0;
            if (!v->samp_done) {
                uint32_t ip = (uint32_t)v->pos;
                if (ip + 3 >= e->frames) v->samp_done = 1;
                else {
                    float t = (float)(v->pos - ip);
                    const int16_t *p0 = frame_ptr(b, v->samp, ip ? ip - 1 : 0), *p1 = frame_ptr(b, v->samp, ip),
                                  *p2 = frame_ptr(b, v->samp, ip + 1), *p3 = frame_ptr(b, v->samp, ip + 2);
                    float m0 = p0[0] + (p0[1] - p0[0]) * blend, m1 = p1[0] + (p1[1] - p1[0]) * blend;
                    float m2 = p2[0] + (p2[1] - p2[0]) * blend, m3 = p3[0] + (p3[1] - p3[0]) * blend;
                    smp = hermite(m0, m1, m2, m3, t) * (1.0f / 32768.0f);
                    v->pos += rate;
                }
            }
            v->lp += lpk * (smp - v->lp);
            float x = v->lp * v->gain * env;
            if (use_ks) {
                float rp = (float)v->ks_w - ks_len;
                while (rp < 0) rp += KS_LEN;
                int r0 = (int)rp;
                float fr = rp - r0;
                float y = v->ks[r0 & KS_MASK] + (v->ks[(r0 + 1) & KS_MASK] - v->ks[r0 & KS_MASK]) * fr;
                v->ks_prev += ks_c * (y - v->ks_prev);
                float o = v->ks_prev * vfb;
                v->ks[v->ks_w & KS_MASK] = o;
                v->ks_w = (v->ks_w + 1) & KS_MASK;
                float ke = v->att * v->rel * v->fade * (v->palm ? v->denv : 1.0f);
                float ko = o * ks_amp * ke;
                x += ko;
                float ab = ko < 0 ? -ko : ko;
                if (ab > ks_peak) ks_peak = ab;
            }
            mixL[i] += x * v->pl;
            mixR[i] += x * v->pr;
        }
        v->t += (frames - start) / SR;
        if (use_ks) { v->ks_level = ks_peak; if (ks_peak < 1e-4f && v->t > 0.2f) v->ks_on = 0; }
        /* a voice ends when both its sample and its pluck layer are spent */
        if (v->delay == 0 && v->att >= 1) {
            int ks_alive = use_ks && v->ks_on && v->rel * v->fade * (v->palm ? v->denv : 1.0f) > 1e-4f;
            int smp_alive = !v->samp_done && v->denv * v->rel * v->fade > 1e-4f;
            if (v->fade <= 0 || (!ks_alive && !smp_alive)) {
                v->on = 0;
                for (int k = 0; k < NSTR; k++) if (s->str_voice[k] == vi) s->str_voice[k] = -1;
            }
        }
    }
    float space = s->p[P_SPACE];
    float vol = s->p[P_VOLUME] / 0.9f;
    float dry = (1.0f - 0.35f * space) * vol, wet = space * vol * 0.9f;
    for (int i = 0; i < frames; i++) {
        float l = mixL[i], r = mixR[i];
        float rl = 0, rr = 0;
        if (space > 0.001f) reverb_frame(s, (l + r) * 0.03f, &rl, &rr);
        float ol = l * dry + rl * wet, orr = r * dry + rr * wet;
        /* soft limit above -2 dBFS */
        if (ol > 0.8f) ol = 0.8f + 0.2f * tanhf((ol - 0.8f) * 5.0f);
        else if (ol < -0.8f) ol = -0.8f + 0.2f * tanhf((ol + 0.8f) * 5.0f);
        if (orr > 0.8f) orr = 0.8f + 0.2f * tanhf((orr - 0.8f) * 5.0f);
        else if (orr < -0.8f) orr = -0.8f + 0.2f * tanhf((orr + 0.8f) * 5.0f);
        out[2 * i] = (int16_t)lrintf(ol * 32767.0f);
        out[2 * i + 1] = (int16_t)lrintf(orr * 32767.0f);
    }
}

static const mpc_engine_t ENGINE = { eng_create, eng_destroy, eng_midi, eng_set_param, eng_get_param, eng_render, NULL, NULL };

const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

/* For the offline demo renderer (tools/render_demo.c): wait until the bank is ready. */
int archtop_bank_state(void) { return bank_state(); }
