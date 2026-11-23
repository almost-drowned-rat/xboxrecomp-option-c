/* SPDX-License-Identifier: MIT
 * vk_smoke.c - Android stage-1 smoke test (2026-11-11). Proves on the device: the APK starts, SDL3 opens a
 * Vulkan renderer (the toolkit renderer is Vulkan 1.3 only), gamepads and touch arrive, the app's internal storage
 * is writable (Android: not portable - everything lives there, maintainer 2026-11-11). Every line goes to logcat
 * (tag SDL/APP) and to <internal storage>/smoke.log. The screen cycles red -> green -> blue once a second; a pad
 * button or a touch turns it white for that frame. Ends after 60 s or on BACK. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

extern char **environ;

static FILE *s_log;

#ifdef XR_SMOKE_TOOLKIT
/* stage 2a: take the address of toolkit entry points so their objects (and what they need) are really linked */
extern unsigned int xbox_HeapAlloc(unsigned int size, unsigned int alignment);
extern void *xbox_GetMemoryOffset(void);
/* The game's dispatch (generated code: xbox_variant.h) - on Android it comes with the game library built on the
 * device; the link test only needs a body. */
typedef void (*smoke_func_t)(void);
smoke_func_t recomp_lookup(unsigned int xbox_va);
smoke_func_t recomp_lookup_manual(unsigned int xbox_va);
smoke_func_t recomp_lookup(unsigned int xbox_va) { (void)xbox_va; return 0; }
smoke_func_t recomp_lookup_manual(unsigned int xbox_va) { (void)xbox_va; return 0; }
#endif

static void say(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    SDL_Log("%s", buf);
    if (s_log) { fprintf(s_log, "%s\n", buf); fflush(s_log); }
}

/* nativeLibraryDir = the folder this libmain.so was loaded from (extractNativeLibs=true: real files) */
static int native_lib_dir(char *out, size_t cap)
{
    Dl_info di;
    char *slash;
    if (!dladdr((void *)native_lib_dir, &di) || !di.dli_fname) return 0;
    SDL_strlcpy(out, di.dli_fname, cap);
    slash = strrchr(out, '/');
    if (!slash) return 0;
    *slash = 0;
    return 1;
}

static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b;
    char buf[16384];
    size_t n;
    if (!a) return 0;
    b = fopen(to, "wb");
    if (!b) { fclose(a); return 0; }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    return fclose(b) == 0;
}

/* The on-device toolchain probe: the argument lists
 * <files>/probe/N_*.args (one argument per line, program first; $NATIVE / $TOOLCHAIN / $FILES) run in order with
 * the shipped clang (libclang_exe.so) and lld (liblld_exe.so -flavor gnu), output to <files>/probe/N.out; then
 * the game's real path: dlopen(<files>/probe/libprobe.so) + probe_mul(6, 7) == 42. */
static void subst(char *out, size_t cap, const char *in, const char *nat, const char *tc, const char *fl)
{
    size_t o = 0;
    while (*in && o + 1 < cap) {
        const char *rep = NULL;
        size_t skip = 0;
        if (!strncmp(in, "$NATIVE", 7)) { rep = nat; skip = 7; }
        else if (!strncmp(in, "$TOOLCHAIN", 10)) { rep = tc; skip = 10; }
        else if (!strncmp(in, "$FILES", 6)) { rep = fl; skip = 6; }
        if (rep) { o += (size_t)SDL_snprintf(out + o, cap - o, "%s", rep); in += skip; if (o >= cap) o = cap - 1; }
        else out[o++] = *in++;
    }
    out[o] = 0;
}

