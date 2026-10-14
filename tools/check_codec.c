/* Lossless check: decode fretty.smp with the engine's own loader and compare with the raw 16-bit audio the packer
 * dumped (FRETTY_DUMP_RAW=raw.bin python3 tools/pack_samples.py ...).
 *   cc -O2 -I<mpc-vst-plugins>/wrapper tools/check_codec.c -lm -lpthread -o check_codec && ./check_codec <dir> raw.bin */
#include "../src/engine.c"
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    void *s = mpc_engine()->create(argv[1]);
    while (bank_state() == BANK_LOADING) usleep(10000);
    if (bank_state() != BANK_READY) { printf("FAIL: bank did not load\n"); return 1; }
    FILE *f = fopen(argv[2], "rb");
    size_t total = 0;
    for (uint32_t i = 0; i < g_bank.nsamp; i++) total += g_bank.ent[i].frames;
    int16_t *raw = malloc(total * 4);
    size_t got = fread(raw, 4, total, f);
    int same = got == total && !memcmp(raw, g_bank.head, total * 4);
    printf("%s: %zu frames in %u samples %s\n", same ? "OK" : "FAIL", total, g_bank.nsamp, same ? "decode bit-exact" : "differ");
    mpc_engine()->destroy(s);
    return !same;
}
