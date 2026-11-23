/* SPDX-License-Identifier: MIT
 * loader.c - libmain.so's entry on Android (2026-11-11, for Panzer Dragoon Orta; template since 2026-11-19:
 * the port's names from port_android.h).
 *
 * The APK carries no game code (maintainer 2026-11-11: one public APK, the game is compiled ON THE DEVICE once
 * from the player's own disc). The game library PORT_GAME_LIB (toolkit runtime + generated C + the port's
 * sources, linked with the runtime SDK's lines) lives in the app's internal storage next to game/ (the extracted
 * disc), saves/ and data/. This loader:
 *   - runs the toolkit's launcher (launcher_android.c, --no-start): the first start picks the disc image through
 *     the system file picker, extracts it, lifts (python), compiles + links (clang, lld) PORT_GAME_LIB; later
 *     starts see its stamp and return at once;
 *   - then dlopen()s the game library from internal storage and calls PORT_GAME_MAIN.
 * Dev markers in <files>: smoke_test.txt -> the stage-1 smoke test (Vulkan, pad / touch, route probes),
 * lift_test.txt -> the timed in-process lift. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include "port_android.h"
#include "pylift.h"
#include "touchpad.h"
#include "driver_check.h"

int smoke_main(int argc, char **argv);
int port_android_launcher(int argc, char **argv);   /* launcher_android.c: the toolkit launcher, the port's config */

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Dev test of the on-device lift (marker file <files>/lift_test.txt): the five steps of the desktop lift
 * (xbe_parser, disasm, func_id, abi_analysis, recomp) on <files>/game/default.xbe into <files>/pipe + <files>/gen,
 * each timed. Config = <files>/python/lib/python3.15/site-packages/PORT_CONFIG_PKG/game.toml (staged with the
 * lifter). */
static int lift_test(const char *files)
{
    char home[1024], xbe[1024], pipe[1024], gen[1024], cfg[1024], d[1024], fid[1024], abi[1024], rec[1024];
    char fj[1024], sj[1024], xj[1024], xjson[1024];
    Uint64 t0, t;
    int rc = 0, i;
    SDL_snprintf(home, sizeof home, "%s/python", files);
    SDL_snprintf(xbe, sizeof xbe, "%s/game/default.xbe", files);
    SDL_snprintf(pipe, sizeof pipe, "%s/pipe", files);
    SDL_snprintf(gen, sizeof gen, "%s/gen", files);
    SDL_snprintf(cfg, sizeof cfg, "%s/lib/python3.15/site-packages/" PORT_CONFIG_PKG "/game.toml", home);
    SDL_snprintf(d, sizeof d, "%s/disasm", pipe);
    SDL_snprintf(fid, sizeof fid, "%s/func_id", pipe);
    SDL_snprintf(abi, sizeof abi, "%s/abi", pipe);
    SDL_snprintf(rec, sizeof rec, "%s/recomp", pipe);
    SDL_snprintf(xjson, sizeof xjson, "%s/xbe.json", pipe);
    SDL_snprintf(fj, sizeof fj, "%s/functions.json", d);
    SDL_snprintf(sj, sizeof sj, "%s/strings.json", d);
    SDL_snprintf(xj, sizeof xj, "%s/xrefs.json", d);
    { const char *mk[] = { pipe, d, fid, abi, rec, gen }; for (i = 0; i < 6; i++) mkdir(mk[i], 0755); }
    {
        const char *a1[] = { xbe, "--json", xjson, "--quiet" };
        const char *a2[] = { xbe, "-o", d, "--analysis-json", xjson, "--force", "--game-config", cfg };
        const char *a3[] = { xbe, "--functions", fj, "--strings", sj, "--xrefs", xj, "-o", fid };
        const char *a4[] = { xbe, "--disasm-dir", d, "--func-id-dir", fid, "--output-dir", abi };
        const char *a5[] = { xbe, "--all", "--game-config", cfg, "--game-name", PORT_GAME_NAME, "--gen-dir", gen,
                             "--disasm-dir", d, "--func-id-dir", fid, "--abi-dir", abi, "-o", rec };
        struct { const char *mod; int n; const char *const *a; } steps[] = {
            { "tools.xbe_parser", 4, a1 }, { "tools.disasm", 8, a2 }, { "tools.func_id", 9, a3 },
            { "tools.abi_analysis", 7, a4 }, { "tools.recomp", 15, a5 },
        };
        t0 = SDL_GetTicks();
        for (i = 0; i < 5 && rc == 0; i++) {
            t = SDL_GetTicks();
            __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LIFTTEST] step %s ...", steps[i].mod);
            rc = pylift_run_module(home, steps[i].mod, steps[i].n, steps[i].a);
            __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LIFTTEST] step %s: exit %d in %.1f s", steps[i].mod, rc,
                                (SDL_GetTicks() - t) / 1000.0);
        }
        __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LIFTTEST] lift %s in %.1f s", rc ? "FAILED" : "done",
                            (SDL_GetTicks() - t0) / 1000.0);
    }
    pylift_finish();
    return rc;
}

