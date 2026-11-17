/* host_linux.c - the port host platform layer (xr_host.h), Linux + Android implementation. From Panzer Dragoon Orta's
 * src/platform/host_linux.c (Orta Recompiled: v1.1.0 Linux port, AppImage; Android port 2026-11-11, bionic is
 * POSIX - the differences are the __ANDROID__ branches below), moved into the toolkit 2026-11-16, names via
 * host_set_port.
 *
 * The same contract as host_win32.c (xr_host.h): layout folders beside the executable - or beside the AppImage file when
 * run from one ($APPIMAGE), so the game, saves and data stay portable next to it -, the log through a pipe with the
 * same repeat collapsing, the time limit, restart with the start environment, atomic writes, dialogs.
 * Android (maintainer 2026-11-11: NOT portable): game/ saves/ data/ live in the app's internal storage; the log is
 * mirrored to logcat (tag = the port's log name); RESTART NOW asks the activity to relaunch the app (an app cannot
 * exec itself: the activity's restartApp(), packaging/android).
 *
 * Crash reports and device traps: one vectored exception handler on the toolkit's signal-backed list
 * (win32_compat AddVectoredExceptionHandler; arm64 since option-c e8cb152), the same routing as host_win32.c (APU,
 * USB host controller, report).
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#endif

#include "kernel.h"
#include "xbox_memory_layout.h"

#include "xr_host.h"
#include "platform/host_paths.h"     /* xbox_host_root / xbox_host_self (option-c f88da14) */
#include "recomp_icall_feedback.h"   /* no-op unless RECOMP_ICALL_FEEDBACK */

extern ptrdiff_t g_xbox_mem_offset;
extern char **environ;

static void host_log_drain(void);

/* ── crash report ────────────────────────────────────────────────── */

static X86Ctx *diag_ctx(void)
{
    static X86Ctx none;
    X86Ctx *c = xbox_diag_ctx();
    return c ? c : &none;
}

static void print_guest_context(void *rip)
{
    X86Ctx *ctx = diag_ctx();
    Dl_info di;
    if (dladdr(rip, &di) && di.dli_sname)
        fprintf(stderr, "  in %s+0x%llX\n", di.dli_sname,
                (unsigned long long)((uintptr_t)rip - (uintptr_t)di.dli_saddr));
    if (g_xbox_mem_offset && g_esp) {
        const uint32_t *sp = (const uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp);
        int shown = 0, i;
        fprintf(stderr, "  guest stack (return addresses, innermost first):\n");
        for (i = 0; i < 256 && shown < 24; i++) {
            uint32_t v = sp[i];
            if (v > g_xbox_code_lo && v < g_xbox_code_hi) {
                fprintf(stderr, "    [esp+%-4d] 0x%08X\n", i * 4, v);
                shown++;
            }
        }
        if (!shown)
            fprintf(stderr, "    (no return addresses in range)\n");
    }
}

/* The same vectored handler as host_win32.c: the toolkit's win32_compat runs the AddVectoredExceptionHandler list
 * from its one sigaction handler (option-c 12c639c; CONTEXT from the ucontext, write flag from the page-fault
 * error code). Trapped device registers are served first - the APU (DirectSound's registers) and the USB host
 * controllers (the pads) -, anything else is a crash: the report ends the log and the search goes on, so the
 * toolkit's chain ends in the default action (core / exit). */
#if defined(__x86_64__) || defined(__aarch64__)
#if defined(__aarch64__)
#define HOST_CTX_PC(c) ((c)->Pc)     /* arm64 CONTEXT (option-c xbox_winnt.h: X[0..30], Sp, Pc, Cpsr) */
#define HOST_PC_NAME "PC"
#else
#define HOST_CTX_PC(c) ((c)->Rip)
#define HOST_PC_NAME "RIP"
#endif
extern bool apu_hook_handle_mmio(PCONTEXT ctx, uintptr_t fault_addr, uint32_t xbox_va, int is_write);
extern int xbox_OhciOwnsAddress(uint32_t xbox_va);
extern int xbox_OhciHandleMmio(void *ctx, uint32_t xbox_va);
#define APU_TRAP_BASE 0xFE800000u
#define APU_TRAP_END  0xFE880000u

