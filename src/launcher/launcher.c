/* launcher.c -- the player's one exe (see launcher.h). */

#include "launcher.h"

#include "game_setup_ui.h"
#include "platform/host_paths.h"
#include "sha256.h"
#include "zip_read.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define LP 1024
#define MAX_ARGS 256
#define TRAILER_MAGIC "XRPAYLD1"
#define TRAILER_SIZE 48                 /* magic (8), payload offset (8, LE), release (32, NUL padded) */

extern void xbox_set_window_icon(const void *bmp, size_t n);   /* kernel.h (window_icon.c) */

typedef struct {
    const XrLauncherConfig *cfg;
    /* root = the WRITABLE install folder (xbox_host_root: the exe's folder, or the folder of the .AppImage);
     * tools = RES, the launcher's files: root/tools unpacked from the payload (Windows), or - bundled - a
     * read-only folder that already holds them (AppImage: <exe>/../share/<slug>/tools, marked by release.txt) */
    char root[LP], tools[LP], game[LP], data[LP], build[LP], gen[LP], pipe[LP], obj[LP];
    char exe[LP];                       /* this exe (the payload trailer) */
    char game_exe[256];                 /* cfg->game_exe, without ".exe" off Windows */
    char release[33];                   /* the payload's release (the trailer, or a bundled release.txt) */
    int bundled;                        /* tools is a read-only, ready folder: no payload, no unpacking */
    char clang[LP], python[LP], rc[LP]; /* toolchain.txt: absolute; rc empty = no resource compiler */
    char ld[LP];                        /* toolchain.txt: the linker, empty = clang links */
    char out[LP];                       /* the game: root/game_exe ({OUT}) */
    XrSetupUi *ui;
    SDL_Mutex *log_mx;
    SDL_IOStream *log;
    double base, span;                  /* the running step's share of the bar */
    char step[128];
} L;

/* ── small helpers ─────────────────────────────────────────────────────── */

static void lg(L *l, const char *fmt, ...)
{
    char line[1400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[LAUNCHER] %s\n", line);
    if (l->log_mx)
        SDL_LockMutex(l->log_mx);
    if (l->log) {
        SDL_WriteIO(l->log, line, strlen(line));
        SDL_WriteIO(l->log, "\n", 1);
        SDL_FlushIO(l->log);
    }
    if (l->log_mx)
        SDL_UnlockMutex(l->log_mx);
}

static void join(char *out, size_t n, const char *a, const char *b)
{
    size_t k = strlen(a);
    snprintf(out, n, "%s%s%s", a, k && (a[k - 1] == '/' || a[k - 1] == '\\') ? "" : "/", b);
}

static int is_file(const char *p)
{
    SDL_PathInfo pi;
    return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_FILE;
}

static int is_dir(const char *p)
{
    SDL_PathInfo pi;
    return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_DIRECTORY;
}

static Sint64 file_size(const char *p)
{
    SDL_PathInfo pi;
    return SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_FILE ? (Sint64)pi.size : -1;
}

/* the whole file as a NUL-terminated string (NULL: missing) */
static char *read_text(const char *p)
{
    size_t n = 0;
    char *t = (char *)SDL_LoadFile(p, &n);
    return t;                           /* SDL_LoadFile adds a NUL */
}

static int write_text(const char *p, const char *text)
{
    return SDL_SaveFile(p, text, strlen(text));
}

static void write_if_missing(L *l, const char *dir, const char *name, const char *text)
{
    char p[LP];
    join(p, sizeof p, dir, name);
    if (!is_file(p) && !write_text(p, text))
        lg(l, "cannot write %s (%s)", p, SDL_GetError());
}

typedef struct { const char *dir; int ok; } RmCtx;
static int rm_tree(const char *p);
static SDL_EnumerationResult SDLCALL rm_cb(void *u, const char *dir, const char *name)
{
    char p[LP];
    RmCtx *c = (RmCtx *)u;
    join(p, sizeof p, dir, name);
    if (!rm_tree(p))
        c->ok = 0;
    return SDL_ENUM_CONTINUE;
}

/* removes p and everything below it - only ever on the launcher's own folders
 * (data/build, tools/bundle/unpack); links are removed, never followed */
static int rm_tree(const char *p)
{
    SDL_PathInfo pi;
    RmCtx c = { p, 1 };
    if (!SDL_GetPathInfo(p, &pi))
        return 1;
    if (pi.type == SDL_PATHTYPE_DIRECTORY)
        SDL_EnumerateDirectory(p, rm_cb, &c);
    return SDL_RemovePath(p) && c.ok;
}

/* the app's native library folder (Android: nativeLibraryDir, the only place programs may be executed from),
 * named by the app's loader in XR_NATIVE_DIR; "" elsewhere */
static const char *native_dir(void)
{
    const char *d = SDL_getenv("XR_NATIVE_DIR");
    return d ? d : "";
}

/* "{TOOLS}/x" with the placeholders filled in */
static void expand(L *l, const char *in, char *out, size_t n, const char *xbe)
{
    size_t o = 0;
    while (*in && o + 1 < n) {
        const char *v = NULL;
        size_t skip = 0;
        if (!strncmp(in, "{TOOLS}", 7)) v = l->tools, skip = 7;
        else if (!strncmp(in, "{NATIVE}", 8)) v = native_dir(), skip = 8;
        else if (!strncmp(in, "{OUT}", 5)) v = l->out, skip = 5;
        else if (!strncmp(in, "{ROOT}", 6)) v = l->root, skip = 6;
        else if (!strncmp(in, "{GEN}", 5)) v = l->gen, skip = 5;
        else if (!strncmp(in, "{PIPE}", 6)) v = l->pipe, skip = 6;
        else if (!strncmp(in, "{XBE}", 5)) v = xbe ? xbe : "", skip = 5;
        if (v) {
            size_t k = strlen(v);
            if (k && (v[k - 1] == '/' || v[k - 1] == '\\'))
                k--;
            if (o + k >= n)
                break;
            memcpy(out + o, v, k);
            o += k;
            in += skip;
        } else {
            out[o++] = *in++;
        }
    }
    out[o] = 0;
}

/* a line of a payload descriptor split at tabs into argv (in place) */
static int split_tabs(char *line, char **argv, int max)
{
    int n = 0;
    char *s = line;
    while (n < max - 1) {
        char *t = strchr(s, '\t');
        argv[n++] = s;
        if (!t)
            break;
        *t = 0;
        s = t + 1;
    }
    argv[n] = NULL;
    return n;
}

/* ── processes (SDL3) ──────────────────────────────────────────────────── */

/* runs argv in cwd, stdout + stderr appended to log_path (NULL: dropped);
 * returns the exit code, -1 when it could not start */
#ifdef __ANDROID__
/* SDL3 has no process support on Android (SDL_PROCESS_DUMMY). fork + execv; the child only calls
 * async-signal-safe functions (the parent has threads). posix_spawn cannot set the working folder before
 * API 34. The environment is inherited (the loader's LD_LIBRARY_PATH / PYTHONHOME reach clang and python). */
static int run_proc(L *l, char **argv, const char *cwd, const char *log_path)
{
    int fd = -1, nul = open("/dev/null", O_RDWR | O_CLOEXEC), st = 0, code = -1;
    pid_t pid;
    if (log_path && (fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644)) < 0)
        lg(l, "cannot open %s: %s", log_path, strerror(errno));
    pid = fork();
    if (pid == 0) {
        int o = fd >= 0 ? fd : nul;
        if (nul >= 0)
            dup2(nul, 0);
        if (o >= 0) {
            dup2(o, 1);
            dup2(o, 2);
        }
        if (cwd && chdir(cwd) != 0)
            _exit(126);
        execv(argv[0], argv);
        _exit(127);
    }
    if (pid < 0) {
        lg(l, "cannot start %s: fork: %s", argv[0], strerror(errno));
    } else {
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
            ;
        if (WIFEXITED(st)) {
            code = WEXITSTATUS(st);
            if (code == 127 || code == 126)
                lg(l, "%s: exit %d (%s)", argv[0], code,
                   code == 127 ? "could not be executed - not in the native library folder?"
                               : "working folder missing");
        } else if (WIFSIGNALED(st)) {
            lg(l, "%s: killed by signal %d", argv[0], WTERMSIG(st));
        }
    }
    if (fd >= 0)
        close(fd);
    if (nul >= 0)
        close(nul);
    return code;
}
#else
static int run_proc(L *l, char **argv, const char *cwd, const char *log_path)
{
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_IOStream *out = log_path ? SDL_IOFromFile(log_path, "ab") : NULL;
    SDL_Process *proc;
    int code = -1;
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv);
    if (cwd)
        SDL_SetStringProperty(p, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    if (out) {
        SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, out);
        SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    } else {
        SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    }
    SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    proc = SDL_CreateProcessWithProperties(p);
    SDL_DestroyProperties(p);
    if (!proc) {
        lg(l, "cannot start %s: %s", argv[0], SDL_GetError());
    } else {
        SDL_WaitProcess(proc, true, &code);
        SDL_DestroyProcess(proc);
    }
    if (out)
        SDL_CloseIO(out);
    return code;
}
#endif

