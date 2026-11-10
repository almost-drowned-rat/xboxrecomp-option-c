# kernel_phys_owner

The physical-page owner table in `src/kernel/kernel_memory.c`. Bus masters (APU `mcpx_apu_phys`, USB
`bus_resolve`) use it to tell whether a physical address means the contiguous window or identity-mapped pool
memory. Since Orta issue #1 stage 6, pool and heap memory get numbers of their own (VA + `XBOX_HEAP_PHYS_BIAS`,
256 MB; `RECOMP_PHYS_BIAS=0` turns it off), so the table only decides for numbers below that: the contiguous
window, or a number a title computed itself.

Checked:
- `MmGetPhysicalAddress` notes the window pages it answers. A pool VA gets VA + 256 MB, which resolves back to the VA, and notes nothing.
- `xbox_PhysNoteRange` is called by the contiguous allocators. It notes every page of a block, so a page nobody asked about resolves to the window. Panzer Dragoon Orta is the case: DirectSound voice 0x44 sits at physical 0x28A200.
- A contiguous block over the page number of an earlier pool answer owns that page. THPS 2X's pool pad buffer used to keep it, and that same sticky note sent Orta's APU into the heap.
- Edges: blocks past 64 MB, zero size, wrap-around.

```
cmake -S tests/kernel_phys_owner -B build/kernel_phys_owner -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/kernel_phys_owner && ctest --test-dir build/kernel_phys_owner --output-on-failure
```
