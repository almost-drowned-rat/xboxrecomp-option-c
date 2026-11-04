/*
 * xbox_variant.c -- several versions of one title in one exe (see xbox_variant.h).
 *
 * Start-up only (registration, the SHA-256 of default.xbe, the folder choice): nothing here runs on a kernel
 * call, DPC or ISR path; the forwarders the port defines with XBOX_VARIANT_FORWARDERS() only read a pointer.
 */
#include "xbox_variant.h"

#include "launcher/sha256.h"

#include <stdio.h>
#include <string.h>

extern FILE *xbox_fopen_utf8(const char *path, const char *mode);

static const XboxVariant *s_vars[XBOX_VARIANT_MAX];
static int s_n;
static const XboxVariant *volatile s_cur;

int xbox_variant_register(const XboxVariant *v)
{
    int i;
    if (!v || !v->id || !v->sha256 || s_n >= XBOX_VARIANT_MAX)
        return -1;
    for (i = 0; i < s_n; i++)
        if (!strcmp(s_vars[i]->id, v->id))
            return i;                                  /* registered twice: the first one stays */
    s_vars[s_n] = v;
    return s_n++;
}

int xbox_variant_count(void)
{
    return s_n;
}

const XboxVariant *xbox_variant_at(int i)
{
    return i >= 0 && i < s_n ? s_vars[i] : NULL;
}

const XboxVariant *xbox_variant_by_id(const char *id)
{
    int i;
    for (i = 0; id && i < s_n; i++)
        if (!strcmp(s_vars[i]->id, id))
            return s_vars[i];
    return NULL;
}

const XboxVariant *xbox_variant_by_sha256(const char *hex)
{
    int i;
    for (i = 0; hex && i < s_n; i++)
        if (!strcmp(s_vars[i]->sha256, hex))
            return s_vars[i];
    return NULL;
}

int xbox_variant_file_sha256(const char *path, char hex[65])
{
    static unsigned char buf[1 << 16];
    XrSha256 s;
    uint8_t out[32];
    size_t n;
    FILE *f = xbox_fopen_utf8(path, "rb");
    if (!f)
        return 0;
    xr_sha256_init(&s);
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        xr_sha256_update(&s, buf, n);
    fclose(f);
    xr_sha256_final(&s, out);
    xr_sha256_hex(out, hex);
    return 1;
}

const XboxVariant *xbox_variant_current(void)
{
    return s_cur;
}

int xbox_variant_select(const XboxVariant *v)
{
    if (!v)
        return 0;
    s_cur = v;
    fprintf(stderr, "  [VARIANT] %s (%s)\n", v->label ? v->label : v->id, v->id);
    return 1;
}

static void supported(char *out, size_t n)
{
    size_t at = 0;
    int i;
    out[0] = 0;
    for (i = 0; i < s_n && at + 1 < n; i++)
        at += (size_t)snprintf(out + at, n - at, "%s%s", i ? ", " : "", s_vars[i]->label ? s_vars[i]->label
                                                                                          : s_vars[i]->id);
}

static int file_exists(const char *path)
{
    FILE *f = xbox_fopen_utf8(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* <root>/<prefix><id>/default.xbe that is that version: 1 and the folder */
static int sub_ok(const char *root, const char *prefix, const XboxVariant *v, char *dir, size_t n, char *why,
                  size_t why_n)
{
    char xbe[1200], hex[65];
    snprintf(dir, n, "%s/%s%s", root, prefix, v->id);
    snprintf(xbe, sizeof xbe, "%s/default.xbe", dir);
    if (!file_exists(xbe))
        return 0;
    if (!xbox_variant_file_sha256(xbe, hex) || strcmp(hex, v->sha256)) {
        if (why)
            snprintf(why, why_n, "%s holds a default.xbe that is not the %s version (SHA-256 %s)", dir,
                     v->label ? v->label : v->id, hex);
        return -1;
    }
    return 1;
}

unsigned xbox_variant_present(const char *game_root, const char *folder_prefix)
{
    unsigned bits = 0;
    char dir[1100];
    int i;
    for (i = 0; folder_prefix && i < s_n && i < 32; i++)
        if (sub_ok(game_root, folder_prefix, s_vars[i], dir, sizeof dir, NULL, 0) == 1)
            bits |= 1u << i;
    return bits;
}

const XboxVariant *xbox_variant_detect(const char *game_root, const char *folder_prefix, const char *want_id,
                                       char *game_dir, size_t game_dir_n, char *why, size_t why_n)
{
    char xbe[1200], hex[65], list[512];
    const XboxVariant *v;
    int i, r;

    if (why && why_n)
        why[0] = 0;
    supported(list, sizeof list);
    /* The player's case: one version, default.xbe directly in the game folder. */
    snprintf(xbe, sizeof xbe, "%s/default.xbe", game_root);
    if (file_exists(xbe)) {
        if (!xbox_variant_file_sha256(xbe, hex)) {
            if (why)
                snprintf(why, why_n, "%s cannot be read", xbe);
            return NULL;
        }
        v = xbox_variant_by_sha256(hex);
        if (!v) {
            if (why)
                snprintf(why, why_n, "This version is not supported (default.xbe SHA-256 %s). Supported "
                         "version(s): %s.", hex, list);
            return NULL;
        }
        if (want_id && strcmp(want_id, v->id))
            fprintf(stderr, "  [VARIANT] %s asked for, but the game folder holds %s - using it\n", want_id, v->id);
        snprintf(game_dir, game_dir_n, "%s", game_root);
        xbox_variant_select(v);
        return v;
    }
    /* The development case: one subfolder per version. */
    if (folder_prefix) {
        if (want_id) {
            v = xbox_variant_by_id(want_id);
            if (!v) {
                if (why)
                    snprintf(why, why_n, "unknown version '%s' (registered: %s)", want_id, list);
                return NULL;
            }
            r = sub_ok(game_root, folder_prefix, v, game_dir, game_dir_n, why, why_n);
            if (r == 1) {
                xbox_variant_select(v);
                return v;
            }
            if (r == 0 && why)
                snprintf(why, why_n, "%s/%s%s/default.xbe not found", game_root, folder_prefix, v->id);
            return NULL;
        }
        for (i = 0; i < s_n; i++)
            if (sub_ok(game_root, folder_prefix, s_vars[i], game_dir, game_dir_n, NULL, 0) == 1) {
                xbox_variant_select(s_vars[i]);
                return s_vars[i];
            }
    }
    if (why)
        snprintf(why, why_n, "no default.xbe in %s%s%s%s. Supported version(s): %s.", game_root,
                 folder_prefix ? " nor in " : "", folder_prefix ? folder_prefix : "", folder_prefix ? "<version>" : "",
                 list);
    return NULL;
}