/* ── a step on a worker thread, the window kept alive meanwhile ────────── */

typedef struct {
    int (*fn)(L *, void *);
    L *l;
    void *arg;
    SDL_AtomicInt done;
    int rc;
    double (*progress)(void *);         /* 0..1 within the step, or NULL */
} Job;

static int SDLCALL job_main(void *p)
{
    Job *j = (Job *)p;
    j->rc = j->fn(j->l, j->arg);
    SDL_SetAtomicInt(&j->done, 1);
    return 0;
}

static void ui_say(L *l, double within, const char *extra)
{
    char t[256];
    snprintf(t, sizeof t, "%s%s%s", l->step, extra && *extra ? " - " : "", extra ? extra : "");
    if (l->ui)
        xr_setup_ui_progress(l->ui, l->base + l->span * (within < 0 ? 0 : within > 1 ? 1 : within), t);
}

/* runs fn on a thread; 0 = done, else fn's code */
static int run_job(L *l, const char *step, double base, double span, int (*fn)(L *, void *), void *arg,
                   double (*progress)(void *))
{
    Job j;
    SDL_Thread *t;
    Uint64 t0 = SDL_GetTicks();
    memset(&j, 0, sizeof j);
    j.fn = fn; j.l = l; j.arg = arg; j.progress = progress;
    snprintf(l->step, sizeof l->step, "%s", step);
    l->base = base;
    l->span = span;
    lg(l, "%s", step);
    t = SDL_CreateThread(job_main, "launcher step", &j);
    if (!t)
        return fn(l, arg);
    while (!SDL_GetAtomicInt(&j.done)) {
        char e[64];
        double f = progress ? progress(arg) : 0.0;
        snprintf(e, sizeof e, "%u s", (unsigned)((SDL_GetTicks() - t0) / 1000));
        ui_say(l, f, e);
        SDL_Delay(100);
    }
    SDL_WaitThread(t, NULL);
    lg(l, "%s: %s after %u s", step, j.rc ? "FAILED" : "done", (unsigned)((SDL_GetTicks() - t0) / 1000));
    return j.rc;
}

/* ── 1. folders + READMEs ──────────────────────────────────────────────── */

static void make_folders(L *l)
{
    const XrLauncherConfig *c = l->cfg;
    char p[LP], text[2048];
    SDL_CreateDirectory(l->game);
    join(p, sizeof p, l->root, "media/shaders");
    SDL_CreateDirectory(p);
    join(p, sizeof p, l->root, "saves");
    SDL_CreateDirectory(p);
    SDL_CreateDirectory(l->data);
    if (!l->bundled)
        SDL_CreateDirectory(l->tools);
    snprintf(text, sizeof text,
             "Put your own %s disc image here: one .iso file - or the extracted disc files\r\n"
             "(default.xbe and its folders directly in this folder).\r\n\r\n"
             "Then start \"%s\" again. The image is checked (game and version) and\r\n"
             "installed into this folder; an .iso put here is deleted after a successful install.\r\n",
             c->disc, c->window_title);
    write_if_missing(l, l->game, "README.txt", text);
    join(p, sizeof p, l->root, "media");
    write_if_missing(l, p, "README.txt",
                     "Your own media (only read, never changed):\r\n\r\n"
                     "- Custom soundtracks: every folder below media that holds music (MP3, FLAC, Ogg Vorbis, WAV)\r\n"
                     "  is one album. .m4a / .aac files are skipped - convert them to MP3 or Ogg first.\r\n"
                     "- Boot animation: media\\boot.mpg (MPEG-1 video + MP2 audio) plays before the game.\r\n"
                     "  Convert your clip once with ffmpeg:\r\n"
                     "  ffmpeg -i boot.mp4 -vf scale=-2:720 -r 30 -c:v mpeg1video -q:v 2 -c:a mp2 -b:a 192k boot.mpg\r\n"
                     "- Shaders: RetroArch slang shader packs (zip files) in media\\shaders\\.\r\n");
}

/* ── 2. the game (the installer) ───────────────────────────────────────── */

typedef struct { SDL_AtomicInt done; char path[LP]; } Pick;

static void SDLCALL pick_cb(void *u, const char *const *files, int filter)
{
    Pick *p = (Pick *)u;
    (void)filter;
    if (files && files[0])
        snprintf(p->path, sizeof p->path, "%s", files[0]);
    SDL_SetAtomicInt(&p->done, 1);
}

