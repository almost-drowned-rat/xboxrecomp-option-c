# Toolkit rules

Standing decisions for the toolkit and every port built on it. They are not re-discussed per game; changing one
needs a maintainer decision.

## Renderer policy

Maintainer decision, 2026-10-17.

**Rule.** There is one renderer path: the NV2A pushbuffer executor, translating to Vulkan. There is no D3D8
API-level HLE renderer. Renderer cost is an executor optimisation problem.

**Rationale** (evidence from Burnout 3's M0 inventory, see also `option_c.md`):

- LTCG titles inline D3D. In Burnout 3, XDK signatures find only 149 of 238 D3D functions, so an API HLE cannot
  hook what is not there as a function.
- Games and engines write pushbuffers directly. In Burnout 3, 8 functions in the game code (RenderWare's Xbox
  driver) and 5 in D3D write NV2A methods themselves; an API HLE does not see them.
- An API HLE would still need a pushbuffer interpreter as a fallback, which means two renderers to maintain.
- The saving is limited to decoding: ~3–5 ms per frame in Burnout 3's intro, ~1.85 ms in a THPS 2X level. Vulkan
  recording and state/shader translation cost the same either way.
- Emulators point the same way: xemu is LLE on the pushbuffer, and Cxbx-Reloaded is moving towards NV2A/pushbuffer
  handling.

**What to do instead**, in priority order:

1. Reduce timer, IRQ and polling load.
2. Event-driven instead of polling.
3. Cache translated pushbuffer segments.
4. Fewer threads and less synchronisation.
5. Shader and texture caches.

All changes portable (Android/ARM64 included), Vulkan only.

**Reopen condition (the only one).** A title misses its performance budget (as in a port plan's performance
target) in gameplay scenes with an optimised executor, backed by measurements. Reopening needs a maintainer
decision.

**Per-title M0 inventory** records, as input for executor tuning and not as an HLE decision:

- LTCG yes/no;
- direct pushbuffer writers outside D3D (count, addresses);
- D3D signature coverage.
