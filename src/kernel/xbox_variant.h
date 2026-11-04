/*
 * xbox_variant.h -- several versions of one title in one exe (maintainer rule, 2026-11-01, every port).
 *
 * A player always uses exactly one version: he picks the image he owns and the launcher lifts only that one.
 * Development needs several at once (Burnout 3: US, PAL A, PAL B; Blood Omen: seven) - to test them in
 * parallel and to switch languages or regions across them. So a port may link several lifts into one exe:
 *
 *   - each version is lifted into its own gen dir; tools/variant_prefix.py writes gen/<v>/recomp_prefix.h,
 *     which renames every symbol the lift defines (sub_X, sub_X_gen, recomp_lookup, xbe_entry_point, ...) and
 *     the four the port's manual file defines (recomp_lookup_manual, recomp_icall_fail_log,
 *     recomp_icall_not_code_log, recomp_unimpl) to <prefix><name>. The port compiles that version's generated C
 *     and its manual file with `-include gen/<v>/recomp_prefix.h` (cmake/XboxVariants.cmake); the generated C
 *     stays byte-identical, and inside a version's code every call goes straight to that version (RECOMP_ICALL
 *     included - no extra indirection on the hot path).
 *   - the port registers each version here (xbox_variant_register, from its main.c), picks one at start
 *     (xbox_variant_detect: SHA-256 of default.xbe), and defines the runtime's unprefixed entry points once with
 *     XBOX_VARIANT_FORWARDERS(): recomp_lookup and recomp_lookup_manual go to the chosen version (the kernel
 *     bridge, the APU and the USB model call them by these names - thread starts, APCs, DPCs: not hot).
 *
 * The game folder: default.xbe directly in it = the player's case, one version (it must be registered);
 * otherwise one subfolder per version, <folder_prefix><id> (e.g. burnout_us, burnout_pal_a) - the development
 * case, all versions extracted side by side; the version comes from the caller (a setting, --variant) or, if
 * none is asked for, the first registered one that is present. An unknown default.xbe is refused with the list
 * of the supported versions.
 *
 * A port with one version and no prefix uses none of this: nothing changes for it.
 */
#ifndef XBOX_VARIANT_H
#define XBOX_VARIANT_H

#include <stddef.h>
#include <stdint.h>

#include "xbox_memory_layout.h"   /* X86Ctx, RECOMP_PARAMS, recomp_func_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *id;                              /* "us", "pal_a" - the gen dir name and the folder suffix */
    const char *label;                           /* "USA", "Europe, disc A" - for messages */
    const char *sha256;                          /* default.xbe, 64 lowercase hex digits */
    recomp_func_t (*lookup)(uint32_t xbox_va);   /* <prefix>recomp_lookup */
    recomp_func_t (*lookup_manual)(uint32_t xbox_va);
    void (*entry)(RECOMP_PARAMS);                /* <prefix>xbe_entry_point */
    int  (*dispatch_init)(void);                 /* <prefix>recomp_dispatch_init */
    const void *port_data;                       /* the port's own per-version data (settings, addresses) */
} XboxVariant;

#define XBOX_VARIANT_MAX 16

/* Register a version (the pointer is kept; static storage). Returns its index, -1 when full. */
int xbox_variant_register(const XboxVariant *v);
int xbox_variant_count(void);
const XboxVariant *xbox_variant_at(int i);
const XboxVariant *xbox_variant_by_id(const char *id);
const XboxVariant *xbox_variant_by_sha256(const char *hex);

/* The SHA-256 of a file as 64 lowercase hex digits; 0 if it cannot be read. */
int xbox_variant_file_sha256(const char *path, char hex[65]);

/* Pick the version for a game folder (see the top of this file) and make it the current one.
 *   game_root      the layout's game folder
 *   folder_prefix  the subfolder prefix of the development case ("burnout_"); NULL = no subfolders
 *   want_id        the version asked for (setting or --variant) or NULL
 *   game_dir       out: the folder that holds the chosen version's default.xbe
 * Returns the version, or NULL with a message in why (unknown image, nothing found, the wanted version not
 * present, a subfolder whose default.xbe is not that version). */
const XboxVariant *xbox_variant_detect(const char *game_root, const char *folder_prefix, const char *want_id,
                                       char *game_dir, size_t game_dir_n, char *why, size_t why_n);

/* The chosen version; NULL before xbox_variant_select / _detect. */
const XboxVariant *xbox_variant_current(void);
int xbox_variant_select(const XboxVariant *v);

/* The versions present in the development layout (subfolders with a matching default.xbe), as a bit per
 * registered index - a language menu lists the languages of all of them. */
unsigned xbox_variant_present(const char *game_root, const char *folder_prefix);

/* The runtime's unprefixed entry points, once in the port's main.c (a multi-version port only - a single-version
 * port's generated C and manual file define these names themselves). */
#define XBOX_VARIANT_FORWARDERS()                                                                  \
    recomp_func_t recomp_lookup(uint32_t xbox_va)                                                  \
    {                                                                                              \
        const XboxVariant *v_ = xbox_variant_current();                                            \
        return v_ ? v_->lookup(xbox_va) : NULL;                                                    \
    }                                                                                              \
    recomp_func_t recomp_lookup_manual(uint32_t xbox_va)                                           \
    {                                                                                              \
        const XboxVariant *v_ = xbox_variant_current();                                            \
        return v_ ? v_->lookup_manual(xbox_va) : NULL;                                             \
    }

#ifdef __cplusplus
}
#endif

#endif /* XBOX_VARIANT_H */