static int choose_image(L *l, char *out, size_t n)
{
    static const SDL_DialogFileFilter f[] = { { "Xbox disc image", "iso;xiso" }, { "All files", "*" } };
    Pick p;
    memset(&p, 0, sizeof p);
#ifdef __ANDROID__
    /* Android filters by MIME type, and .iso has none it knows for sure: no filter, every file shows */
    (void)f;
    SDL_ShowOpenFileDialog(pick_cb, &p, NULL, NULL, 0, NULL, false);
#else
    SDL_ShowOpenFileDialog(pick_cb, &p, NULL, f, 2, NULL, false);
#endif
    while (!SDL_GetAtomicInt(&p.done)) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            ;
        SDL_Delay(20);
    }
    if (!p.path[0])
        return 0;
    if (strstr(p.path, "://")) {        /* a URI (Android: content://): the port makes it a readable path */
        if (!l->cfg->open_uri || !l->cfg->open_uri(p.path, out, n) || !out[0]) {
            lg(l, "disc image picked as %s, which cannot be opened%s", p.path,
               l->cfg->open_uri ? "" : " (the port gives no open_uri)");
            xr_setup_ui_message(l->ui, 1, "The chosen file cannot be opened - please choose it again.");
            return 0;
        }
        lg(l, "disc image picked: %s as %s", p.path, out);
        return 1;
    }
    snprintf(out, n, "%s", p.path);
    lg(l, "disc image picked: %s", out);
    return 1;
}

enum { B_CHOOSE = 1, B_OPEN, B_AGAIN, B_QUIT };

static int no_game_dialog(L *l)
{
    /* short labels: the Windows message box gives every button the same fixed width */
#ifdef __ANDROID__
    /* the game folder is the app's own storage: no file manager reaches it, so only the picker */
    const SDL_MessageBoxButtonData b[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, B_CHOOSE, "Choose image..." },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, B_QUIT, "Quit" },
    };
#else
    const SDL_MessageBoxButtonData b[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, B_CHOOSE, "Choose image..." },
        { 0, B_OPEN, "Open folder" },
        { 0, B_AGAIN, "Check again" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, B_QUIT, "Quit" },
    };
#endif
    char msg[1600];
    SDL_MessageBoxData d;
    int id = B_QUIT;
#ifdef __ANDROID__
    snprintf(msg, sizeof msg,
             "%s not found.\n\nChoose your %s disc image (.iso) with \"Choose image...\".\n"
             "It is read from where it is and not changed; the game is installed into the app.",
             l->cfg->game, l->cfg->disc);
#else
    snprintf(msg, sizeof msg,
             "%s not found.\n\nPlace your %s disc image (.iso) or the extracted disc files in:\n\n%s\n\n"
             "and choose \"Check again\" - or pick the disc image where it is now with \"Choose image...\"\n"
             "(it is read from there and not changed). \"Open folder\" shows the game folder.",
             l->cfg->game, l->cfg->disc, l->game);
#endif
    memset(&d, 0, sizeof d);
    d.flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
    d.title = l->cfg->window_title;
    d.message = msg;
    d.numbuttons = (int)(sizeof b / sizeof b[0]);
    d.buttons = b;
    if (!SDL_ShowMessageBox(&d, &id))
        return B_QUIT;
    return id;
}

static void open_folder(L *l, const char *dir)
{
    char url[LP + 16], *s;
    snprintf(url, sizeof url, "file:///%s", dir);
    for (s = url; *s; s++)
        if (*s == '\\')
            *s = '/';
    if (!SDL_OpenURL(url))
        lg(l, "cannot open %s: %s", url, SDL_GetError());
}

static void setup_log(void *ctx, const char *text)
{
    lg((L *)ctx, "setup: %s", text);
}

typedef struct { L *l; } UiCtx;

static void setup_progress(void *ctx, double f, const char *text)
{
    L *l = (L *)ctx;
    snprintf(l->step, sizeof l->step, "Installing the game");
    l->base = 0.0;
    l->span = 0.10;
    ui_say(l, f, text);
}

static int setup_cancelled(void *ctx)
{
    L *l = (L *)ctx;
    return l->ui && xr_setup_ui_cancelled(l->ui);
}

/* 1 = the game is there (sha1 filled), 0 = quit */
static int ensure_game(L *l, const char *image_arg, char sha1[41])
{
    char picked[LP] = "";
    if (image_arg)
        snprintf(picked, sizeof picked, "%s", image_arg);
    for (;;) {
        xr_game_setup_opts o;
        xr_game_setup_result r;
        int rc;
        memset(&o, 0, sizeof o);
        o.game_dir = l->game;
        o.game = l->cfg->game;
        o.title_id = l->cfg->title_id;
        o.versions = l->cfg->versions;
        o.version_count = l->cfg->version_count;
        o.ctx = l;
        o.log = setup_log;
        o.progress = setup_progress;
        o.cancelled = setup_cancelled;
        o.image_path = picked[0] ? picked : NULL;
        rc = xr_game_setup(&o, &r);
        lg(l, "game: %s - %s", xr_game_setup_code_name(rc), r.message);
        if (rc == XR_SETUP_READY || rc == XR_SETUP_INSTALLED) {
            snprintf(sha1, 41, "%s", r.xbe_sha1);
            if (!sha1[0]) {
                /* a ready folder: the installer read the XBE for its checks; ask once more for the hash */
                snprintf(sha1, 41, "unknown");
            }
            return 1;
        }
        if (rc != XR_SETUP_NO_GAME || picked[0]) {
            xr_setup_ui_message(l->ui, 1, r.message);
            if (xr_no_dialogs() || picked[0] == 0)
                return 0;
            picked[0] = 0;              /* a bad pick: ask again */
            continue;
        }
        if (xr_no_dialogs())
            return 0;
        switch (no_game_dialog(l)) {
        case B_CHOOSE:
            if (!choose_image(l, picked, sizeof picked))
                picked[0] = 0;
            break;
        case B_OPEN:
            open_folder(l, l->game);
            break;
        case B_AGAIN:
            break;
        default:
            return 0;
        }
    }
}

/* ── 3. the payload ────────────────────────────────────────────────────── */

typedef struct { SDL_AtomicInt done_n; int total; } Prog;

static double prog_frac(void *a)
{
    Prog *p = (Prog *)a;
    return p->total ? (double)SDL_GetAtomicInt(&p->done_n) / p->total : 0.0;
}

static int read_trailer(L *l, Uint64 *offset)
{
    SDL_IOStream *f = SDL_IOFromFile(l->exe, "rb");
    unsigned char t[TRAILER_SIZE];
    Sint64 size;
    int ok = 0, i;
    if (!f)
        return 0;
    size = SDL_GetIOSize(f);
    if (size > TRAILER_SIZE && SDL_SeekIO(f, size - TRAILER_SIZE, SDL_IO_SEEK_SET) >= 0
        && SDL_ReadIO(f, t, sizeof t) == sizeof t && !memcmp(t, TRAILER_MAGIC, 8)) {
        *offset = 0;
        for (i = 7; i >= 0; i--)
            *offset = (*offset << 8) | t[8 + i];
        memcpy(l->release, t + 16, 32);
        l->release[32] = 0;
        ok = *offset < (Uint64)size;
    }
    SDL_CloseIO(f);
    return ok;
}

typedef struct { Prog p; Uint64 offset; } UnpackArg;

