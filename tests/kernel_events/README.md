# Kernel events

KEVENTs a title builds by writing the header itself, with no
`KeInitializeEvent` call, checked through the thunk dispatcher. Like
`tests/memory_regressions`, it needs no title and no game files: it runs the
real memory layout on the small synthetic XBE from `tools/conformance`.

```
cmake -S tests/kernel_events -B build/kernel-events -A x64
cmake --build build/kernel-events --config Release
ctest --test-dir build/kernel-events -C Release --output-on-failure
```

| Test | What it checks |
|---|---|
| `kernel_events_default` | No switch set. Pins what every title gets today: the guest address goes to Win32 as a `HANDLE`, so after `KeSetEvent` the wait still fails. |
| `kernel_events_title_kevents` | `RECOMP_TITLE_KEVENTS=1`. The table below. |

Each check fails on v0.12.0 (every wait returns `0xC0000001`) and passes with
the switch:

| Check | Why it matters |
|---|---|
| An unset synchronization event times out | A zero timeout must report the state, not an error. |
| `KeSetEvent` sets it; the wait succeeds and clears `SignalState` | A synchronization wait consumes the signal, in guest memory too. |
| A second wait times out again | The same. |
| `SignalState` written to 1 by the title counts | XDK code sets and clears events by writing the header. |
| A notification event stays set across two waits | Notification events are not consumed. |
| `KeResetEvent` clears it | Host event and guest header agree. |

Not covered: a wait that really blocks and is released from another thread
(the bridges use a host event for exactly that, so a zero timeout checks the
same state), and any title.

Both modes also check `KeWaitForMultipleObjects` through the dispatcher
(sp00n f775a5a, Vlad Yanhursky): events made by `KeInitializeEvent` resolve in
either mode; a title-built event first reached at a nonzero WaitAny index
resolves with the switch, and without it the wait fails and keeps the header.
With the switch: WaitAny consumes only the synchronization event it selects;
a header reset before the wait clears a stale host signal; a WaitAll timeout
or a failed wait consumes nothing, a successful WaitAll consumes every
synchronization event; notification events stay set; title-built and
`KeInitializeEvent` events mix. Every call checks the stdcall stack cleanup.

With the multiple wait resolving title-built events without the header sync
(`ke_guest_event` in place of `ke_guest_event_prepare`), five of these checks
fail. Not covered: concurrent header writes, event reinitialization,
abandoned mutexes.
