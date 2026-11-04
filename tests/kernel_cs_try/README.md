# kernel_cs_try

Tests `RtlTryEnterCriticalSection` (306) with `RtlLeaveCriticalSection` (294) and `RtlEnterCriticalSection` (277),
called through the thunk dispatcher on two host threads.

The bridge used to give TryEnter a private host lock, while Leave released the shared one. After a Try + Leave, the
try lock stayed held for ever, and the shared lock was released by a thread that did not own it. Panzer Dragoon
Orta's CRI file layer stopped at about 41 s because of this (2026-11-01). Now all three calls use one host
lock per guest critical section (`kernel_rtl.c`).

Checked:
- TryEnter fails on the other thread while the section is held.
- After a Leave, the other thread's TryEnter succeeds.
- After a Try + Leave, a plain Enter on the other thread does not block.

```
cmake -S tests/kernel_cs_try -B build/kernel_cs_try -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/kernel_cs_try && ctest --test-dir build/kernel_cs_try --output-on-failure
```