static int unpack_job(L *l, void *a)
{
    UnpackArg *u = (UnpackArg *)a;
    char zp[LP], err[256], marker[LP];
    SDL_IOStream *in = SDL_IOFromFile(l->exe, "rb"), *out;
    Sint64 end;
    ZipFile *z;
    int i, bad = 0;
    void *buf;
    if (!in)
        return 1;
    end = SDL_GetIOSize(in) - TRAILER_SIZE;
    join(zp, sizeof zp, l->tools, "payload.zip");
    out = SDL_IOFromFile(zp, "wb");
    if (!out || SDL_SeekIO(in, (Sint64)u->offset, SDL_IO_SEEK_SET) < 0) {
        SDL_CloseIO(in);
        if (out)
            SDL_CloseIO(out);
        return 1;
    }
    buf = malloc(1 << 20);
    while (buf && SDL_TellIO(in) < end) {
        Sint64 left = end - SDL_TellIO(in);
        size_t want = left > (1 << 20) ? (1 << 20) : (size_t)left, got = SDL_ReadIO(in, buf, want);
        if (!got || SDL_WriteIO(out, buf, got) != got) {
            bad = 1;
            break;
        }
    }
    free(buf);
    SDL_CloseIO(in);
    SDL_CloseIO(out);
    if (bad)
        return 1;
    if (!(z = zip_open(zp, err, sizeof err))) {
        lg(l, "payload: %s", err);
        return 1;
    }
    u->p.total = zip_count(z);
    for (i = 0; i < zip_count(z) && !bad; i++) {
        const ZipEntry *e = zip_entry(z, i);
        char rel[LP], dst[LP], *slash;
        void *data;
        if (!zip_safe_path(e->name, rel, sizeof rel)) {
            lg(l, "payload: refused entry %s", e->name);
            bad = 1;
            break;
        }
        join(dst, sizeof dst, l->tools, rel);
        if (e->dir) {
            SDL_CreateDirectory(dst);
        } else {
            if ((slash = strrchr(dst, '/')) != NULL) {
                *slash = 0;
                SDL_CreateDirectory(dst);
                *slash = '/';
            }
            data = malloc(e->usize ? e->usize : 1);
            if (!data || !zip_extract(z, i, data, err, sizeof err) || !SDL_SaveFile(dst, data, e->usize)) {
                lg(l, "payload: %s: %s", e->name, data ? err : "out of memory");
                bad = 1;
            }
            free(data);
        }
        SDL_AddAtomicInt(&u->p.done_n, 1);
    }
    zip_close(z);
    SDL_RemovePath(zp);
    if (bad)
        return 1;
    join(marker, sizeof marker, l->tools, "release.txt");
    return write_text(marker, l->release) ? 0 : 1;
}

/* ── 4. the compiler ───────────────────────────────────────────────────── */

/* Everything ships in the release (maintainer, 2026-11-10: the player downloads nothing after the release
 * download, the first start runs offline): no download, no fallback to one. toolchain.txt (format 2, tab
 * separated, paths relative to the tools folder):
 *
 *   format  2
 *   clang   llvm-mingw/bin/clang.exe        the compiler driver (Linux: llvm/bin/clang)
 *   python  python/python.exe               the lifter's Python (Linux: python/bin/python3)
 *   rc      llvm-mingw/bin/llvm-rc.exe      optional: the resource compiler (Windows); without it no .rc is built
 *   bundle  bundle/<zip> <sha256> <size> <folder in the zip> <folder under tools>
 *                                           optional: a shipped zip unpacked at the first start (Windows); without
 *                                           it the compiler is already unpacked (an AppImage's read-only tools)
 *   ld      {NATIVE}/liblld_exe.so          optional: the linker run for the link line instead of clang (Android:
 *                                           lld called directly, its line starts with "-flavor gnu")
 *
 * clang / python / rc / ld may start with {NATIVE}/ instead: the app's native library folder (Android,
 * XR_NATIVE_DIR), the only place an app may execute files from since API 29.
 *
 * The zip goes through the toolkit's reader (no system tools: Wine / Proton have no tar.exe or powershell). */
#define PACKAGE_INCOMPLETE "The package is incomplete - please download it again."

typedef struct {
    int format;
    char clang[LP], python[LP], rc[LP], ld[LP];  /* relative, as written */
    char zip[LP], sha[65], folder[256], dest[256];
    Sint64 size;
    Prog unpack;                        /* entries unpacked */
} Toolchain;

/* a relative path that stays inside the tools folder (no "..", not absolute) */
static int safe_rel(const char *p)
{
    return p[0] && p[0] != '/' && p[0] != '\\' && !strchr(p, ':') && !strstr(p, "..");
}

/* a toolchain.txt program: relative to the tools folder, or "{NATIVE}/<file>" in the native library folder */
static int tc_path(L *l, const char *rel, char *out, size_t n)
{
    if (!strncmp(rel, "{NATIVE}/", 9)) {
        if (!native_dir()[0] || !safe_rel(rel + 9))
            return 0;
        join(out, n, native_dir(), rel + 9);
        return 1;
    }
    if (!safe_rel(rel))
        return 0;
    join(out, n, l->tools, rel);
    return 1;
}

static int read_toolchain(L *l, Toolchain *t)
{
    char p[LP], *text, *line, *save = NULL;
    int bad = 0;
    memset(t, 0, sizeof *t);
    join(p, sizeof p, l->tools, "toolchain.txt");
    if (!(text = read_text(p)))
        return 0;
    for (line = SDL_strtok_r(text, "\r\n", &save); line; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        char *argv[8];
        int n = split_tabs(line, argv, 8);
        if (n < 1 || argv[0][0] == '#')
            continue;
        if (n < 2)
            continue;
        if (!strcmp(argv[0], "format")) t->format = SDL_atoi(argv[1]);
        else if (!strcmp(argv[0], "clang")) snprintf(t->clang, sizeof t->clang, "%s", argv[1]);
        else if (!strcmp(argv[0], "python")) snprintf(t->python, sizeof t->python, "%s", argv[1]);
        else if (!strcmp(argv[0], "rc")) snprintf(t->rc, sizeof t->rc, "%s", argv[1]);
        else if (!strcmp(argv[0], "ld")) snprintf(t->ld, sizeof t->ld, "%s", argv[1]);
        else if (!strcmp(argv[0], "bundle") && n >= 6) {
            snprintf(t->zip, sizeof t->zip, "%s", argv[1]);
            snprintf(t->sha, sizeof t->sha, "%s", argv[2]);
            t->size = (Sint64)SDL_strtoll(argv[3], NULL, 10);
            snprintf(t->folder, sizeof t->folder, "%s", argv[4]);
            snprintf(t->dest, sizeof t->dest, "%s", argv[5]);
        } else if (!strcmp(argv[0], "url")) {
            bad = 1;                    /* an old descriptor: it would want a download */
        }
    }
    SDL_free(text);
    if (bad || t->format != 2)
        return 0;
    if (t->zip[0] && (!safe_rel(t->zip) || !safe_rel(t->dest) || strlen(t->sha) != 64 || t->size <= 0))
        return 0;
    l->rc[0] = l->ld[0] = 0;
    if (!tc_path(l, t->clang, l->clang, sizeof l->clang) || !tc_path(l, t->python, l->python, sizeof l->python))
        return 0;
    if (t->rc[0] && !tc_path(l, t->rc, l->rc, sizeof l->rc))
        return 0;
    if (t->ld[0] && !tc_path(l, t->ld, l->ld, sizeof l->ld))
        return 0;
    return 1;
}

