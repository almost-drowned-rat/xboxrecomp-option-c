/*
 * mmio_decode_a64.h -- the AArch64 counterpart of mmio_decode.h: decode the load/store that faulted on a trapped
 * device page, service it against the device model, step over it (Android / arm64 Linux).
 *
 * Portable on purpose: no ucontext here. The caller copies the faulting thread's registers into MmioA64Regs (x0-x30,
 * sp, pc), reads the instruction word at pc, and passes the fault address; on success it copies the registers back.
 * So the whole decoder runs - and is tested - on an x86-64 host with instruction words as data
 * (tests/mmio_decode_a64).
 *
 * Covered (what clang emits for volatile device accesses and the runtime's atomics), general registers only:
 *   LDR/STR/LDRB/STRB/LDRH/STRH/LDRSB/LDRSH/LDRSW
 *     unsigned scaled immediate, unscaled (LDUR/STUR), pre-index, post-index (writeback),
 *     register offset (LSL / UXTW / SXTW / SXTX, optionally scaled)
 *   LDP/STP (32 / 64 bit), LDPSW: signed offset, pre-index, post-index
 *   LDAR/STLR, LDAPR (acquire / release), LDXR/STXR, LDAXR/STLXR (exclusive: the device model is the only other
 *     party, so the store always succeeds and reports 0)
 *   LSE atomics LDADD / LDCLR / LDEOR / LDSET / SWP (any acquire / release variant): read-modify-write
 * Refused (returns 0, so the fault reports the instruction instead of corrupting the guest): SIMD / FP registers,
 * PRFM, LDR literal, CAS/CASP, LDXP/STXP, and any form whose computed address is not the fault address.
 *
 * Register 31 is SP as a base and XZR as a data register. 32-bit (W) results clear the upper half; sign-extending
 * loads extend to 32 or 64 bits as their opcode says.
 */
#ifndef MMIO_DECODE_A64_H
#define MMIO_DECODE_A64_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef MMIO_RW_FN_DEFINED
#define MMIO_RW_FN_DEFINED
typedef uint64_t (*mmio_read_fn)(void *dev, uint32_t off, int size);
typedef void     (*mmio_write_fn)(void *dev, uint32_t off, uint64_t val, int size);
#endif

typedef struct {
    uint64_t x[31];                     /* x0 .. x30 */
    uint64_t sp;
    uint64_t pc;
} MmioA64Regs;

/* register n as a data source: 31 = XZR */
static inline uint64_t mmio_a64_get(const MmioA64Regs *r, int n) { return n == 31 ? 0 : r->x[n]; }
/* register n as a base: 31 = SP */
static inline uint64_t mmio_a64_base(const MmioA64Regs *r, int n) { return n == 31 ? r->sp : r->x[n]; }
static inline void mmio_a64_set_base(MmioA64Regs *r, int n, uint64_t v) { if (n == 31) r->sp = v; else r->x[n] = v; }

/* a load result into Rt: 31 discards; a 32-bit destination clears the upper half */
static inline void mmio_a64_put(MmioA64Regs *r, int n, uint64_t v, int is64)
{
    if (n != 31)
        r->x[n] = is64 ? v : (v & 0xFFFFFFFFu);
}

static inline uint64_t mmio_a64_mask(int size) { return size >= 8 ? ~0ULL : (1ULL << (size * 8)) - 1; }

static inline int64_t mmio_a64_sext(uint64_t v, int bits) { return (int64_t)(v << (64 - bits)) >> (64 - bits); }

/* a loaded value of `size` bytes as the destination wants it: zero- or sign-extended to 32 / 64 bits */
static inline uint64_t mmio_a64_extend(uint64_t v, int size, int sign, int dst64)
{
    v &= mmio_a64_mask(size);
    if (sign)
        v = (uint64_t)mmio_a64_sext(v, size * 8);
    return dst64 ? v : (v & 0xFFFFFFFFu);
}

