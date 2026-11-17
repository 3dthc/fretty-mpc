/* MPC's preset name follows the BROWSE page (vst.json ENGINE_PROGRAM): the plugin through the real VST2 wrapper, as MPC
 * calls it. tests/program_test.sh builds it. */
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
static int updates;
static intptr_t host(AEffect *e, int32_t op, int32_t i, intptr_t v, void *p, float o) {
    (void)e; (void)i; (void)v; (void)o;
    if (op == 1) return 2400;                 /* audioMasterVersion */
    if (op == 42) updates++;                  /* audioMasterUpdateDisplay */
    if (op == 7) return 0;
    (void)p; return 0;
}
static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf(ok_ ? "ok   " : "FAIL "); printf(__VA_ARGS__); putchar('\n'); fails += !ok_; } while (0)
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
static void run(AEffect *a, int blocks) {
    float L[128], R[128], *o[2] = {L, R};
    for (int k = 0; k < blocks; k++) a->pr(a, NULL, o, 128);
}
static void tap(AEffect *a, const char *k) { a->setP(a, idx(k), 1.0f); run(a, 4); usleep(300000); a->setP(a, idx(k), 0.5f); run(a, 120); }
static void name(AEffect *a, char *out) { out[0] = 0; a->d(a, 5, 0, 0, out, 0); }   /* effGetProgramName */
int main(void) {
    AEffect *a = VSTPluginMain(host);
    a->d(a, 0, 0, 0, NULL, 0); a->d(a, 10, 0, 0, NULL, 44100); a->d(a, 11, 0, 128, NULL, 0); a->d(a, 12, 0, 1, NULL, 0);
    char n[64];
    run(a, 50); usleep(2100000); run(a, 50);   /* past the quiet time after opening */
    tap(a, "cpick_1");                          /* ALL */
    tap(a, "rpick_5");
    int p = (int)a->d(a, 3, 0, 0, NULL, 0); name(a, n);   /* effGetProgram */
    CHECK(p == 4 && !strcmp(n, "80s | Dreamy Bloom"), "BROWSE, ALL row 5: MPC's current preset is #%d \"%s\"", p, n);
    CHECK(updates > 0, "MPC is told to redraw (%d UpdateDisplay)", updates);
    tap(a, "rpick_12");
    p = (int)a->d(a, 3, 0, 0, NULL, 0); name(a, n);
    CHECK(p == 11 && strcmp(n, "80s | Dreamy Bloom"), "row 12: #%d \"%s\"", p, n);
    a->d(a, 2, 0, 30, NULL, 0); run(a, 120);    /* effSetProgram from MPC's PRESET menu */
    p = (int)a->d(a, 3, 0, 0, NULL, 0); run(a, 120); usleep(300000); run(a, 120);
    int p2 = (int)a->d(a, 3, 0, 0, NULL, 0);
    CHECK(p == 30 && p2 == 30, "a pick in MPC's menu stays (#%d, then #%d)", p, p2);
    a->d(a, 1, 0, 0, NULL, 0);
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails != 0;
}
