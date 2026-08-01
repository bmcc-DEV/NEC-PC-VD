/*
 * aureal_a3d.h - NEC Aureal Engine A3D 2.0 audio DSP.
 *
 * 64 hardware channels, each processing 16-bit PCM at 48 kHz with:
 *   - 3D position (X,Y,Z), azimuth/elevation
 *   - Simplified binaural HRTF (inter-aural time/level difference model)
 *   - Doppler pitch control (16.16 fixed)
 *   - Per-channel biquad filter, reverb and chorus sends
 * Stereo 48 kHz mix output (SDL2 audio queue or WAV file).
 *
 * Register space: 4 KB at physical 0x14000000 (1024 dwords).
 *   Global: 0x000 master volume, 0x004 sample rate, 0x008 status, 0x00C IRQ
 *   Channel ch (0..63): block at 0x40 + ch*16
 */
#ifndef VIPER_AUREAL_A3D_H
#define VIPER_AUREAL_A3D_H

#include <stdint.h>
#include <stdbool.h>

#define AUREAL_RATE          48000
#define AUREAL_CHANNELS      64
#define AUREAL_REGS          4096     /* 16 KB register window */

/* global register offsets (dword indices) */
enum {
    A3D_REG_MASTER_VOL = 0x000 / 4,   /* [31:16] left, [15:0] right (16.16) */
    A3D_REG_SAMPLE_RATE = 0x004 / 4,
    A3D_REG_STATUS      = 0x008 / 4,  /* bit 0: busy */
    A3D_REG_IRQ         = 0x00C / 4,
    A3D_CHAN_BASE       = 0x040 / 4,  /* channel 0 block */
    A3D_CHAN_STRIDE     = 0x040 / 4,  /* 16 dwords per channel */
};

/* channel register offsets (within a channel block) */
enum {
    A3D_CH_CTRL       = 0,   /* bit0 enable, bit1 loop */
    A3D_CH_PITCH      = 1,   /* 16.16 Doppler/pitch ratio */
    A3D_CH_SRC        = 2,   /* physical address of 16-bit PCM */
    A3D_CH_LEN        = 3,   /* length in samples */
    A3D_CH_POS        = 4,   /* 16.16 playback position */
    A3D_CH_VOL_L      = 5,   /* 16.16 */
    A3D_CH_VOL_R      = 6,   /* 16.16 */
    A3D_CH_POS_X      = 7,   /* float 3D position */
    A3D_CH_POS_Y      = 8,
    A3D_CH_POS_Z      = 9,
    A3D_CH_AZIMUTH    = 10,  /* float degrees (-180..180) */
    A3D_CH_ELEVATION  = 11,  /* float degrees */
    A3D_CH_REVERB     = 12,  /* 16.16 send amount */
    A3D_CH_CHORUS     = 13,  /* 16.16 send amount */
    A3D_CH_BIQUAD0    = 14,  /* packed b0,b1,b2,a1 (8-bit each, /128) */
    A3D_CH_BIQUAD1    = 15,  /* low 8 bits = a2 (/128) */
};

typedef struct Bus Bus;

typedef struct AurealA3D AurealA3D;

AurealA3D* aureal_create(void);
void aureal_destroy(AurealA3D* a);
void aureal_set_bus(AurealA3D* a, Bus* bus);
void aureal_reset(AurealA3D* a);

/* MMIO access: offset is a byte address within the 4 KB window */
uint32_t aureal_read(AurealA3D* a, uint32_t offset);
void aureal_write(AurealA3D* a, uint32_t offset, uint32_t data, uint32_t mask);

/* Render `frames` of 48 kHz interleaved stereo (s16) */
void aureal_render(AurealA3D* a, int16_t* out, int frames);

/* Debug accessors */
uint32_t aureal_reg_read(AurealA3D* a, int regnum);
void aureal_reg_write(AurealA3D* a, int regnum, uint32_t data);

#endif /* VIPER_AUREAL_A3D_H */