static void toolchain_probe(const char *lib, const char *files)
{
    static const char *const steps[] = { "0_version", "1_compile_lib", "2_link_lib", "3_compile_exe",
                                         "4_link_exe", "5_run_exe" };
    char tc[600], fl[600];
    int s;
    SDL_snprintf(tc, sizeof tc, "%s/tools/toolchain", files);
    SDL_snprintf(fl, sizeof fl, "%s/probe", files);
    if (access(tc, F_OK) != 0) { say("[TCPROBE] no toolchain in the APK - skipped"); return; }
    for (s = 0; s < 6; s++) {
        char path[700], outp[700], line[1200], ldp[700];
        char *argv[96], *envp[2];
        static char store[96][1200];
        int argc = 0, rc, st = 0;
        FILE *f;
        pid_t pid;
        posix_spawn_file_actions_t fa;
        Uint64 t0 = SDL_GetTicks();
        SDL_snprintf(path, sizeof path, "%s/%s.args", fl, steps[s]);
        if (!(f = fopen(path, "r"))) { say("[TCPROBE] %s: no args file", steps[s]); return; }
        while (argc < 95 && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0]) continue;
            subst(store[argc], sizeof store[argc], line, lib, tc, fl);
            argv[argc] = store[argc];
            argc++;
        }
        fclose(f);
        argv[argc] = NULL;
        SDL_snprintf(outp, sizeof outp, "%s/%d.out", fl, s);
        SDL_snprintf(ldp, sizeof ldp, "LD_LIBRARY_PATH=%s", fl);
        envp[0] = ldp; envp[1] = NULL;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, outp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        rc = posix_spawn(&pid, argv[0], &fa, NULL, argv, envp);
        posix_spawn_file_actions_destroy(&fa);
        if (rc != 0) { say("[TCPROBE] %s: posix_spawn %s: %s", steps[s], argv[0], strerror(rc)); return; }
        waitpid(pid, &st, 0);
        {
            char first[300] = "";
            FILE *o = fopen(outp, "r");
            if (o) { if (!fgets(first, sizeof first, o)) first[0] = 0; fclose(o); }
            first[strcspn(first, "\r\n")] = 0;
            say("[TCPROBE] %s: exit %d in %.2f s, first line \"%s\"", steps[s], WIFEXITED(st) ? WEXITSTATUS(st) : -1,
                (SDL_GetTicks() - t0) / 1000.0, first);
        }
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { if (s < 5) return; }
    }
    {
        char so[700];
        void *h;
        SDL_snprintf(so, sizeof so, "%s/libprobe.so", fl);
        h = dlopen(so, RTLD_NOW | RTLD_LOCAL);
        if (!h) { say("[TCPROBE] DLOPEN OF THE ON-DEVICE BUILT LIBRARY FAILED: %s", dlerror()); return; }
        {
            int (*mul)(int, int) = (int (*)(int, int))dlsym(h, "probe_mul");
            say("[TCPROBE] ON-DEVICE BUILT LIBRARY: probe_mul(6, 7) = %d (want 42) - the game's route %s",
                mul ? mul(6, 7) : -1, mul && mul(6, 7) == 42 ? "WORKS" : "BROKEN");
        }
        dlclose(h);
    }
}

/* The two route probes (riskiest assumptions of "everything on the phone"). */
static void route_probes(const char *files)
{
    char lib[512], src[600], dst[600];
    if (!native_lib_dir(lib, sizeof lib)) { say("[PROBE] nativeLibraryDir not found (dladdr)"); return; }
    say("[PROBE] nativeLibraryDir %s", lib);
    /* 1: dlopen of a library from internal storage (the game library's way) */
    SDL_snprintf(src, sizeof src, "%s/libprobe_dl.so", lib);
    SDL_snprintf(dst, sizeof dst, "%s/libprobe_dl_copy.so", files ? files : ".");
    if (!files || !copy_file(src, dst)) say("[PROBE] DLOPEN: copy %s -> %s FAILED", src, dst);
    else {
        void *h = dlopen(dst, RTLD_NOW | RTLD_LOCAL);
        if (!h) say("[PROBE] DLOPEN FROM INTERNAL STORAGE FAILED: %s", dlerror());
        else {
            int (*f)(int) = (int (*)(int))dlsym(h, "xr_probe_answer");
            say("[PROBE] DLOPEN FROM INTERNAL STORAGE OK: xr_probe_answer(20) = %d (want 41)", f ? f(20) : -1);
            dlclose(h);
        }
    }
    /* 2: execve of an executable shipped as lib*.so (the on-device compiler's way) */
    {
        char exe[600];
        char *args[3];
        pid_t pid;
        int st = 0, rc;
        SDL_snprintf(exe, sizeof exe, "%s/libprobe_exe.so", lib);
        args[0] = exe; args[1] = (char *)"hello"; args[2] = NULL;
        rc = posix_spawn(&pid, exe, NULL, NULL, args, environ);
        if (rc != 0) say("[PROBE] EXEC FROM nativeLibraryDir FAILED: posix_spawn %s", strerror(rc));
        else if (waitpid(pid, &st, 0) < 0) say("[PROBE] EXEC: waitpid failed");
        else say("[PROBE] EXEC FROM nativeLibraryDir %s: exit %d (want 42)",
                 WIFEXITED(st) && WEXITSTATUS(st) == 42 ? "OK" : "WRONG", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    }
    /* 3: glslang on the device (the toolkit's libxr_glslang.so + its link test, compiles a shader to SPIR-V); its
     * output goes to <files>/glslang_test.txt, LD_LIBRARY_PATH = nativeLibraryDir for its NEEDED libxr_glslang.so */
    {
        char exe[600], outp[600], ldp[620];
        char *args[2], *envp[2];
        posix_spawn_file_actions_t fa;
        pid_t pid;
        int st = 0, rc;
        SDL_snprintf(exe, sizeof exe, "%s/libxr_glslang_test.so", lib);
        if (access(exe, F_OK) != 0) { say("[PROBE] GLSLANG: test not in the APK"); toolchain_probe(lib, files); return; }
        SDL_snprintf(outp, sizeof outp, "%s/glslang_test.txt", files ? files : ".");
        SDL_snprintf(ldp, sizeof ldp, "LD_LIBRARY_PATH=%s", lib);
        args[0] = exe; args[1] = NULL;
        envp[0] = ldp; envp[1] = NULL;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 1, outp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&fa, 1, 2);
        rc = posix_spawn(&pid, exe, &fa, NULL, args, envp);
        posix_spawn_file_actions_destroy(&fa);
        if (rc != 0) say("[PROBE] GLSLANG: posix_spawn %s", strerror(rc));
        else if (waitpid(pid, &st, 0) < 0) say("[PROBE] GLSLANG: waitpid failed");
        else {
            char line[256] = "";
            FILE *f = fopen(outp, "r");
            if (f) { if (!fgets(line, sizeof line, f)) line[0] = 0; fclose(f); }
            line[strcspn(line, "\r\n")] = 0;
            say("[PROBE] GLSLANG ON DEVICE: exit %d, first line \"%s\"", WIFEXITED(st) ? WEXITSTATUS(st) : -1, line);
        }
    }
    toolchain_probe(lib, files);
}

