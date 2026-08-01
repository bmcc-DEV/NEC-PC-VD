/*
 * voodoo2_ec.h - 3dfx Voodoo2 EC (100 MHz, 16 MB unified SGRAM, dual TMU).
 *
 * The EC (embedded) variant uses a single unified 16 MB SGRAM pool for both
 * the framebuffer and textures (texBaseAddr addresses into the same pool).
 *
 * MMIO aperture (16 MB, mapped at physical 0x10000000 in the MIPS bus):
 *   0x000000-0x1FFFFF  Register window (register dword = (off>>2)&0xFF,
 *                       chip select = (off>>12)&3) + CMDFIFO write window
 *                       at bit 21 (0x200000)
 *   0x400000-0x7FFFFF  Linear framebuffer
 *   0x800000-0xFFFFFF  Texture memory port
 */
#ifndef VIPER_VOODOO2_EC_H
#define VIPER_VOODOO2_EC_H

#include <stdint.h>
#include <stdbool.h>

#define VOODOO2_EC_CLOCK     100000000ull   /* 100 MHz */
#define VOODOO2_EC_SGRAM_SIZE 0x01000000ul  /* 16 MB unified SGRAM */
#define VOODOO2_EC_CLIP_BIT   (1u << 12)    /* fbiInit1 bit: display blanked */

