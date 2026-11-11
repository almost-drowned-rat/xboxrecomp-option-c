/*
 * mmio_decode_a64 test (2026-11-11):the AArch64 trapped-MMIO decoder on any host.
 * Instruction words as data (assembler encodings), a simulated register file, a device that records every access.
 * Each case: the right device access (offset, size, value, read/write), the right register result (width, sign),
 * writeback, pc + 4 - and the refusals (SIMD, PRFM, CAS, address mismatch) change nothing.
 */
#include "platform/mmio_decode_a64.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, what) do { if (c) printf("  PASS  %s\n", what); \
                            else { printf("  FAIL  %s (line %d)\n", what, __LINE__); fails++; } } while (0)

/* the device: 256 bytes of registers, little-endian, every access logged */
typedef struct { uint8_t mem[256]; int n; struct { int write; uint32_t off; int size; uint64_t val; } log[8]; } Dev;

static uint64_t dev_rd(void *d, uint32_t off, int size)
{
    Dev *v = (Dev *)d;
    uint64_t x = 0;
    memcpy(&x, v->mem + off, (size_t)size);
    if (v->n < 8) { v->log[v->n].write = 0; v->log[v->n].off = off; v->log[v->n].size = size; v->log[v->n].val = x; v->n++; }
    return x;
}

static void dev_wr(void *d, uint32_t off, uint64_t val, int size)
{
    Dev *v = (Dev *)d;
    memcpy(v->mem + off, &val, (size_t)size);
    if (v->n < 8) { v->log[v->n].write = 1; v->log[v->n].off = off; v->log[v->n].size = size; v->log[v->n].val = val; v->n++; }
}

#define BASE 0x7f0000100000ULL                  /* host address of device offset 0 */
static Dev d;
static MmioA64Regs r;

static void reset(void)
{
    int i;
    memset(&d, 0, sizeof d);
    for (i = 0; i < 256; i++)
        d.mem[i] = (uint8_t)(0x80 + i);         /* high bits set: sign extension shows */
    memset(&r, 0, sizeof r);
    for (i = 0; i < 31; i++)
        r.x[i] = 0xAAAAAAAA00000000ULL + (uint64_t)i;   /* upper half poisoned: W results must clear it */
    r.sp = BASE + 0x40;
    r.pc = 0x1000;
}

static int run(uint32_t insn, uint32_t off)
{
    return mmio_a64_emulate(&r, insn, BASE + off, off, &d, dev_rd, dev_wr);
}

static uint64_t u64at(uint32_t off) { uint64_t x = 0; memcpy(&x, d.mem + off, 8); return x; }