static int file_sha256(const char *p, char hex[65])
{
    SDL_IOStream *f = SDL_IOFromFile(p, "rb");
    XrSha256 s;
    uint8_t d[32];
    void *buf = malloc(1 << 20);
    size_t got;
    if (!f || !buf) {
        if (f)
            SDL_CloseIO(f);
        free(buf);
        return 0;
    }
    xr_sha256_init(&s);
    while ((got = SDL_ReadIO(f, buf, 1 << 20)) > 0)
        xr_sha256_update(&s, buf, got);
    SDL_CloseIO(f);
    free(buf);
    xr_sha256_final(&s, d);
    xr_sha256_hex(d, hex);
    return 1;
}

/* t->zip here is the absolute path (ensure_toolchain) */
static int check_job(L *l, void *a)
{
    Toolchain *t = (Toolchain *)a;
    char hex[65];
    if (!file_sha256(t->zip, hex))
        return 1;
    lg(l, "compiler bundle sha256 %s (expected %s)", hex, t->sha);
    return SDL_strcasecmp(hex, t->sha) ? 2 : 0;
}

static double unpack_frac(void *a)
{
    return prog_frac(&((Toolchain *)a)->unpack);
}

/* the zip into tools/bundle/unpack (every entry through the zip-slip guard, streamed: libLLVM is 78 MB), then its
 * folder moved to tools/<dest> with the zip's SHA-256 as the marker. The zip stays: a damaged or deleted
 * compiler folder is unpacked again offline. */
static int unpack_tc_job(L *l, void *a)
{
    Toolchain *t = (Toolchain *)a;
    char tmp[LP], from[LP], to[LP], marker[LP], err[256];
    ZipFile *z;
    int i, bad = 0;
    join(tmp, sizeof tmp, l->tools, "bundle/unpack");
    rm_tree(tmp);
    SDL_CreateDirectory(tmp);
    if (!(z = zip_open(t->zip, err, sizeof err))) {
        lg(l, "compiler zip: %s", err);
        return 1;
    }
    t->unpack.total = zip_count(z);
    for (i = 0; i < zip_count(z) && !bad; i++) {
        const ZipEntry *e = zip_entry(z, i);
        char rel[LP], dst[LP], *slash;
        if (!zip_safe_path(e->name, rel, sizeof rel)) {
            lg(l, "compiler zip: refused entry %s", e->name);
            bad = 1;
            break;
        }
        join(dst, sizeof dst, tmp, rel);
        if (e->dir) {
            SDL_CreateDirectory(dst);
        } else {
            if ((slash = strrchr(dst, '/')) != NULL) {
                *slash = 0;
                SDL_CreateDirectory(dst);   /* creates the parents too */
                *slash = '/';
            }
            if (!zip_extract_to_file(z, i, dst, err, sizeof err)) {
                lg(l, "compiler zip: %s: %s", e->name, err);
                bad = 1;
            }
        }
        SDL_AddAtomicInt(&t->unpack.done_n, 1);
    }
    zip_close(z);
    if (bad)
        return 1;
    lg(l, "compiler zip: %d entries unpacked", t->unpack.total);
    join(from, sizeof from, tmp, t->folder);
    join(to, sizeof to, l->tools, t->dest);
    rm_tree(to);
    if (!SDL_RenamePath(from, to)) {
        lg(l, "cannot move %s to %s: %s", from, to, SDL_GetError());
        return 1;
    }
    rm_tree(tmp);
    join(marker, sizeof marker, to, ".sha256");
    return write_text(marker, t->sha) ? 0 : 1;
}

static int ensure_toolchain(L *l)
{
    Toolchain t;
    char marker[LP], *have;
    if (!read_toolchain(l, &t)) {
        lg(l, "toolchain.txt missing, damaged or not format 2 (a download line?) in %s", l->tools);
        xr_setup_ui_message(l->ui, 1, PACKAGE_INCOMPLETE);
        return 0;
    }
    if (t.zip[0]) {
        char dest[LP], zrel[LP];
        join(dest, sizeof dest, l->tools, t.dest);
        join(marker, sizeof marker, dest, ".sha256");
        have = read_text(marker);
        if (have && !SDL_strcasecmp(have, t.sha) && is_file(l->clang) && is_file(l->python)) {
            SDL_free(have);
            return 1;
        }
        SDL_free(have);
        snprintf(zrel, sizeof zrel, "%s", t.zip);
        join(t.zip, sizeof t.zip, l->tools, zrel);
        if (file_size(t.zip) != t.size) {
            lg(l, "compiler bundle %s: size %lld, expected %lld", t.zip, (long long)file_size(t.zip),
               (long long)t.size);
            xr_setup_ui_message(l->ui, 1, PACKAGE_INCOMPLETE);
            return 0;
        }
        if (run_job(l, "Checking the compiler", 0.10, 0.04, check_job, &t, NULL)) {
            xr_setup_ui_message(l->ui, 1, PACKAGE_INCOMPLETE);
            return 0;
        }
        if (run_job(l, "Unpacking the compiler (once)", 0.14, 0.12, unpack_tc_job, &t, unpack_frac)) {
            xr_setup_ui_message(l->ui, 1, "The compiler could not be unpacked - see data/launcher.log.");
            return 0;
        }
    }
    if (!is_file(l->clang) || !is_file(l->python) || (l->ld[0] && !is_file(l->ld))) {
        lg(l, "compiler %s, python %s or linker %s missing", l->clang, l->python, l->ld[0] ? l->ld : "(clang)");
        xr_setup_ui_message(l->ui, 1, PACKAGE_INCOMPLETE);
        return 0;
    }
    return 1;
}

/* ── 5. lift ───────────────────────────────────────────────────────────── */

typedef struct { Prog p; const char *xbe; } LiftArg;

static int lift_job(L *l, void *a)
{
    LiftArg *la = (LiftArg *)a;
    char p[LP], *text, *line, *save = NULL, py[LP], cwd[LP], logp[LP];
    int lines = 0;
    join(p, sizeof p, l->tools, "lift.txt");
    if (!(text = read_text(p)))
        return 1;
    for (line = text; *line; line++)
        if (*line == '\n')
            lines++;
    la->p.total = lines ? lines : 1;
    snprintf(py, sizeof py, "%s", l->python);
    join(cwd, sizeof cwd, l->tools, "lifter");
    join(logp, sizeof logp, l->build, "lift.log");
    for (line = SDL_strtok_r(text, "\r\n", &save); line; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        static char exp[MAX_ARGS][LP];
        char *raw[MAX_ARGS], *argv[MAX_ARGS + 3];
        int n = split_tabs(line, raw, MAX_ARGS), i, k = 0;
        if (n < 1 || raw[0][0] == '#')
            continue;
        if (!strcmp(raw[0], "mkdir")) {
            for (i = 1; i < n; i++) {
                expand(l, raw[i], exp[0], LP, la->xbe);
                SDL_CreateDirectory(exp[0]);
            }
            SDL_AddAtomicInt(&la->p.done_n, 1);
            continue;
        }
        argv[k++] = py;
        argv[k++] = "-I";
        argv[k++] = "-B";               /* no __pycache__ next to the lifter (read-only inside an AppImage) */
        for (i = 1; i < n && k < MAX_ARGS + 2; i++) {
            expand(l, raw[i], exp[i], LP, la->xbe);
            argv[k++] = exp[i];
        }
        argv[k] = NULL;
        lg(l, "lift: %s", raw[0]);
        if (run_proc(l, argv, cwd, logp) != 0) {
            lg(l, "lift step %s failed - see %s", raw[0], logp);
            SDL_free(text);
            return 1;
        }
        SDL_AddAtomicInt(&la->p.done_n, 1);
    }
    SDL_free(text);
    return 0;
}