static LONG veh_handler(PEXCEPTION_POINTERS ep)
{
    X86Ctx *ctx = diag_ctx();   /* diagnostics: this thread's registered register set */
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    if (code == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t fault_addr = ep->ExceptionRecord->ExceptionInformation[1];
        uint32_t xbox_va = (uint32_t)(fault_addr - (uintptr_t)g_xbox_mem_offset);

#if !defined(XR_PORTHOST_NO_APU)        /* the device traps: XBOXRECOMP_LLE_APU / _USB (CMakeLists.txt) */
        if (xbox_va >= APU_TRAP_BASE && xbox_va < APU_TRAP_END
                && apu_hook_handle_mmio(ep->ContextRecord, fault_addr, xbox_va,
                                        ep->ExceptionRecord->ExceptionInformation[0] ? 1 : 0))
            return EXCEPTION_CONTINUE_EXECUTION;
#endif
#if !defined(XR_PORTHOST_NO_USB)
        if (xbox_OhciOwnsAddress(xbox_va) && xbox_OhciHandleMmio(ep->ContextRecord, xbox_va))
            return EXCEPTION_CONTINUE_EXECUTION;
#endif

        fprintf(stderr, "[CRASH] Access violation at " HOST_PC_NAME "=0x%llX, fault addr=0x%llX (%s)\n",
                (unsigned long long)HOST_CTX_PC(ep->ContextRecord), (unsigned long long)fault_addr,
                ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read");
        fprintf(stderr, "  Xbox VA of fault: 0x%08X\n", xbox_va);
    } else if (code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
        fprintf(stderr, "[CRASH] exception 0x%08lX at " HOST_PC_NAME "=0x%llX\n",
                (unsigned long)code, (unsigned long long)HOST_CTX_PC(ep->ContextRecord));
    } else {
        return EXCEPTION_CONTINUE_SEARCH;   /* SIGTRAP (debugger), FPU signals: not ours */
    }
    fprintf(stderr, "  Xbox regs: eax=0x%08X ecx=0x%08X edx=0x%08X esp=0x%08X\n", g_eax, g_ecx, g_edx, g_esp);
    fprintf(stderr, "  Xbox regs: ebx=0x%08X esi=0x%08X edi=0x%08X\n", g_ebx, g_esi, g_edi);
    print_guest_context((void *)(uintptr_t)HOST_CTX_PC(ep->ContextRecord));
    RECOMP_ICALL_FEEDBACK_DUMP();   /* a title that dies mid-boot is the one whose targets we want */
    host_log_drain();               /* the process is about to die */
    return EXCEPTION_CONTINUE_SEARCH;
}

static void install_crash_handlers(void)
{
    /* last in the list (as on Windows): the runtime's own traps (AC97, null guard, watchpoints) stay in front */
    if (!AddVectoredExceptionHandler(0, veh_handler))
        fprintf(stderr, "[HOST] AddVectoredExceptionHandler failed -- no pad / APU register traps\n");
}
#else
static void install_crash_handlers(void)
{
    /* toolkit: MMIO trap decoders exist for x86-64 and arm64 only */
    fprintf(stderr, "[HOST] no vectored exception handlers on this CPU -- no pad / APU register traps\n");
}
#endif

void host_debug_start(void)
{
    /* <prefix>_WATCH_WRITE (debug registers) is Windows only */
}

/* ── time limit / quit ───────────────────────────────────────────── */

static void host_terminate(int code)
{
    RECOMP_ICALL_FEEDBACK_DUMP();
    host_log_drain();
    _exit(code);
}

static void *time_limit_thread(void *arg)
{
    double secs = *(double *)arg;
    struct timespec ts;
    ts.tv_sec = (time_t)secs;
    ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ;
    fprintf(stderr, "[HOST] --max-seconds %.0f reached, exiting\n", secs);
    host_terminate(0);
    return NULL;
}

void host_quit(void)
{
    fprintf(stderr, "[HOST] window closed, exiting\n");
    host_terminate(0);
}

/* ── paths ───────────────────────────────────────────────────────── */

static void join(char *out, const char *dir, const char *name)
{
    snprintf(out, HOST_PATH_MAX, "%s/%s", dir, name);
}

static void mkdir_p(const char *path)
{
    char tmp[HOST_PATH_MAX];
    size_t i;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (i = 1; tmp[i]; i++)
        if (tmp[i] == '/') {
            tmp[i] = 0;
            mkdir(tmp, 0755);
            tmp[i] = '/';
        }
    mkdir(tmp, 0755);
}

/* The folder the player sees: the toolkit's writable install root (option-c f88da14, platform/host_paths.h) =
 * beside the AppImage file when run from one ($APPIMAGE, believed only when the exe lies under $APPDIR), else
 * beside the executable. Without its trailing separator here (join() adds one). */
static void root_dir(char *out)
{
#ifdef __ANDROID__
    /* Android: the app's internal storage (not portable, maintainer 2026-11-11; kept on update, gone on uninstall) */
    const char *r = SDL_GetAndroidInternalStoragePath();
#else
    const char *r = xbox_host_root();
#endif
    size_t n;
    snprintf(out, HOST_PATH_MAX, "%s", (r && *r) ? r : "./");
    n = strlen(out);
    if (n > 1 && out[n - 1] == '/')
        out[n - 1] = 0;
}

/* ── log file with repeat collapsing (as host_win32.c) ───────────── */

#define HOST_LOG_KEEP      3
#define HOST_LOG_LINE_MAX  1024
#define HOST_LOG_VERBATIM  400

static FILE     *s_log;
static int       s_log_pipe_w = -1;
static int       s_log_pipe_r = -1;
static pthread_t s_log_thread;
static int       s_log_thread_on;
static volatile int s_log_thread_done;
static char      s_prev_key[HOST_LOG_LINE_MAX];
static char      s_last_line[HOST_LOG_LINE_MAX];
static unsigned long s_repeats;
static int       s_verbatim;

static void log_key(const char *line, char *key, size_t n)
{
    size_t k = 0;
    while (*line && k + 1 < n) {
        if (isxdigit((unsigned char)*line) && (isdigit((unsigned char)*line)
                || (line[1] && isxdigit((unsigned char)line[1])))) {
            while (*line && (isxdigit((unsigned char)*line) || *line == 'x'))
                line++;
            key[k++] = '#';
        } else {
            key[k++] = *line++;
        }
    }
    key[k] = '\0';
}

static void log_flush_run(void)
{
    if (s_repeats > HOST_LOG_KEEP - 1) {
        unsigned long hidden = s_repeats - (HOST_LOG_KEEP - 1);
        fprintf(s_log, "  [log] ... %lu more similar line(s), last: %s", hidden, s_last_line);
    }
    s_repeats = 0;
}

static void log_line(const char *line)
{
    char key[HOST_LOG_LINE_MAX];
    static int raw = -1;
#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_INFO, host_port_log_name(), line);   /* every line, uncollapsed: adb logcat -s <log name> */
#endif
    if (!strncmp(line, "[CRASH]", 7) || !strncmp(line, "[WATCHDOG]", 10)) {
        log_flush_run();
        s_prev_key[0] = '\0';
        s_verbatim = HOST_LOG_VERBATIM;
    }
    if (s_verbatim > 0) {
        s_verbatim--;
        fputs(line, s_log);
        return;
    }
    if (raw < 0) {
        const char *e = host_port_getenv("LOG_RAW");
        raw = e && *e && *e != '0';
    }
    if (raw) {
        fputs(line, s_log);
        return;
    }
    if (!strncmp(line, "[FTRACE]", 8) || !strncmp(line, "  [DRAW]", 8)) {
        log_flush_run();
        s_prev_key[0] = '\0';
        fputs(line, s_log);
        return;
    }
    log_key(line, key, sizeof(key));
    if (s_prev_key[0] && !strcmp(key, s_prev_key)) {
        s_repeats++;
        if (s_repeats < HOST_LOG_KEEP)
            fputs(line, s_log);
        else
            snprintf(s_last_line, sizeof(s_last_line), "%s", line);
        return;
    }
    log_flush_run();
    snprintf(s_prev_key, sizeof(s_prev_key), "%s", key);
    fputs(line, s_log);
}

static void *log_thread(void *unused)
{
    char buf[4096], line[HOST_LOG_LINE_MAX];
    size_t len = 0;
    ssize_t n, i;
    (void)unused;
    while ((n = read(s_log_pipe_r, buf, sizeof(buf))) > 0 || (n < 0 && errno == EINTR)) {
        for (i = 0; i < n; i++) {
            if (len + 2 < sizeof(line))
                line[len++] = buf[i];
            if (buf[i] == '\n' || len + 2 >= sizeof(line)) {
                if (line[len - 1] != '\n')
                    line[len++] = '\n';
                line[len] = '\0';
                log_line(line);
                len = 0;
            }
        }
        fflush(s_log);
    }
    if (len) {
        line[len] = '\0';
        log_line(line);
    }
    log_flush_run();
    fflush(s_log);
    s_log_thread_done = 1;
    return NULL;
}

static void host_log_drain(void)
{
    if (s_log_pipe_w < 0)
        return;
    fflush(stdout);
    fflush(stderr);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    close(s_log_pipe_w);
    s_log_pipe_w = -1;
    if (s_log_thread_on) {
        /* bounded wait: 5 s, as on Windows (bionic has no pthread_timedjoin_np: poll the thread's done flag) */
#ifdef __ANDROID__
        int t;
        for (t = 0; t < 500 && !s_log_thread_done; t++)
            usleep(10000);
        if (s_log_thread_done)
            pthread_join(s_log_thread, NULL);
#else
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5;
        pthread_timedjoin_np(s_log_thread, NULL, &ts);
#endif
        s_log_thread_on = 0;
    }
}

static void open_log(const char *data_dir)
{
    char log[HOST_PATH_MAX], prev[HOST_PATH_MAX];
    int fds[2];
    {
        char n1[96], n2[112];
        snprintf(n1, sizeof n1, "%s.log", host_port_log_name());
        snprintf(n2, sizeof n2, "%s.previous.log", host_port_log_name());
        join(log, data_dir, n1);
        join(prev, data_dir, n2);
    }
    rename(log, prev);
    s_log = fopen(log, "w");
    if (!s_log)
        return;
    if (pipe(fds) != 0) {
        fclose(s_log);
        if (freopen(log, "w", stdout))
            dup2(STDOUT_FILENO, STDERR_FILENO);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        return;
    }
    s_log_pipe_r = fds[0];
    s_log_pipe_w = fds[1];
    fcntl(s_log_pipe_r, F_SETFD, FD_CLOEXEC);
    fcntl(s_log_pipe_w, F_SETFD, FD_CLOEXEC);
    dup2(s_log_pipe_w, STDOUT_FILENO);
    dup2(s_log_pipe_w, STDERR_FILENO);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pthread_create(&s_log_thread, NULL, log_thread, NULL) == 0)
        s_log_thread_on = 1;
    atexit(host_log_drain);
}

