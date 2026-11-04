/* recomp_fp_round24 (option C, F4/F4b): the integer exponent test must give bit-identical results to the original
 * float-range test, for every class of double - edges of float's range, both sides of FLT_MAX, subnormals, zeros,
 * infinities, NaNs and 20 million random bit patterns spread over all exponents. */
#include "recomp_types.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the version before F4, verbatim */
static double round24_reference(double x) {
    double ax = fabs(x);
    int e;
    if ((ax <= 3.4028234663852886e38 && ax >= 1.1754943508222875e-38) || ax == 0.0
        || x != x || ax == INFINITY)
        return (double)(float)x;
    x = frexp(x, &e);
    return ldexp((double)(float)x, e);
}

static uint64_t bits(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }
static double from(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }

static int g_fail;
static void check(double x)
{
    double a = recomp_fp_round24(x), r = round24_reference(x);
    if (bits(a) != bits(r) && !(a != a && r != r)) {        /* NaN payloads may differ only if both are NaN */
        if (g_fail++ < 10)
            printf("FAIL x=%a (%016llX): new %a, reference %a\n", x, (unsigned long long)bits(x), a, r);
    }
}

int main(void)
{
    static const double edges[] = {
        0.0, -0.0, 1.0, -1.0, 1.0 / 3.0, 0.1, 96.0 * (1.0 / 255.0),
        3.4028234663852886e38, 3.4028235677973366e38, 3.4028236e38, 3.40282357e38, 1.7014118346046923e38,
        3.4028234663852886e38 * 1.0000000001, 3.4028234663852886e38 * 0.9999999999,
        6.805646932770577e38, 1e39, 1e300, 1.7976931348623157e308,
        1.1754943508222875e-38, 1.1754942106924411e-38, 1.1754944909521339e-38, 1e-38, 1.401298464324817e-45,
        7.006492321624085e-46, 1e-45, 1e-46, 1e-300, 4.9406564584124654e-324, 2.2250738585072014e-308,
        INFINITY, -INFINITY, NAN, -NAN,
    };
    size_t i;
    uint64_t s = 0x9E3779B97F4A7C15ull;
    for (i = 0; i < sizeof edges / sizeof edges[0]; i++) {
        check(edges[i]);
        check(-edges[i]);
        check(from(bits(edges[i]) + 1));
        check(from(bits(edges[i]) - 1));
    }
    for (i = 0; i < 20000000; i++) {                       /* xorshift64*: all exponents, all mantissas */
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        check(from(s * 0x2545F4914F6CDD1Dull));
    }
    for (i = 0; i < 2048 * 64; i++)                         /* every exponent, mantissas around 0 and full */
        check(from(((uint64_t)(i >> 6) << 52) | ((i & 32) ? ~0ull >> 12 : 0) ^ (uint64_t)(i & 31)));
    printf(g_fail ? "%d mismatch(es)\n" : "ok: new recomp_fp_round24 == reference on all inputs\n", g_fail);
    return g_fail != 0;
}
