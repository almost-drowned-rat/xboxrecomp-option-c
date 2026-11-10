/*
 * Memory regressions: the guest heap and address space, checked against the
 * bugs they had.
 *
 * Runs the real memory layout on the small synthetic XBE from
 * tools/conformance, so no title and no game files are needed. The bridge is
 * called through the thunk dispatcher, the path a title's kernel calls take.
 *
 * One process per mode, because the switches are read once:
 *
 *   default       No switch set. Pins what every title gets today, so a change
 *                 that alters the default shows up here. Since 6e489a6 the
 *                 default reclaims, so it runs the heap-reclaim checks.
 *   no-reclaim    RECOMP_HEAP_RECLAIM=0: the old never-reuse heap, pinned.
 *   heap-reclaim  RECOMP_HEAP_RECLAIM=1. Each check fails without the fix:
 *
 *   1. A small request after a large free takes only what it needs. Reuse used
 *      to hand over the whole block, so a 16-byte request could take 2 MB.
 *   2. Freeing three neighbours in an awkward order merges all three. Merging
 *      left an empty slot behind that later frees did not step over, so the
 *      third block never joined the other two.
 *   3. NtFreeVirtualMemory(MEM_RELEASE) on memory NtAllocateVirtualMemory took
 *      from the heap gives it back. It handed the 32-bit guest slot to the host
 *      VirtualFree as a pointer, which failed, so none of it ever returned.
 *   5. MEM_DECOMMIT then MEM_COMMIT on heap memory gives zeroed pages, as on
 *      the console. Both were no-ops, so the old contents came back.
 *   6. MmFreeContiguousMemory gives a contiguous block back and the next
 *      request of that size reuses it; the default keeps the bump allocator.
 *   7. (every mode but ext-vma) The APU reads a physical address from the
 *      storage MmGetPhysicalAddress handed it out for: contiguous pages inside
 *      the image range through the window, RAM pages below the contiguous
 *      high-water mark as RAM (Burnout 3's noise and silence).
 *   8. (every mode but ext-vma) A new thread's TIB starts at PASSIVE_LEVEL
 *      (fs:[0x24] = 0) although it is a copy of the main TIB made while the
 *      main thread is at DISPATCH_LEVEL (Orta issue #1); its NtTib.Self is
 *      itself and the KPCR area up to 0x100 (fs:[0x58]) is zero, not TLS data.
 *
 *   ext-vma       RECOMP_EXT_VMA=1. A title that reserves a specific address
 *                 above the RAM mirrors gets it, can commit inside it and use
 *                 the memory, is told the truth when it asks, and is refused
 *                 cleanly for an address that would alias live memory.
 *   ext-vma-128   The same switch on a 128 MB map, where the mirrors already
 *                 reach past user space. The tracker must stay out of the way
 *                 rather than reserve a range of negative size.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include "guest_vmem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Option C: this host test owns one guest register set; g_eax, g_esp and
 * RECOMP_CALL_GUEST reach it through `ctx` like everywhere else. */
static X86Ctx s_test_ctx = X86CTX_INIT;
#define ctx (&s_test_ctx)


/* Provided by the generated title; this harness has none. */
/* recomp_func_t: void(X86Ctx *ctx, uint8_t *base) - xbox_memory_layout.h / recomp_types.h */
recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }

extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);

enum { S_ALLOC, S_FREE, S_QUERY, N_SLOTS };
static const uint32_t ORD[N_SLOTS] = {
    184,   /* NtAllocateVirtualMemory */
    199,   /* NtFreeVirtualMemory */
    217,   /* NtQueryVirtualMemory */
};

static uint32_t scratch;                 /* guest VA of a 64 KB block */
#define THUNK_VA (scratch)
#define STACK_VA (scratch + 0x1000)
#define OUT_VA   (scratch + 0x2000)

static uint32_t slot_va[N_SLOTS];
static int failures;

static uint32_t *G(uint32_t va)
{
    return (uint32_t *)((uintptr_t)va + xbox_GetMemoryOffset());
}

static void check(int ok, const char *what, const char *detail)
{
    printf("  %-66s %s", what, ok ? "PASS" : "FAIL");
    if (!ok && detail) printf(" -- %s", detail);
    putchar('\n');
    if (!ok) failures++;
}

