# pad_record

Tests the pad recorder (`src/input/xinput_record.c`, `RECOMP_PAD_RECORD`) and the pad script's new syntax
(`src/input/xinput_script.c`). The script syntax gains a read time base `r<n>:`, a frame time base `f<n>:`, exact
analog values (`a=128`) and exact stick values (`lx=-12000`).

A script in the read base is played through `xbox_InputGetState` while the recorder records it. Checked:
- Steps fire on the right reads, with their values.
- A runtime sample (movie skip, `xbox_input_host_read`) moves neither the read clock nor the recording.
- The recording holds exactly the script's held states, in its syntax.

```
cmake -S tests/pad_record -B build/pad_record -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/pad_record && ctest --test-dir build/pad_record --output-on-failure
```
