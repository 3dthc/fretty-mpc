/* Offline demo: drives the Archtop engine like MPC would (128-frame blocks, MIDI before each block) and writes
 * a WAV, so the sound can be checked on a PC before it goes near a device.
 *   cc -O2 -Iwrapper-dir tools/render_demo.c src/engine.c -lm -lpthread -o render_demo
 *   ./render_demo <dir with archtop.smp> out.wav */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine.h"

int archtop_bank_state(void);

typedef struct { double t; uint8_t a, b, c; const char *pk, *pv; } ev_t;
static ev_t evs[4096];
static int nev;
static void on(double t, int k, int v) { evs[nev++] = (ev_t){ t, 0x90, (uint8_t)k, (uint8_t)v, 0, 0 }; }
static void off(double t, int k) { evs[nev++] = (ev_t){ t, 0x80, (uint8_t)k, 0, 0, 0 }; }
static void note(double t, double d, int k, int v) { on(t, k, v); off(t + d, k); }
static void prm(double t, const char *k, const char *v) { evs[nev++] = (ev_t){ t, 0, 0, 0, k, v }; }
static void chord(double t, double d, const int *ks, int n, int v) { for (int i = 0; i < n; i++) note(t, d, ks[i], v); }
static int cmp(const void *a, const void *b) {
    double x = ((const ev_t *)a)->t, y = ((const ev_t *)b)->t;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <data dir> out.wav\n", argv[0]); return 2; }
    const mpc_engine_t *E = mpc_engine();
    void *s = E->create(argv[1]);
    for (int i = 0; i < 3000 && archtop_bank_state() == 1; i++) usleep(10000);
    if (archtop_bank_state() != 2) { fprintf(stderr, "bank not ready (%d)\n", archtop_bank_state()); return 1; }

    const int C[] = { 48, 52, 55, 60, 64 }, Am[] = { 45, 52, 57, 60, 64 }, F[] = { 41, 48, 53, 57, 60, 65 },
              G[] = { 43, 47, 50, 55, 59, 67 };
    double t = 0.2;
    if (argc > 3 && !strcmp(argv[3], "pitch")) {      /* every note 40..84, engine off, for a pitch check */
        prm(0, "engine", "0"); prm(0, "space", "0"); prm(0, "noise", "0");
        for (int k = 40; k <= 84; k++) { note(t, 0.9, k, 100); t += 1.0; }
        goto run;
    }
    /* 1. strummed progression, default settings */
    const int *prog[] = { C, Am, F, G };
    int pn[] = { 5, 5, 6, 6 };
    for (int i = 0; i < 4; i++) { chord(t, 1.6, prog[i], pn[i], 100); t += 1.8; }
    t += 0.8;
    /* 2. single-note melody with legato slurs */
    int mel[] = { 64, 67, 69, 67, 69, 71, 72, 71, 69, 67, 64 };
    for (int i = 0; i < 11; i++) { note(t, (i % 3 == 1) ? 0.36 : 0.30, mel[i], 90 + (i % 3) * 10); t += 0.28; }
    t += 1.2;
    /* 3. palm-muted riff */
    prm(t, "artic", "1");
    int riff[] = { 40, 40, 43, 40, 45, 40, 43, 47 };
    for (int i = 0; i < 16; i++) { note(t, 0.14, riff[i % 8], 80 + (i % 4 == 0) * 40); t += 0.16; }
    prm(t, "artic", "0");
    t += 0.6;
    /* 4. same chord: pickup only, mic only, engine off vs heavy engine */
    prm(t, "blend", "0"); chord(t, 1.4, C, 5, 100); t += 1.8;
    prm(t, "blend", "1"); chord(t, 1.4, C, 5, 100); t += 1.8;
    prm(t, "blend", "0.35"); prm(t, "engine", "0"); chord(t, 1.4, G, 6, 100); t += 1.8;
    prm(t, "engine", "1"); prm(t, "eng_mix", "0.8"); prm(t, "eng_sustain", "0.95"); chord(t, 2.5, G, 6, 100); t += 3.0;
    prm(t, "eng_mix", "0.25"); prm(t, "eng_sustain", "0.6");
    /* 5. low notes, drop tuning, dead notes and knocks */
    prm(t, "drop", "1"); note(t, 1.5, 37, 110); t += 1.6; prm(t, "drop", "0");
    for (int k = 12; k < 28; k++) { if (k == 22 || k == 23) continue; note(t, 0.1, k, 100); t += 0.18; }
    t += 1.5;

run:
    qsort(evs, nev, sizeof evs[0], cmp);
    int total = (int)(t * 44100) / 128 + 1, ei = 0;
    int16_t *pcm = malloc((size_t)total * 128 * 4);
    long clips = 0; int peak = 0;
    for (int blk = 0; blk < total; blk++) {
        double bt = (blk + 1) * 128 / 44100.0;
        while (ei < nev && evs[ei].t < bt) {
            if (evs[ei].pk) E->set_param(s, evs[ei].pk, evs[ei].pv);
            else { uint8_t m[3] = { evs[ei].a, evs[ei].b, evs[ei].c }; E->midi(s, m, 3); }
            ei++;
        }
        E->render(s, pcm + (size_t)blk * 256, 128);
        for (int i = 0; i < 256; i++) {
            int a = abs(pcm[(size_t)blk * 256 + i]);
            if (a > peak) peak = a;
            if (a >= 32700) clips++;
        }
    }
    char buf[64];
    E->get_param(s, "chord", buf, sizeof buf);
    printf("rendered %.1f s, peak %.1f dBFS, %ld samples at the limit, last chord '%s'\n", t, 20 * __builtin_log10(peak / 32768.0 + 1e-9), clips, buf);
    E->destroy(s);
    FILE *f = fopen(argv[2], "wb");
    uint32_t data = (uint32_t)total * 128 * 4, x;
    fwrite("RIFF", 1, 4, f); x = 36 + data; fwrite(&x, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    x = 16; fwrite(&x, 4, 1, f);
    uint16_t h[2] = { 1, 2 }; fwrite(h, 2, 2, f);
    x = 44100; fwrite(&x, 4, 1, f); x = 44100 * 4; fwrite(&x, 4, 1, f);
    h[0] = 4; h[1] = 16; fwrite(h, 2, 2, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(pcm, 1, data, f);
    fclose(f);
    free(pcm);
    return 0;
}