static uint32_t call(int slot, int nargs, const uint32_t *args)
{
    uint32_t *sp = G(STACK_VA);
    int i;

    sp[0] = 0xBEEF0001u;
    for (i = 0; i < nargs; i++)
        sp[1 + i] = args[i];
    g_esp = STACK_VA;
    g_eax = 0xDEADBEEFu;
    RECOMP_CALL_GUEST(recomp_lookup_kernel(slot_va[slot]));
    return g_eax;
}

/* NtAllocateVirtualMemory(&base, 0, &size, type, RW). On return *base and *size
 * hold what the kernel wrote back. */
static uint32_t nt_alloc_at(uint32_t *base, uint32_t *size, uint32_t type)
{
    uint32_t args[5], st;

    *G(OUT_VA) = *base;
    *G(OUT_VA + 4) = *size;
    args[0] = OUT_VA; args[1] = 0; args[2] = OUT_VA + 4;
    args[3] = type; args[4] = 0x04;
    st = call(S_ALLOC, 5, args);
    *base = *G(OUT_VA);
    *size = *G(OUT_VA + 4);
    return st;
}

/* Reserve and commit `size` bytes anywhere. */
static uint32_t nt_alloc(uint32_t size, uint32_t *out_base)
{
    uint32_t base = 0;
    uint32_t st = nt_alloc_at(&base, &size, 0x3000);

    *out_base = base;
    return st;
}

/* NtFreeVirtualMemory(&base, &size = 0, MEM_RELEASE). */
static uint32_t nt_release(uint32_t base)
{
    uint32_t args[3];

    *G(OUT_VA) = base;
    *G(OUT_VA + 4) = 0;
    args[0] = OUT_VA; args[1] = OUT_VA + 4; args[2] = 0x8000;
    return call(S_FREE, 3, args);
}

/* NtQueryVirtualMemory(base, &info); fills mbi[7] with the guest
 * MEMORY_BASIC_INFORMATION fields. */
static uint32_t nt_query(uint32_t base, uint32_t mbi[7])
{
    uint32_t args[2], st, i;

    args[0] = base; args[1] = OUT_VA + 0x100;
    st = call(S_QUERY, 2, args);
    for (i = 0; i < 7; i++)
        mbi[i] = *G(OUT_VA + 0x100 + 4 * i);
    return st;
}

