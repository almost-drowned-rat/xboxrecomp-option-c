# kernel_apc

File I/O completion APCs (`NtReadFile` / `NtWriteFile` with an ApcRoutine, i.e. XAPI's ReadFileEx / WriteFileEx).

- Default: the bridge runs the routine before the call returns (inline, as since Halo).
- `RECOMP_APC_DEFERRED=1` (opt-in, 2026-11-01 for Panzer Dragoon Orta's CRI file layer): the routine is
  queued on the issuing guest thread and runs at its next alertable user-mode wait (`KeDelayExecutionThread`,
  `NtWaitForSingleObjectEx`, `NtWaitForMultipleObjectsEx`, `KeWaitForSingleObject`, `KeWaitForMultipleObjects`),
  which then returns `STATUS_USER_APC` (0xC0) without waiting, as on the console.

The program runs twice (`kernel_apc_inline`, `kernel_apc_deferred`) because the switch is read once per process.
Checked: inline delivery; deferred - not run by the call itself, nor by a non-alertable or kernel-mode wait; run by
an alertable user-mode `SleepEx` with 0xC0 and the caller's registers intact; the next wait waits normally; two
queued APCs run in order at one `WaitForSingleObjectEx`.

```
cmake -S tests/kernel_apc -B build/kernel_apc -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/kernel_apc && ctest --test-dir build/kernel_apc --output-on-failure
```