/* ── 6. compile + link ─────────────────────────────────────────────────── */

#define MAX_UNITS 256
typedef struct {
    Prog p;
    char *cc[MAX_ARGS];                 /* the common compile args */
    int ncc;
    char src[MAX_UNITS][LP], objp[MAX_UNITS][LP];
    int nsrc;
    SDL_AtomicInt next, failed;
    char *link[MAX_ARGS];
    int nlink;
    char rc_file[LP], res[LP];
    char *rc_args[MAX_ARGS];
    int nrc;
    char copy[16][LP];
    int ncopy;
    char clang[LP], llvm_rc[LP], ld[LP];
    char *text;                        /* the descriptor's buffer the args point into */
    char argbuf[MAX_ARGS * 3][LP];      /* expanded args */
    int nargbuf;
} Build;

static char *bstore(L *l, Build *b, const char *raw)
{
    char *d;
    if (b->nargbuf >= MAX_ARGS * 3)
        return (char *)raw;
    d = b->argbuf[b->nargbuf++];
    expand(l, raw, d, LP, NULL);
    return d;
}

static SDL_EnumerationResult SDLCALL gen_cb(void *u, const char *dir, const char *name)
{
    Build *b = (Build *)u;
    size_t k = strlen(name);
    if (k > 2 && !strcmp(name + k - 2, ".c") && b->nsrc < MAX_UNITS)
        join(b->src[b->nsrc++], LP, dir, name);
    return SDL_ENUM_CONTINUE;
}

static int read_build(L *l, Build *b)
{
    char p[LP], *line, *save = NULL;
    join(p, sizeof p, l->tools, "build.txt");
    if (!(b->text = read_text(p)))
        return 0;
    for (line = SDL_strtok_r(b->text, "\r\n", &save); line; line = SDL_strtok_r(NULL, "\r\n", &save)) {
        char *raw[MAX_ARGS];
        int n = split_tabs(line, raw, MAX_ARGS), i;
        if (n < 1 || raw[0][0] == '#')
            continue;
        if (!strcmp(raw[0], "cc")) {
            for (i = 1; i < n && b->ncc < MAX_ARGS - 1; i++)
                b->cc[b->ncc++] = bstore(l, b, raw[i]);
        } else if (!strcmp(raw[0], "src") && n > 1 && b->nsrc < MAX_UNITS) {
            expand(l, raw[1], b->src[b->nsrc++], LP, NULL);
        } else if (!strcmp(raw[0], "gen")) {
            SDL_EnumerateDirectory(l->gen, gen_cb, b);
        } else if (!strcmp(raw[0], "link")) {
            for (i = 1; i < n && b->nlink < MAX_ARGS - 1; i++)
                b->link[b->nlink++] = bstore(l, b, raw[i]);
        } else if (!strcmp(raw[0], "rc") && n > 1) {
            expand(l, raw[1], b->rc_file, LP, NULL);
            for (i = 2; i < n && b->nrc < MAX_ARGS - 1; i++)
                b->rc_args[b->nrc++] = bstore(l, b, raw[i]);
        } else if (!strcmp(raw[0], "copy") && n > 1 && b->ncopy < 16) {
            expand(l, raw[1], b->copy[b->ncopy++], LP, NULL);
        }
    }
    snprintf(b->clang, sizeof b->clang, "%s", l->clang);
    snprintf(b->llvm_rc, sizeof b->llvm_rc, "%s", l->rc);
    snprintf(b->ld, sizeof b->ld, "%s", l->ld);
    if (b->rc_file[0] && !b->llvm_rc[0])
        lg(l, "build.txt names %s but toolchain.txt no resource compiler: built without it", b->rc_file);
    return b->ncc > 0 && b->nsrc > 0 && b->nlink > 0;
}

typedef struct { L *l; Build *b; } Worker;

static int SDLCALL compile_worker(void *p)
{
    Worker *w = (Worker *)p;
    Build *b = w->b;
    for (;;) {
        int i = SDL_AddAtomicInt(&b->next, 1), k = 0, j;
        char *argv[MAX_ARGS + 8], logp[LP];
        if (i >= b->nsrc)
            break;
        argv[k++] = b->clang;
        for (j = 0; j < b->ncc; j++)
            argv[k++] = b->cc[j];
        argv[k++] = "-c";
        argv[k++] = b->src[i];
        argv[k++] = "-o";
        argv[k++] = b->objp[i];
        argv[k] = NULL;
        snprintf(logp, sizeof logp, "%s.log", b->objp[i]);
        SDL_RemovePath(logp);
        if (run_proc(w->l, argv, w->l->root, logp) != 0) {
            lg(w->l, "compile failed: %s - see %s", b->src[i], logp);
            SDL_AddAtomicInt(&b->failed, 1);
        }
        SDL_AddAtomicInt(&b->p.done_n, 1);
    }
    return 0;
}

static int compile_job(L *l, void *a)
{
    Build *b = (Build *)a;
    int n = SDL_GetNumLogicalCPUCores(), i;
    const char *cap = SDL_getenv("XR_LAUNCHER_JOBS");   /* all cores for a player; a cap for shared machines */
    SDL_Thread *t[64];
    Worker w = { l, b };
    if (cap && SDL_atoi(cap) > 0 && SDL_atoi(cap) < n)
        n = SDL_atoi(cap);
    if (n < 1)
        n = 1;
    if (n > 64)
        n = 64;
    if (n > b->nsrc)
        n = b->nsrc;
    b->p.total = b->nsrc;
    for (i = 0; i < b->nsrc; i++) {
        const char *s = strrchr(b->src[i], '/'), *bs = strrchr(b->src[i], '\\');
        char name[LP];
        if (bs > s)
            s = bs;
        snprintf(name, sizeof name, "%03d_%s.o", i, s ? s + 1 : b->src[i]);
        join(b->objp[i], LP, l->obj, name);
    }
    lg(l, "compiling %d files on %d threads", b->nsrc, n);
    for (i = 0; i < n; i++)
        t[i] = SDL_CreateThread(compile_worker, "compile", &w);
    for (i = 0; i < n; i++)
        if (t[i])
            SDL_WaitThread(t[i], NULL);
    return SDL_GetAtomicInt(&b->failed) ? 1 : 0;
}

