/* apu_module: the APU module (docs/apu_module.md) loads next to the exe, its API is forwarded, and a missing
 * module leaves the API returning nothing instead of crashing.
 *
 *   apu_module_test         the module CMake copied next to this exe: loaded, the GP wiring round-trips
 *   apu_module_test none    RECOMP_APU_MODULE points at no file: nothing loaded, calls return 0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu.h"
#include "apu_gp.h"

extern const char *xr_apu_module_path(void);

/* the title's dispatch, which the runtime links against (no title here) */
void *recomp_lookup(unsigned long address) { (void)address; abort(); }
void *recomp_lookup_manual(unsigned long address) { (void)address; abort(); }

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(int argc, char **argv)
{
    int none = argc > 1 && !strcmp(argv[1], "none");
    apu_gp_reset();                                   /* the first call loads the module */
    if (none) {
        CHECK(xr_apu_module_path()[0] == 0);
        CHECK(apu_gp_wired() == 0);
        apu_gp_direct(0, 0, 1.0f);                    /* no module: a no-op, no crash */
        CHECK(apu_gp_wired() == 0);
        CHECK(apu_gp_bin_gain(0) == 0.0f);
    } else {
        CHECK(xr_apu_module_path()[0] != 0);
        printf("module: %s\n", xr_apu_module_path());
        CHECK(apu_gp_wired() == 0);
        apu_gp_direct(3, 1, 0.5f);                    /* bin 3 -> speaker 1 at half level */
        CHECK(apu_gp_wired() == 1);
        CHECK(apu_gp_bin_speaker(3) == 1);
        CHECK(apu_gp_bin_gain(3) == 0.5f);
        apu_gp_lfe_lowpass(120);
        CHECK(apu_gp_lfe_lowpass_hz() == 120);
        apu_gp_reset();
        CHECK(apu_gp_wired() == 0);
    }
    printf("apu_module (%s): %s\n", none ? "none" : "loaded", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
