/* The NEON forms of the MMX helpers against the portable forms they replace
 * on ARM64 hosts (templates/runtime/recomp_types.h, "NEON forms"; step 6d).
 *
 * The same check as tests/mmx_sse2 for x86: every helper with a NEON form runs
 * on the same random operands both ways, with the saturation / wrap-around
 * edge patterns mixed in and, for the shifts, every count from 0 to 70 plus
 * huge 64-bit counts. The portable forms are the definition (tests/mmx_integer
 * checks them against the real MMX instructions).
 *
 * On an ARM64 host without the NEON forms (RECOMP_MMX_SCALAR) the comparison
 * would be the portable forms against themselves, so the test fails instead of
 * passing vacuously; on other hosts it skips. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define RECOMP_GENERATED_CODE 1
#include "../../templates/runtime/recomp_types.h"

#if !defined(RECOMP_MMX_NEON)
int main(void)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    fprintf(stderr, "FAIL: ARM64 host but the NEON forms are off - nothing to compare\n");
    return 1;
#else
    puts("SKIP: no NEON forms on this host");
    return 0;
#endif
}
#else

static uint64_t s_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 7; s_rng ^= s_rng << 17;
    return s_rng;
}

static const uint64_t s_edges[] = {
    0, ~0ull, 0x7F7F7F7F7F7F7F7Full, 0x8080808080808080ull, 0x7FFF7FFF7FFF7FFFull, 0x8000800080008000ull,
    0x7FFFFFFF7FFFFFFFull, 0x8000000080000000ull, 0x00FF00FF00FF00FFull, 0xFF00FF00FF00FF00ull,
    0x0001000100010001ull, 0x0123456789ABCDEFull,
};

static RecompMmx operand(unsigned i)
{
    RecompMmx r;
    r.q = (i % 4 == 0) ? s_edges[(i / 4) % (sizeof s_edges / sizeof s_edges[0])] : rnd();
    return r;
}

static unsigned s_checks, s_fails;

static void check(const char *name, uint64_t a, uint64_t b, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got != want && s_fails++ < 20)
        fprintf(stderr, "FAIL %s(%016llX, %016llX): neon %016llX portable %016llX\n", name,
                (unsigned long long)a, (unsigned long long)b, (unsigned long long)got, (unsigned long long)want);
}

#define BIN(OP) { RecompMmx a = operand(i), b = operand(i + 1); \
                  check(#OP, a.q, b.q, OP(a, b).q, OP##_C(a, b).q); }
#define SHIFT(OP) { RecompMmx a = operand(i); uint64_t c = counts[i % ncounts]; \
                    check(#OP, a.q, c, OP(a, c).q, OP##_C(a, c).q); }

int main(void)
{
    uint64_t counts[80];
    unsigned ncounts = 0, i;

    for (i = 0; i <= 70; i++)
        counts[ncounts++] = i;
    counts[ncounts++] = 0xFFFFFFFFull;
    counts[ncounts++] = 0x100000000ull;
    counts[ncounts++] = 0x8000000000000010ull;
    counts[ncounts++] = ~0ull;

    for (i = 0; i < 20000; i++) {
        BIN(MMX_PADDB) BIN(MMX_PADDW) BIN(MMX_PADDD) BIN(MMX_PSUBB) BIN(MMX_PSUBW) BIN(MMX_PSUBD)
        BIN(MMX_PADDSB) BIN(MMX_PADDSW) BIN(MMX_PSUBSB) BIN(MMX_PSUBSW)
        BIN(MMX_PADDUSB) BIN(MMX_PSUBUSB) BIN(MMX_PADDUSW) BIN(MMX_PSUBUSW)
        BIN(MMX_PMULLW) BIN(MMX_PMULHW) BIN(MMX_PAVGB) BIN(MMX_PAVGW) BIN(MMX_PMINSW) BIN(MMX_PMAXSW)
        BIN(MMX_PCMPEQB) BIN(MMX_PCMPEQW) BIN(MMX_PCMPEQD) BIN(MMX_PCMPGTB) BIN(MMX_PCMPGTW) BIN(MMX_PCMPGTD)
        BIN(MMX_PMADDWD)
        BIN(MMX_PUNPCKLBW) BIN(MMX_PUNPCKHBW) BIN(MMX_PUNPCKLWD) BIN(MMX_PUNPCKHWD)
        BIN(MMX_PUNPCKLDQ) BIN(MMX_PUNPCKHDQ)
        BIN(MMX_PACKSSWB) BIN(MMX_PACKUSWB) BIN(MMX_PACKSSDW) BIN(MMX_PSADBW)
        SHIFT(MMX_PSLLW) SHIFT(MMX_PSRLW) SHIFT(MMX_PSLLD) SHIFT(MMX_PSRLD)
        SHIFT(MMX_PSLLQ) SHIFT(MMX_PSRLQ) SHIFT(MMX_PSRAW) SHIFT(MMX_PSRAD)
        {
            RecompMmx a = operand(i);
            check("MMX_PMOVMSKB", a.q, 0, MMX_PMOVMSKB(a), MMX_PMOVMSKB_C(a));
        }
    }
    if (s_fails) {
        fprintf(stderr, "FAIL: %u of %u checks differ\n", s_fails, s_checks);
        return 1;
    }
    printf("PASS: 46 MMX helpers, %u checks, NEON forms equal the portable forms\n", s_checks);
    return 0;
}
#endif
