# kernel_irql_gate

The one-CPU gate (`src/kernel/kernel_hal.c`, maintainer 2026-10-26). On the console one CPU runs everything: while
a thread holds DISPATCH_LEVEL no DPC runs, and while an ISR or DPC runs no thread does. Here DPCs and device ISRs
run on host threads beside the guest threads; Burnout 3's USB done-queue DPC ran while a guest thread was raised
(24 % of all DPC runs) and the race intermittently hung in a USB wait and crashed in the USB driver.

Checks, with the gate on: a host deliverer is deferred while a guest thread holds DISPATCH_LEVEL (raised twice,
nested) and granted after its last lower; a vblank (an edge) that comes twice meanwhile stays latched and is
delivered exactly once afterwards (xbox_GateTakeEdge - nothing is dropped); nested enters on the host thread and the deliverer's own raise do not
wait for itself; a guest thread raising while a host holds the gate waits until it leaves; the gate is free
afterwards. The off arm (`RECOMP_DPC_GATE=0`) is the negative control: deferral and waiting do not happen.
