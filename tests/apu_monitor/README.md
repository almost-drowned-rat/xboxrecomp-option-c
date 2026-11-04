# apu_monitor

`mcpx_apu_monitor_frame` must hand the VP/DSP mix to the output device.

Found on Burnout 3 US (2026-10-26, untimed diagnosis, 180 s): the game's voices filled mixbins 0, 1, 3, 4 and 5
in 121 of 171 seconds and `monitor.frame_buf` peaked at 28,719, but every buffer handed to XAudio2 was 0. The
monitor cleared `frame_buf` before each block and rendered only the test tone and the software mixer (the
DirectSound HLE voices) into it - 1024 samples per 256-sample block, so that mixer also ran four blocks ahead of
real time while the full XAudio2 queue refused three of every four buffers.

The monitor now puts each 256-sample block (VP/DSP mix + tone + software mixer) into an output ring, clears
`frame_buf`, and the device takes whole buffers from the ring. The test feeds eight patterned blocks and checks
that they come out of the ring once, in order, that `frame_buf` is clear after each block (an idle block is
silence, not a repeat), and that a frame which is not the eighth adds nothing.

No negative-control arm: the old monitor had no ring to take from, so the test cannot run against it; the
measurement above is the before-state, and the check that `frame_buf` is cleared fails if the clear is removed.