static int link_job(L *l, void *a)
{
    Build *b = (Build *)a;
    char *argv[MAX_UNITS + MAX_ARGS + 8], out[LP], logp[LP];
    int k = 0, i, j;
    join(logp, sizeof logp, l->build, "link.log");
    SDL_RemovePath(logp);
    if (b->rc_file[0] && b->llvm_rc[0]) {
        char *rv[MAX_ARGS + 8];
        int r = 0;
        join(b->res, sizeof b->res, l->obj, "resources.res");
        rv[r++] = b->llvm_rc;
        for (i = 0; i < b->nrc; i++)
            rv[r++] = b->rc_args[i];
        rv[r++] = "/fo";
        rv[r++] = b->res;
        rv[r++] = b->rc_file;
        rv[r] = NULL;
        if (run_proc(l, rv, l->root, logp) != 0)
            return 1;
    }
    join(out, sizeof out, l->root, l->game_exe);
    argv[k++] = b->ld[0] ? b->ld : b->clang;
    for (i = 0; i < b->nlink && strcmp(b->link[i], "{OBJECTS}"); i++)
        ;
    if (i < b->nlink) {
        /* a full line (a linker run directly: crt objects before, libraries after the game's objects): the
         * objects go where {OBJECTS} stands, the output is the line's own {OUT} */
        for (i = 0; i < b->nlink; i++) {
            if (strcmp(b->link[i], "{OBJECTS}")) {
                argv[k++] = b->link[i];
                continue;
            }
            for (j = 0; j < b->nsrc; j++)
                argv[k++] = b->objp[j];
            if (b->rc_file[0] && b->llvm_rc[0])
                argv[k++] = b->res;
        }
    } else {
        argv[k++] = "-o";
        argv[k++] = out;
        for (i = 0; i < b->nsrc; i++)
            argv[k++] = b->objp[i];
        if (b->rc_file[0] && b->llvm_rc[0])
            argv[k++] = b->res;
        for (i = 0; i < b->nlink; i++)
            argv[k++] = b->link[i];
    }
    argv[k] = NULL;
    if (run_proc(l, argv, l->root, logp) != 0)
        return 1;
    for (i = 0; i < b->ncopy; i++) {
        const char *s = strrchr(b->copy[i], '/'), *bs = strrchr(b->copy[i], '\\');
        char dst[LP];
        size_t n = 0;
        void *d;
        if (bs > s)
            s = bs;
        join(dst, sizeof dst, l->root, s ? s + 1 : b->copy[i]);
        if (!(d = SDL_LoadFile(b->copy[i], &n)) || !SDL_SaveFile(dst, d, n)) {
            lg(l, "cannot copy %s: %s", b->copy[i], SDL_GetError());
            SDL_free(d);
            return 1;
        }
        SDL_free(d);
    }
    return 0;
}

/* ── the game ──────────────────────────────────────────────────────────── */

