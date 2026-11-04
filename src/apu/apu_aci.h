/*
 * apu_aci.h -- the MCPX audio codec interface (AC'97 bus master): channel model and its interrupt (vector 6).
 *
 * DirectSound starts the analog (NABM +0x110) and digital (+0x170) output channels at init and enables their
 * buffer-completion interrupts; its ISR (Burnout 3 US 0x002FEAC4) services a channel per completed buffer and
 * keeps a play position from the buffer lengths. Without a model nothing ever completed. See apu_aci.c.
 */
#ifndef APU_ACI_H
#define APU_ACI_H

#include <stdint.h>

/* Hooks into the AC'97 write trap (xbox_memory_layout.c); does nothing when the trap is not armed
 * (RECOMP_AC97_READY unset) or RECOMP_AC97_MODEL=0. */
void mcpx_aci_attach(void);
/* Plays the running channels for this many output frames (48 kHz) and raises/lowers the interrupt line. */
void mcpx_aci_tick(unsigned frames);
/* "[AC97] ..." counters since the last call, for the APU's 60-s report; 0 when the model is not attached. */
int mcpx_aci_report(char *buf, int size);

/* The channel logic on a plain register block (NABM +0x100..+0x17F as bytes, index 0 = +0x100), for tests:
 * after_write applies the semantics of a write at block offset off, given the block before it; play advances by
 * frames. Both return the interrupt line level (GLOB_STA 0x40 | 0x10 pending). mem reads guest memory. */
typedef const int16_t *(*mcpx_aci_mem_fn)(uint32_t phys, uint32_t bytes);
int mcpx_aci_block_after_write(volatile uint8_t *blk, const uint8_t *before, unsigned off);
int mcpx_aci_block_play(volatile uint8_t *blk, unsigned frames, mcpx_aci_mem_fn mem);

#endif /* APU_ACI_H */
