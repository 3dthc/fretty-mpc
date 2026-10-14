/* Lossless check: decode every engine/<instrument>.smp with the engine's own loader and compare it with the raw 16-bit
 * audio the packer dumped (FRETTY_DUMP_RAW=<dir> python3 tools/pack_samples.py ... writes <dir>/<instrument>.raw).
 *   cc -O2 -I<mpc-vst-plugins>/wrapper tools/check_codec.c -lm -lpthread -o check_codec && ./check_codec engine <dir> */
#include "../src/engine.c"
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    void *s = mpc_engine()->create(argv[1]);
    int bad = 0;
    for (int b = 0; b < NINST; b++) {
        bank_request(b);
        while (bank_state_of(b) == BANK_LOADING) usleep(10000);
        if (bank_state_of(b) != BANK_READY) { printf("FAIL %s: did not load\n", INST_FILE[b]); bad = 1; continue; }
        char path[1400];
        snprintf(path, sizeof path, "%s/%s.raw", argv[2], INST_FILE[b]);
        FILE *f = fopen(path, "rb");
        size_t total = 0;
        for (uint32_t i = 0; i < g_banks[b].nsamp; i++) total += g_banks[b].ent[i].frames;
        int16_t *raw = malloc(total * 4);
        size_t got = f ? fread(raw, 4, total, f) : 0;
        if (f) fclose(f);
        int same = got == total && !memcmp(raw, g_banks[b].head, total * 4);
        printf("%s %-12s %9zu frames, %3u samples, gain %.2f: %s\n", same ? "OK  " : "FAIL", INST_FILE[b], total,
               g_banks[b].nsamp, g_banks[b].gain, same ? "decode bit-exact" : "differs");
        bad |= !same;
        free(raw);
    }
    mpc_engine()->destroy(s);
    return bad;
}