#ifdef __ANDROID__
/* no process: the game is a library (libGAME.so in root) the app loads once xr_launcher_main returned 0 */
static int start_game(L *l)
{
    if (!is_file(l->out)) {
        lg(l, "the game %s is missing", l->out);
        return 0;
    }
    lg(l, "ready: %s (the app loads it)", l->out);
    return 1;
}
#else
static int start_game(L *l)
{
    char exe[LP];
    char *argv[] = { exe, NULL };
    SDL_PropertiesID p = SDL_CreateProperties();
    SDL_Process *proc;
    join(exe, sizeof exe, l->root, l->game_exe);
    SDL_SetPointerProperty(p, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv);
    SDL_SetStringProperty(p, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, l->root);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(p, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
    /* no console window next to the game (a console-subsystem exe would get one;
       the game logs to data\ anyway) */
    SDL_SetBooleanProperty(p, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    proc = SDL_CreateProcessWithProperties(p);
    SDL_DestroyProperties(p);
    if (!proc) {
        lg(l, "cannot start %s: %s", exe, SDL_GetError());
        return 0;
    }
    lg(l, "started %s", exe);
    SDL_DestroyProcess(proc);           /* the game keeps running; the launcher ends */
    return 1;
}
#endif

/* ── the system's Vulkan loader (Linux) ────────────────────────────────── */

/* The game links libvulkan.so.1 (the GPU driver belongs to the system, the package carries no loader): without it
 * the dynamic loader stops the game before any message of ours. Checked here, before the first-start build, so the
 * player hears it at once and in words. Windows always has vulkan-1.dll with a GPU driver. */
static int vulkan_loader_present(L *l)
{
#if defined(__linux__) && !defined(__ANDROID__)
    SDL_SharedObject *so = SDL_LoadObject("libvulkan.so.1");
    if (!so) {
        lg(l, "no libvulkan.so.1: %s", SDL_GetError());
        xr_setup_ui_message(l->ui, 1,
                            "Vulkan driver missing - install the Vulkan driver for your GPU "
                            "(Mesa: vulkan-radeon / vulkan-intel / mesa-vulkan-drivers; NVIDIA: its own driver), "
                            "then start again.");
        return 0;
    }
    SDL_UnloadObject(so);
#else
    (void)l;
#endif
    return 1;
}

/* ── where the launcher's files are ────────────────────────────────────── */

/* The tools folder (RES). Bundled = read-only and ready (release.txt inside, written by the release script):
 *   1. $XR_LAUNCHER_TOOLS (tests, a distribution package), when it holds release.txt;
 *   2. <exe folder>/../share/<slug>/tools - the AppImage layout (usr/bin/<launcher>, usr/share/<slug>/tools);
 * else root/tools, unpacked from the payload appended to the exe (Windows, a plain Linux folder). */
static void find_tools(L *l, const char *base)
{
    const char *env = SDL_getenv("XR_LAUNCHER_TOOLS");
    char p[LP], cand[LP];
    l->bundled = 0;
    if (env && *env) {
        join(p, sizeof p, env, "release.txt");
        if (is_file(p)) {
            snprintf(l->tools, sizeof l->tools, "%s", env);
            l->bundled = 1;
            return;
        }
        fprintf(stderr, "[LAUNCHER] XR_LAUNCHER_TOOLS=%s holds no release.txt - ignored\n", env);
    }
    if (base && l->cfg->slug && *l->cfg->slug) {
        snprintf(cand, sizeof cand, "%s../share/%s/tools", base, l->cfg->slug);
        join(p, sizeof p, cand, "release.txt");
        if (is_file(p)) {
            snprintf(l->tools, sizeof l->tools, "%s", cand);
            l->bundled = 1;
            return;
        }
    }
    join(l->tools, sizeof l->tools, l->root, "tools");
}

/* a bundled folder's release.txt names the release (the stamp: a new AppImage over the old one builds again) */
static int read_bundled_release(L *l)
{
    char p[LP], *t;
    size_t k;
    join(p, sizeof p, l->tools, "release.txt");
    if (!(t = read_text(p)))
        return 0;
    k = strcspn(t, "\r\n");
    while (k && (t[k - 1] == ' ' || t[k - 1] == '\t'))
        k--;
    if (k > 32)
        k = 32;
    memcpy(l->release, t, k);
    l->release[k] = 0;
    SDL_free(t);
    return l->release[0] != 0;
}

/* ── main ──────────────────────────────────────────────────────────────── */

int xr_launcher_main(const XrLauncherConfig *cfg, int argc, char **argv)
{
    static L l;
    const char *image = NULL, *base;
    int repair = 0, no_start = 0, i, ok = 0;
    char p[LP], sha1[41] = "", stamp_want[256], *stamp_have, logp[LP], xbe[LP];
    Uint64 payload_ofs = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--repair")) repair = 1;
        else if (!strcmp(argv[i], "--no-start")) no_start = 1;
        else if (!strcmp(argv[i], "--image") && i + 1 < argc) image = argv[++i];
    }
    memset(&l, 0, sizeof l);
    l.cfg = cfg;
    SDL_Init(SDL_INIT_VIDEO);
    xbox_set_window_icon(cfg->icon_bmp, cfg->icon_bmp_size);
    base = SDL_GetBasePath();           /* the exe's folder: inside an AppImage the read-only mount */
    snprintf(l.root, sizeof l.root, "%s", xbox_host_root()[0] ? xbox_host_root() : base ? base : "./");
    snprintf(l.exe, sizeof l.exe, "%s", xbox_host_self());
    snprintf(l.game_exe, sizeof l.game_exe, "%s", cfg->game_exe);
#ifndef _WIN32
    {                                   /* "ORTA.exe" in the config is "ORTA" here */
        size_t k = strlen(l.game_exe);
        if (k > 4 && !SDL_strcasecmp(l.game_exe + k - 4, ".exe"))
            l.game_exe[k - 4] = 0;
    }
#endif
    find_tools(&l, base);
    join(l.out, sizeof l.out, l.root, l.game_exe);
    join(l.game, sizeof l.game, l.root, "game");
    join(l.data, sizeof l.data, l.root, "data");
    join(l.build, sizeof l.build, l.data, "build");
    join(l.gen, sizeof l.gen, l.build, "gen");
    join(l.pipe, sizeof l.pipe, l.build, "pipeline");
    join(l.obj, sizeof l.obj, l.build, "obj");
    make_folders(&l);
    l.log_mx = SDL_CreateMutex();
    join(logp, sizeof logp, l.data, "launcher.log");
    l.log = SDL_IOFromFile(logp, "wb");
    lg(&l, "%s launcher in %s (tools %s, %s)", cfg->window_title, l.root, l.tools,
       l.bundled ? "bundled, read-only" : "from the payload");
    l.ui = xr_setup_ui_open(cfg->window_title);

    if (l.bundled) {
        if (!read_bundled_release(&l)) {
            xr_setup_ui_message(l.ui, 1, PACKAGE_INCOMPLETE);
            goto out;
        }
    } else if (!read_trailer(&l, &payload_ofs)) {
        xr_setup_ui_message(l.ui, 1, PACKAGE_INCOMPLETE);
        goto out;
    }
    lg(&l, "release %s", l.release);
    if (!vulkan_loader_present(&l))
        goto out;
    if (!ensure_game(&l, image, sha1))
        goto out;

    snprintf(stamp_want, sizeof stamp_want, "%s\n%s\n", l.release, sha1);
    join(p, sizeof p, l.build, "stamp.txt");
    stamp_have = read_text(p);
    join(xbe, sizeof xbe, l.root, l.game_exe);
    if (repair) {
        lg(&l, "repair: building again");
        rm_tree(l.build);
        if (!l.bundled) {               /* the payload is unpacked again; a bundled folder is the AppImage's */
            join(p, sizeof p, l.tools, "release.txt");
            SDL_RemovePath(p);
        }
    } else if (stamp_have && !strcmp(stamp_have, stamp_want) && is_file(xbe)) {
        SDL_free(stamp_have);
        ok = no_start || start_game(&l);
        goto out;
    }
    SDL_free(stamp_have);

    /* 3. payload (a bundled tools folder is the payload, already unpacked) */
    if (!l.bundled) {
        char *have;
        join(p, sizeof p, l.tools, "release.txt");
        have = read_text(p);
        if (!have || strcmp(have, l.release)) {
            UnpackArg u;
            memset(&u, 0, sizeof u);
            u.offset = payload_ofs;
            if (run_job(&l, "Unpacking the launcher's files", 0.10, 0.02, unpack_job, &u, prog_frac)) {
                SDL_free(have);
                xr_setup_ui_message(l.ui, 1, "The launcher's files could not be unpacked - see data\\launcher.log.");
                goto out;
            }
        }
        SDL_free(have);
    }
    /* 4. compiler */
    if (!ensure_toolchain(&l))
        goto out;
    /* 5. lift */
    rm_tree(l.build);
    SDL_CreateDirectory(l.gen);
    SDL_CreateDirectory(l.pipe);
    SDL_CreateDirectory(l.obj);
    {
        LiftArg la;
        char gxbe[LP];
        memset(&la, 0, sizeof la);
        join(gxbe, sizeof gxbe, l.game, "default.xbe");
        la.xbe = gxbe;
        if (run_job(&l, "Translating the game's code (once, a few minutes)", 0.30, 0.40, lift_job, &la, prog_frac)) {
            xr_setup_ui_message(l.ui, 1, "Translating the game's code failed - see data\\build\\lift.log.");
            goto out;
        }
    }
    /* 6. compile + link */
    {
        static Build b;
        memset(&b, 0, sizeof b);
        if (!read_build(&l, &b)) {
            xr_setup_ui_message(l.ui, 1, "The launcher's build.txt is missing or damaged - start it with --repair.");
            goto out;
        }
        if (run_job(&l, "Compiling", 0.70, 0.27, compile_job, &b, prog_frac)) {
            xr_setup_ui_message(l.ui, 1, "Compiling failed - see data\\build\\obj\\*.log.");
            SDL_free(b.text);
            goto out;
        }
        if (run_job(&l, "Linking", 0.97, 0.03, link_job, &b, NULL)) {
            xr_setup_ui_message(l.ui, 1, "Linking failed - see data\\build\\link.log.");
            SDL_free(b.text);
            goto out;
        }
        SDL_free(b.text);
    }
    /* the intermediate files are not needed to play (~160 MB): only the logs and the stamp stay */
    rm_tree(l.pipe);
    rm_tree(l.gen);
    rm_tree(l.obj);
    join(p, sizeof p, l.build, "stamp.txt");
    write_text(p, stamp_want);
    if (l.bundled) {
        /* the game's log line "version: <release> (...)" reads tools/release.txt (platform/host_info.c), which
         * a read-only bundle has elsewhere: the release name goes to data/release.txt for it */
        char rel[64];
        join(p, sizeof p, l.data, "release.txt");
        snprintf(rel, sizeof rel, "%s\n", l.release);
        write_text(p, rel);
    }
    lg(&l, "built %s", xbe);
    ok = no_start || start_game(&l);
out:
    xr_setup_ui_close(l.ui);
    if (l.log)
        SDL_CloseIO(l.log);
    SDL_Quit();
    return ok ? 0 : 1;
}
