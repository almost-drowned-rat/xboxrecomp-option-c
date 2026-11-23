/*
 * DVD SCSI pass-through (upstream sp00nz v0.17.0 #294, MattSG 71f22da; option-c form 2026-11-19, U2):
 * NtDeviceIoControlFile (ordinal 196) on the synthetic DVD handle with a 44-byte SCSI_PASS_THROUGH_DIRECT whose CDB
 * is READ DVD STRUCTURE (0xAD) and whose DataBuffer starts out 0xCC: the bridge answers GOOD and zeroes the buffer
 * instead of leaving the title's heap bytes. Runs the real memory layout on the synthetic XBE (tools/conformance),
 * the guest register set through ctx like every option-c test.
 */
#include "kernel.h"
#include "xbox_memory_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static X86Ctx s_test_ctx = X86CTX_INIT;
#define ctx (&s_test_ctx)

recomp_func_t recomp_lookup(uint32_t xbox_va);
recomp_func_t recomp_lookup(uint32_t xbox_va) { (void)xbox_va; return NULL; }
recomp_func_t recomp_lookup_manual(uint32_t xbox_va);
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) { (void)xbox_va; return NULL; }
extern recomp_func_t recomp_lookup_kernel(uint32_t xbox_va);

static uint8_t *G8(uint32_t va) { return (uint8_t *)((uintptr_t)va + xbox_GetMemoryOffset()); }
static uint32_t *G32(uint32_t va) { return (uint32_t *)G8(va); }

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
    size_t xbe_len = 0;
    unsigned char *xbe;
    uint32_t scratch, thunk, stack, in, data, ios, slot, i;
    uint32_t *sp;
    int ok = 1;

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
    scratch = xbox_HeapAlloc(0x10000, 4096);
    thunk = scratch, stack = scratch + 0x1000, in = scratch + 0x2000, data = scratch + 0x3000, ios = scratch + 0x4000;
    *G32(thunk) = 0x80000000u | 196;                      /* NtDeviceIoControlFile */
    xbox_kernel_set_thunk_address(thunk, 1);
    xbox_kernel_bridge_init();
    slot = *G32(thunk);
    if (!recomp_lookup_kernel(slot)) {
        puts("ordinal 196 did not resolve to a bridge entry point");
        return 2;
    }

    memset(G8(in), 0, 44);
    G8(in)[2] = 0x02;                                     /* ScsiStatus: CHECK CONDITION */
    *G32(in + 12) = 64;                                   /* DataTransferLength */
    *G32(in + 20) = data;                                 /* DataBuffer */
    G8(in)[28] = 0xAD;                                    /* READ DVD STRUCTURE */
    memset(G8(data), 0xCC, 64);

    sp = G32(stack);
    memset(sp, 0, 64);
    sp[0] = 0xBEEF0001u;
    sp[1] = 0xDECD0001u;                                  /* the synthetic DVD handle */
    sp[5] = ios;                                          /* IoStatusBlock */
    sp[6] = 0x4D014u;                                     /* IOCTL_SCSI_PASS_THROUGH_DIRECT */
    sp[7] = in;                                           /* input */
    sp[8] = 44;
    g_esp = stack;
    g_eax = 0xDEADBEEFu;
    RECOMP_CALL_GUEST(recomp_lookup_kernel(slot));

    if (g_eax != 0 || g_esp != stack + 44) { printf("FAIL status=%08X esp=%08X (want 0, %08X)\n", g_eax, g_esp, stack + 44); ok = 0; }
    if (G8(in)[2] != 0) { puts("FAIL ScsiStatus not GOOD"); ok = 0; }
    for (i = 0; i < 64; i++)
        if (G8(data)[i]) { puts("FAIL DataBuffer not zeroed"); ok = 0; break; }
    if (!ok) return 1;
    puts("PASS: DVD SCSI pass-through returns GOOD with a zeroed DataBuffer");
    return 0;
}