/* Register indices (dword offsets = byte offset / 4), Voodoo 2 layout */
enum v2_reg {
    V2_REG_STATUS        = 0x000/4, V2_REG_INTRCTRL    = 0x004/4,
    V2_REG_VERTEXAX      = 0x008/4, V2_REG_VERTEXAY    = 0x00C/4,
    V2_REG_VERTEXBX      = 0x010/4, V2_REG_VERTEXBY    = 0x014/4,
    V2_REG_VERTEXCX      = 0x018/4, V2_REG_VERTEXCY    = 0x01C/4,
    V2_REG_STARTR        = 0x020/4, V2_REG_STARTG      = 0x024/4,
    V2_REG_STARTB        = 0x028/4, V2_REG_STARTZ      = 0x02C/4,
    V2_REG_STARTA        = 0x030/4, V2_REG_STARTS      = 0x034/4,
    V2_REG_STARTT        = 0x038/4, V2_REG_STARTW      = 0x03C/4,
    V2_REG_DRDX          = 0x040/4, V2_REG_DGDX        = 0x044/4,
    V2_REG_DBDX          = 0x048/4, V2_REG_DZDX        = 0x04C/4,
    V2_REG_DADX          = 0x050/4, V2_REG_DSDX        = 0x054/4,
    V2_REG_DTDX          = 0x058/4, V2_REG_DWDX        = 0x05C/4,
    V2_REG_DRDY          = 0x060/4, V2_REG_DGDY        = 0x064/4,
    V2_REG_DBDY          = 0x068/4, V2_REG_DZDY        = 0x06C/4,
    V2_REG_DADY          = 0x070/4, V2_REG_DSDY        = 0x074/4,
    V2_REG_DTDY          = 0x078/4, V2_REG_DWDY        = 0x07C/4,
    V2_REG_TRIANGLECMD   = 0x080/4,
    V2_REG_FVERTEXAX     = 0x088/4, V2_REG_FVERTEXAY    = 0x08C/4,
    V2_REG_FVERTEXBX     = 0x090/4, V2_REG_FVERTEXBY    = 0x094/4,
    V2_REG_FVERTEXCX     = 0x098/4, V2_REG_FVERTEXCY    = 0x09C/4,
    V2_REG_FSTARTR       = 0x0A0/4, V2_REG_FSTARTG      = 0x0A4/4,
    V2_REG_FSTARTB       = 0x0A8/4, V2_REG_FSTARTZ      = 0x0AC/4,
    V2_REG_FSTARTA       = 0x0B0/4, V2_REG_FSTARTS      = 0x0B4/4,
    V2_REG_FSTARTT       = 0x0B8/4, V2_REG_FSTARTW      = 0x0BC/4,
    V2_REG_FDRDX         = 0x0C0/4, V2_REG_FDGDX        = 0x0C4/4,
    V2_REG_FDBDX         = 0x0C8/4, V2_REG_FDZDX        = 0x0CC/4,
    V2_REG_FDADX         = 0x0D0/4, V2_REG_FDSDX        = 0x0D4/4,
    V2_REG_FDTDX         = 0x0D8/4, V2_REG_FDWDX        = 0x0DC/4,
    V2_REG_FDRDY         = 0x0E0/4, V2_REG_FDGDY        = 0x0E4/4,
    V2_REG_FDBDY         = 0x0E8/4, V2_REG_FDZDY        = 0x0EC/4,
    V2_REG_FDADY         = 0x0F0/4, V2_REG_FDSDY        = 0x0F4/4,
    V2_REG_FDTDY         = 0x0F8/4, V2_REG_FDWDY        = 0x0FC/4,
    V2_REG_FTRIANGLECMD  = 0x100/4,
    V2_REG_FBZCOLORPATH  = 0x104/4, V2_REG_FOGMODE      = 0x108/4,
    V2_REG_ALPHAMODE     = 0x10C/4, V2_REG_FBZMODE      = 0x110/4,
    V2_REG_LFBMODE       = 0x114/4,
    V2_REG_CLIPLEFTRIGHT = 0x118/4, V2_REG_CLIPLOWYHIGHY= 0x11C/4,
    V2_REG_NOPCMD        = 0x120/4, V2_REG_FASTFILLCMD  = 0x124/4,
    V2_REG_SWAPBUFFERCMD = 0x128/4, V2_REG_FOGCOLOR     = 0x12C/4,
    V2_REG_ZACOLOR       = 0x130/4, V2_REG_CHROMAKEY    = 0x134/4,
    V2_REG_CHROMARANGE   = 0x138/4, V2_REG_USERINTRCMD  = 0x13C/4,
    V2_REG_STIPPLE       = 0x140/4, V2_REG_COLOR0       = 0x144/4,
    V2_REG_COLOR1        = 0x148/4,
    V2_REG_FOGTABLE      = 0x160/4,
    V2_REG_CMDFIFOBASEADDR = 0x1E0/4, V2_REG_CMDFIFOBUMP = 0x1E4/4,
    V2_REG_CMDFIFORDPTR    = 0x1E8/4, V2_REG_CMDFIFOAMIN = 0x1EC/4,
    V2_REG_CMDFIFOAMAX    = 0x1F0/4, V2_REG_CMDFIFODEPTH = 0x1F4/4,
    V2_REG_CMDFIFOHOLES   = 0x1F8/4,
    V2_REG_FBIINIT4     = 0x200/4, V2_REG_VRETRACE     = 0x204/4,
    V2_REG_BACKPORCH    = 0x208/4, V2_REG_VIDEODIMENSIONS = 0x20C/4,
    V2_REG_FBIINIT0     = 0x210/4, V2_REG_FBIINIT1     = 0x214/4,
    V2_REG_FBIINIT2     = 0x218/4, V2_REG_FBIINIT3     = 0x21C/4,
    V2_REG_HSYNC        = 0x220/4, V2_REG_VSYNC        = 0x224/4,
    V2_REG_CLUTDATA     = 0x228/4, V2_REG_DACDATA      = 0x22C/4,
    V2_REG_MAXRGBDELTA  = 0x230/4, V2_REG_HBORDER      = 0x234/4,
    V2_REG_VBORDER      = 0x238/4, V2_REG_BORDERCOLOR  = 0x23C/4,
    V2_REG_HVRETRACE    = 0x240/4,
    V2_REG_FBIINIT5     = 0x244/4, V2_REG_FBIINIT6     = 0x248/4,
    V2_REG_FBIINIT7     = 0x24C/4,
    V2_REG_FBISWAPHISTORY = 0x258/4, V2_REG_FBITRIANGLESOUT = 0x25C/4,
    V2_REG_SSETUPMODE   = 0x260/4, V2_REG_SVX          = 0x264/4,
    V2_REG_SVY          = 0x268/4, V2_REG_SARGB        = 0x26C/4,
    V2_REG_SRED         = 0x270/4, V2_REG_SGREEN       = 0x274/4,
    V2_REG_SBLUE        = 0x278/4, V2_REG_SALPHA       = 0x27C/4,
    V2_REG_SVZ          = 0x280/4, V2_REG_SWB          = 0x284/4,
    V2_REG_SWTMU0       = 0x288/4, V2_REG_SS_W0        = 0x28C/4,
    V2_REG_ST_W0        = 0x290/4, V2_REG_SWTMU1       = 0x294/4,
    V2_REG_SS_W1        = 0x298/4, V2_REG_ST_W1        = 0x29C/4,
    V2_REG_SDRAWTRI     = 0x2A0/4, V2_REG_SBEGINTRI    = 0x2A4/4,
    V2_REG_BLTSRCBASEADDR = 0x2C0/4, V2_REG_BLTDSTBASEADDR = 0x2C4/4,
    V2_REG_BLTXYSTRIDES   = 0x2C8/4, V2_REG_BLTSRCCHROMARANGE = 0x2CC/4,
    V2_REG_BLTDSTCHROMARANGE = 0x2D0/4,
    V2_REG_BLTCLIPX      = 0x2D4/4, V2_REG_BLTCLIPY     = 0x2D8/4,
    V2_REG_BLTSRCXY      = 0x2E0/4, V2_REG_BLTDSTXY     = 0x2E4/4,
    V2_REG_BLTSIZE       = 0x2E8/4, V2_REG_BLTROP       = 0x2EC/4,
    V2_REG_BLTCOLOR      = 0x2F0/4, V2_REG_BLTCOMMAND   = 0x2F8/4,
    V2_REG_BLTDATA       = 0x2FC/4,
    V2_REG_TEXTUREMODE   = 0x300/4, V2_REG_TLOD         = 0x304/4,
    V2_REG_TDETAIL       = 0x308/4, V2_REG_TEXBASEADDR  = 0x30C/4,
    V2_REG_TEXBASEADDR_1 = 0x310/4, V2_REG_TEXBASEADDR_2 = 0x314/4,
    V2_REG_TEXBASEADDR_3_8 = 0x318/4, V2_REG_TREXINIT0  = 0x31C/4,
    V2_REG_TREXINIT1     = 0x320/4,
    V2_REG_TMU_BASE      = 0x300/4,
    V2_REG_TMU_COUNT     = 0x100/4,   /* 64 dwords per TMU */
};

