/* xbox_variant (several versions of one title in one exe): the SHA-256 of a file (FIPS 180-4 vector), the
 * registry, the player's case (default.xbe in the game folder: known / unknown version), the development case
 * (one subfolder per version: first present, the one asked for, missing, a wrong default.xbe in it), the present
 * bits, and the runtime's forwarders (XBOX_VARIANT_FORWARDERS) reaching the chosen version's lookups. */
#include "xbox_variant.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                                       fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* Two fake "lifts": their lookups answer with distinct markers. */
static void fn_a(RECOMP_PARAMS) { (void)ctx; (void)base; }
static void fn_b(RECOMP_PARAMS) { (void)ctx; (void)base; }
static void fn_am(RECOMP_PARAMS) { (void)ctx; (void)base; }
static void fn_bm(RECOMP_PARAMS) { (void)ctx; (void)base; }
static recomp_func_t look_a(uint32_t va) { return va == 0x11000u ? fn_a : NULL; }
static recomp_func_t look_b(uint32_t va) { return va == 0x11000u ? fn_b : NULL; }
static recomp_func_t man_a(uint32_t va) { return va == 0x22000u ? fn_am : NULL; }
static recomp_func_t man_b(uint32_t va) { return va == 0x22000u ? fn_bm : NULL; }

/* What the port's main.c does once in a multi-version build. */
XBOX_VARIANT_FORWARDERS()

static char s_root[1024];

static void put(const char *rel, const char *text)
{
    char p[1200];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", s_root, rel);
    f = fopen(p, "wb");
    if (!f) { failures++; fprintf(stderr, "cannot write %s\n", p); return; }
    fputs(text, f);
    fclose(f);
}

static void mkd(const char *rel)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", s_root, rel);
    MKDIR(p);
}

static void rm(const char *rel)
{
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", s_root, rel);
    remove(p);
}

int main(int argc, char **argv)
{
    static XboxVariant va = { "us", "USA", NULL, look_a, man_a, NULL, NULL, NULL };
    static XboxVariant vb = { "pal_a", "Europe, disc A", NULL, look_b, man_b, NULL, NULL, NULL };
    static char ha[65], hb[65];
    char hex[65], dir[1200], why[1024], p[1200];
    const XboxVariant *v;

    if (argc < 2) {
        printf("usage: xbox_variant_test <scratch dir>\n");
        return 2;
    }
    snprintf(s_root, sizeof s_root, "%s", argv[1]);
    MKDIR(s_root);
    mkd("single");
    mkd("dev");
    mkd("dev/game_us");
    mkd("dev/game_pal_a");
    mkd("empty");

    /* FIPS 180-4 example: SHA-256("abc") */
    put("abc.txt", "abc");
    snprintf(p, sizeof p, "%s/abc.txt", s_root);
    CHECK(xbox_variant_file_sha256(p, hex), "sha of abc.txt");
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256(abc) = %s", hex);
    CHECK(!xbox_variant_file_sha256("/no/such/file", hex), "missing file has no hash");

    /* Two "default.xbe" contents, their hashes registered as the versions. */
    put("a.bin", "version A image");
    put("b.bin", "version B image");
    snprintf(p, sizeof p, "%s/a.bin", s_root); xbox_variant_file_sha256(p, ha);
    snprintf(p, sizeof p, "%s/b.bin", s_root); xbox_variant_file_sha256(p, hb);
    va.sha256 = ha;
    vb.sha256 = hb;
    CHECK(xbox_variant_current() == NULL, "nothing chosen yet");
    CHECK(recomp_lookup(0x11000u) == NULL, "forwarder without a version: NULL");
    CHECK(xbox_variant_register(&va) == 0 && xbox_variant_register(&vb) == 1, "register");
    CHECK(xbox_variant_register(&va) == 0 && xbox_variant_count() == 2, "registered twice: kept once");
    CHECK(xbox_variant_by_id("pal_a") == &vb && xbox_variant_by_sha256(ha) == &va && !xbox_variant_by_id("x"), "find");

    /* The player's case: default.xbe in the game folder. */
    put("single/default.xbe", "version B image");
    snprintf(p, sizeof p, "%s/single", s_root);
    v = xbox_variant_detect(p, "game_", NULL, dir, sizeof dir, why, sizeof why);
    CHECK(v == &vb && !strcmp(dir, p), "single: version B in %s (%s)", dir, why);
    CHECK(xbox_variant_current() == &vb, "single: B current");
    CHECK(recomp_lookup(0x11000u) == fn_b && recomp_lookup_manual(0x22000u) == fn_bm, "forwarders reach B");
    v = xbox_variant_detect(p, "game_", "us", dir, sizeof dir, why, sizeof why);
    CHECK(v == &vb, "single: the folder wins over a wanted id");
    put("single/default.xbe", "some other image");
    v = xbox_variant_detect(p, "game_", NULL, dir, sizeof dir, why, sizeof why);
    CHECK(!v && strstr(why, "not supported") && strstr(why, "USA") && strstr(why, "Europe, disc A"),
          "single: unknown image refused, supported list given: %s", why);
    CHECK(xbox_variant_current() == &vb, "a refused detect leaves the current version");

    /* The development case: one subfolder per version. */
    put("dev/game_us/default.xbe", "version A image");
    put("dev/game_pal_a/default.xbe", "version B image");
    snprintf(p, sizeof p, "%s/dev", s_root);
    CHECK(xbox_variant_present(p, "game_") == 3u, "both present");
    v = xbox_variant_detect(p, "game_", NULL, dir, sizeof dir, why, sizeof why);
    CHECK(v == &va && strstr(dir, "game_us"), "dev: first registered present = us (%s)", dir);
    CHECK(recomp_lookup(0x11000u) == fn_a, "forwarders reach A");
    v = xbox_variant_detect(p, "game_", "pal_a", dir, sizeof dir, why, sizeof why);
    CHECK(v == &vb && strstr(dir, "game_pal_a"), "dev: the wanted one (%s)", dir);
    v = xbox_variant_detect(p, "game_", "pal_b", dir, sizeof dir, why, sizeof why);
    CHECK(!v && strstr(why, "unknown version"), "dev: unregistered id refused: %s", why);
    put("dev/game_pal_a/default.xbe", "version A image");          /* the US image in the PAL folder */
    v = xbox_variant_detect(p, "game_", "pal_a", dir, sizeof dir, why, sizeof why);
    CHECK(!v && strstr(why, "not the Europe, disc A version"), "dev: wrong image in its folder: %s", why);
    CHECK(xbox_variant_present(p, "game_") == 1u, "only us present now");
    rm("dev/game_us/default.xbe");
    rm("dev/game_pal_a/default.xbe");
    v = xbox_variant_detect(p, "game_", "us", dir, sizeof dir, why, sizeof why);
    CHECK(!v && strstr(why, "not found"), "dev: wanted folder empty: %s", why);

    /* Nothing at all. */
    snprintf(p, sizeof p, "%s/empty", s_root);
    v = xbox_variant_detect(p, "game_", NULL, dir, sizeof dir, why, sizeof why);
    CHECK(!v && strstr(why, "no default.xbe") && strstr(why, "Supported"), "empty: %s", why);

    printf(failures ? "xbox_variant: %d check(s) FAILED\n" : "xbox_variant: all checks passed\n", failures);
    return failures != 0;
}