/* device offset of a host address inside the trapped page, given the fault's address and offset */
static inline uint32_t mmio_a64_off(uint64_t addr, uint64_t fault, uint32_t fault_off)
{
    return (uint32_t)(fault_off + (addr - fault));
}

/* 1 if the instruction word `insn` (at r->pc, which faulted on `fault` = device offset `off`) was serviced; the
 * registers then hold the result and pc points past it. 0: not understood or not this address - nothing changed. */
static inline int mmio_a64_emulate(MmioA64Regs *r, uint32_t insn, uint64_t fault, uint32_t off, void *dev,
                                   mmio_read_fn rd, mmio_write_fn wr)
{
    int rt = insn & 31, rn = (insn >> 5) & 31;
    int size = 1 << (insn >> 30);
    if (!rd || !wr)
        return 0;

    /* ── LSE atomics: LDADD / LDCLR / LDEOR / LDSET (opc 0-3), SWP (o3) - before the general block, whose mask
     * they share (bit 21 set, bits 11:10 = 00) ── */
    /* LDAPR (RCpc acquire load): 1x 111 0 00 1 0 1 11111 1100 00 Rn Rt */
    if ((insn & 0x3FFFFC00u) == 0x38BFC000u) {
        if (mmio_a64_base(r, rn) != fault)
            return 0;
        mmio_a64_put(r, rt, rd(dev, off, size) & mmio_a64_mask(size), size == 8);
        r->pc += 4;
        return 1;
    }
    if ((insn & 0x3F200C00u) == 0x38200000u) {
        int o3 = (insn >> 15) & 1, aop = (insn >> 12) & 7, rs = (insn >> 16) & 31;
        uint64_t old, src, res, mask = mmio_a64_mask(size);
        if (mmio_a64_base(r, rn) != fault)
            return 0;
        if (o3 && aop != 0)
            return 0;                                           /* unallocated / other forms */
        if (!o3 && aop > 3)
            return 0;                                           /* LDSMAX ... LDUMIN: not emitted for devices */
        old = rd(dev, off, size) & mask;
        src = mmio_a64_get(r, rs) & mask;
        res = o3 ? src : aop == 0 ? old + src : aop == 1 ? old & ~src : aop == 2 ? old ^ src : old | src;
        wr(dev, off, res & mask, size);
        mmio_a64_put(r, rt, old, size == 8);
        r->pc += 4;
        return 1;
    }

    /* ── load/store register: unsigned immediate / unscaled / pre / post / register offset ── */
    if ((insn & 0x3B000000u) == 0x39000000u || (insn & 0x3B000000u) == 0x38000000u) {
        int opc = (insn >> 22) & 3, wb = 0, is_load, sign, dst64;
        uint64_t base = mmio_a64_base(r, rn), ea, newbase = 0;
        if (insn & (1u << 26))
            return 0;                                           /* SIMD / FP register */
        if ((insn & 0x3B000000u) == 0x39000000u) {              /* unsigned scaled imm12 */
            ea = base + (uint64_t)((insn >> 10) & 0xFFF) * (uint64_t)size;
        } else if (!(insn & (1u << 21))) {                      /* imm9: unscaled / post / pre */
            int64_t imm9 = mmio_a64_sext((insn >> 12) & 0x1FF, 9);
            int mode = (insn >> 10) & 3;
            if (mode == 2)
                return 0;                                       /* LDTR/STTR (unprivileged): not emitted */
            ea = mode == 1 ? base : base + (uint64_t)imm9;      /* post: the old base */
            if (mode == 1 || mode == 3) {
                wb = 1;
                newbase = base + (uint64_t)imm9;
            }
        } else if (((insn >> 10) & 3) == 2) {                   /* register offset */
            int option = (insn >> 13) & 7, s = (insn >> 12) & 1;
            uint64_t m = mmio_a64_get(r, (insn >> 16) & 31);
            switch (option) {
            case 2: m &= 0xFFFFFFFFu; break;                    /* UXTW */
            case 3: break;                                      /* LSL / UXTX */
            case 6: m = (uint64_t)(int64_t)(int32_t)(uint32_t)m; break;   /* SXTW */
            case 7: break;                                      /* SXTX */
            default: return 0;
            }
            ea = base + (s ? m << (insn >> 30) : m);
        } else {
            return 0;                                           /* bit 21 with 11:10 = 01/11: unallocated here */
        }
        if (size == 8 && opc >= 2)
            return 0;                                           /* PRFM / unallocated */
        if (size == 4 && opc == 3)
            return 0;                                           /* unallocated */
        if (ea != fault)
            return 0;
        is_load = opc != 0;
        sign = opc >= 2;
        dst64 = size == 8 || opc == 2;                          /* opc 2: sign-extend to 64; opc 3: to 32 */
        if (wb && rn == rt && rn != 31)
            return 0;                                           /* writeback onto the data register: unpredictable */
        if (is_load)
            mmio_a64_put(r, rt, mmio_a64_extend(rd(dev, off, size), size, sign, dst64), dst64);
        else
            wr(dev, off, mmio_a64_get(r, rt) & mmio_a64_mask(size), size);
        if (wb)
            mmio_a64_set_base(r, rn, newbase);
        r->pc += 4;
        return 1;
    }

    /* ── load/store exclusive / ordered: LDXR STXR LDAXR STLXR LDAR STLR ── */
    if ((insn & 0x3F000000u) == 0x08000000u) {
        int o2 = (insn >> 23) & 1, l = (insn >> 22) & 1, o1 = (insn >> 21) & 1, rs = (insn >> 16) & 31;
        if (o1)
            return 0;                                           /* LDXP / STXP / CASP: not handled */
        if (o2 && ((insn >> 10) & 0x1F) != 0x1F)
            return 0;                                           /* CAS family has Rt2 != 11111 */
        if (mmio_a64_base(r, rn) != fault)
            return 0;
        if (l) {
            mmio_a64_put(r, rt, rd(dev, off, size) & mmio_a64_mask(size), size == 8);
        } else {
            wr(dev, off, mmio_a64_get(r, rt) & mmio_a64_mask(size), size);
            if (!o2)
                mmio_a64_put(r, rs, 0, 0);                      /* STXR status: 0 = stored */
        }
        r->pc += 4;
        return 1;
    }

    /* ── load/store pair: STP / LDP / LDPSW, offset / pre / post ── */
    if ((insn & 0x3A000000u) == 0x28000000u) {
        int opc = insn >> 30, l = (insn >> 22) & 1, mode = (insn >> 23) & 3, rt2 = (insn >> 10) & 31;
        int esize, sign, dst64;
        int64_t imm7 = mmio_a64_sext((insn >> 15) & 0x7F, 7);
        uint64_t base = mmio_a64_base(r, rn), ea;
        if (insn & (1u << 26))
            return 0;                                           /* SIMD / FP pair */
        if (mode == 0)
            return 0;                                           /* LDNP / STNP: not emitted for devices */
        if (opc == 3 || (opc == 1 && !l))
            return 0;
        esize = opc == 2 ? 8 : 4;
        sign = opc == 1;                                        /* LDPSW */
        dst64 = opc != 0;
        ea = mode == 1 ? base : base + (uint64_t)(imm7 * esize);
        if (ea != fault)
            return 0;                                           /* the pair must start on the trapped address */
        if (mode != 2 && (rn == rt || rn == rt2) && rn != 31)
            return 0;
        if (l) {
            uint64_t a = rd(dev, off, esize), b = rd(dev, off + (uint32_t)esize, esize);
            if (rt == rt2)
                return 0;
            mmio_a64_put(r, rt, mmio_a64_extend(a, esize, sign, dst64), dst64);
            mmio_a64_put(r, rt2, mmio_a64_extend(b, esize, sign, dst64), dst64);
        } else {
            wr(dev, off, mmio_a64_get(r, rt) & mmio_a64_mask(esize), esize);
            wr(dev, off + (uint32_t)esize, mmio_a64_get(r, rt2) & mmio_a64_mask(esize), esize);
        }
        if (mode != 2)                                          /* post (1) / pre (3): writeback */
            mmio_a64_set_base(r, rn, base + (uint64_t)(imm7 * esize));
        r->pc += 4;
        return 1;
    }
    return 0;
}