int smoke_main(int argc, char **argv);   /* loader.c runs it when no game is installed */
int smoke_main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    const char *dir;
    char path[512];
    Uint64 t0;
    int frames = 0, running = 1, flash = 0;
    (void)argc; (void)argv;

    dir = SDL_GetAndroidInternalStoragePath();
    if (dir) { SDL_snprintf(path, sizeof path, "%s/smoke.log", dir); s_log = fopen(path, "w"); }
    say("[SMOKE] start, internal storage %s (log %s)", dir ? dir : "?", s_log ? "open" : "NOT writable");
    route_probes(dir);
#ifdef XR_SMOKE_TOOLKIT
    say("[SMOKE] toolkit linked: xbox_HeapAlloc %p, xbox_GetMemoryOffset %p", (void *)xbox_HeapAlloc,
        (void *)xbox_GetMemoryOffset);
#endif

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "vulkan");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) { say("[SMOKE] SDL_Init: %s", SDL_GetError()); return 1; }
    win = SDL_CreateWindow("smoke test", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_VULKAN);
    if (!win) { say("[SMOKE] window: %s", SDL_GetError()); return 1; }
    ren = SDL_CreateRenderer(win, "vulkan");
    if (!ren) { say("[SMOKE] VULKAN RENDERER FAILED: %s", SDL_GetError()); return 1; }
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(win, &w, &h);
        say("[SMOKE] renderer %s, %dx%d pixels, display %s", SDL_GetRendererName(ren), w, h,
            SDL_GetDisplayName(SDL_GetDisplayForWindow(win)) ? SDL_GetDisplayName(SDL_GetDisplayForWindow(win)) : "?");
    }
    SDL_SetRenderVSync(ren, 1);
    t0 = SDL_GetTicks();
    while (running && SDL_GetTicks() - t0 < 60000) {
        SDL_Event e;
        Uint64 s;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = 0; break;
            case SDL_EVENT_KEY_DOWN:
                say("[SMOKE] key %s", SDL_GetKeyName(e.key.key));
                if (e.key.key == SDLK_AC_BACK) running = 0;
                break;
            case SDL_EVENT_GAMEPAD_ADDED: {
                SDL_Gamepad *g = SDL_OpenGamepad(e.gdevice.which);
                say("[SMOKE] gamepad added: %s", g ? SDL_GetGamepadName(g) : SDL_GetError());
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                say("[SMOKE] pad button %s", SDL_GetGamepadStringForButton((SDL_GamepadButton)e.gbutton.button));
                flash = 1;
                break;
            case SDL_EVENT_FINGER_DOWN:
                say("[SMOKE] touch %.3f %.3f", e.tfinger.x, e.tfinger.y);
                flash = 1;
                break;
            case SDL_EVENT_WILL_ENTER_BACKGROUND: say("[SMOKE] to background"); break;
            case SDL_EVENT_DID_ENTER_FOREGROUND: say("[SMOKE] back in foreground"); break;
            default: break;
            }
        }
        s = (SDL_GetTicks() - t0) / 1000 % 3;
        if (flash) SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
        else SDL_SetRenderDrawColor(ren, s == 0 ? 200 : 0, s == 1 ? 200 : 0, s == 2 ? 200 : 0, 255);
        flash = 0;
        SDL_RenderClear(ren);
        SDL_RenderPresent(ren);
        frames++;
    }
    say("[SMOKE] end: %d frames in %.1f s (%.1f fps)", frames, (SDL_GetTicks() - t0) / 1000.0,
        frames * 1000.0 / (double)(SDL_GetTicks() - t0 + 1));
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (s_log) fclose(s_log);
    return 0;
}
