# apu_aci

The AC'97 output channel model (`src/apu/apu_aci.c`) on a plain register block, driven the way Burnout 3's
DirectSound drives it (2026-10-26 diagnosis: reset both channels, BDBAR, CIV = LVI = 0, CR = 0x1D).

Checks: a reset halts the channel (SR = DCH); buffers complete at 48 kHz in 256-frame steps and set BCIS, which
raises the line with GLOB_STA 0x40 (analog; 0x10 is the digital channel, DirectSound's table at 0x00300454); the
ISR's write-back of SR and GLOB_STA & ~1 clears the status and lowers the line; codec ready (0x100) survives; the
channel halts at LVI (DCH | CELV) and resumes with the next buffer on a new LVI; CIV and PICB are read-only.

Before the model nothing completed at all: the registers kept DirectSound's init values for the whole run and
vector 6 was never raised (results\ac97_diag). That is the before-state; this test has no off arm.