/* Does the load/store word `insn` write memory? For the write flag of an access violation when the signal frame
 * has no ESR record (qemu-user 8.2 gives none; a real Linux / Android kernel does). Stores, store pairs, STLR,
 * STXR and the LSE atomics (read AND write) count as writes; loads, LDAR/LDXR and anything not a general
 * load/store as reads. */
static inline int mmio_a64_insn_writes(uint32_t insn)
{
    if ((insn & 0x3FFFFC00u) == 0x38BFC000u)            /* LDAPR: shares the LSE pattern below (as in the decoder) */
        return 0;
    if ((insn & 0x3B000000u) == 0x38000000u) {          /* register / unscaled / pre / post / LSE atomics */
        if ((insn & 0x00200C00u) == 0x00200000u)          /* bit 21 set, bits 11:10 = 00: LSE atomic (or SWP) */
            return 1;
        return ((insn >> 22) & 3) == 0;                   /* opc 00 = store */
    }
    if ((insn & 0x3B000000u) == 0x39000000u)            /* unsigned scaled immediate */
        return ((insn >> 22) & 3) == 0;
    if ((insn & 0x3A000000u) == 0x28000000u)            /* LDP / STP / LDPSW (and the no-allocate pairs) */
        return !((insn >> 22) & 1);                       /* L = 0: store */
    if ((insn & 0x3F000000u) == 0x08000000u)            /* exclusive / ordered: LDXR, STXR, LDAR, STLR */
        return !((insn >> 22) & 1);
    return 0;
}

