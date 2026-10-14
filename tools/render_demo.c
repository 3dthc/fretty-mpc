/* Offline demo: drives the Fretty engine like MPC would (128-frame blocks, MIDI before each block) and writes
 * a WAV, so the sound can be checked on a PC before it goes near a device.
 *   cc -O2 -Iwrapper-dir tools/render_demo.c src/engine.c -lm -lpthread -o render_demo
 *   ./render_demo <dir with fretty.smp> out.wav */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine.h"

int fretty_bank_state(void);
int fretty_bank_state_of(int i);
int fretty_ninst(void);

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
    {   /* load every instrument first (the plugin loads one when it is first selected) */
        int16_t tmp[256];
        static const int CHOICE_OF_BANK[] = { 0, 1, 2, 3, 4, 5, 7, 8 };   /* one INSTRUMENT choice per sample file */
        for (int k = 0; k < fretty_ninst(); k++) {
            char v[16]; snprintf(v, sizeof v, "%d", CHOICE_OF_BANK[k]);
            E->set_param(s, "instrument", v);
            E->render(s, tmp, 128);
            for (int w = 0; w < 3000 && fretty_bank_state_of(k) == 1; w++) usleep(10000);
            if (fretty_bank_state_of(k) != 2) { fprintf(stderr, "instrument %d not ready (%d)\n", k, fretty_bank_state_of(k)); return 1; }
        }
        E->set_param(s, "instrument", "0");
    }

    const int C[] = { 48, 52, 55, 60, 64 }, Am[] = { 45, 52, 57, 60, 64 }, F[] = { 41, 48, 53, 57, 60, 65 },
              G[] = { 43, 47, 50, 55, 59, 67 };
    double t = 0.2;
    if (argc > 3 && !strcmp(argv[3], "pitch")) {      /* every note 40..84, engine off, for a pitch check */
        prm(0, "eng_mix", "0"); prm(0, "space", "0"); prm(0, "noise", "0");
        if (argc > 4) prm(0, "instrument", argv[4]);
        for (int k = 40; k <= 84; k++) { note(t, 0.9, k, 100); t += 1.0; }
        goto run;
    }
    if (argc > 3 && !strcmp(argv[3], "violin")) {     /* bowed: long held notes (the loop), legato, chords; then pizz */
        prm(0, "instrument", "5"); prm(0, "space", "0.25");
        note(t, 7.0, 55, 90); t += 7.6;                                   /* G3 held 7 s */
        note(t, 6.0, 76, 115); t += 6.6;                                  /* E5 loud, 6 s */
        note(t, 4.0, 69, 45); t += 4.6;                                   /* A4 soft */
        prm(t, "legato", "1");
        int mel[] = { 67, 69, 71, 72, 74, 72, 71, 69, 67 };
        for (int q = 0; q < 9; q++) { note(t, 0.62, mel[q], 95); t += 0.55; }
        t += 1.2;
        chord(t, 2.5, (int[]){ 55, 62, 71 }, 3, 100); t += 3.2;
        prm(t, "a_attack", "0.6"); note(t, 3.0, 64, 100); t += 3.8; prm(t, "a_attack", "0");
        prm(t, "instrument", "6");                                         /* pizzicato */
        for (int q = 0; q < 12; q++) { note(t, 0.2, mel[q % 9] - 12 * (q % 2), 70 + (q % 3) * 20); t += 0.25; }
        chord(t, 1.0, C, 5, 100); t += 1.8;
        goto run;
    }
    if (argc > 3 && !strcmp(argv[3], "glide")) {      /* steel-style slides on the electric, then the dan tranh */
        static const char *IN[2] = { "1", "7" };
        for (int k = 0; k < 2; k++) {
            prm(t, "instrument", IN[k]); prm(t, "legato", "1"); prm(t, "glide", "0.45"); prm(t, "vibrato", "0.2");
            prm(t, "strum_mode", "0"); prm(t, "space", "0.3");
            int line[] = { 64, 67, 69, 67, 64, 62, 64 };
            for (int q = 0; q < 7; q++) { note(t, 0.75, line[q], 100); t += 0.6; }
            t += 1.5;
        }
        goto run;
    }
    if (argc > 3 && !strcmp(argv[3], "chords")) {     /* chord pads: one note per chord, A minor, three voicings */
        prm(0, "chord_mode", "1"); prm(0, "chord_key", "9"); prm(0, "chord_scale", "1");
        prm(0, "strum_mode", "3"); prm(0, "strum_speed", "0.35"); prm(0, "space", "0.3");
        prm(0, "hum_time", "0.5"); prm(0, "hum_vel", "0.4");
        int prog[] = { 57, 65, 60, 67 };                                  /* i - VI - III - VII: Am F C G */
        for (int v = 0; v < 3; v++) {
            char vs[8]; snprintf(vs, sizeof vs, "%d", v); prm(t, "chord_voicing", vs);
            if (v == 2) { prm(t, "chord_mode", "2"); prm(t, "instrument", "1"); prm(t, "chorus", "0.6");
                          prm(t, "echo", "0.3"); prm(t, "echo_sync", "1"); prm(t, "echo_time", "0.45"); }
            for (int q = 0; q < 4; q++) for (int r = 0; r < 4; r++) { note(t, 0.32, prog[q], 85 + (r == 0) * 25); t += 0.3; }
            t += 0.6;
        }
        goto run;
    }
    if (argc > 3 && !strcmp(argv[3], "strum")) {      /* strum modes: down, up, alternate */
        for (int m = 1; m <= 3; m++) {
            char mv[16]; snprintf(mv, sizeof mv, "%d", m);
            prm(t, "strum_mode", mv);
            for (int q = 0; q < 4; q++) { chord(t, 0.4, G, 6, 100); t += 0.45; }
            t += 1.0;
        }
        goto run;
    }
    if (argc > 3 && !strcmp(argv[3], "tour")) {       /* every instrument, the effects and the synth */
        static const char *IN[10] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
        for (int k = 0; k < 10; k++) {
            prm(t, "instrument", IN[k]);
            chord(t + 0.05, 1.4, C, 5, 100); t += 1.6;
            int m[] = { 64, 67, 69, 72 };
            for (int q = 0; q < 4; q++) { note(t, 0.3, m[q], 70 + q * 15); t += 0.3; }
            t += 0.8;
        }
        prm(t, "instrument", "1");                                       /* electric: crunch, then chorus */
        prm(t, "drive", "0.55"); chord(t + 0.05, 1.4, G, 6, 110); t += 1.8;
        prm(t, "drive", "0.9"); for (int q = 0; q < 8; q++) { note(t, 0.14, 40 + (q % 2) * 7, 120); t += 0.18; } t += 0.5;
        prm(t, "drive", "0"); prm(t, "chorus", "0.7"); chord(t + 0.05, 1.8, Am, 5, 95); t += 2.2;
        prm(t, "chorus", "0");
        prm(t, "instrument", "0"); prm(t, "b_on", "1");                  /* archtop + synth pad */
        prm(t, "wave", "2"); prm(t, "attack", "0.45"); prm(t, "release", "0.6"); prm(t, "cutoff", "0.5");
        chord(t + 0.05, 2.5, F, 6, 90); t += 3.5;
        prm(t, "a_on", "0"); prm(t, "wave", "3"); prm(t, "attack", "0.02"); prm(t, "release", "0.3");   /* synth solo */
        prm(t, "cutoff", "0.55"); prm(t, "resonance", "0.6");
        for (int q = 0; q < 8; q++) { note(t, 0.2, 52 + (q * 5) % 12, 100); t += 0.25; }
        t += 1.0;
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
    prm(t, "blend", "0.35"); chord(t, 1.4, G, 6, 100); t += 1.8;
    prm(t, "eng_mix", "0.8"); prm(t, "eng_sustain", "0.95"); chord(t, 2.5, G, 6, 100); t += 3.0;
    prm(t, "eng_mix", "0"); prm(t, "eng_sustain", "0.6");
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
