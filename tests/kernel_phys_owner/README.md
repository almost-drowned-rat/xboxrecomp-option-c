# kernel_phys_owner

The physical-page owner table in `src/kernel/kernel_memory.c`. Bus masters (APU `mcpx_apu_phys`, USB
`bus_resolve`) use it to tell whether a physical address means the contiguous window or identity-mapped pool
memory, because both share physical numbers here.

Checked:
- `MmGetPhysicalAddress` notes the page it answers, and the last answer wins.
- `xbox_PhysNoteRange` is called by the contiguous allocators. It notes every page of a block, so a page nobody asked about resolves to the window. Panzer Dragoon Orta is the case: DirectSound voice 0x44 sits at physical 0x28A200.
- A page that was already answered keeps its answer. THPS 2X is the case: its pool pad buffer shares a page number with later contiguous blocks.
- Edges: blocks past 64 MB, zero size, wrap-around.

```
cmake -S tests/kernel_phys_owner -B build/kernel_phys_owner -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build/kernel_phys_owner && ctest --test-dir build/kernel_phys_owner --output-on-failure
```