/* <files>/data/import: the launcher's copy of a picked disc image (PortActivity.importContent), flat */
static void import_cleanup(const char *files)
{
    char d[1024], p[1300];
    char **names;
    int i;
    SDL_snprintf(d, sizeof d, "%s/data/import", files);
    if (!exists(d)) return;
    names = SDL_GlobDirectory(d, "*", 0, NULL);
    for (i = 0; names && names[i]; i++) {
        SDL_snprintf(p, sizeof p, "%s/%s", d, names[i]);
        remove(p);
    }
    SDL_free(names);
    rmdir(d);
    __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] imported image copy removed");
}

/* PortActivity.onGameStarting() (JNI): the touch controls show from now on */
static void game_starting(void)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act = (jobject)SDL_GetAndroidActivity();
    jclass c;
    jmethodID m;
    if (!env || !act) return;
    c = (*env)->GetObjectClass(env, act);
    m = (*env)->GetStaticMethodID(env, c, "onGameStarting", "()V");
    if (m) (*env)->CallStaticVoidMethod(env, c, m);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
    (*env)->DeleteLocalRef(env, act);
}

/* ApplicationInfo.nativeLibraryDir (JNI): the folder of clang, lld, python (the only exec-able place) */
static int native_lib_dir(char *out, size_t n)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act = (jobject)SDL_GetAndroidActivity();
    jclass ca, cai;
    jobject ai;
    jfieldID f;
    jstring s;
    const char *c;
    out[0] = 0;
    if (!env || !act) return 0;
    ca = (*env)->GetObjectClass(env, act);
    ai = (*env)->CallObjectMethod(env, act, (*env)->GetMethodID(env, ca, "getApplicationInfo",
                                                                 "()Landroid/content/pm/ApplicationInfo;"));
    cai = (*env)->GetObjectClass(env, ai);
    f = (*env)->GetFieldID(env, cai, "nativeLibraryDir", "Ljava/lang/String;");
    s = (jstring)(*env)->GetObjectField(env, ai, f);
    c = (*env)->GetStringUTFChars(env, s, NULL);
    SDL_snprintf(out, n, "%s", c);
    (*env)->ReleaseStringUTFChars(env, s, c);
    (*env)->DeleteLocalRef(env, s);
    (*env)->DeleteLocalRef(env, cai);
    (*env)->DeleteLocalRef(env, ai);
    (*env)->DeleteLocalRef(env, ca);
    (*env)->DeleteLocalRef(env, act);
    return out[0] != 0;
}

/* The BACK menu's "Controllers" page (PortActivity, JNI): the game library's ports (toolkit agent-l/pads) - one
 * line per port "<port>\t<pad name>" (empty name = no pad), first line "merged" or "ports"; "" without the calls
 * (an older game library) or before the game. nativeSwapPorts: a pad to another player by hand. */
static void *s_game;

JNIEXPORT jstring JNICALL Java_xboxrecomp_port_PortActivity_nativePadPorts(JNIEnv *env, jclass c)
{
    void (*name)(int, char *, size_t) = s_game ? (void (*)(int, char *, size_t))dlsym(s_game, "xbox_InputPadName") : NULL;
    int (*merged)(void) = s_game ? (int (*)(void))dlsym(s_game, "xbox_InputPadsMerged") : NULL;
    char out[1024], n[256];
    int p, len;
    (void)c;
    if (!name || !merged)
        return (*env)->NewStringUTF(env, "");
    len = snprintf(out, sizeof out, "%s\n", merged() ? "merged" : "ports");
    for (p = 0; p < 4 && len < (int)sizeof out; p++) {
        name(p, n, sizeof n);
        len += snprintf(out + len, sizeof out - (size_t)len, "%d\t%s\n", p, n);
    }
    return (*env)->NewStringUTF(env, out);
}