static unsigned char *load_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void set_env(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "default";
    const char *path = argc > 2 ? argv[2] : "tools/conformance/test.xbe";
    int off = !strcmp(mode, "no-reclaim");
    int reclaim = !strcmp(mode, "heap-reclaim") || !strcmp(mode, "default");
    int ext128 = !strcmp(mode, "ext-vma-128");
    int ext = ext128 || !strcmp(mode, "ext-vma");
    size_t xbe_len = 0;
    unsigned char *xbe;
    char d[160];
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!reclaim && !ext && !off) {
        printf("unknown mode '%s'\n", mode);
        return 2;
    }
    if (!strcmp(mode, "heap-reclaim"))
        set_env("RECOMP_HEAP_RECLAIM", "1");
    if (off)
        set_env("RECOMP_HEAP_RECLAIM", "0");
    if (ext)
        set_env("RECOMP_EXT_VMA", "1");
    if (ext128)
        xbox_SetTotalRam(XBOX_DEVKIT_RAM);

    xbe = load_file(path, &xbe_len);
    if (!xbe) {
        printf("cannot read %s -- run: python tools/conformance/mkxbe.py\n", path);
        return 2;
    }
    if (!xbox_MemoryLayoutInit(xbe, xbe_len)) {
        puts("xbox_MemoryLayoutInit failed");
        return 2;
    }

    scratch = xbox_HeapAlloc(0x10000, 4096);
    for (i = 0; i < N_SLOTS; i++)
        *G(THUNK_VA + 4 * i) = 0x80000000u | ORD[i];
    xbox_kernel_set_thunk_address(THUNK_VA, N_SLOTS);
    xbox_kernel_bridge_init();
    for (i = 0; i < N_SLOTS; i++) {
        slot_va[i] = *G(THUNK_VA + 4 * i);
        if (!recomp_lookup_kernel(slot_va[i])) {
            printf("ordinal %u did not resolve to a bridge entry point\n", ORD[i]);
            return 2;
        }
    }
    printf("mode: %s\n", mode);

    /* 1. A small request after a large free. */
    if (!ext) {
        const uint32_t big = 2u * 1024 * 1024;
        uint32_t a = xbox_HeapAlloc(big, 4096);
        uint32_t guard = xbox_HeapAlloc(4096, 16);
        uint32_t s1, s2;

        (void)guard;
        xbox_HeapFree(a);
        s1 = xbox_HeapAlloc(16, 16);
        s2 = xbox_HeapAlloc(16, 16);

        if (reclaim) {
            snprintf(d, sizeof d, "block is %u bytes, expected 16", xbox_HeapBlockSize(s1));
            check(xbox_HeapBlockSize(s1) == 16, "16-byte request takes 16 bytes of a freed 2 MB", d);
            snprintf(d, sizeof d, "second request at 0x%08X, outside 0x%08X", s2, a);
            check(s2 >= a && s2 < a + big, "the rest of the block is still available", d);
        } else {
            snprintf(d, sizeof d, "block is %u bytes", xbox_HeapBlockSize(s1));
            check(s1 == a && xbox_HeapBlockSize(s1) == big,
                  "no-reclaim: reuse hands over the whole freed block", d);
            check(s2 < a || s2 >= a + big,
                  "no-reclaim: the second request cannot share it", NULL);
        }
    }

    /* 2. Three neighbours freed so that the third meets an empty slot. */
    if (reclaim) {
        uint32_t x = xbox_HeapAlloc(64, 16), y = xbox_HeapAlloc(64, 16);
        uint32_t z = xbox_HeapAlloc(64, 16), w = xbox_HeapAlloc(64, 16);
        uint32_t m;

        (void)w;
        xbox_HeapFree(x);
        xbox_HeapFree(y);        /* merges into x; y's slot is left empty */
        xbox_HeapFree(z);        /* must step over that slot to reach x */
        m = xbox_HeapAlloc(192, 16);
        snprintf(d, sizeof d, "got 0x%08X, expected 0x%08X", m, x);
        check(y == x + 64 && z == y + 64 && m == x,
              "three freed neighbours merge into one 192-byte block", d);
    }

    /* 2b. A freed block at the top of the heap goes back to the bump pointer,
     * so a request bigger than the block (but within block + top room) fits
     * there (Orta's end-of-episode-3 cut scene, 2026-11-08). */
    if (reclaim) {
        uint32_t lo = xbox_HeapAlloc(64, 16);
        uint32_t t1 = xbox_HeapAlloc(0x20000, 16);
        uint32_t t2 = xbox_HeapAlloc(0x20000, 16);
        uint32_t big;

        (void)lo;
        xbox_HeapFree(t2);
        xbox_HeapFree(t1);       /* merges with t2, both back to the bump pointer */
        big = xbox_HeapAlloc(0x60000, 16);
        snprintf(d, sizeof d, "got 0x%08X, expected 0x%08X", big, t1);
        check(t2 == t1 + 0x20000 && big == t1,
              "a top free block and the room above it serve one larger request", d);
        xbox_HeapFree(big);
    }

    /* 2c. Worker stacks are sized as asked (floor 128 KB, 0 = 512 KB), more
     * than 32 can live at once, and a freed sized stack goes back whole
     * (Orta's episode 3 -> 4 loader, hang, 2026-11-08). */
    if (reclaim) {
        uint32_t tops[40], s64, s0, again;
        int i, all = 1;

        s64 = xbox_AllocThreadStackSized(64 * 1024);
        s0  = xbox_AllocThreadStackSized(0);
        snprintf(d, sizeof d, "64 KB ask -> %u bytes, 0 -> %u bytes",
                 xbox_HeapBlockSize(s64 + 16 - 128 * 1024), xbox_HeapBlockSize(s0 + 16 - 512 * 1024));
        check(s64 && s0 && xbox_HeapBlockSize(s64 + 16 - 128 * 1024) == 128 * 1024
                    && xbox_HeapBlockSize(s0 + 16 - 512 * 1024) == 512 * 1024,
              "a worker stack takes the size asked, at least 128 KB; 0 keeps 512 KB", d);
        xbox_FreeThreadStack(s0);
        xbox_FreeThreadStack(s64);
        check(xbox_HeapBlockSize(s64 + 16 - 128 * 1024) == 0, "a sized stack is freed whole", NULL);
        for (i = 0; i < 40; i++)
            all &= (tops[i] = xbox_AllocThreadStackSized(16 * 1024)) != 0;
        check(all, "40 workers get a stack at once (the cap was 32)", NULL);
        for (i = 0; i < 40; i++)
            xbox_FreeThreadStack(tops[i]);
        again = xbox_AllocThreadStackSized(16 * 1024);
        check(again != 0, "the pool is whole again after they end", NULL);
        xbox_FreeThreadStack(again);
    }

    /* 3. Releasing NtAllocateVirtualMemory memory returns it. */
    if (reclaim) {
        const uint32_t size = 2u * 1024 * 1024;
        uint32_t b1, b2, st;

        st = nt_alloc(size, &b1);
        check(st == 0 && b1 != 0, "NtAllocateVirtualMemory takes 2 MB from the heap", NULL);
        st = nt_release(b1);
        snprintf(d, sizeof d, "status 0x%08X", st);
        check(st == 0, "NtFreeVirtualMemory(MEM_RELEASE) succeeds", d);
        check(xbox_HeapBlockSize(b1) == 0, "the block is no longer live", NULL);
        st = nt_alloc(size, &b2);
        snprintf(d, sizeof d, "got 0x%08X, expected 0x%08X", b2, b1);
        check(st == 0 && b2 == b1, "the next 2 MB request reuses it", d);
    }

    /* 5. Decommitted heap memory comes back zeroed. */
    if (reclaim) {
        const uint32_t size = 0x10000;
        uint32_t b1, base, sz, args[3], st;

        nt_alloc(size, &b1);
        memset(G(b1), 0xA5, size);
        *G(OUT_VA) = b1 + 0x1000;            /* decommit the second page only */
        *G(OUT_VA + 4) = 0x1000;
        args[0] = OUT_VA; args[1] = OUT_VA + 4; args[2] = 0x4000;   /* MEM_DECOMMIT */
        st = call(S_FREE, 3, args);
        base = b1 + 0x1000; sz = 0x1000;
        nt_alloc_at(&base, &sz, 0x1000);     /* MEM_COMMIT it again */
        snprintf(d, sizeof d, "status 0x%08X, page 0x%08X, neighbour 0x%08X",
                 st, *G(b1 + 0x1000), *G(b1 + 0x2000));
        check(st == 0 && *G(b1 + 0x1000) == 0 && *G(b1 + 0x1FFC) == 0 &&
              *G(b1) == 0xA5A5A5A5u && *G(b1 + 0x2000) == 0xA5A5A5A5u,
              "a decommitted page comes back zeroed, its neighbours kept", d);
    }

    /* 6. Contiguous memory can be given back. */
    if (!ext) {
        extern int xbox_ContiguousFree(uint32_t addr);
        uint32_t c1 = xbox_ContiguousAlloc(0x20000, 4096);
        uint32_t c2;

        snprintf(d, sizeof d, "free said %d", xbox_ContiguousFree(c1));
        c2 = xbox_ContiguousAlloc(0x20000, 4096);
        if (reclaim) {
            char d2[160];
            snprintf(d2, sizeof d2, "%s, got 0x%08X, expected 0x%08X", d, c2, c1);
            check(c1 && c2 == c1, "a freed contiguous block is reused", d2);
        } else {
            check(c1 && c2 != c1, "no-reclaim: contiguous memory is never reused", d);
        }
    }

    /* 7. The APU resolves a physical address the way MmGetPhysicalAddress
     *    handed it out (Burnout 3: a streamed voice in contiguous pages whose
     *    numbers lie in the image range was read from the image - noise; pool
     *    RAM below the contiguous high-water mark was read from the window -
     *    silence). */
    if (!ext) {
        extern uint8_t *g_apu_ram_ptr;
        extern uint8_t *mcpx_apu_phys(uint64_t addr);
        extern uint32_t g_xbox_image_lo, g_xbox_image_hi;
        uint8_t *saved = g_apu_ram_ptr, *base = (uint8_t *)(uintptr_t)0x10000000u;
        uint32_t lo = g_xbox_image_lo, hi = g_xbox_image_hi;
        uint32_t c = xbox_ContiguousAlloc(0x2000, 4096);
        uint32_t p = (uint32_t)xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)c);   /* owner: contiguous */
        uint32_t pool = p + 0x1000;     /* a pool VA with the same number as a contiguous page */
        uint32_t unknown = p + 0x3000;  /* never asked about */
        /* Pool memory gets a number of its own (VA + XBOX_HEAP_PHYS_BIAS) - it no longer has to win the page. */
        uint32_t pool_phys;

        xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)(c + 0x1000));          /* the block's 2nd page: contiguous */
        pool_phys = (uint32_t)xbox_MmGetPhysicalAddress((PVOID)(uintptr_t)pool);
        g_apu_ram_ptr = base;
        g_xbox_image_lo = p & ~0xFFFFu; /* the image covers all three */
        g_xbox_image_hi = (p & ~0xFFFFu) + 0x100000u;
        snprintf(d, sizeof d, "contiguous 0x%08X -> +0x%08X", p, (uint32_t)(mcpx_apu_phys(p) - base));
        check(mcpx_apu_phys(p) == base + 0x80000000u + p,
              "APU: a contiguous page inside the image range is read through the window", d);
        snprintf(d, sizeof d, "pool 0x%08X (phys 0x%08X) -> +0x%08X", pool, pool_phys,
                 (uint32_t)(mcpx_apu_phys(pool_phys) - base));
        check(mcpx_apu_phys(pool_phys) == base + pool,
              "APU: a pool buffer's MmGetPhysicalAddress number is read as that pool memory", d);
        snprintf(d, sizeof d, "contiguous 0x%08X -> +0x%08X", p + 0x1000, (uint32_t)(mcpx_apu_phys(p + 0x1000) - base));
        check(mcpx_apu_phys(p + 0x1000) == base + 0x80000000u + p + 0x1000,
              "APU: the contiguous page with the pool VA's number stays the window's", d);
        snprintf(d, sizeof d, "unknown 0x%08X -> +0x%08X", unknown, (uint32_t)(mcpx_apu_phys(unknown) - base));
        check(mcpx_apu_phys(unknown) == base + unknown,
              "APU: a page nobody asked about inside the image stays the image", d);
        g_apu_ram_ptr = saved;
        g_xbox_image_lo = lo;
        g_xbox_image_hi = hi;
    }

    /* 8. A new thread's TIB starts at PASSIVE_LEVEL (fs:[0x24] = 0) even when the main thread is raised as it is
     *    made (Orta issue #1: the copy of the main TIB carried its IRQL byte 2 into loader workers - the XDK CRT
     *    bug-checked 0xA as they ended, and DirectSound skipped its lock). The rest of the copy is kept. */
    if (!ext) {
        uint32_t tmpl = xbox_HeapAlloc(64, 16), tib;
        uint8_t *main_irql = (uint8_t *)G(XBOX_TIB_MAIN + 0x24);
        uint8_t saved = *main_irql;
        uint32_t saved_08 = *G(XBOX_TIB_MAIN + 0x08);

        memset(G(tmpl), 0x5A, 64);
        xbox_TestSetTlsTemplate(tmpl, 32);
        *main_irql = 2;                            /* the main thread at DISPATCH_LEVEL */
        *G(XBOX_TIB_MAIN + 0x08) = 0x00ABCDEFu;     /* a field the copy must keep (stack limit) */
        tib = xbox_AllocThreadTib();
        snprintf(d, sizeof d, "tib 0x%08X, fs:[0x24] = %u", tib, tib ? *(uint8_t *)G(tib + 0x24) : 0xFFu);
        check(tib && *(uint8_t *)G(tib + 0x24) == 0,
              "a thread made while main is at DISPATCH starts at PASSIVE", d);
        check(tib && *G(tib + 0x08) == 0x00ABCDEFu && *G(tib) == 0xFFFFFFFFu,
              "the rest of the main TIB is inherited, own SEH chain", NULL);
        snprintf(d, sizeof d, "self 0x%08X, fs:[0x58] 0x%08X", tib ? *G(tib + 0x18) : 0, tib ? *G(tib + 0x58) : 0);
        check(tib && *G(tib + 0x18) == tib, "NtTib.Self (fs:[0x18]) is the new TIB", d);
        check(tib && *G(tib + 0x58) == 0 && *G(tib + 0xFC) == 0,
              "the KPCR area past 0x40 is zero, not TLS data (fs:[0x58] DpcRoutineActive)", d);
        check(tib && *G(*G(tib + 0x04) - 28) == 0x5A5A5A5Au && *G(tib + 0x04) - 32 >= tib + 0x100,
              "fs:[4] still points just past this thread's TLS copy, behind the KPCR area", NULL);
        *main_irql = saved;
        *G(XBOX_TIB_MAIN + 0x08) = saved_08;
        xbox_TestSetTlsTemplate(0, 0);
    }

    /* 4. Above the RAM mirrors: [0x74000000, 0x7FFE0000) at 64 MB. */
    {
        const uint32_t hi = 0x76000000u;
        uint32_t base, size, st, mbi[7];

        if (ext128) {
            check(!guest_vmem_active(),
                  "128 MB: the mirrors reach past user space, tracker stays off", NULL);
        } else if (!ext) {
            base = hi; size = 0x100000;
            st = nt_alloc_at(&base, &size, 0x2000);
            snprintf(d, sizeof d, "status 0x%08X, base 0x%08X", st, base);
            check(base != hi, "without RECOMP_EXT_VMA an explicit high base is not honoured", d);
        } else if (ext) {
            /* Reserve the exact address. */
            base = hi; size = 0x100000;
            st = nt_alloc_at(&base, &size, 0x2000);
            snprintf(d, sizeof d, "status 0x%08X, base 0x%08X", st, base);
            check(st == 0 && base == hi && size == 0x100000,
                  "reserve at 0x76000000 returns that address", d);

            /* Commit the first 64 KB and use it. */
            base = hi; size = 0x10000;
            st = nt_alloc_at(&base, &size, 0x1000);
            check(st == 0 && base == hi, "commit inside the reservation succeeds", NULL);
            *G(hi) = 0xC0FFEE01u;
            *G(hi + 0xFFFC) = 0xC0FFEE02u;
            check(*G(hi) == 0xC0FFEE01u && *G(hi + 0xFFFC) == 0xC0FFEE02u,
                  "the committed memory is real", NULL);

            /* Ask about both halves. */
            st = nt_query(hi, mbi);
            snprintf(d, sizeof d, "state 0x%X size 0x%X base 0x%X", mbi[4], mbi[3], mbi[1]);
            check(st == 0 && mbi[0] == hi && mbi[1] == hi && mbi[3] == 0x10000 &&
                  mbi[4] == 0x1000, "query: the committed part is MEM_COMMIT, 64 KB", d);
            st = nt_query(hi + 0x10000, mbi);
            snprintf(d, sizeof d, "state 0x%X size 0x%X base 0x%X", mbi[4], mbi[3], mbi[1]);
            check(st == 0 && mbi[0] == hi + 0x10000 && mbi[1] == hi &&
                  mbi[3] == 0xF0000 && mbi[4] == 0x2000,
                  "query: the rest is MEM_RESERVE, 960 KB, same allocation base", d);

            /* The same range again is a conflict, not a second grant. */
            base = hi; size = 0x100000;
            st = nt_alloc_at(&base, &size, 0x2000);
            snprintf(d, sizeof d, "status 0x%08X", st);
            check(st == 0xC0000018u, "reserving it again is STATUS_CONFLICTING_ADDRESSES", d);

            /* An address inside the mirrors would alias live memory. */
            base = 0x10000000u; size = 0x100000;
            st = nt_alloc_at(&base, &size, 0x2000);
            snprintf(d, sizeof d, "status 0x%08X", st);
            check(st == 0xC0000018u, "an address inside the mirrors is refused", d);
            st = nt_query(0x10000000u, mbi);
            snprintf(d, sizeof d, "state 0x%X", mbi[4]);
            check(st == 0 && mbi[4] == 0x2000,
                  "query: the mirrors read as reserved, so a scanner moves on", d);

            /* base = 0 still comes from the heap, below the mirrors. */
            st = nt_alloc(0x10000, &base);
            snprintf(d, sizeof d, "status 0x%08X, base 0x%08X", st, base);
            check(st == 0 && base && base < 0x04000000u,
                  "base = 0 still comes from the heap", d);

            /* Release gives the range back. */
            st = nt_release(hi);
            snprintf(d, sizeof d, "status 0x%08X", st);
            check(st == 0, "release of the reservation succeeds", d);
            st = nt_query(hi, mbi);
            check(st == 0 && mbi[4] == 0x10000, "query: it reads as MEM_FREE again", NULL);
            base = hi; size = 0x100000;
            st = nt_alloc_at(&base, &size, 0x3000);
            check(st == 0 && base == hi && *G(hi) == 0,
                  "reserving it again works, and the memory is zero", NULL);
        }
    }

    xbox_MemoryLayoutShutdown();
    free(xbe);
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
