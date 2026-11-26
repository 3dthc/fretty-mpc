/* Playing notes must not rebuild MPC's screen: an UpdateDisplay closes (or jumps) MPC's PRESET popup. Run by
 * tests/program_test.sh, beside engine/ so the instruments load. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "params.h"
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect *, int32_t, int32_t, intptr_t, void *, float);
    void *p; void (*setP)(AEffect *, int32_t, float); float (*getP)(AEffect *, int32_t);
    int32_t nprog, npar, ni, no, flags; intptr_t r1, r2; int32_t delay, ir1, ir2; float ratio; void *obj, *user; int32_t uid, ver;
    void (*pr)(AEffect *, float **, float **, int32_t); void *prd; char fut[56]; };
extern AEffect *VSTPluginMain(cb);
static int updates, autom[NPARAMS];
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)v; (void)p; (void)o;
    if (op == 1) return 2400; if (op == 42) updates++; if (op == 0 && i >= 0 && i < NPARAMS) autom[i]++; return 0; }
typedef struct { int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset; unsigned char m[4]; char x[12]; } ME;
typedef struct { int32_t n; intptr_t r; void *ev[2]; } EV;
static void midi(AEffect *a, int s, int k, int v) { ME e = {1, sizeof(ME), 0, 0, 0, 0, {(unsigned char)s, (unsigned char)k, (unsigned char)v, 0}, {0}}; EV ev = {1, 0, {&e, 0}}; a->d(a, 25, 0, 0, &ev, 0); }
static void run(AEffect *a, int blocks) { float L[128], R[128], *o[2] = {L, R}; for (int k = 0; k < blocks; k++) a->pr(a, NULL, o, 128); }
static int fails;
/* A4 TUNE: play A4 and find its period by autocorrelation (lag 60-160 samples, interpolated), at a given knob position */
static double pitch_at(AEffect *a, int ia4, float norm) {
    static float buf[8192];
    a->setP(a, ia4, norm); run(a, 40);
    midi(a, 0x90, 69, 100); run(a, 20);
    float L[128], R[128], *o[2] = {L, R};
    for (int k = 0; k < 64; k++) { a->pr(a, NULL, o, 128); memcpy(buf + k * 128, L, sizeof L); }
    midi(a, 0x80, 69, 0); run(a, 400);
    double c[200] = {0}; int best = 60;
    for (int lag = 55; lag <= 165; lag++) { for (int i = 0; i + lag < 8192; i++) c[lag] += buf[i] * buf[i + lag]; }
    for (int lag = 60; lag <= 160; lag++) if (c[lag] > c[best]) best = lag;
    double d = (c[best - 1] - c[best + 1]) / (2 * (c[best - 1] - 2 * c[best] + c[best + 1]));
    return 44100.0 / (best + d);
}
/* the fretboard dots: whatever string shows a dot, its drawn note (standard tuning, DROP D\u266d on the low string,
 * counted from the nut) must be the note just played, with or without CAPO and DROP */
static int find_param(AEffect *a, const char *key) {
    (void)a; for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, key)) return i;
    return -1;
}
static int fret_dots(AEffect *a) {
    static const int TUN[6] = {40, 45, 50, 55, 59, 64};
    int ic = find_param(a, "capo"), id = find_param(a, "drop"), is[6], bad = 0, seen = 0;
    for (int k = 0; k < 6; k++) { char nm[32]; snprintf(nm, 32, "s%d_fret", k + 1); is[k] = find_param(a, nm); }
    if (ic < 0 || id < 0 || is[0] < 0) { printf("FAIL fretboard test: params not found\n"); return 1; }
    int capos[3] = {0, 3, 7}, recent[4] = {-1, -1, -1, -1};
    for (int c = 0; c < 3; c++) for (int dr = 0; dr < 2; dr++) {
        a->setP(a, ic, capos[c] / 9.0f); a->setP(a, id, (float)dr); run(a, 20);
        for (int key = 38; key <= 84; key++) {
            midi(a, 0x90, key, 110); run(a, 6);
            for (int st = 0; st < 6; st++) {
                char t[16] = {0}; a->d(a, 7, is[st], 0, t, 0); int f = atoi(t);
                if (f > 0) { seen++; int note = TUN[st] - (st == 0 && dr ? 3 : 0) + (f - 1);
                    int ok = note == key; for (int r = 0; r < 4; r++) ok |= note == recent[r];   /* a string still ringing */
                    if (!ok) { if (bad < 5) printf("     capo %d drop %d: played %d, string %d shows fret %d (note %d)\n", capos[c], dr, key, st + 1, f - 1, note); bad++; } }
            }
            midi(a, 0x80, key, 0); run(a, 60);
            recent[3] = recent[2]; recent[2] = recent[1]; recent[1] = recent[0]; recent[0] = key;
        }
    }
    a->setP(a, ic, 0); a->setP(a, id, 0);
    printf("%s fretboard dots land on the played note (capo 0/3/7, drop off/on; %d dots, %d wrong)\n", bad || !seen ? "FAIL" : "ok  ", seen, bad);
    return bad || !seen;
}
int main(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, 0, 0, 0, NULL, 0); a->d(a, 10, 0, 0, NULL, 44100); a->d(a, 11, 0, 128, NULL, 0); a->d(a, 12, 0, 1, NULL, 0);
    run(a, 400); usleep(2500000); run(a, 400);
    updates = 0; memset(autom, 0, sizeof autom);
    int keys[8] = {52, 55, 59, 64, 57, 60, 64, 69};
    for (int r = 0; r < 4; r++) for (int k = 0; k < 8; k++) { midi(a, 0x90, keys[k], 100); run(a, 60); midi(a, 0x80, keys[k], 0); run(a, 20); }
    run(a, 800);
    printf("%s 32 notes played: %d full screen rebuilds (UpdateDisplay; each closes MPC's PRESET popup)\n", updates ? "FAIL" : "ok  ", updates); fails += updates != 0;
    fails += fret_dots(a);
    {   int ia4 = NPARAMS - 1; char nm[64] = {0}; a->d(a, 8, ia4, 0, nm, 0);
        double f440 = pitch_at(a, ia4, 25.0f / 51), f466 = pitch_at(a, ia4, 1.0f), r = f466 / f440;
        int ok = !strncmp(nm, "A4", 2) && r > 1.045 && r < 1.075;
        printf("%s A4 TUNE moves the pitch: %.1f Hz at 440, %.1f Hz at 466 (ratio %.3f, want 1.059) [%s]\n", ok ? "ok  " : "FAIL", f440, f466, r, nm);
        fails += !ok; a->setP(a, ia4, 25.0f / 51); }
    a->d(a, 1, 0, 0, NULL, 0);                 /* effClose */
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails;
}