/* ── the signal frame ─────────────────────────────────────────────────────────
 * On arm64 Linux / Android, ucontext's mcontext ends in __reserved[4096]: a list of records, each a
 * { u32 magic; u32 size; } head followed by its data, ended by a zero magic. The ESR record (magic 0x45535201,
 * "ESR\x01") carries the exception syndrome: EC (bits 31:26) 0x24 / 0x25 = data abort, WnR (bit 6) = write,
 * and with ISV (bit 24) the access size (SAS, bits 23:22), sign extension (SSE, 21), the data register (SRT,
 * 20:16) and whether it is 64-bit (SF, 15). */
#define MMIO_A64_ESR_MAGIC 0x45535201u

/* 1 and *esr filled when the record list holds an ESR record */
static inline int mmio_a64_find_esr(const void *reserved, size_t len, uint64_t *esr)
{
    const uint8_t *p = (const uint8_t *)reserved;
    size_t at = 0;
    while (at + 8 <= len) {
        uint32_t magic, size;
        memcpy(&magic, p + at, 4);
        memcpy(&size, p + at + 4, 4);
        if (magic == 0 || size < 8 || at + size > len)
            return 0;
        if (magic == MMIO_A64_ESR_MAGIC && size >= 16) {
            memcpy(esr, p + at + 8, 8);
            return 1;
        }
        at += size;
    }
    return 0;
}

typedef struct { int data_abort, write, valid, size, sign, rt, sf; } MmioA64Syndrome;

static inline MmioA64Syndrome mmio_a64_syndrome(uint64_t esr)
{
    MmioA64Syndrome s;
    uint32_t ec = (uint32_t)(esr >> 26) & 0x3F;
    memset(&s, 0, sizeof s);
    s.data_abort = ec == 0x24 || ec == 0x25;
    s.write = (int)((esr >> 6) & 1);
    s.valid = (int)((esr >> 24) & 1);
    if (s.valid) {
        s.size = 1 << ((esr >> 22) & 3);
        s.sign = (int)((esr >> 21) & 1);
        s.rt = (int)((esr >> 16) & 31);
        s.sf = (int)((esr >> 15) & 1);
    }
    return s;
}

#endif /* MMIO_DECODE_A64_H */