/* ── restart ─────────────────────────────────────────────────────── */

static char **s_env0;
static int    s_argc;
static char **s_argv;
static int    s_restarted;

int host_was_restarted(void)
{
    return s_restarted;
}

static void keep_start_env(int argc, char **argv)
{
    int n = 0, i;
    while (environ && environ[n]) n++;
    s_env0 = (char **)calloc((size_t)n + 1, sizeof *s_env0);
    for (i = 0; s_env0 && i < n; i++)
        s_env0[i] = strdup(environ[i]);
    s_argc = argc;
    s_argv = argv;
}

/* A new process with the same arguments (minus the input files) and the start environment; this one ends. The new
 * one waits for this pid to be gone (--restart-wait) before it opens the log. From an AppImage the AppImage file is
 * started again, not the mounted copy (the mount goes away with this process). */
#ifdef __ANDROID__
/* An app cannot exec itself: the activity's restartApp() (packaging/android) starts RestartActivity in its own
 * process (it waits for this one to end, then starts the game again); this process ends; the settings are already
 * saved. */
void host_restart(void)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act = env ? (jobject)SDL_GetAndroidActivity() : NULL;
    jclass cls;
    jmethodID m;
    fprintf(stderr, "[HOST] restarting the app to apply the settings\n");
    host_log_drain();
    if (!act) { fprintf(stderr, "[HOST] restart: no activity -- the settings apply at the next start\n"); return; }
    cls = (*env)->GetObjectClass(env, act);
    m = cls ? (*env)->GetMethodID(env, cls, "restartApp", "()V") : NULL;
    if (m) (*env)->CallVoidMethod(env, act, m);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (cls) (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    _exit(0);
}
#else
void host_restart(void)
{
    char exe[HOST_PATH_MAX], pidbuf[32];
    char **args = (char **)calloc((size_t)s_argc + 4, sizeof *args);
    int i, k = 0;
    pid_t child;
    snprintf(exe, sizeof exe, "%s", xbox_host_self());   /* $APPIMAGE inside an AppImage, else this exe */
    if (!args) return;
    if (!exe[0]) {
        free(args);
        fprintf(stderr, "[HOST] restart: own file unknown -- the settings apply at the next start\n");
        return;
    }
    snprintf(pidbuf, sizeof pidbuf, "%lu", (unsigned long)getpid());
    args[k++] = exe;
    args[k++] = "--restart-wait";
    args[k++] = pidbuf;
    for (i = 1; i < s_argc; i++) {
        if (!strcmp(s_argv[i], "--input-script") || !strcmp(s_argv[i], "--input-live") ||
            !strcmp(s_argv[i], "--restart-wait")) {
            i++;
            continue;
        }
        args[k++] = s_argv[i];
    }
    args[k] = NULL;
    fprintf(stderr, "[HOST] restarting to apply the settings\n");
    child = fork();
    if (child == 0) {
        setsid();
        execve(exe, args, s_env0 ? s_env0 : environ);
        _exit(127);
    }
    free(args);
    if (child < 0) {
        fprintf(stderr, "[HOST] restart failed (errno %d) -- the settings apply at the next start\n", errno);
        return;
    }
    host_terminate(0);
}
#endif

