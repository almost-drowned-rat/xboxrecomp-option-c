/*
 * Soundtrack callbacks get the guest register context (2026-11-18, for THPS 2X 2.0: a port's game-track
 * source runs the title's own lifted WMA decoder, which needs ctx).
 *
 * A fake game-track source checks that every callback - open (xdk_music_create), read (Process), rewind (Flush,
 * Seek to 0), close (Release) and the open / close of xdk_music_jump - gets the caller's ctx and base, and its own
 * state pointer. Runs the real memory layout on the small synthetic XBE from tools/conformance (guest heap for the
 * music object and the packet), no title and no game files.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "xdk_soundtrack.h"
#include "media_playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X86Ctx s_test_ctx = X86CTX_INIT;

/* Provided by the generated title; this harness has none. */
recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

static int failures;
static uint8_t *s_base;
static int s_state = 0x5A;              /* the source's own state: XdkMusicSource.ctx */
static int s_opens, s_reads, s_rewinds, s_closes, s_bad;
static int s_last_track = -1;

static void check(int ok, const char *what)
{
    printf("  %-60s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static void seen(X86Ctx *ctx, uint8_t *base, void *c)
{
    if (ctx != &s_test_ctx || base != s_base || (c && c != &s_state))
        s_bad++;
}

static long src_read(RECOMP_PARAMS, void *c, int16_t *out, long frames)
{
    seen(ctx, base, c);
    s_reads++;
    memset(out, 0, (size_t)frames * 4u);
    return frames;
}
static int src_rewind(RECOMP_PARAMS, void *c) { seen(ctx, base, c); s_rewinds++; return 0; }
static void src_close(RECOMP_PARAMS, void *c) { seen(ctx, base, c); s_closes++; }

static int open_track(RECOMP_PARAMS, int track, XdkMusicSource *src)
{
    seen(ctx, base, NULL);
    s_opens++;
    s_last_track = track;
    src->ctx = &s_state;
    src->read = src_read;
    src->rewind = src_rewind;
    src->close = src_close;
    return 1;
}

static uint32_t *G(uint32_t va) { return (uint32_t *)((uintptr_t)va + xbox_GetMemoryOffset()); }

static uint32_t s_stack;
/* A method of the music object with nargs stdcall arguments, like the title calls it. */
static void method(void (*fn)(RECOMP_PARAMS), int nargs, const uint32_t *args)
{
    X86Ctx *ctx = &s_test_ctx;
    int i;
    G(s_stack)[0] = 0xBEEF0001u;
    for (i = 0; i < nargs; i++)
        G(s_stack)[1 + i] = args[i];
    g_esp = s_stack;
    fn(ctx, s_base);
}

static unsigned char *load_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (unsigned char *)malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "tools/conformance/test.xbe";
    X86Ctx *ctx = &s_test_ctx;
    uint8_t *base;
    size_t xbe_len = 0;
    unsigned char *xbe;
    uint32_t obj, scratch, pkt, buf, args[4];
    MediaSong first = { -1, 3 }, other = { -1, 5 };
    MediaPlaylist pl;

    setvbuf(stdout, NULL, _IONBF, 0);
    xbe = load_file(path, &xbe_len);
    if (!xbe) {
        printf("cannot read %s -- run: python tools/conformance/mkxbe.py\n", path);
        return 2;
    }
    if (!xbox_MemoryLayoutInit(xbe, xbe_len)) {
        puts("xbox_MemoryLayoutInit failed");
        return 2;
    }
    s_base = base = (uint8_t *)(uintptr_t)xbox_GetMemoryOffset();
    scratch = xbox_HeapAlloc(0x10000, 4096);
    s_stack = scratch + 0x1000;
    pkt = scratch + 0x2000;
    buf = scratch + 0x3000;

    puts("single game track:");
    xdk_music_bind(0x00011000u, open_track);
    obj = xdk_music_create(RECOMP_ARGS, first, NULL, 0);
    check(obj != 0 && s_opens == 1 && s_last_track == 3, "xdk_music_create opens the game track");
    G(pkt)[0] = buf; G(pkt)[1] = 4096; G(pkt)[2] = 0; G(pkt)[3] = 0; G(pkt)[4] = 0;
    args[0] = obj; args[1] = 0; args[2] = pkt;
    method(xdk_music_Process, 3, args);
    check(s_reads >= 1, "Process reads through the source");
    args[0] = obj;
    method(xdk_music_Flush, 1, args);
    args[0] = obj; args[1] = 0; args[2] = 0; args[3] = 0;
    method(xdk_music_Seek, 4, args);
    check(s_rewinds == 2, "Flush and Seek(0, FILE_BEGIN) rewind it");
    args[0] = obj;
    method(xdk_music_Release, 1, args);
    check(s_closes == 1, "Release closes it");

    puts("playlist, jump:");
    memset(&pl, 0, sizeof pl);
    pl.items = (MediaSong *)calloc(2, sizeof *pl.items);
    pl.items[0] = first;
    pl.items[1] = other;
    pl.n = 2;
    obj = xdk_music_create(RECOMP_ARGS, first, &pl, 0);
    check(obj != 0 && s_opens == 2, "xdk_music_create with a playlist opens the first song");
    check(xdk_music_jump(RECOMP_ARGS, other) == 1 && s_opens == 3 && s_closes == 2 && s_last_track == 5,
          "xdk_music_jump closes the song and opens the other");
    args[0] = obj;
    method(xdk_music_Release, 1, args);

    check(s_bad == 0, "every callback got the caller's ctx, base and its own state");
    printf("%s (%d failure%s)\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
