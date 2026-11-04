# apu_doorbell

`RECOMP_APU_DSP_ACK=auto` (src/apu/apu_dsp.c). DirectSound posts a command
word to its DSP and spins until the DSP clears it; with no DSP56300 the APU
stub answers it. The word is not at a fixed address: GPSADDR points at the
scatter-gather table of the GP scratch space, the XDK DirectSound GP program
takes its commands in the first scratch page (SGE entry 0), and the doorbell
is that page + 0x810, wherever the contiguous allocator put it.

## Running

    cmake -S tests/apu_doorbell -B build/apu-doorbell
    cmake --build build/apu-doorbell
    ctest --test-dir build/apu-doorbell

`apu_doorbell_auto`: nothing is acknowledged before GPSADDR and entry 0 are
set, a command at SGE[0] + 0x810 is cleared and its neighbours are not, and
a reprogrammed entry 0 is followed. `apu_doorbell_off` is the negative
control: without `=auto` the same command stays posted.