static void set_input_file(const char *var, const char *prefix, const char *path)
{
    char full[PATH_MAX], val[HOST_PATH_MAX + 2];   /* realpath needs PATH_MAX (bionic fortify checks it) */
    if (!realpath(path, full))
        snprintf(full, sizeof full, "%s", path);
    snprintf(val, sizeof val, "%s%s", prefix, full);
    setenv(var, val, 1);
}

int host_init(int argc, char **argv, HostOptions *opt)
{
    static double s_limit;
    int console = 0, i;
    unsigned long wait_pid = 0;

    keep_start_env(argc, argv);
    memset(opt, 0, sizeof(*opt));
    root_dir(opt->exe_dir);
    join(opt->game_dir, opt->exe_dir, "game");
    join(opt->save_dir, opt->exe_dir, "saves");
    join(opt->data_dir, opt->exe_dir, "data");

    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--console")) { console = 1; continue; }
        if (!v) break;
        if (!strcmp(a, "--max-seconds")) opt->max_seconds = atof(v);
        else if (!strcmp(a, "--game-dir")) snprintf(opt->game_dir, HOST_PATH_MAX, "%s", v);
        else if (!strcmp(a, "--save-dir")) snprintf(opt->save_dir, HOST_PATH_MAX, "%s", v);
        else if (!strcmp(a, "--data-dir")) snprintf(opt->data_dir, HOST_PATH_MAX, "%s", v);
        else if (!strcmp(a, "--input-script")) set_input_file("RECOMP_PAD_SCRIPT", "@", v);
        else if (!strcmp(a, "--input-live")) set_input_file("RECOMP_PAD_LIVE", "", v);
        else if (!strcmp(a, "--restart-wait")) wait_pid = strtoul(v, NULL, 10);
        else continue;
        i++;
    }
    if (wait_pid) {                      /* host_restart: the old process leaves first (at most 10 s) */
        int t;
        s_restarted = 1;
        for (t = 0; t < 100 && kill((pid_t)wait_pid, 0) == 0; t++)
            usleep(100000);
    }

    mkdir_p(opt->save_dir);
    mkdir_p(opt->data_dir);
    if (!console) open_log(opt->data_dir);
    else { setvbuf(stdout, NULL, _IONBF, 0); setvbuf(stderr, NULL, _IONBF, 0); }
    /* Nothing outside the folder (CLAUDE.md, portable layout): the GPU drivers' shader caches go into data/
     * (portable on every system after the first start). Mesa + NVIDIA. */
    {
        char cache[HOST_PATH_MAX];
        join(cache, opt->data_dir, "shadercache");
        mkdir_p(cache);
        host_setenv_default("MESA_SHADER_CACHE_DIR", cache);
        host_setenv_default("__GL_SHADER_DISK_CACHE_PATH", cache);
        host_setenv_default("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP", "1");
    }
    if (chdir(opt->data_dir) != 0)
        fprintf(stderr, "[HOST] chdir %s failed (errno %d)\n", opt->data_dir, errno);
    RECOMP_ICALL_FEEDBACK_INIT();
    install_crash_handlers();

    if (opt->max_seconds > 0) {
        pthread_t t;
        s_limit = opt->max_seconds;
        if (pthread_create(&t, NULL, time_limit_thread, &s_limit) == 0)
            pthread_detach(t);
    }
    return 0;
}