JNIEXPORT void JNICALL Java_xboxrecomp_port_PortActivity_nativeSwapPorts(JNIEnv *env, jclass c, jint a, jint b)
{
    int (*swap)(int, int) = s_game ? (int (*)(int, int))dlsym(s_game, "xbox_InputSwapPorts") : NULL;
    (void)env; (void)c;
    if (swap) swap(a, b);
}

int main(int argc, char **argv)
{
    const char *files = SDL_GetAndroidInternalStoragePath();
    char lib[1024], xbe[1024], native[1024], p[1024];
    void *h;
    int rc;
    int (*game_main)(int, char **);
    /* forced landscape (maintainer 2026-11-11), installer and game: SDL would otherwise turn the activity to
       portrait for a window it cannot fit (S23: the game's swapchain came up 1080x2340); the hint lives in the
       shared libSDL3.so, so the game's SDL_Init sees it too */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (!files) return smoke_main(argc, argv);
    SDL_snprintf(lib, sizeof lib, "%s/lift_test.txt", files);
    if (exists(lib)) {
        remove(lib);                             /* once per marker */
        return lift_test(files);
    }
    SDL_snprintf(lib, sizeof lib, "%s/smoke_test.txt", files);
    if (exists(lib)) {
        remove(lib);
        return smoke_main(argc, argv);
    }
    /* the first start (and every later one): the toolkit's launcher - pick the disc image, extract, lift, compile,
     * link; later starts see the stamp and return at once. Its processes (python, clang, lld) run from
     * nativeLibraryDir; its read-only folder is <files>/tools (the APK's assets, extracted by PortActivity). */
    native_lib_dir(native, sizeof native);
    SDL_setenv_unsafe("XR_NATIVE_DIR", native, 1);
    SDL_setenv_unsafe("XR_HOST_ROOT", files, 1);         /* the toolkit's xbox_host_root on Android */
    SDL_setenv_unsafe("LD_LIBRARY_PATH", native, 1);     /* python_exe needs libpython3.15.so from there */
    {   /* the game's commit for its version line (option-c 902dd3d reads XR_GAME_COMMIT from the env on Android;
           the SDK is prebuilt): tools/release.txt = "android-dev-<commit>" */
        char rel[256];
        FILE *f;
        SDL_snprintf(p, sizeof p, "%s/tools/release.txt", files);
        if ((f = fopen(p, "r")) != NULL) {
            if (fgets(rel, sizeof rel, f)) {
                const char *c = SDL_strrchr(rel, '-');
                rel[strcspn(rel, "\r\n")] = 0;
                SDL_setenv_unsafe("XR_GAME_COMMIT", c ? c + 1 : rel, 1);
            }
            fclose(f);
        }
    }
    SDL_snprintf(p, sizeof p, "%s/data", files);
    mkdir(p, 0700);
    SDL_snprintf(p, sizeof p, "%s/data/tmp", files);     /* clang / python temp files (no /tmp on Android) */
    mkdir(p, 0700);
    SDL_setenv_unsafe("TMPDIR", p, 1);
    SDL_snprintf(p, sizeof p, "%s/tools", files);
    SDL_setenv_unsafe("XR_LAUNCHER_TOOLS", p, 1);
    SDL_snprintf(p, sizeof p, "%s/python", files);
    SDL_setenv_unsafe("XR_PYTHON_HOME", p, 1);
#ifdef PORT_LAUNCHER_JOBS
    if (!SDL_getenv("XR_LAUNCHER_JOBS"))
        SDL_setenv_unsafe("XR_LAUNCHER_JOBS", PORT_LAUNCHER_JOBS, 1);   /* big lifted units on 8 GB phones */
#endif
    __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] launcher (native %s, page size %ld)", native,
                        sysconf(_SC_PAGESIZE));
    if (!driver_check(files)) {                  /* Adreno below Vulkan 1.3: the player's Turnip driver first */
        __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] no graphics driver chosen - quit");
        return 1;
    }
    rc = port_android_launcher(argc, argv);
    import_cleanup(files);                       /* the copy of the picked image (launcher_android.c) */
    if (rc != 0) {
        __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] launcher ended without a game");
        return 1;
    }
    SDL_snprintf(lib, sizeof lib, "%s/" PORT_GAME_LIB, files);
    SDL_snprintf(xbe, sizeof xbe, "%s/game/default.xbe", files);
    if (!exists(lib) || !exists(xbe)) {
        __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] no game after the launcher (%s: %s, %s: %s)",
                            lib, exists(lib) ? "yes" : "no", xbe, exists(xbe) ? "yes" : "no");
        return 1;
    }
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");   /* again: the launcher's SDL_Quit reset it */
    /* SDL's surface calls through the same Vulkan library as the game (system driver or Turnip, vkshim/) */
    SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, "libvulkan_port.so");
    if (!SDL_getenv("RECOMP_FLIP_STALL_MS"))      /* dev builds: a stall dump after 500 ms without a frame (option-c cd23de2) */
        SDL_setenv_unsafe("RECOMP_FLIP_STALL_MS", "500", 1);
    touchpad_init();                             /* the touch controls' virtual gamepad (touchpad.c) */
    game_starting();                             /* TouchOverlay shows from now on */
    h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        const char *e = dlerror();
        __android_log_print(ANDROID_LOG_ERROR, PORT_TAG, "[LOADER] dlopen %s failed: %s", lib, e ? e : "?");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, PORT_TITLE,
                                 "The game could not be loaded. Please run the first start again.", NULL);
        return 1;
    }
    s_game = h;
    game_main = (int (*)(int, char **))dlsym(h, PORT_GAME_MAIN);
    if (!game_main) {
        __android_log_print(ANDROID_LOG_ERROR, PORT_TAG, "[LOADER] %s missing in %s", PORT_GAME_MAIN, lib);
        return 1;
    }
