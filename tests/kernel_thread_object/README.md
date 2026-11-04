# kernel_thread_object

What `ObReferenceObjectByHandle` hands back for a thread handle (`src/kernel/kernel_bridge.c`, "Guest thread
objects"). XAPI's `GetExitCodeThread` reads the object's `Header.SignalState` (+4) and `ExitStatus` (+0x120). The
bridge used to return NULL, which crashed Panzer Dragoon Orta's CRT polling an ended movie thread
(2026-11-01).

The test runs the real memory layout on `tools/conformance/test.xbe` (as `kernel_events`) and spawns real worker
threads through `PsCreateSystemThreadEx` in SPAWN mode. Checks:
- **Running thread:** a ThreadObject, SignalState 0, ExitStatus STILL_ACTIVE.
- **Routine returns:** SignalState 1, ExitStatus = its eax.
- **PsTerminateSystemThread(0xF0000001):** ExitStatus = 0xF0000001.
- **Non-thread handle:** keeps the old answer (NULL, STATUS_SUCCESS).

```
cmake -S tests/kernel_thread_object -B build/kernel_thread_object -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/kernel_thread_object && ctest --test-dir build/kernel_thread_object --output-on-failure
```
