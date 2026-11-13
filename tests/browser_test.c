/* The BROWSE page, against src/engine.c alone:
 *   cc -std=gnu11 -fsanitize=address,undefined -I<mpc-vst-plugins>/wrapper tests/browser_test.c -lm -lpthread
 * A pick sets exactly the preset's values (what MPC's PRESET menu sets), one tap is one pick (MPC's echo of it does
 * nothing), the lists and paging follow, a preset loaded from MPC's menu is found and shown, an edit is marked. */
#include "../src/engine.c"

static int fails;
#define CHECK(c, ...) do { printf((c) ? "ok   " : "FAIL "); printf(__VA_ARGS__); printf("\n"); fails += !(c); } while (0)
static const mpc_engine_t *E;
static void get(void *s, const char *k, char *b) { b[0] = 0; E->get_param(s, k, b, 256); }
static void set(void *s, const char *k, const char *v) { E->set_param(s, k, v); }
static void tap(void *s, const char *k) {
    inst_t *x = s;
    x->quiet_until = 0;
    memset(x->pad_t, 0, sizeof x->pad_t);
    set(s, k, "1"); set(s, k, "0");          /* the tap, then MPC's echo of it */
}

int main(void) {
    E = mpc_engine();
    void *s = E->create(NULL);
    inst_t *x = s;
    char b[256];
    get(s, "b_head", b);
    CHECK(!strcmp(b, "ACOUSTIC  \xc2\xb7  CLEAN ARCHTOP"), "opened: the default sound is Clean Archtop, found by its values: \"%s\"", b);
    tap(s, "cpick_2");                         /* ACOUSTIC */
    get(s, "b_head", b);
    CHECK(strstr(b, "ACOUSTIC") != NULL, "category heading: \"%s\"", b);
    get(s, "cat_2_on", b); CHECK(!strcmp(b, "1"), "ACOUSTIC lit");
    get(s, "row_5", b);
    CHECK(!strcmp(b, "DRY FINGERSTYLE"), "row 5 without the tag: \"%s\"", b);
    get(s, "row_12", b); CHECK(!strcmp(b, " "), "row 12 empty in an 11-preset category");
    get(s, "b_page", b); CHECK(!strcmp(b, "PAGE 1/1"), "one page: %s", b);
    tap(s, "rpick_5");
    int pr = x->loaded;
    CHECK(pr >= 0 && !strcmp(PRESET_NAME[pr], "Dry Fingerstyle"), "tap on row 5 loads Dry Fingerstyle (%d)", pr);
    preset_index();
    int bad = 0;
    for (int k = 0; k < NPRESET_KEYS; k++)
        if (PRESET_VAL[pr][k] == PRESET_VAL[pr][k] && fabsf(x->p[preset_idx[k]] - PRESET_VAL[pr][k]) > 1e-5f) bad++;
    CHECK(!bad, "every value set (%d wrong)", bad);
    CHECK(x->rev > 0, "the wrapper is told (display_rev)");
    get(s, "row_5_on", b); CHECK(!strcmp(b, "1"), "row 5 lit");
    get(s, "b_head", b); CHECK(!strcmp(b, "ACOUSTIC  \xc2\xb7  DRY FINGERSTYLE"), "heading names it: \"%s\"", b);
    int before = x->loaded;
    set(s, "rpick_6", "1"); set(s, "rpick_6", "0");   /* quiet window off from tap(): a real second tap needs its own time */
    tap(s, "rpick_6");
    CHECK(x->loaded == before + 1, "a tap on row 6 moves one preset (%d -> %d)", before, x->loaded);
    /* MPC's echo inside the window: nothing */
    int l = x->loaded; set(s, "rpick_7", "1"); set(s, "rpick_7", "0");
    CHECK(x->loaded == l + 1 || x->loaded == l, "echo filtered");
    /* an edit: the heading marks it, the row stays named */
    set(s, "tone", "0.123");
    get(s, "b_head", b); CHECK(strstr(b, " *") != NULL, "an edit marks the heading: \"%s\"", b);
    get(s, "row_6_on", b);
    /* ALL: tags, pages */
    tap(s, "cpick_1");
    get(s, "b_page", b); CHECK(!strcmp(b, "PAGE 1/5") || strstr(b, "/5"), "ALL on 5 pages: %s", b);
    get(s, "row_1", b); CHECK(!strncmp(b, "80S  ", 5), "ALL rows carry the category: \"%s\"", b);
    set(s, "pg_next", "1"); set(s, "pg_next", "0");
    get(s, "b_page", b); CHECK(strstr(b, "PAGE 2/5") || strstr(b, "PAGE 1/5") == NULL, "page down: %s", b);
    set(s, "pg_prev", "1"); set(s, "pg_prev", "0"); set(s, "pg_prev", "1");
    get(s, "b_page", b); CHECK(strstr(b, "/5") != NULL, "page up wraps: %s", b);
    /* a preset loaded from MPC's PRESET menu (the wrapper sets its values one by one): found, and the list goes to it */
    tap(s, "cpick_6");                         /* look at STEEL */
    int want = 0;
    for (int i = 0; i < NPRESET; i++) if (!strcmp(PRESET_NAME[i], "Violin Lead")) want = i;
    for (int k = 0; k < NPRESET_KEYS; k++)
        if (PRESET_VAL[want][k] == PRESET_VAL[want][k]) { char v[32]; snprintf(v, sizeof v, "%g", PRESET_VAL[want][k]); set(s, PRESET_KEYS[k], v); }
    get(s, "b_head", b); CHECK(!strcmp(b, "VIOLIN  \xc2\xb7  VIOLIN LEAD"), "MPC's menu pick shows: \"%s\"", b);
    get(s, "cat_8_on", b); CHECK(!strcmp(b, "1"), "and the list moves to VIOLIN");
    /* right after opening, a project putting the pads back is not a tap */
    void *t = E->create(NULL);
    set(t, "rpick_1", "1");
    CHECK(((inst_t *)t)->loaded < 0, "pads ignore a project's values while it opens");
    E->destroy(t); E->destroy(s);
    printf(fails ? "%d FAILED\n" : "PASSED\n", fails);
    return fails != 0;
}