void host_setenv_default(const char *name, const char *value)
{
    if (!getenv(name))
        setenv(name, value, 1);
}

void host_setenv(const char *name, const char *value)
{
    if (value && *value) setenv(name, value, 1);
    else unsetenv(name);
}

void host_sleep_ms(unsigned ms)
{
    usleep((useconds_t)ms * 1000u);
}

void *host_mutex_new(void)
{
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof *m);
    pthread_mutexattr_t a;
    if (!m) return NULL;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}

int host_mutex_take(void *m, unsigned ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000u;
    ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return pthread_mutex_timedlock((pthread_mutex_t *)m, &ts) == 0;
}

void host_mutex_give(void *m)
{
    pthread_mutex_unlock((pthread_mutex_t *)m);
}

int host_write_file_atomic(const char *path, const char *pending_dir, const void *data, size_t n)
{
    char tmp[HOST_PATH_MAX];
    const char *base = path + strlen(path);
    FILE *f;
    int ok;
    while (base > path && base[-1] != '/' && base[-1] != '\\')
        base--;
    mkdir_p(pending_dir);
    join(tmp, pending_dir, base);
    if (!(f = fopen(tmp, "wb")))
        return 0;
    ok = fwrite(data, 1, n, f) == n;
    ok = fflush(f) == 0 && ok;
    ok = fsync(fileno(f)) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (!ok)
        return 0;
    return rename(tmp, path) == 0;
}

void *host_read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *data;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = size > 0 ? malloc((size_t)size) : NULL;
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) *out_size = (size_t)size;
    return data;
}

void host_fatal(const char *msg)
{
    fprintf(stderr, "[FATAL] %s\n", msg);
    fflush(stderr);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, host_port_title(), msg, NULL);
}

void host_notice(const char *msg)
{
    const char *nd = getenv("RECOMP_NO_DIALOGS");
    fprintf(stderr, "[NOTICE] %s\n", msg);
    fflush(stderr);
    if (nd && *nd && *nd != '0')
        return;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, host_port_title(), msg, NULL);
}
