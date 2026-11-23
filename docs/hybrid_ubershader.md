# Hybrid ubershader (SHADER COMPILE: HYBRID) - design

Order: maintainer 2026-11-18 (4th async mode), stage 2 = CPU bridge (2026-11-18 ~22:55), default only after the
measurement criteria (a) image equality, (b) GPU cost deck8t / deck4t in numbers, (c) ready before the first frame
(2026-11-19). Reference for the idea only: Dolphin's hybrid ubershaders; wootbeer/ortarecomp (GPL-3.0, design only).
Nothing of either is copied.

## Where it plugs in

`vk_draw_raw` -> `async_pipe` (nv2a_vk.c). Today a miss there returns `VK_NULL_HANDLE` = the draw is skipped
(SKIP) or waited for up to 2.5 ms per frame (WAIT). HYBRID: on a miss the draw is drawn by a pipeline made of

- the draw's own **vertex shader** when a program with the same vertex key (`vkey`) exists already (VS modules
  depend on `vkey` only), else (stage 2) the **pass-through VS** with vertices from the CPU interpreter
  (`src/kernel/nv2a_vsh_interp.c`), and
- the **uber fragment shader** (one SPIR-V, built at device start): the texture shader + register combiners +
  final combiner + alpha test interpreted from the draw's `Nv2aPshKey`, read from the fragment uniform block.

The specialised program keeps building on the worker as now; when it is installed the draw switches to it
(no visible switch if (a) holds).

## Stage 1 - pixel ubershader

1. **Key in the uniform block.** `FsBlock` gets the key words at its end (`uvec4 u_pk[10]`: colour / alpha ICW +
   OCW x 8, control, final0 / 1, shader_program, other_input, shadow) - 160 bytes more per draw in the ring; the
   specialised shaders declare the block without them (a shorter std140 block over the same range is valid).
2. **Uber FS source** (`nv2a_uber_psh()` next to `nv2a_gl_psh()` in gl_psh.c, the same rules written as loops
   and switches: input mapping, AB / CD products, dot, mux by R0.a, scale / bias, destinations, blue-to-alpha,
   final combiner, fog, alpha test, the texture modes 0 / 1 / 4 / 5 / 6 / 7 / 15 / 16 / 2D for the rest).
   One rule set, two emitters: a change in gl_psh.c must be made in both (a test compares them, see below).
3. **Not covered by stage 1** (the draw falls back to the mode's old behaviour = WAIT then skip): CUBE_MAP stages
   (the binding is a samplerCube in the specialised layout), shadow-map stages, point sprites. Counted in the
   `[vk] async` line (`uber fallback N`).
4. **Pipelines.** The uber FS still needs a graphics pipeline per (VS, fixed state). With
   `VK_EXT_graphics_pipeline_library` (NVIDIA, AMD, Mesa incl. Turnip) the fragment part is a library built once
   at start and the vertex-input / pre-raster part per VS; linking is fast (no optimisation) and done on the
   executor. Without the extension: one full pipeline per (VS, fixed state) built synchronously on first use -
   measured; if it hitches like a normal compile, HYBRID falls back to WAIT on such devices and says so.
5. **Prebuilt SPIR-V.** The uber FS is compiled with glslang at device start (or taken from the SPIR-V cache) and
   its library built then - criterion (c): logged `[VK] ubershader ready in N ms` before the first frame.

## Stage 2 - CPU bridge (vertex programs not built yet)

`nv2a_vsh_interp.c` runs the draw's vertex program on the CPU for its vertices; the pass-through VS (oPos, oD0 /
oD1, oT0-3, oFog as attributes) draws them with the uber FS. Only when no VS with this `vkey` exists. CPU cost
counted per frame (`bridge N verts, X ms`); a GPU interpreter only if that cost measurably hurts.

## Proof (before any default change)

- unit test: uber FS vs specialised FS - both compiled, rendered offscreen for a set of recorded keys (Orta's
  pipeline list = 405 programs) and compared per pixel (tolerance 1/255);
- Orta regress 4/4, `RECOMP_VK_ASYNC=hybrid` first start with empty caches: skipped draws -> 0;
- screenshots: same frame with hybrid forced (`RECOMP_VK_UBER_FORCE=1`: every draw through the uber FS) vs normal;
- G3 PANEL full / deck8t / deck4t: GPU ms with the uber FS forced (cost bound) and in HYBRID (real cost).

## Measurement "a" (2026-11-21, done)

`RECOMP_VK_DRAW_COST=1`, Orta level 1: vk_draw_raw 0.6-1.2 ms per frame (170-420 draws); setup ~50 %, vertices ~25 %,
indices + constants + descriptors + draw ~20 %, uniform blocks ~5 %. "Set only the changed state" can only touch the
last two: ~0.1-0.15 ms per frame at best - reported to the maintainer with the advice not to build it; he decided
2026-11-21: not built. The counters stay as a diagnostic.
