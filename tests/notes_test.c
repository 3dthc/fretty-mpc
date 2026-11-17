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
int main(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, 0, 0, 0, NULL, 0); a->d(a, 10, 0, 0, NULL, 44100); a->d(a, 11, 0, 128, NULL, 0); a->d(a, 12, 0, 1, NULL, 0);
    run(a, 400); usleep(2500000); run(a, 400);
    updates = 0; memset(autom, 0, sizeof autom);
    int keys[8] = {52, 55, 59, 64, 57, 60, 64, 69};
    for (int r = 0; r < 4; r++) for (int k = 0; k < 8; k++) { midi(a, 0x90, keys[k], 100); run(a, 60); midi(a, 0x80, keys[k], 0); run(a, 20); }
    run(a, 800);
    printf("%s 32 notes played: %d full screen rebuilds (UpdateDisplay; each closes MPC's PRESET popup)\n", updates ? "FAIL" : "ok  ", updates); fails += updates != 0;
    a->d(a, 1, 0, 0, NULL, 0);                 /* effClose */
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails;
}