int main(void)
{
    MmioA64Regs before;
    uint64_t esr;

    printf("loads, unsigned immediate\n");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0xB9400020u, 0x10) && r.x[0] == (u64at(0x10) & 0xFFFFFFFFu) && r.pc == 0x1004,
          "ldr w0, [x1]: 32-bit read, upper half cleared, pc+4");
    CHECK(d.n == 1 && !d.log[0].write && d.log[0].off == 0x10 && d.log[0].size == 4, "  one 4-byte read at 0x10");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0xF9400020u, 0x10) && r.x[0] == u64at(0x10), "ldr x0, [x1]: 64-bit read");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0x39400020u, 0x10) && r.x[0] == 0x90, "ldrb w0, [x1]: zero-extended byte");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0x79400020u, 0x10) && r.x[0] == 0x9190, "ldrh w0, [x1]: zero-extended half");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0x39800020u, 0x10) && r.x[0] == 0xFFFFFFFFFFFFFF90ULL, "ldrsb x0, [x1]: sign-extended to 64");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0x39C00020u, 0x10) && r.x[0] == 0xFFFFFF90ULL, "ldrsb w0, [x1]: sign-extended to 32, upper clear");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0x79C00020u, 0x10) && r.x[0] == 0xFFFF9190ULL, "ldrsh w0, [x1]");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0xB9800020u, 0x10) && r.x[0] == 0xFFFFFFFF93929190ULL, "ldrsw x0, [x1]");
    reset(); r.x[3] = BASE + 0x0C;
    CHECK(run(0xB9400462u, 0x10) && r.x[2] == 0x93929190ULL, "ldr w2, [x3, #4]: scaled imm12");
    reset();
    CHECK(run(0xB94003E5u, 0x40) && r.x[5] == 0xC3C2C1C0ULL, "ldr w5, [sp]: SP as the base");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0xB940003Fu, 0x10) && r.x[0] == 0xAAAAAAAA00000000ULL && d.n == 1, "ldr wzr, [x1]: read, result dropped");

    printf("stores\n");
    reset(); r.x[3] = BASE + 0x0C; r.x[2] = 0xDEADBEEF12345678ULL;
    CHECK(run(0xB9000462u, 0x10) && d.n == 1 && d.log[0].write && d.log[0].size == 4 && d.log[0].val == 0x12345678u,
          "str w2, [x3, #4]: 4-byte write of the low half");
    reset(); r.x[1] = BASE + 0x20; r.x[0] = 0x0102030405060708ULL;
    CHECK(run(0xF9000020u, 0x20) && d.log[0].size == 8 && u64at(0x20) == 0x0102030405060708ULL, "str x0, [x1]");
    reset(); r.x[1] = BASE + 0x20; r.x[0] = 0x1FF;
    CHECK(run(0x39000020u, 0x20) && d.log[0].size == 1 && d.log[0].val == 0xFF, "strb w0, [x1]");
    reset(); r.x[1] = BASE + 0x20;
    CHECK(run(0x7900003Fu, 0x20) && d.log[0].size == 2 && d.log[0].val == 0, "strh wzr, [x1]: zero stored");

    printf("unscaled, pre / post index\n");
    reset(); r.x[1] = BASE + 0x14;
    CHECK(run(0xB85FC020u, 0x10) && r.x[0] == 0x93929190ULL && r.x[1] == BASE + 0x14, "ldur w0, [x1, #-4]: no writeback");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0xB8404420u, 0x10) && r.x[0] == 0x93929190ULL && r.x[1] == BASE + 0x14, "ldr w0, [x1], #4: post-index");
    reset(); r.x[1] = BASE + 0x0C;
    CHECK(run(0xB8404C20u, 0x10) && r.x[0] == 0x93929190ULL && r.x[1] == BASE + 0x10, "ldr w0, [x1, #4]!: pre-index");

    printf("register offset\n");
    reset(); r.x[1] = BASE; r.x[2] = 0x10;
    CHECK(run(0xB8626820u, 0x10) && r.x[0] == 0x93929190ULL, "ldr w0, [x1, x2]");
    reset(); r.x[1] = BASE; r.x[2] = 0xFFFFFFFF00000004ULL;
    CHECK(run(0xB8625820u, 0x10) && r.x[0] == 0x93929190ULL, "ldr w0, [x1, w2, uxtw #2]: upper bits of w2 ignored");
    reset(); r.x[1] = BASE + 0x20; r.x[2] = 0x00000000FFFFFFFCULL;   /* w2 = -4 */
    CHECK(run(0xB862D820u, 0x10) && r.x[0] == 0x93929190ULL, "ldr w0, [x1, w2, sxtw #2]: negative index");

    printf("pairs\n");
    reset(); r.x[2] = BASE + 0x10;
    CHECK(run(0x29400440u, 0x10) && r.x[0] == 0x93929190ULL && r.x[1] == 0x97969594ULL && d.n == 2 && d.log[1].off == 0x14,
          "ldp w0, w1, [x2]: two 4-byte reads");
    reset(); r.sp = BASE + 0x10; r.x[0] = 1; r.x[1] = 2;
    CHECK(run(0xA90107E0u, 0x20) && u64at(0x20) == 1 && u64at(0x28) == 2 && r.sp == BASE + 0x10,
          "stp x0, x1, [sp, #16]: two 8-byte writes, no writeback");
    reset(); r.x[2] = BASE + 0x10;
    CHECK(run(0xA8C10440u, 0x10) && r.x[0] == u64at(0x10) && r.x[1] == u64at(0x18) && r.x[2] == BASE + 0x20,
          "ldp x0, x1, [x2], #16: post-index writeback");
    reset(); r.x[2] = BASE + 0x10;
    CHECK(run(0x69400440u, 0x10) && r.x[0] == 0xFFFFFFFF93929190ULL && r.x[1] == 0xFFFFFFFF97969594ULL,
          "ldpsw x0, x1, [x2]: sign-extended pair");

    printf("ordered / exclusive\n");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0x88DFFC20u, 0x10) && r.x[0] == 0x93929190ULL, "ldar w0, [x1]");
    reset(); r.x[1] = BASE + 0x10; r.x[0] = 0x55;
    CHECK(run(0x889FFC20u, 0x10) && d.log[0].write && d.log[0].val == 0x55, "stlr w0, [x1]");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0xB8BFC020u, 0x10) && r.x[0] == 0x93929190ULL, "ldapr w0, [x1]");
    reset(); r.x[1] = BASE + 0x10;
    CHECK(run(0x885F7C20u, 0x10) && r.x[0] == 0x93929190ULL, "ldxr w0, [x1]");
    reset(); r.x[1] = BASE + 0x10; r.x[0] = 0x77; r.x[2] = 99;
    CHECK(run(0x88027C20u, 0x10) && d.log[0].write && d.log[0].val == 0x77 && r.x[2] == 0, "stxr w2, w0, [x1]: stored, status 0");

    printf("LSE atomics\n");
    reset(); r.x[2] = BASE + 0x10; r.x[1] = 1;
    CHECK(run(0xB8210040u, 0x10) && r.x[0] == 0x93929190ULL && d.log[1].val == 0x93929191ULL, "ldadd w1, w0, [x2]: old value, sum written");
    reset(); r.x[2] = BASE + 0x10; r.x[1] = 0x0F;
    CHECK(run(0xB8213040u, 0x10) && d.log[1].val == (0x93929190ULL | 0x0F), "ldset w1, w0, [x2]: OR");
    reset(); r.x[2] = BASE + 0x10; r.x[1] = 0x90;
    CHECK(run(0xB8211040u, 0x10) && d.log[1].val == (0x93929190ULL & ~0x90ULL), "ldclr w1, w0, [x2]: AND NOT");
    reset(); r.x[2] = BASE + 0x10; r.x[1] = 0x1234;
    CHECK(run(0xB8218040u, 0x10) && r.x[0] == 0x93929190ULL && d.log[1].val == 0x1234, "swp w1, w0, [x2]");

    printf("refused: nothing changes\n");
    reset(); r.x[1] = BASE + 0x10; before = r;
    CHECK(!run(0x3DC00020u, 0x10) && !memcmp(&r, &before, sizeof r) && d.n == 0, "ldr q0, [x1] (SIMD)");
    CHECK(!run(0xF9800020u, 0x10) && d.n == 0, "prfm pldl1keep, [x1]");
    CHECK(!run(0x88A07C41u, 0x10) && d.n == 0, "cas w0, w1, [x2] (CAS family)");
    CHECK(!run(0xB9400020u, 0x14) && !memcmp(&r, &before, sizeof r) && d.n == 0, "ldr w0, [x1] with another fault address");
    CHECK(!run(0x18000000u, 0x10) && d.n == 0, "ldr w0, <literal>");
    r.x[1] = BASE + 0x10;
    CHECK(!run(0xB8404421u, 0x10) && d.n == 0, "ldr w1, [x1], #4 (writeback onto the data register)");

    printf("signal frame: ESR record\n");
    {
        uint8_t res[4096];
        uint32_t w;
        uint64_t e = (0x24ULL << 26) | (1ULL << 24) | (2ULL << 22) | (5ULL << 16) | (1ULL << 6);
        MmioA64Syndrome s;
        memset(res, 0, sizeof res);
        w = 0x46508001u; memcpy(res, &w, 4); w = 528; memcpy(res + 4, &w, 4);          /* FPSIMD record first */
        w = MMIO_A64_ESR_MAGIC; memcpy(res + 528, &w, 4); w = 16; memcpy(res + 532, &w, 4);
        memcpy(res + 536, &e, 8);
        CHECK(mmio_a64_find_esr(res, sizeof res, &esr) && esr == e, "ESR found after the FPSIMD record");
        s = mmio_a64_syndrome(esr);
        CHECK(s.data_abort && s.write && s.valid && s.size == 4 && !s.sign && s.rt == 5, "syndrome: data abort, write, 4 bytes, x5");
        memset(res, 0, sizeof res);
        CHECK(!mmio_a64_find_esr(res, sizeof res, &esr), "no ESR record: not found");
        w = 0x1234u; memcpy(res, &w, 4); w = 9000; memcpy(res + 4, &w, 4);
        CHECK(!mmio_a64_find_esr(res, sizeof res, &esr), "a record running past the end: not found, no overrun");
    }

    printf("write flag from the instruction (no ESR record)\n");
    CHECK(!mmio_a64_insn_writes(0xB9400020u) && !mmio_a64_insn_writes(0x39C00020u), "ldr / ldrsb: read");
    CHECK(mmio_a64_insn_writes(0xB9000462u) && mmio_a64_insn_writes(0x7900003Fu), "str / strh: write");
    CHECK(!mmio_a64_insn_writes(0xB85FC020u) && !mmio_a64_insn_writes(0xB8626820u), "ldur / ldr register offset: read");
    CHECK(mmio_a64_insn_writes(0xB81FC020u), "stur w0, [x1, #-4]: write");
    CHECK(!mmio_a64_insn_writes(0x29400440u) && mmio_a64_insn_writes(0xA90107E0u), "ldp: read, stp: write");
    CHECK(!mmio_a64_insn_writes(0x88DFFC20u) && mmio_a64_insn_writes(0x889FFC20u), "ldar: read, stlr: write");
    CHECK(!mmio_a64_insn_writes(0x885F7C20u) && mmio_a64_insn_writes(0x88027C20u), "ldxr: read, stxr: write");
    CHECK(mmio_a64_insn_writes(0xB8210040u) && mmio_a64_insn_writes(0xB8218040u), "ldadd / swp: write");
    CHECK(!mmio_a64_insn_writes(0xB8BFC020u), "ldapr: read");
    CHECK(!mmio_a64_insn_writes(0xD503201Fu), "nop (not a load/store): read");

    if (fails) {
        printf("FAIL: %d check(s)\n", fails);
        return 1;
    }
    puts("PASS: AArch64 MMIO decoder (loads/stores all sizes and signs, offsets, pre/post, pairs, ordered, exclusive, LSE) + ESR");
    return 0;
}
