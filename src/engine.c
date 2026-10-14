/* Fretty: a sampled guitar (and friends) for MPC OS (mpc_engine, see mpc-vst-plugins wrapper/engine.h).
 *
 * Layer A, the instrument: one of ten sampled instruments (all CC0, tools/pack_samples.py, engine/<name>.smp):
 * an archtop (Karoryfer Shinyguitar: each take is a pickup + mic pair, so BLEND mixes them per sample), an electric
 * (Karoryfer Emilyguitar, mono), a folk harp, a concert harp and a strumstick (VCSL, stereo), and a violin bowed or
 * plucked (VSCO 2 CE, stereo; the bowed notes loop, so they sustain while held), and two zithers (VCSL): a dan tranh
 * and a psaltery, plucked or bowed. Each sample file is losslessly
 * compressed on disk and decoded into RAM the first time an instrument is used (shared by every instance).
 *
 * Playing model: six strings. Each note-on batch (a chord arrives in one block) is assigned to strings
 * (open strings, capo, drop tuning, a hand position that follows the music, or a fixed POSITION).
 * One sound per string: a new note on a string chokes the old one, or with LEGATO and the old key still
 * held, slurs into it (hammer-on / pull-off). STRUM spreads a chord low string to high.
 * PLUCK layers a plucked-string model (Karplus-Strong) on every note (off at zero). SWELL / RELEASE shape layer A.
 *
 * Layer B, the synth: every note also plays two detuned oscillators (sine, triangle, saw or square) through a
 * resonant low-pass filter (with an envelope amount) and an ADSR envelope. A and B switch on and off independently
 * (solo either); with both on, MIX balances them (the middle is both at full).
 *
 * Effects on the mix: DRIVE (an amp-style soft clipper), CHORUS, ECHO (a stereo ping-pong delay with darkening
 * repeats), SPACE (reverb). Each is fully bypassed at zero. */
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
#define DLY_LEN 49152                /* echo line: 1.11 s */

enum { ZT_NOTE = 0, ZT_RELEASE = 1, ZT_NOISE = 2 };

/* ---------- the shared sample banks: one per instrument, loaded on first use ---------- */
typedef struct { uint32_t off, frames; } ent_t;
typedef struct __attribute__((packed)) { uint8_t type, lo, hi, root, lv, hv, nrr, pad; uint16_t rr[MAX_RR]; } zone_t;

enum { BANK_NONE, BANK_LOADING, BANK_READY, BANK_FAILED, BANK_RETIRING };
enum { FLAG_DUAL = 1, FLAG_MONO = 2, FLAG_BOWED = 4 };
#define NINST 8                     /* sample files */
static const char *INST_FILE[NINST] = { "acoustic", "electric", "folkharp", "concertharp", "strumstick", "violin",
                                        "dantranh", "psaltery" };
#define NCHOICE 10                  /* the INSTRUMENT options: a file and, for files with articulations, which one */
static const int CHOICE_BANK[NCHOICE] = { 0, 1, 2, 3, 4, 5, 5, 6, 7, 7 };
static const int CHOICE_ART[NCHOICE] = { -1, -1, -1, -1, -1, 0, 1, -1, 1, 0 };   /* 0 bowed, 1 plucked */

typedef struct {
    int state, stop;
    char path[1200];
    uint8_t *map; size_t maplen;
    uint32_t nsamp, nzone, flags;
    float gain;
    const ent_t *ent; const zone_t *zone;
    const uint32_t *loop;                     /* FLAG_BOWED: per sample, the loop start (0 = none; ends at the end) */
    int16_t *head; uint32_t *hoff, *hlen;     /* all samples, decoded to interleaved stereo */
    pthread_t loader; int have_loader, own_tables;
} bank_t;

static bank_t g_banks[NINST];
static int g_shutdown;               /* the last instance is going: loaders stop what they are doing */
static int g_bank_users[NINST];      /* instances that have the instrument picked */
static double g_bank_idle[NINST];    /* when the last of them moved on (monotonic seconds) */
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int g_users;                 /* live instances in this process */
static char g_data_dir[1024];
static pthread_mutex_t g_bank_mx = PTHREAD_MUTEX_INITIALIZER;

static int bank_state_of(int i) { return __atomic_load_n(&g_banks[i].state, __ATOMIC_ACQUIRE); }

/* ---------- sample file (written by tools/pack_samples.py) ----------
 * header: "ATSP", u32 version (3), u32 samples, u32 zones, u32 data offset, u32 flags (1 = pickup + mic pair,
 * 2 = mono, 4 = bowed), f32 level trim; then per sample { u32 byte offset into data, u32 frames }, then the zones
 * (zone_t; its last byte is the articulation in a file that has them), with flag 4 a u32 loop start per sample, then
 * the data. Each sample: u32 len0, u32 len1, then one or two bitstreams (left/pickup, right/mic; mono has one). A stream is 1024-frame blocks: a header byte
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

static void bank_free_data(bank_t *b) {
    free(b->map);
    if (b->own_tables) { free((void *)b->ent); free((void *)b->zone); free((void *)b->loop); }
    free(b->head); free(b->hoff); free(b->hlen);
    b->map = NULL; b->ent = NULL; b->zone = NULL; b->loop = NULL; b->head = NULL; b->hoff = b->hlen = NULL;
    b->own_tables = 0;
}

/* Memory: before a new instrument loads, give back the ones no Fretty has picked for a while (their last notes long
 * gone). A retiring bank is unplayable at once (voices on it stop at their next block), and is freed only a second
 * later, so no audio thread can still be reading it. */
#ifndef BANK_IDLE_S
#define BANK_IDLE_S 20.0
#endif
static int lock_unless_shutdown(void) {   /* the last instance's destroy holds the lock while it joins loaders */
    while (pthread_mutex_trylock(&g_bank_mx)) {
        if (__atomic_load_n(&g_shutdown, __ATOMIC_ACQUIRE)) return 0;
        usleep(2000);
    }
    return 1;
}

static void release_idle_banks(int except) {
    for (int i = 0; i < NINST; i++) {
        if (i == except) continue;
        if (!lock_unless_shutdown()) return;
        int go = g_banks[i].state == BANK_READY && __atomic_load_n(&g_bank_users[i], __ATOMIC_ACQUIRE) == 0 &&
                 now_s() - g_bank_idle[i] > BANK_IDLE_S;
        if (go) __atomic_store_n(&g_banks[i].state, BANK_RETIRING, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_bank_mx);
        if (!go) continue;
        for (int w = 0; w < 50 && !__atomic_load_n(&g_shutdown, __ATOMIC_ACQUIRE); w++) usleep(20000);
        if (!lock_unless_shutdown()) return;
        if (__atomic_load_n(&g_bank_users[i], __ATOMIC_ACQUIRE) == 0 && !g_banks[i].stop) {
            bank_t *b = &g_banks[i];
            if (b->have_loader) { pthread_join(b->loader, NULL); b->have_loader = 0; }
            bank_free_data(b);
            __atomic_store_n(&b->state, BANK_NONE, __ATOMIC_RELEASE);      /* picked again: it loads again */
        } else __atomic_store_n(&g_banks[i].state, BANK_READY, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_bank_mx);
    }
}