typedef struct Voodoo2EC Voodoo2EC;

typedef struct {
    float x, y;
    float r, g, b, a;
    float z, wb;
    float w0, s0, t0;
    float w1, s1, t1;
} V2SetupVertex;

Voodoo2EC* voodoo2ec_create(void);
void voodoo2ec_destroy(Voodoo2EC* v);
void voodoo2ec_reset(Voodoo2EC* v);

/* MMIO access: offset is a byte address within the 16 MB aperture */
uint32_t voodoo2ec_read(Voodoo2EC* v, uint32_t offset);
void voodoo2ec_write(Voodoo2EC* v, uint32_t offset, uint32_t data, uint32_t mask);

/* Render the front buffer into a 32-bit ARGB raster. Returns 1 if changed. */
int voodoo2ec_update(Voodoo2EC* v, uint32_t* rgb, int width, int height);

/* Debug / test accessors */
uint32_t voodoo2ec_reg_read(Voodoo2EC* v, int regnum);
void voodoo2ec_reg_write(Voodoo2EC* v, int regnum, uint32_t data);
uint32_t voodoo2ec_fifo_depth(Voodoo2EC* v);
const uint8_t* voodoo2ec_sgram(Voodoo2EC* v);
uint32_t voodoo2ec_sgram_size(Voodoo2EC* v);

/* CMDFIFO callbacks (used by voodoo_fifo.c) */
void voodoo2ec_cmdfifo_reg_write(Voodoo2EC* v, uint32_t regnum, uint32_t data);
void voodoo2ec_cmdfifo_2d_write(Voodoo2EC* v, uint32_t index, uint32_t data);
void voodoo2ec_cmdfifo_set_setup_mode(Voodoo2EC* v, uint32_t value);
void voodoo2ec_cmdfifo_triangle_vertex(Voodoo2EC* v, const V2SetupVertex* sv,
                                       uint32_t command, uint32_t index);
void voodoo2ec_cmdfifo_lfb_write(Voodoo2EC* v, uint32_t offset, uint32_t data);
void voodoo2ec_cmdfifo_texture_write(Voodoo2EC* v, uint32_t offset, uint32_t data);

#endif /* VIPER_VOODOO2_EC_H */
