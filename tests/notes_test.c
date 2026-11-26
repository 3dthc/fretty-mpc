/* Playing notes must not rebuild MPC's screen: an UpdateDisplay closes (or jumps) MPC's PRESET popup. Run by
 * tests/program_test.sh, beside engine/ so the instruments load. */
#include <stdint.h>
#include <stdio.h>
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
int main(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, 0, 0, 0, NULL, 0); a->d(a, 10, 0, 0, NULL, 44100); a->d(a, 11, 0, 128, NULL, 0); a->d(a, 12, 0, 1, NULL, 0);
    run(a, 400); usleep(2500000); run(a, 400);
    updates = 0; memset(autom, 0, sizeof autom);
    int keys[8] = {52, 55, 59, 64, 57, 60, 64, 69};
    for (int r = 0; r < 4; r++) for (int k = 0; k < 8; k++) { midi(a, 0x90, keys[k], 100); run(a, 60); midi(a, 0x80, keys[k], 0); run(a, 20); }
    run(a, 800);
    printf("%s 32 notes played: %d full screen rebuilds (UpdateDisplay; each closes MPC's PRESET popup)\n", updates ? "FAIL" : "ok  ", updates); fails += updates != 0;
    {   int ia4 = NPARAMS - 1; char nm[64] = {0}; a->d(a, 8, ia4, 0, nm, 0);
        double f440 = pitch_at(a, ia4, 25.0f / 51), f466 = pitch_at(a, ia4, 1.0f), r = f466 / f440;
        int ok = !strncmp(nm, "A4", 2) && r > 1.045 && r < 1.075;
        printf("%s A4 TUNE moves the pitch: %.1f Hz at 440, %.1f Hz at 466 (ratio %.3f, want 1.059) [%s]\n", ok ? "ok  " : "FAIL", f440, f466, r, nm);
        fails += !ok; a->setP(a, ia4, 25.0f / 51); }
    a->d(a, 1, 0, 0, NULL, 0);                 /* effClose */
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails;
}