static void *loader_main(void *arg) {
    bank_t *b = arg;
    release_idle_banks((int)(b - g_banks));
    FILE *f = fopen(b->path, "rb");
    long size = 0;
    if (!f || fseek(f, 0, SEEK_END) || (size = ftell(f)) < 28 || fseek(f, 0, SEEK_SET)) goto fail;
    b->maplen = (size_t)size;
    b->map = malloc(b->maplen);
    if (!b->map || fread(b->map, 1, b->maplen, f) != b->maplen) goto fail;
    fclose(f); f = NULL;
    {
        const uint8_t *m = b->map;
        uint32_t ver, ns, nz, doff, flags;
        float gain;
        if (memcmp(m, "ATSP", 4)) goto fail;
        memcpy(&ver, m + 4, 4); memcpy(&ns, m + 8, 4); memcpy(&nz, m + 12, 4); memcpy(&doff, m + 16, 4);
        memcpy(&flags, m + 20, 4); memcpy(&gain, m + 24, 4);
        size_t need = 28 + (size_t)ns * sizeof(ent_t) + (size_t)nz * sizeof(zone_t) + ((flags & FLAG_BOWED) ? (size_t)ns * 4 : 0);
        if (ver != 3 || ns == 0 || ns > 65535 || nz == 0 || nz > 1024 || doff != need || doff > b->maplen) goto fail;
        if (!(gain > 0.05f && gain < 20.0f)) gain = 1.0f;
        b->nsamp = ns; b->nzone = nz; b->flags = flags; b->gain = gain;
        b->ent = (const ent_t *)(m + 28);
        b->zone = (const zone_t *)(m + 28 + ns * sizeof(ent_t));
        b->loop = (flags & FLAG_BOWED) ? (const uint32_t *)(m + 28 + ns * sizeof(ent_t) + nz * sizeof(zone_t)) : NULL;
        for (uint32_t z = 0; z < nz; z++)
            for (int k = 0; k < b->zone[z].nrr && k < MAX_RR; k++)
                if (b->zone[z].rr[k] >= ns) goto fail;
        size_t total = 0, dlen = b->maplen - doff;
        for (uint32_t i = 0; i < ns; i++) {
            if (b->ent[i].frames < 8 || b->ent[i].frames > 60 * 44100 || (size_t)b->ent[i].off + 8 > dlen) goto fail;
            total += b->ent[i].frames;
            if (b->loop && b->loop[i] && b->loop[i] + 64 >= b->ent[i].frames) goto fail;
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
            if (decode_channel(src + 8, l0, dst, b->ent[i].frames)) goto fail;
            if (b->flags & FLAG_MONO)
                for (uint32_t k = 0; k < b->ent[i].frames; k++) dst[2 * (size_t)k + 1] = dst[2 * (size_t)k];
            else if (decode_channel(src + 8 + l0, l1, dst + 1, b->ent[i].frames)) goto fail;
            b->hoff[i] = (uint32_t)at; b->hlen[i] = b->ent[i].frames;
            at += b->ent[i].frames;
        }
        if (b->stop) goto fail;
    }
    {   /* keep only the small tables and the decoded audio; drop the compressed file */
        ent_t *e = malloc(b->nsamp * sizeof(ent_t));
        zone_t *z = malloc(b->nzone * sizeof(zone_t));
        uint32_t *lp = b->loop ? malloc(b->nsamp * 4) : NULL;
        if (!e || !z || (b->loop && !lp)) { free(e); free(z); free(lp); goto fail; }
        memcpy(e, b->ent, b->nsamp * sizeof(ent_t)); memcpy(z, b->zone, b->nzone * sizeof(zone_t));
        if (lp) memcpy(lp, b->loop, b->nsamp * 4);
        b->ent = e; b->zone = z; b->loop = lp;
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

static void banks_attach(const char *data_dir) {
    pthread_mutex_lock(&g_bank_mx);
    if (g_users++ == 0) snprintf(g_data_dir, sizeof g_data_dir, "%s", data_dir ? data_dir : ".");
    pthread_mutex_unlock(&g_bank_mx);
}

/* Start loading an instrument's samples if nobody has yet (cheap to call every block). */
static void bank_request(int i) {
    if (i < 0 || i >= NINST || bank_state_of(i) != BANK_NONE) return;
    pthread_mutex_lock(&g_bank_mx);
    bank_t *b = &g_banks[i];
    if (b->state == BANK_NONE) {
        if (b->have_loader) pthread_join(b->loader, NULL);      /* a retired bank's old loader thread */
        memset(b, 0, sizeof *b);
        snprintf(b->path, sizeof b->path, "%s/%s.smp", g_data_dir, INST_FILE[i]);
        b->state = BANK_LOADING;
        b->have_loader = !pthread_create(&b->loader, NULL, loader_main, b);
        if (!b->have_loader) b->state = BANK_FAILED;
    }
    pthread_mutex_unlock(&g_bank_mx);
}

static void banks_detach(void) {
    pthread_mutex_lock(&g_bank_mx);
    if (--g_users == 0) {
        __atomic_store_n(&g_shutdown, 1, __ATOMIC_RELEASE);   /* a loader busy releasing banks gives up the lock */
        for (int i = 0; i < NINST; i++) g_banks[i].stop = 1;
        for (int i = 0; i < NINST; i++) {
            bank_t *b = &g_banks[i];
            if (b->have_loader) pthread_join(b->loader, NULL);
            bank_free_data(b);
            memset(b, 0, sizeof *b);
            g_bank_users[i] = 0;
        }
        __atomic_store_n(&g_shutdown, 0, __ATOMIC_RELEASE);
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
    P_ENG_MIX, P_ENG_COLOR, P_ENG_SUSTAIN, P_ENG_DETUNE, P_SPACE, P_VOLUME, P_THEME,
    P_INSTRUMENT, P_DRIVE, P_CHORUS, P_A_ON, P_B_ON, P_A_LEVEL, P_B_LEVEL,
    P_WAVE, P_OCTAVE, P_SYN_DETUNE, P_CUTOFF, P_RESO, P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE,
    P_STRUM_MODE, P_MIX, P_A_ATTACK, P_A_RELEASE, P_A_VEL, P_B_VEL, P_B_FENV, P_GLIDE, P_ECHO, P_ECHO_TIME,
    P_CHORD_MODE, P_CHORD_KEY, P_CHORD_SCALE, P_CHORD_VOICING, P_HUM_TIME, P_HUM_VEL, P_ECHO_SYNC, NP
};
/* P_STRUM, P_ENGINE, P_A_LEVEL and P_B_LEVEL are kept (parameter order is fixed) but no longer used: STRUM MODE,
 * the PLUCK level (on above zero) and MIX replace them. */
static const char *PKEY[NP] = {
    "blend", "tone", "damp", "vibrato", "position", "capo", "strum_speed", "noise",
    "artic", "legato", "strum", "drop", "engine",
    "eng_mix", "eng_color", "eng_sustain", "eng_detune", "space", "volume", "theme",
    "instrument", "drive", "chorus", "a_on", "b_on", "a_level", "b_level",
    "wave", "octave", "syn_detune", "cutoff", "resonance", "attack", "decay", "sustain", "release",
    "strum_mode", "mix", "a_attack", "a_release", "a_vel", "b_vel", "b_fenv", "glide", "echo", "echo_time",
    "chord_mode", "chord_key", "chord_scale", "chord_voicing", "hum_time", "hum_vel", "echo_sync"
};
static const float PDEF[NP] = {
    0.35f, 1.0f, 0.0f, 0.0f, 0, 0, 0.35f, 0.55f,
    0, 1, 1, 0, 0,
    0.0f, 0.55f, 0.60f, 0.25f, 0.0f, 0.80f, 0,
    0, 0, 0, 1, 0, 0.80f, 0.50f,
    2, 0, 0.15f, 0.60f, 0.15f, 0.02f, 0.40f, 0.60f, 0.30f,
    1, 0.5f, 0, 0, 1.0f, 0.65f, 0, 0, 0, 0.6f,
    0, 0, 0, 0, 0, 0, 0
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
    float lp, lpr, lp_k_scale;
    int palm, bank;
    uint32_t loop_s, loop_e;           /* a bowed sustain loops [loop_s, loop_e) while it sounds */
    float glide, glide_k;              /* GLIDE: semitones still to slide (decays to 0), per-block factor */
    /* synth layer */
    int syn_on, syn_stage;          /* 0 attack, 1 decay/sustain, 2 release, 3 done */
    float syn_env, syn_hz, syn_vel, ph1, ph2, ic1, ic2;
    /* plucked-string layer */
    float *ks; int ks_on, ks_w; float ks_prev, ks_gain, ks_level, ks_hz;
    float meter;           /* recent output peak, for the string picture */
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
    /* effects */
    float drv_lp[2], drv_dc_x[2], drv_dc_y[2], drv_hp[2];
    float *cho; int cho_w; float cho_ph;
    float *dly; int dly_w; float dly_t, dly_lp[2];        /* ECHO: two 1.1 s lines, smoothed time, repeat filters */
    char chord[32];
    uint8_t disp_vib[NSTR], disp_fret[NSTR];   /* what the page's string pictures show (s<n>_vib / s<n>_fret) */
    int rev, last_state, cur_bank, cur_choice;
    double clock;                    /* seconds of audio rendered */
    double last_strum; int strum_up;
    float bpm;                       /* the host's tempo (vst.json HAS_LFO_BPM sends "lfo_bpm"); 0 = not known */
    /* chord pads: what each pad key is playing, and how many held pads use each chord note */
    uint8_t pad_notes[128][6], pad_n[128], note_ref[128];
    int chord_deg;                   /* the degree (1-7) last played, 0 = none: lights the PERFORM page's tile */
    int bank_held;                   /* the bank this instance holds a "use" on (memory release), -1 none */
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
static const zone_t *find_zone(const bank_t *b, int type, int key, int vel, int art) {
    const zone_t *best = NULL;
    int bd = 1000;
    for (uint32_t z = 0; z < b->nzone; z++) {
        const zone_t *zz = &b->zone[z];
        if (zz->type != type || !zz->nrr) continue;
        if (art >= 0 && type == ZT_NOTE && zz->pad != art) continue;
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
    const bank_t *b = &g_banks[nv->bank];
    if (s->p[P_A_ON] < 0.5f) return;
    float amt = s->p[P_NOISE];
    if (amt < 0.01f) return;
    const zone_t *z = find_zone(b, ZT_RELEASE, nv->key, 64, -1);
    if (!z) return;
    float level = nv->denv * nv->rel * nv->fade;
    if (level < 0.05f) return;
    voice_t *v = alloc_voice(s);
    voice_common(s, v, VK_RELEASE, nv->str, nv->key, pick_rr(s, b, z), (float)(nv->key - z->root),
                 amt * amt * 0.9f * (level > 0.7f ? 1.0f : level / 0.7f));
    v->bank = nv->bank;
    v->releasing = 1;                 /* a one-shot: never "held" */
}

static void release_voice(inst_t *s, voice_t *v) {
    if (!v->on || v->releasing) return;
    v->held = 0;
    v->releasing = 1;
    float sec = v->palm ? 0.05f : v->loop_e ? 0.22f : 0.18f;          /* a bow leaves the string a little slower */
    float ar = s->p[P_A_RELEASE];
    if (ar > 0.005f) { float t = 0.05f * powf(60.0f, ar); if (t > sec) sec = t; }   /* RELEASE: let it ring on */
    v->rel_k = expf(-1.0f / (sec * SR));
    if (v->syn_on && v->syn_stage < 2) v->syn_stage = 2;
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
    v->ks_gain = 1.0f - s->p[P_A_VEL] * 0.75f * (1.0f - vel / 127.0f);
    v->ks_level = 1;
}

/* A string note: choke what the string was playing (or slur from it), then start the sample (+ the pluck). */
static int bowed(const inst_t *s) { return CHOICE_ART[s->cur_choice] == 0; }

static void start_note(inst_t *s, int key, int vel, int str, int fret, int delay, int allow_legato) {
    const bank_t *b = &g_banks[s->cur_bank];
    int art = CHOICE_ART[s->cur_choice], bow = art == 0;
    const zone_t *z = find_zone(b, ZT_NOTE, key, vel, art);
    if (!z) return;
    int legato = 0;
    float old_key = 0;
    int old = str >= 0 ? s->str_voice[str] : -1;
    if (old >= 0 && s->v[old].on) {
        voice_t *ov = &s->v[old];
        legato = allow_legato && s->p[P_LEGATO] > 0.5f && ov->held && !ov->releasing && ov->kind == VK_NOTE &&
                 abs(key - ov->key) <= ((bow || s->p[P_GLIDE] > 0.005f) ? 12 : 5);
        if (legato) old_key = ov->key + ov->glide;      /* where the old note was, mid-slide included */
        ov->held = 0;
        choke(ov, legato ? (bow ? 45.0f : 12.0f) : 6.0f);
    }
    voice_t *v = alloc_voice(s);
    float span = (float)(z->hv - z->lv);
    float g = 0.6f + 0.4f * (span > 0 ? (vel - z->lv) / span : 1.0f);
    if (vel < 40) g *= 0.25f + 0.75f * vel / 40.0f;      /* soft playing gets properly soft, not just the soft layer */
    g = 1.0f - s->p[P_A_VEL] * (1.0f - g);               /* VELOCITY (A): how much the gain follows; layers always do */
    uint32_t smp = pick_rr(s, b, z);
    voice_common(s, v, VK_NOTE, str, key, smp, (float)(key - z->root), g);
    v->bank = s->cur_bank;
    v->held = 1;
    if (bow && b->loop && b->loop[smp]) { v->loop_s = b->loop[smp]; v->loop_e = b->ent[smp].frames; }
    if (s->p[P_B_ON] > 0.5f) {         /* the synth layer plays the same note */
        v->syn_on = 1; v->syn_stage = 0; v->syn_env = 0;
        v->syn_hz = 440.0f * powf(2.0f, (key - 69) / 12.0f + lroundf(s->p[P_OCTAVE]));
        v->syn_vel = 1.0f - s->p[P_B_VEL] * (1.0f - vel / 127.0f);
        v->ph1 = frnd(s); v->ph2 = frnd(s);
    }
    v->delay = delay;
    float sw = s->p[P_A_ATTACK];                         /* SWELL: fade the note in, like a volume knob */
    if (sw > 0.005f) v->att_step = 1.0f / (0.005f * powf(400.0f, sw) * SR);
    int palm = s->p[P_ARTIC] > 0.5f && art != 1;       /* pizzicato is already short */
    v->palm = palm;
    if (legato) {
        v->pos = (bow ? 0.12 : 0.045) * SR;   /* past the attack: the finger (or the same bow), not a new pick */
        v->gain *= bow ? 0.92f : 0.78f;
        v->att_step = 1.0f / ((bow ? 0.035f : 0.006f) * SR);
        float gl = s->p[P_GLIDE];
        if (gl > 0.005f) {                    /* GLIDE: slide from the old note, like a steel bar or a bent string */
            float t = 0.015f * powf(40.0f, gl);                      /* 15 ms .. 0.6 s to get (nearly) there */
            v->glide = old_key - key;
            v->glide_k = expf(-128.0f * 4.0f / (t * SR));             /* per 128-frame block, ~2% left after t */
            v->gain = g * (bow ? 0.95f : 0.9f);                       /* a slide doesn't re-pluck: barely softer */
        }
    }
    float d = s->p[P_DAMP];
    if (palm) { v->hold = 0.012f; v->dk = expf(-1.0f / (0.09f * SR)); v->lp_k_scale = 0.38f; }
    else if (d > 0.005f) {
        v->hold = 0.02f + 0.35f * (1 - d);
        float tau = 0.12f + 6.0f * (1 - d) * (1 - d);
        v->dk = expf(-1.0f / (tau * SR));
    }
    if (s->p[P_ENG_MIX] > 0.005f && v->ks) excite_ks(s, v, key, vel);
    if (str >= 0) {
        s->str_voice[str] = (int)(v - s->v);
        s->str_key[str] = key;
        s->str_fret[str] = fret;
    }
}

static void start_noise(inst_t *s, int key, int vel) {
    const bank_t *b = &g_banks[s->cur_bank];
    if (s->p[P_A_ON] < 0.5f) return;
    const zone_t *z = find_zone(b, ZT_NOISE, key, vel, -1);
    if (!z) return;
    voice_t *v = alloc_voice(s);
    voice_common(s, v, VK_NOISE, -1, key, pick_rr(s, b, z), 0, 0.3f + 0.7f * vel / 127.0f);
    v->bank = s->cur_bank;
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

/* ---------- chord pads: every note plays the chord of its degree in KEY / SCALE ---------- */
static const int SCALES[6][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },     /* major */
    { 0, 2, 3, 5, 7, 8, 10 },     /* natural minor */
    { 0, 2, 3, 5, 7, 9, 10 },     /* dorian */
    { 0, 2, 4, 5, 7, 9, 10 },     /* mixolydian */
    { 0, 2, 3, 5, 7, 8, 11 },     /* harmonic minor */
    { 0, 1, 3, 5, 7, 8, 10 },     /* phrygian */
};
static int chord_scale(const inst_t *s) { int k = (int)lroundf(s->p[P_CHORD_SCALE]); return k < 0 ? 0 : k > 5 ? 5 : k; }
static int chord_key(const inst_t *s) { int k = (int)lroundf(s->p[P_CHORD_KEY]); return ((k % 12) + 12) % 12; }

/* the intervals above the root of degree d (0-6): third, fifth, seventh */
static void degree_tones(const inst_t *s, int d, int *t3, int *t5, int *t7) {
    const int *sc = SCALES[chord_scale(s)];
    int r = sc[d];
    *t3 = (sc[(d + 2) % 7] - r + 12) % 12;
    *t5 = (sc[(d + 4) % 7] - r + 12) % 12;
    *t7 = (sc[(d + 6) % 7] - r + 12) % 12;
}

static int degree_name(const inst_t *s, int d, char *buf, int n) {
    int t3, t5, t7;
    degree_tones(s, d, &t3, &t5, &t7);
    const char *root = NOTE_NAME[(chord_key(s) + SCALES[chord_scale(s)][d]) % 12];
    const char *q;
    if ((int)lroundf(s->p[P_CHORD_MODE]) == 2) {
        q = t3 == 4 && t5 == 7 && t7 == 11 ? "maj7" : t3 == 4 && t5 == 7 && t7 == 10 ? "7" : t3 == 3 && t5 == 7 && t7 == 10 ? "m7"
          : t3 == 3 && t5 == 7 && t7 == 11 ? "m(maj7)" : t3 == 3 && t5 == 6 && t7 == 10 ? "m7b5" : t3 == 3 && t5 == 6 && t7 == 9 ? "dim7"
          : t3 == 4 && t5 == 8 ? "+maj7" : "7";
    } else
        q = t3 == 4 && t5 == 7 ? "" : t3 == 3 && t5 == 7 ? "m" : t3 == 3 && t5 == 6 ? "dim" : t3 == 4 && t5 == 8 ? "aug" : "";
    return snprintf(buf, n, "%s%s", root, q);
}

/* key -> the chord's notes as a guitarist would voice them; returns how many (and the degree, 1-7) */
static int chord_for(inst_t *s, int key, int *out, int *deg) {
    const int *sc = SCALES[chord_scale(s)];
    int pc = ((key - chord_key(s)) % 12 + 12) % 12, d = 0;
    for (int i = 0; i < 7; i++) if (sc[i] <= pc) d = i;           /* a note between two degrees takes the lower one */
    int t3, t5, t7, sev = (int)lroundf(s->p[P_CHORD_MODE]) == 2;
    degree_tones(s, d, &t3, &t5, &t7);
    int rpc = (chord_key(s) + sc[d]) % 12;
    int voicing = (int)lroundf(s->p[P_CHORD_VOICING]), n = 0;
    *deg = d + 1;
    if (voicing == 1) {                     /* BARRE: six strings, the root on the low E string (frets 0-11) */
        int r = 40 + ((rpc - 4 + 12) % 12);
        int v[6] = { r, r + t5, sev ? r + t7 : r + 12, r + 12 + t3, r + 12 + t5, r + 24 };
        for (int i = 0; i < 6; i++) out[n++] = v[i];
    } else if (voicing == 2) {              /* HIGH: a close voicing on the top strings */
        int tones[4] = { 0, t3, t5, t7 }, c = sev ? 4 : 3;
        for (int i = 0; i < c; i++) { int x = 60 + ((rpc + tones[i]) % 12); out[n++] = x < 62 ? x + 12 : x; }
    } else {                                /* OPEN: five strings, the root on the A string */
        int r = 45 + ((rpc - 9 + 12) % 12);
        int v[5] = { r, r + t5, sev ? r + t7 : r + 12, r + 12 + t3, r + 12 + t5 };
        for (int i = 0; i < 5; i++) out[n++] = v[i];
    }
    for (int i = 0; i < n; i++) if (out[i] > 100) out[i] -= 12;
    for (int i = 1; i < n; i++)                                   /* low to high: one string each, in order */
        for (int j = i; j > 0 && out[j - 1] > out[j]; j--) { int t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
    (void)key;
    return n;
}

/* ECHO SYNC: the TIME knob steps through note lengths at the host's tempo */
static const float ECHO_BEATS[12] = { 0.125f, 1.0f / 6, 0.25f, 1.0f / 3, 0.375f, 0.5f, 2.0f / 3, 0.75f, 1.0f, 4.0f / 3, 1.5f, 2.0f };
static const char *ECHO_DIV[12] = { "1/32", "1/16T", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D", "1/4", "1/2T", "1/4D", "1/2" };
static int echo_div(const inst_t *s) { int k = (int)(s->p[P_ECHO_TIME] * 12); return k < 0 ? 0 : k > 11 ? 11 : k; }
static float echo_seconds(const inst_t *s) {
    if (s->p[P_ECHO_SYNC] > 0.5f) {
        float bpm = s->bpm > 0 ? s->bpm : 120.0f;
        float t = ECHO_BEATS[echo_div(s)] * 60.0f / bpm;
        while (t > 1.05f) t *= 0.5f;                                  /* past the line's length: the half */
        return t;
    }
    return 0.06f * powf(1.0f / 0.06f, s->p[P_ECHO_TIME]);
}

/* ---------- note batches: string assignment + strum ---------- */
static int cmp_key(const void *a, const void *b) { return ((const pend_t *)a)->key - ((const pend_t *)b)->key; }
static int cmp_str(const void *a, const void *b) { return ((const pend_t *)a)->str - ((const pend_t *)b)->str; }

static void play_batch(inst_t *s, pend_t *pd, int n, int chord_pad) {
    if (!n) return;
    qsort(pd, n, sizeof *pd, cmp_key);
    int used = 0;
    int pos = (int)lroundf(s->p[P_POSITION]);
    float target = pos > 0 ? (float)pos : s->hand;
    float fsum = 0; int fcount = 0;
    /* LEGATO (or GLIDE): a single note played while another is still held stays on that string, so it slurs or
     * slides from it instead of being picked on a free string */
    int keep_str = -1;
    if (n == 1 && s->p[P_LEGATO] > 0.5f) {
        int lim = (bowed(s) || s->p[P_GLIDE] > 0.005f) ? 12 : 5, bd = 99;
        for (int st = 0; st < NSTR; st++) {
            int ov = s->str_voice[st];
            if (ov < 0 || !s->v[ov].on || !s->v[ov].held || s->v[ov].releasing || s->v[ov].kind != VK_NOTE) continue;
            int d = abs(pd[0].key - s->v[ov].key), fr = pd[0].key - open_note(s, st);
            if (d <= lim && fr >= 0 && fr <= (st == NSTR - 1 ? 24 : 17) && d < bd) { bd = d; keep_str = st; }
        }
    }
    for (int i = 0; i < n; i++) {
        int best = -1, bfret = 0;
        float bs = 1e9f;
        if (pd[i].str >= 0) {          /* a chord pad's shape names the string: keep it if the fret is on the neck */
            int st = pd[i].str, fr = pd[i].key - open_note(s, st);
            if (st < NSTR && !(used & (1 << st)) && fr >= 0 && fr <= (st == NSTR - 1 ? 24 : 17)) {
                used |= 1 << st; pd[i].fret = fr;
                if (fr > 0) { fsum += fr; fcount++; }
                continue;
            }
            pd[i].str = -1;
        }
        if (keep_str >= 0) {
            best = keep_str; bfret = pd[0].key - open_note(s, keep_str);
            used |= 1 << best; pd[i].str = best; pd[i].fret = bfret;
            if (bfret > 0) { fsum += bfret; fcount++; }
            continue;
        }
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
    /* STRUM: off, down (low string first), up (high first, a little lighter) or alternating down / up while the
     * chords keep coming (a gap over a second starts again on a down stroke). A bowed violin plays chords together. */
    int mode = (int)lroundf(s->p[P_STRUM_MODE]);
    int strum = n > 1 && mode > 0 && !bowed(s);
    int up = 0;
    if (strum) {
        if (mode == 2) up = 1;
        else if (mode == 3) { up = (s->clock - s->last_strum < 1.0) ? !s->strum_up : 0; }
        s->strum_up = up; s->last_strum = s->clock;
        qsort(pd, n, sizeof *pd, cmp_str);
        if (up) for (int i = 0; i < n / 2; i++) { pend_t t = pd[i]; pd[i] = pd[n - 1 - i]; pd[n - 1 - i] = t; }
    }
    float gap = (0.004f + 0.036f * s->p[P_STRUM_SPEED]) * SR * (up ? 0.8f : 1.0f);
    float ht = s->p[P_HUM_TIME], hv = s->p[P_HUM_VEL];
    int batch_lag = ht > 0.005f ? (int)(frnd(s) * ht * 0.018f * SR) : 0;     /* HUMANIZE: the whole hit a little late */
    for (int i = 0; i < n; i++) {
        int delay = strum ? (int)(i * gap) : 0;
        if (ht > 0.005f) delay += batch_lag + (strum ? (int)(frnd(s) * ht * 0.35f * gap) : 0);   /* and each string */
        int vel = pd[i].vel;
        if (strum) { if (up) vel = vel * 85 / 100; vel -= i * 3; if (vel < 1) vel = 1; }
        if (hv > 0.005f) { vel += (int)lroundf((frnd(s) * 2 - 1) * hv * 22); vel = vel < 1 ? 1 : vel > 127 ? 127 : vel; }
        start_note(s, pd[i].key, vel, pd[i].str, pd[i].fret, delay, n == 1);
        if (pd[i].off) {
            int vi = s->str_voice[pd[i].str];
            if (vi >= 0) release_voice(s, &s->v[vi]);
        }
    }
    if (!chord_pad) name_chord(s);      /* a chord pad already named its chord */
}

/* ---------- engine API ---------- */
static void *eng_create(const char *data_dir) {
    inst_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    for (int i = 0; i < NP; i++) s->p[i] = PDEF[i];
    s->ksmem = calloc((size_t)MAX_V * KS_LEN, sizeof(float));
    s->cho = calloc(2 * 2048, sizeof(float));
    s->dly = calloc(2 * DLY_LEN, sizeof(float));
    if (!s->ksmem || !s->cho || !s->dly || !reverb_init(s)) {
        free(s->ksmem); free(s->cho); free(s->dly); free(s->rv); free(s); return NULL;
    }
    for (int i = 0; i < MAX_V; i++) s->v[i].ks = s->ksmem + (size_t)i * KS_LEN;
    for (int i = 0; i < NSTR; i++) { s->str_voice[i] = -1; s->str_key[i] = -1; s->str_fret[i] = -1; }
    s->hand = 2;
    s->rng = 0x9E3779B9u ^ (uint32_t)(uintptr_t)s;
    s->last_state = -1;
    s->bank_held = -1;
    snprintf(s->chord, sizeof s->chord, "-");
    banks_attach(data_dir);
    bank_request(0);
    return s;
}

static void bank_hold(inst_t *s, int bank) {    /* this instance has `bank` picked (memory release counts these) */
    if (s->bank_held == bank) return;
    if (s->bank_held >= 0 && __atomic_sub_fetch(&g_bank_users[s->bank_held], 1, __ATOMIC_ACQ_REL) == 0)
        g_bank_idle[s->bank_held] = now_s();
    if (bank >= 0) __atomic_add_fetch(&g_bank_users[bank], 1, __ATOMIC_ACQ_REL);
    s->bank_held = bank;
}

static void eng_destroy(void *inst) {
    inst_t *s = inst;
    bank_hold(s, -1);
    banks_detach();
    free(s->ksmem); free(s->cho); free(s->dly); free(s->rv); free(s);
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
    if (!strcmp(key, "lfo_bpm")) { float b = (float)atof(val); if (b > 20 && b < 400) { s->bpm = b; s->rev++; } return; }
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
    case P_OCTAVE: { int k = (int)lroundf(v); return snprintf(buf, n, k > 0 ? "+%d OCT" : k < 0 ? "%d OCT" : "0 OCT", k); }
    case P_SYN_DETUNE: return snprintf(buf, n, "%d ct", (int)lroundf(v * 50));
    case P_CUTOFF: { float hz = 30.0f * powf(2.0f, v * 9.3f); return hz >= 1000 ? snprintf(buf, n, "%.1f kHz", hz / 1000) : snprintf(buf, n, "%d Hz", (int)hz); }
    case P_MIX: {
        int a = (int)lroundf(fminf(1.0f, 2.0f * (1.0f - v)) * 100), bb = (int)lroundf(fminf(1.0f, 2.0f * v) * 100);
        return snprintf(buf, n, "A%d / B%d", a, bb); }                    /* short: fits under the knob at 23 px */
    case P_A_ATTACK: {
        if (v <= 0.005f) return snprintf(buf, n, "OFF");
        float t = 0.005f * powf(400.0f, v);
        return t >= 1 ? snprintf(buf, n, "%.2f s", t) : snprintf(buf, n, "%d ms", (int)lroundf(t * 1000)); }
    case P_A_RELEASE: {
        if (v <= 0.005f) return snprintf(buf, n, "NATURAL");
        float t = 0.05f * powf(60.0f, v);
        return t >= 1 ? snprintf(buf, n, "%.2f s", t) : snprintf(buf, n, "%d ms", (int)lroundf(t * 1000)); }
    case P_GLIDE: {
        if (v <= 0.005f) return snprintf(buf, n, "OFF");
        return snprintf(buf, n, "%d ms", (int)lroundf(15.0f * powf(40.0f, v))); }
    case P_ECHO: return v < 0.005f ? snprintf(buf, n, "OFF") : snprintf(buf, n, "%d%%", (int)lroundf(v * 100));
    case P_ECHO_TIME:
        if (s->p[P_ECHO_SYNC] > 0.5f) return snprintf(buf, n, "%s", ECHO_DIV[echo_div(s)]);
        return snprintf(buf, n, "%d ms", (int)lroundf(1000.0f * 0.06f * powf(1.0f / 0.06f, v)));
    case P_HUM_TIME: return v < 0.005f ? snprintf(buf, n, "OFF") : snprintf(buf, n, "%d ms", (int)lroundf(v * 18));
    case P_HUM_VEL: return v < 0.005f ? snprintf(buf, n, "OFF") : snprintf(buf, n, "+/-%d", (int)lroundf(v * 22));
    case P_B_FENV: return v < 0.005f ? snprintf(buf, n, "OFF") : snprintf(buf, n, "+%.1f OCT", 6.0f * v);
    case P_ENG_MIX: return v <= 0.005f ? snprintf(buf, n, "OFF") : snprintf(buf, n, "%d%%", (int)lroundf(v * 100));
    case P_ATTACK: case P_DECAY: case P_RELEASE: {
        float t = 0.001f * powf(4000.0f, v);
        return t >= 1 ? snprintf(buf, n, "%.2f s", t) : snprintf(buf, n, "%d ms", (int)lroundf(t * 1000)); }
    default: return snprintf(buf, n, "%d%%", (int)lroundf(v * 100));
    }
}

static int eng_get_param(void *inst, const char *key, char *buf, int n) {
    inst_t *s = inst;
    if (n <= 0) return 0;
    int st = bank_state_of(s->cur_bank);
    if (st != s->last_state) { s->last_state = st; s->rev++; }
    if (!strcmp(key, "display_rev")) return snprintf(buf, n, "%d", s->rev);
    if (!strcmp(key, "status"))
        return snprintf(buf, n, "%s", st == BANK_READY ? "READY" : st == BANK_FAILED ? "SAMPLES MISSING" : "LOADING...");
    if (!strcmp(key, "chord")) return snprintf(buf, n, "%s", s->chord);
    if (!strncmp(key, "chord_", 6) && key[6] >= '1' && key[6] <= '7' && !key[7]) {
        if ((int)lroundf(s->p[P_CHORD_MODE]) == 0) return snprintf(buf, n, " ");
        return degree_name(s, key[6] - '1', buf, n);
    }
    if (!strcmp(key, "chord_deg")) return snprintf(buf, n, "%d", (int)lroundf(s->p[P_CHORD_MODE]) ? s->chord_deg : 0);
    if (!strcmp(key, "load_state")) return snprintf(buf, n, "%d", st == BANK_READY ? 0 : st == BANK_FAILED ? 2 : 1);
    if (key[0] == 's' && key[1] >= '1' && key[1] <= '6' && key[2] == '_') {
        int st = key[1] - '1';
        if (!strcmp(key + 3, "vib")) return snprintf(buf, n, "%d", s->disp_vib[st]);
        if (!strcmp(key + 3, "fret")) return snprintf(buf, n, "%d", s->disp_fret[st]);
    }
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
    memset(s->pad_n, 0, sizeof s->pad_n); memset(s->note_ref, 0, sizeof s->note_ref); s->chord_deg = 0;
}

/* one note of a chord (or a plain key) lets go: pending in this block, or sounding */
static void note_off(inst_t *s, int key, pend_t *pd, int np) {
    for (int i = 0; i < np; i++) if (pd[i].key == key && !pd[i].off) { pd[i].off = 1; return; }
    for (int i = 0; i < MAX_V; i++) {
        voice_t *v = &s->v[i];
        if (v->on && v->kind == VK_NOTE && v->key == key && v->held) release_voice(s, v);
    }
}

/* a chord pad lets go: each of its notes stops once no other held pad still uses it */
static void release_pad(inst_t *s, int pad, pend_t *pd, int np) {
    for (int k = 0; k < s->pad_n[pad]; k++) {
        int x = s->pad_notes[pad][k];
        if (s->note_ref[x] && --s->note_ref[x] == 0) note_off(s, x, pd, np);
    }
    s->pad_n[pad] = 0;
    int any = 0;
    for (int k = 0; k < 128; k++) if (s->pad_n[k]) any = 1;
    if (!any && s->chord_deg) { s->chord_deg = 0; s->rev++; }
}

static void handle_events(inst_t *s) {
    pend_t pd[NSTR * 4];
    int np = 0, ready = bank_state_of(s->cur_bank) == BANK_READY, chord_batch = 0;
    for (int e = 0; e < s->nev; e++) {
        uint8_t st = s->ev[e].st & 0xF0, d1 = s->ev[e].d1 & 0x7F, d2 = s->ev[e].d2 & 0x7F;
        if (st == 0x90 && d2 > 0) {
            s->key_down[d1] = 1;
            if (!ready) continue;
            if (d1 == 22) { s->p[P_ARTIC] = 0; s->rev++; continue; }          /* keyswitches: A#0 sustain, B0 palm mute */
            if (d1 == 23) { s->p[P_ARTIC] = 1; s->rev++; continue; }
            if (d1 < 28) { start_noise(s, d1, d2); continue; }                 /* dead strings and body knocks */
            if ((int)lroundf(s->p[P_CHORD_MODE]) > 0) {          /* CHORD PADS: the key plays its degree's chord */
                int notes[6], deg, c = chord_for(s, d1, notes, &deg);
                release_pad(s, d1, pd, np);                       /* the same pad again: its old chord lets go first */
                s->pad_n[d1] = (uint8_t)c;
                for (int k = 0; k < c; k++) {
                    s->pad_notes[d1][k] = (uint8_t)notes[k];
                    if (s->note_ref[notes[k]] < 255) s->note_ref[notes[k]]++;
                    int dup = 0;
                    for (int q = 0; q < np; q++) if (pd[q].key == notes[k] && !pd[q].off) dup = 1;
                    /* the shape: the lowest note on the lowest string it needs, then one string up per note */
                    if (!dup && np < (int)(sizeof pd / sizeof pd[0])) { pend_t p = { notes[k], d2, 0, NSTR - c + k, 0 }; pd[np++] = p; }
                }
                s->chord_deg = deg; degree_name(s, deg - 1, s->chord, sizeof s->chord); s->rev++;
                chord_batch = 1;
                continue;
            }
            if (np < (int)(sizeof pd / sizeof pd[0])) { pend_t p = { d1, d2, 0, -1, 0 }; pd[np++] = p; }
        } else if (st == 0x80 || (st == 0x90 && d2 == 0)) {
            s->key_down[d1] = 0;
            if (s->pad_n[d1]) { release_pad(s, d1, pd, np); continue; }
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
    if (np) play_batch(s, pd, np, chord_batch);
}

static inline float hermite(float xm1, float x0, float x1, float x2, float t) {
    float c = (x1 - xm1) * 0.5f, v = x0 - x1, w = c + v, a = w + v + (x2 - x0) * 0.5f, bn = w + a;
    return ((((a * t) - bn) * t + c) * t + x0);
}

/* PolyBLEP: removes the aliasing step at a waveform's jump (t = phase, dt = phase step). */
static inline float blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static inline float osc(int wave, float ph, float dt) {
    switch (wave) {
    case 0: return sinf(2 * PI_F * ph);
    case 1: return 4.0f * fabsf(ph - 0.5f) - 1.0f;
    case 2: return 2.0f * ph - 1.0f - blep(ph, dt);
    default: {
        float q = ph < 0.5f ? 1.0f : -1.0f;
        float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        return q + blep(ph, dt) - blep(p2, dt);
    }
    }
}

static void eng_render(void *inst, int16_t *out, int frames) {
    inst_t *s = inst;
    int want = (int)lroundf(s->p[P_INSTRUMENT]);
    if (want < 0) want = 0;
    if (want >= NCHOICE) want = NCHOICE - 1;
    if (want != s->cur_choice) { s->cur_choice = want; s->rev++; }
    if (CHOICE_BANK[want] != s->cur_bank) { s->cur_bank = CHOICE_BANK[want]; s->rev++; }
    bank_hold(s, s->cur_bank);
    bank_request(s->cur_bank);
    handle_events(s);
    float mixL[128], mixR[128];
    if (frames > 128) frames = 128;
    memset(mixL, 0, sizeof mixL); memset(mixR, 0, sizeof mixR);

    float blend = s->p[P_BLEND];
    float tone_hz = 600.0f * powf(2.0f, s->p[P_TONE] * 5.0f);
    int tone_open = s->p[P_TONE] >= 0.99f;           /* fully open: no filter at all, the raw recording */
    float vib_depth = (s->p[P_VIBRATO] + s->modwheel) * 0.30f;       /* semitones, up to 30 cents each */
    s->vib_ph += 2 * PI_F * 5.2f * frames / SR;
    if (s->vib_ph > 2 * PI_F) s->vib_ph -= 2 * PI_F;
    float vib_sin = sinf(s->vib_ph);
    float eng_mix = s->p[P_ENG_MIX];
    int eng_on = eng_mix > 0.005f;
    float ks_fb = 0.94f + 0.0595f * sqrtf(s->p[P_ENG_SUSTAIN]);
    float ks_c = 0.15f + 0.8f * s->p[P_ENG_COLOR];
    int a_on = s->p[P_A_ON] > 0.5f, b_on = s->p[P_B_ON] > 0.5f;
    float mix = s->p[P_MIX];
    float a_gain = (a_on && b_on) ? fminf(1.0f, 2.0f * (1.0f - mix)) : 1.0f;      /* MIX: the middle is both at full */
    /* synth settings for this block */
    int wave = (int)lroundf(s->p[P_WAVE]); if (wave < 0) wave = 0; if (wave > 3) wave = 3;
    float b_gain = 0.32f * ((a_on && b_on) ? fminf(1.0f, 2.0f * mix) : 1.0f);
    float fenv = s->p[P_B_FENV] * 6.0f;                           /* the filter envelope, in octaves at full */
    float det = s->p[P_SYN_DETUNE] * 50.0f / 1200.0f;            /* octaves, split between the two oscillators */
    float d_up = powf(2.0f, det * 0.5f), d_dn = 1.0f / d_up;
    float cut0 = 30.0f * powf(2.0f, s->p[P_CUTOFF] * 9.3f);
    float kres = 2.0f - 1.9f * s->p[P_RESO];                      /* SVF damping: 2 = no peak, 0.1 = ringing */
    float t_att = 0.001f * powf(4000.0f, s->p[P_ATTACK]);
    float t_dec = 0.001f * powf(4000.0f, s->p[P_DECAY]);
    float t_rel = 0.001f * powf(4000.0f, s->p[P_RELEASE]);
    float sus = s->p[P_SUSTAIN];
    float att_step = 1.0f / (t_att * SR);
    float dec_k = expf(-1.0f / (t_dec * SR)), rel_k = expf(-1.0f / (t_rel * SR));

    for (int vi = 0; vi < MAX_V; vi++) {
        voice_t *v = &s->v[vi];
        if (!v->on) continue;
        const bank_t *b = &g_banks[v->bank];
        int bank_ok = bank_state_of(v->bank) == BANK_READY;
        if (!bank_ok) { v->on = 0; continue; }
        const ent_t *e = &b->ent[v->samp];
        int dual = b->flags & FLAG_DUAL, stereo = !(b->flags & (FLAG_DUAL | FLAG_MONO));
        float bgain = b->gain * a_gain;
        float vib = 0;
        if (v->kind == VK_NOTE && vib_depth > 0) {
            float fin = (v->t - 0.25f) / 0.5f;
            fin = fin < 0 ? 0 : fin > 1 ? 1 : fin;
            vib = vib_depth * fin * vib_sin;
        }
        float bend = v->kind == VK_NOTE ? s->bend : 0;
        float pitch_mul = powf(2.0f, (bend + vib + v->glide) / 12.0f);
        if (v->glide != 0) { v->glide *= v->glide_k; if (fabsf(v->glide) < 0.002f) v->glide = 0; }
        double rate = pow(2.0, v->semis / 12.0) * pitch_mul;
        float lpk = (tone_open && v->lp_k_scale >= 1.0f) ? 1.0f
                  : 1.0f - expf(-2 * PI_F * fminf(tone_hz * v->lp_k_scale, 18000.0f) / SR);
        float ks_len = 0;
        int use_ks = v->ks_on && eng_on && a_on;
        if (use_ks) {
            ks_len = SR / (v->ks_hz * pitch_mul);
            if (ks_len > KS_LEN - 4) ks_len = KS_LEN - 4;
        }
        float vfb = ks_fb * (v->releasing ? 0.995f : 1.0f);
        if (v->palm) vfb *= 0.985f;
        float ks_amp = eng_mix * 1.0f * v->ks_gain;
        /* synth: per-block filter coefficients (cutoff follows the key by half) */
        int syn = v->syn_on && v->syn_stage < 3;
        float g_svf = 0, a1 = 0, a2 = 0, a3 = 0, dt1 = 0, dt2 = 0;
        if (syn) {
            float fc = cut0 * powf(2.0f, (v->key - 60) / 24.0f + fenv * v->syn_env);
            if (fc > 18000.0f) fc = 18000.0f;
            g_svf = tanf(PI_F * fc / SR);
            a1 = 1.0f / (1.0f + g_svf * (g_svf + kres)); a2 = g_svf * a1; a3 = g_svf * a2;
            float hz = v->syn_hz * pitch_mul;
            dt1 = hz * d_up / SR; dt2 = hz * d_dn / SR;
            if (dt1 > 0.45f) dt1 = 0.45f;
            if (dt2 > 0.45f) dt2 = 0.45f;
        }
        float ks_peak = 0, out_peak = 0;
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
            float xl = 0, xr = 0;
            if (!v->samp_done) {
                uint32_t ip = (uint32_t)v->pos;
                if (!v->loop_e && ip + 3 >= e->frames) v->samp_done = 1;
                else if (a_on) {
                    float t = (float)(v->pos - ip);
                    uint32_t i1 = ip + 1, i2 = ip + 2;
                    if (v->loop_e) {         /* the frames after the loop end are the loop start's */
                        uint32_t ll = v->loop_e - v->loop_s;
                        if (i1 >= v->loop_e) i1 -= ll;
                        if (i2 >= v->loop_e) i2 -= ll;
                    }
                    const int16_t *p0 = frame_ptr(b, v->samp, ip ? ip - 1 : 0), *p1 = frame_ptr(b, v->samp, ip),
                                  *p2 = frame_ptr(b, v->samp, i1), *p3 = frame_ptr(b, v->samp, i2);
                    if (stereo) {
                        xl = hermite(p0[0], p1[0], p2[0], p3[0], t) * (1.0f / 32768.0f);
                        xr = hermite(p0[1], p1[1], p2[1], p3[1], t) * (1.0f / 32768.0f);
                    } else {
                        float bl = dual ? blend : 0.0f;
                        float m0 = p0[0] + (p0[1] - p0[0]) * bl, m1 = p1[0] + (p1[1] - p1[0]) * bl;
                        float m2 = p2[0] + (p2[1] - p2[0]) * bl, m3 = p3[0] + (p3[1] - p3[0]) * bl;
                        xl = hermite(m0, m1, m2, m3, t) * (1.0f / 32768.0f);
                    }
                    v->pos += rate;
                } else v->pos += rate;
                if (v->loop_e && v->pos >= v->loop_e) v->pos -= v->loop_e - v->loop_s;
            }
            v->lp += lpk * (xl - v->lp);
            float x = v->lp * v->gain * bgain * env, y = 0;
            if (stereo) { v->lpr += lpk * (xr - v->lpr); y = v->lpr * v->gain * bgain * env; }
            if (use_ks) {
                float rp = (float)v->ks_w - ks_len;
                while (rp < 0) rp += KS_LEN;
                int r0 = (int)rp;
                float fr = rp - r0;
                float yy = v->ks[r0 & KS_MASK] + (v->ks[(r0 + 1) & KS_MASK] - v->ks[r0 & KS_MASK]) * fr;
                v->ks_prev += ks_c * (yy - v->ks_prev);
                float o = v->ks_prev * vfb;
                v->ks[v->ks_w & KS_MASK] = o;
                v->ks_w = (v->ks_w + 1) & KS_MASK;
                float ke = v->att * v->rel * v->fade * (v->palm ? v->denv : 1.0f);
                float ko = o * ks_amp * ke * a_gain;
                x += ko; if (stereo) y += ko;
                float ab = ko < 0 ? -ko : ko;
                if (ab > ks_peak) ks_peak = ab;
            }
            float sy = 0;
            if (syn) {
                /* ADSR */
                if (v->syn_stage == 0) { v->syn_env += att_step; if (v->syn_env >= 1) { v->syn_env = 1; v->syn_stage = 1; } }
                else if (v->syn_stage == 1) v->syn_env = sus + (v->syn_env - sus) * dec_k;
                else if (v->syn_stage == 2) { v->syn_env *= rel_k; if (v->syn_env < 1e-4f) { v->syn_env = 0; v->syn_stage = 3; } }
                float o = 0.5f * (osc(wave, v->ph1, dt1) + osc(wave, v->ph2, dt2));
                v->ph1 += dt1; if (v->ph1 >= 1) v->ph1 -= 1;
                v->ph2 += dt2; if (v->ph2 >= 1) v->ph2 -= 1;
                /* TPT state-variable low-pass */
                float v3 = o - v->ic2, v1 = a1 * v->ic1 + a2 * v3, v2 = v->ic2 + a2 * v->ic1 + a3 * v3;
                v->ic1 = 2 * v1 - v->ic1; v->ic2 = 2 * v2 - v->ic2;
                sy = v2 * v->syn_env * v->syn_vel * b_gain * v->fade;
            }
            float ax = (x < 0 ? -x : x) + (sy < 0 ? -sy : sy);
            if (ax > out_peak) out_peak = ax;
            if (stereo) { mixL[i] += (x + sy) * v->pl; mixR[i] += (y + sy) * v->pr; }   /* same power as the centred mono path */
            else { mixL[i] += (x + sy) * v->pl; mixR[i] += (x + sy) * v->pr; }
        }
        v->t += (frames - start) / SR;
        v->meter = out_peak > v->meter * 0.97f ? out_peak : v->meter * 0.97f;
        if (use_ks) { v->ks_level = ks_peak; if (ks_peak < 1e-4f && v->t > 0.2f) v->ks_on = 0; }
        /* a voice ends when its sample, its pluck layer and its synth are all spent */
        if (v->delay == 0 && v->att >= 1) {
            int ks_alive = use_ks && v->ks_on && v->rel * v->fade * (v->palm ? v->denv : 1.0f) > 1e-4f;
            int smp_alive = a_on && !v->samp_done && v->denv * v->rel * v->fade > 1e-4f;
            int syn_alive = v->syn_on && v->syn_stage < 3;
            if (v->fade <= 0 || (!ks_alive && !smp_alive && !syn_alive)) {
                v->on = 0;
                for (int k = 0; k < NSTR; k++) if (s->str_voice[k] == vi) s->str_voice[k] = -1;
            }
        }
    }
    s->clock += frames / SR;
    /* string pictures: how hard each string rings (0-3) and which fret is lit (0 none, 1 open, 2.. fret 1..) */
    for (int st = 0; st < NSTR; st++) {
        int vi = s->str_voice[st], vib = 0, fret = 0;
        if (vi >= 0 && s->v[vi].on && s->v[vi].kind == VK_NOTE && s->v[vi].delay == 0) {
            const voice_t *v = &s->v[vi];
            float lvl = v->meter;
            vib = lvl > 0.10f ? 3 : lvl > 0.03f ? 2 : lvl > 0.006f ? 1 : 0;
            if (vib) { fret = s->str_fret[st] + 1; if (fret < 1) fret = 1; if (fret > 16) fret = 16; }
        }
        if (vib != s->disp_vib[st] || fret != s->disp_fret[st]) { s->disp_vib[st] = (uint8_t)vib; s->disp_fret[st] = (uint8_t)fret; s->rev++; }
    }

    /* ---- effects: drive -> chorus -> space -> volume -> soft limit ---- */
    float drive = s->p[P_DRIVE], chorus = s->p[P_CHORUS];
    float space = s->p[P_SPACE];
    float vol = s->p[P_VOLUME] / 0.9f;
    float dry = (1.0f - 0.35f * space) * vol, wet = space * vol * 0.9f;
    float pre = 1.0f + 30.0f * drive * drive, bias = 0.15f * drive, tb = tanhf(bias);
    float comp = 0.35f / tanhf(0.35f * pre) / (1.0f + 3.0f * drive);            /* clipping raises the average: keep it even */
    float cab_k = 1.0f - expf(-2 * PI_F * (9000.0f - 5500.0f * drive) / SR);      /* speaker-ish top end */
    float hp_k = 1.0f - expf(-2 * PI_F * 90.0f / SR);                            /* tighten the lows before clipping */
    float cho_rate = 2 * PI_F * 0.8f / SR, cho_depth = 0.0035f * SR * chorus, cho_base = 0.012f * SR;
    float echo = s->p[P_ECHO];
    float dly_target = echo_seconds(s) * SR;                       /* 60 ms .. 1 s, or a note length (ECHO SYNC) */
    if (s->dly_t <= 0) s->dly_t = dly_target;
    float dly_fb = 0.22f + 0.5f * echo, dly_wet = 0.65f * echo;
    float dly_lpk = 1.0f - expf(-2 * PI_F * 3200.0f / SR);                           /* each repeat a little darker */
    for (int i = 0; i < frames; i++) {
        float ch[2] = { mixL[i], mixR[i] };
        if (drive > 0.001f)
            for (int c = 0; c < 2; c++) {
                float xin = ch[c];
                s->drv_hp[c] += hp_k * (xin - s->drv_hp[c]);
                float xh = xin - s->drv_hp[c] * (0.6f * drive);
                float yv = (tanhf(xh * pre + bias) - tb) * comp;
                s->drv_lp[c] += cab_k * (yv - s->drv_lp[c]);
                /* DC blocker: the bias makes the clipper slightly asymmetric */
                float o = s->drv_lp[c] - s->drv_dc_x[c] + 0.995f * s->drv_dc_y[c];
                s->drv_dc_x[c] = s->drv_lp[c]; s->drv_dc_y[c] = o;
                ch[c] = o;
            }
        if (chorus > 0.001f) {
            s->cho_ph += cho_rate; if (s->cho_ph > 2 * PI_F) s->cho_ph -= 2 * PI_F;
            int w = s->cho_w;
            s->cho[w] = ch[0]; s->cho[2048 + w] = ch[1];
            for (int c = 0; c < 2; c++) {
                float d = cho_base + cho_depth * (1.0f + (c ? cosf(s->cho_ph) : sinf(s->cho_ph)));
                float rp = (float)w - d;
                while (rp < 0) rp += 2048;
                int r0 = (int)rp; float fr = rp - r0;
                const float *buf = s->cho + c * 2048;
                float dl = buf[r0 & 2047] + (buf[(r0 + 1) & 2047] - buf[r0 & 2047]) * fr;
                ch[c] = ch[c] * (1.0f - 0.3f * chorus) + dl * 0.75f * chorus;
            }
            s->cho_w = (w + 1) & 2047;
        }
        if (echo > 0.004f) {      /* ping-pong: the left line feeds the right and back, so repeats walk across */
            s->dly_t += 0.0005f * (dly_target - s->dly_t);           /* a moved TIME knob glides, no clicks */
            float rp = (float)s->dly_w - s->dly_t;
            while (rp < 0) rp += DLY_LEN;
            int r0 = (int)rp; float fr = rp - r0;
            int r1 = r0 + 1 < DLY_LEN ? r0 + 1 : 0;
            float yl = s->dly[r0] + (s->dly[r1] - s->dly[r0]) * fr;
            float yr = s->dly[DLY_LEN + r0] + (s->dly[DLY_LEN + r1] - s->dly[DLY_LEN + r0]) * fr;
            s->dly_lp[0] += dly_lpk * (yl - s->dly_lp[0]);
            s->dly_lp[1] += dly_lpk * (yr - s->dly_lp[1]);
            s->dly[s->dly_w] = 0.5f * (ch[0] + ch[1]) + dly_fb * s->dly_lp[1];
            s->dly[DLY_LEN + s->dly_w] = dly_fb * s->dly_lp[0];
            if (++s->dly_w >= DLY_LEN) s->dly_w = 0;
            ch[0] += dly_wet * yl; ch[1] += dly_wet * yr;
        } else if (s->dly_lp[0] != 0 || s->dly_lp[1] != 0) {      /* switched off: forget the old repeats */
            memset(s->dly, 0, 2 * DLY_LEN * sizeof(float)); s->dly_lp[0] = s->dly_lp[1] = 0; s->dly_t = dly_target;
        }
        float rl = 0, rr = 0;
        if (space > 0.001f) reverb_frame(s, (ch[0] + ch[1]) * 0.03f, &rl, &rr);
        float ol = ch[0] * dry + rl * wet, orr = ch[1] * dry + rr * wet;
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

/* For the offline tools (tools/render_demo.c): an instrument's load state (2 = ready). */
int fretty_bank_state(void) { return bank_state_of(0); }
int fretty_bank_state_of(int i) { return i >= 0 && i < NINST ? bank_state_of(i) : -1; }
int fretty_ninst(void) { return NINST; }
