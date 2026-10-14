/* Engine state for tools/make_shots.py: reads "key value" lines (a preset, plus extras) from stdin, plays a chord,
 * then prints every parameter as "key<TAB>value<TAB>display text", and, with "strum <ms>" as argv[2], the string
 * pictures every <ms> while a chord rings ("t<TAB>s1_vib..s6_vib s1_fret..s6_fret").
 *   cc -O2 -I<mpc-vst-plugins>/wrapper tools/shot_state.c src/engine.c -lm -lpthread -o shot_state
 *   ./shot_state engine [strum 40] < settings.txt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine.h"
int fretty_bank_state_of(int i);
static const int BANK_OF[10] = { 0, 1, 2, 3, 4, 5, 5, 6, 7, 7 };
int main(int argc, char **argv) {
    const mpc_engine_t *E = mpc_engine();
    void *s = E->create(argv[1]);
    char k[64], v[64], keys[256][64]; int nk = 0, chord[8] = { 45, 52, 57, 60, 64 }, nc = 5, play = 1;
    while (scanf("%63s %63s", k, v) == 2) {
        if (!strcmp(k, "#key")) { snprintf(keys[nk++], 64, "%s", v); continue; }   /* a key to report */
        if (!strcmp(k, "#noplay")) { play = 0; continue; }
        if (!strcmp(k, "#chord")) { nc = 0; char *p = v; while (*p && nc < 8) { chord[nc++] = (int)strtol(p, &p, 10); if (*p == ',') p++; } continue; }
        E->set_param(s, k, v);
    }
    int16_t buf[256];
    char ib[32]; E->get_param(s, "instrument", ib, sizeof ib);
    int bank = BANK_OF[atoi(ib) < 0 ? 0 : atoi(ib) > 9 ? 9 : atoi(ib)];
    E->render(s, buf, 128);
    while (fretty_bank_state_of(bank) == 1) { usleep(10000); E->render(s, buf, 128); }
    int strum = argc > 3 && !strcmp(argv[2], "strum") ? atoi(argv[3]) : 0;
    if (play) for (int i = 0; i < nc; i++) { uint8_t m[3] = { 0x90, (uint8_t)chord[i], 100 }; E->midi(s, m, 3); }
    int blocks = strum ? 344 * 4 : 70, every = strum ? (int)(strum * 44.1 / 128 + 0.5) : 0;
    for (int b = 0; b < blocks; b++) {
        if (strum && b == 344 * 2 && play) for (int i = 0; i < nc; i++) { uint8_t m[3] = { 0x80, (uint8_t)chord[i], 0 }; E->midi(s, m, 3); }
        E->render(s, buf, 128);
        if (strum && b % every == 0) {
            printf("t\t%.3f", b * 128 / 44100.0);
            for (int st = 1; st <= 6; st++) { char kk[16], o[16]; snprintf(kk, 16, "s%d_vib", st); E->get_param(s, kk, o, 16); printf("\t%s", o); }
            for (int st = 1; st <= 6; st++) { char kk[16], o[16]; snprintf(kk, 16, "s%d_fret", st); E->get_param(s, kk, o, 16); printf("\t%s", o); }
            printf("\n");
        }
    }
    if (!strum)
        for (int i = 0; i < nk; i++) {
            char val[64] = "", disp[64] = "", dk[96];
            E->get_param(s, keys[i], val, sizeof val);
            snprintf(dk, sizeof dk, "%s_display", keys[i]);
            if (E->get_param(s, dk, disp, sizeof disp) <= 0) disp[0] = 0;
            printf("%s\t%s\t%s\n", keys[i], val, disp);
        }
    E->destroy(s);
    return 0;
}