#ifdef PORT_TOUCH_AIM
    {   /* touch aiming: the port's PORT_TOUCH_AIM in the game library (an older game library has none: the overlay
         * keeps its normal layout) */
        touchpad_set_aim((int (*)(int, float, float, float, float))dlsym(h, PORT_TOUCH_AIM));
    }
#endif
    {   /* external pads before built-in / touch ones (toolkit agent-l/pads; an older game library has no such call) */
        void (*set_prio)(int (*)(const char *)) = (void (*)(int (*)(const char *)))dlsym(h, "xbox_InputSetPadPriority");
        if (set_prio) set_prio(touchpad_pad_priority);
        __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] pad priority %s", set_prio ? "set" : "not in this game library");
    }
    {   /* saves/ and media/ where the player can reach them (maintainer 2026-11-11): the app's folder in shared
         * storage, /storage/emulated/0/Android/data/<package>/files - adb push / a file manager work there, unlike
         * the internal storage (closed to adb for a release build). No extra permission. Removed on uninstall like
         * the rest. PortActivity.migrateUserFiles moved older saves/media there. Not available (no shared storage
         * mounted): the internal folder as before. */
        static char save_dir[1024], media_dir[1024];
        static char *args[64];
        const char *ext = SDL_GetAndroidExternalStoragePath();
        int n = 0, i;
        if (ext && (SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE)) {
            /* files the game writes there (saves, settings) readable for the shared folder's group too, so adb
             * pull / a file manager can back them up - an app's default umask 077 left them -rw------- */
            umask(0007);
            SDL_snprintf(save_dir, sizeof save_dir, "%s/saves", ext);
            SDL_snprintf(media_dir, sizeof media_dir, "%s/media", ext);
            mkdir(media_dir, 0777);                  /* 0770 with the umask above: adb / file managers may write */
            SDL_setenv_unsafe(PORT_MEDIA_ENV, media_dir, 1);
            for (i = 0; i < argc && n < 60; i++) args[n++] = argv[i];
            args[n++] = "--save-dir";
            args[n++] = save_dir;
            args[n] = NULL;
            __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] saves + media in %s", ext);
            __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] starting the game from %s", lib);
            return game_main(n, args);
        }
        __android_log_print(ANDROID_LOG_WARN, PORT_TAG, "[LOADER] no shared storage: saves + media stay internal");
    }
    __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[LOADER] starting the game from %s", lib);
    return game_main(argc, argv);
}
