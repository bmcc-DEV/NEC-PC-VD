/*
 * voodoo2_ec.c - 3dfx Voodoo2 EC implementation (16 MB unified SGRAM).
 *
 * Port of the C++ Voodoo2 device to the PC-Viper C core. Textures are
 * sampled from the same 16 MB SGRAM pool as the framebuffer (unified
 * memory architecture). Hardware triangle setup engine + CMDFIFO.
 */
#include "voodoo2_ec.h"
#include "voodoo_fifo.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BIT(v, n) (((v) >> (n)) & 1u)
#define BITS(v, s, l) (((v) >> (s)) & ((1u << (l)) - 1))

/* clip / blank */
#define FBIINIT1_BLANK (1u << 12)

static inline int v2_min(int a, int b) { return a < b ? a : b; }
static inline int v2_max(int a, int b) { return a > b ? a : b; }

static inline float as_float(uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static inline uint32_t as_u32(float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    return v;
}

/* ------------------------------------------------------------------ */
/* device state                                                        */
/* ------------------------------------------------------------------ */

struct Voodoo2EC {
    uint8_t* sgram;
    uint32_t sgram_mask;

    uint32_t regs[1024];
    uint32_t tmu_regs[2][V2_REG_TMU_COUNT];

    V2Cmdfifo cmdfifo;

    uint32_t sverts;
    V2SetupVertex svert[3];

    uint32_t rgboffs[3];
    uint32_t auxoffs;
    int frontbuf, backbuf;
    bool video_changed;
    int swaps_pending;
    int rowpixels;
    int height;
    int xoffs, yoffs;

    uint32_t clut[33];
    bool clut_dirty;
    uint32_t pen[65536];

    uint32_t init_enable;
    bool vblank;
};

/* forward declarations */
void voodoo2ec_recompute(Voodoo2EC* v);
void voodoo2ec_fastfill(Voodoo2EC* v);
void voodoo2ec_begin_triangle(Voodoo2EC* v);
void voodoo2ec_draw_triangle_setup(Voodoo2EC* v);
void voodoo2ec_setup_draw(Voodoo2EC* v);
void voodoo2ec_triangle_legacy(Voodoo2EC* v);
void voodoo2ec_rasterize(Voodoo2EC* v, const V2SetupVertex* v0,
                         const V2SetupVertex* v1, const V2SetupVertex* v2,
                         bool use_tex0, bool use_tex1);
void voodoo2ec_blit(Voodoo2EC* v);
uint32_t voodoo2ec_lfb_read(Voodoo2EC* v, uint32_t offset);
void voodoo2ec_lfb_write(Voodoo2EC* v, uint32_t offset, uint32_t data, uint32_t mask);
void voodoo2ec_texture_write(Voodoo2EC* v, uint32_t offset, uint32_t data);
void voodoo2ec_draw_framebuffer(Voodoo2EC* v, uint32_t* out, int w, int h);
static void v2_reg_write_chip(Voodoo2EC* v, int regnum, uint32_t data, int chipmask);

/* ------------------------------------------------------------------ */
/* texel format tables                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t rgb332[256];
    uint32_t alpha8[256];
    uint32_t int8[256];
    uint32_t ai44[256];
    uint32_t rgb565[65536];
    uint32_t argb1555[65536];
    uint32_t argb4444[65536];
} V2TexelFormat;

static uint8_t expand_n(uint32_t v, int n) {
    if (n == 8) return (uint8_t)(v & 0xFF);
    if (n == 6) return (uint8_t)((v << 2) | (v >> 4));
    if (n == 5) return (uint8_t)((v << 3) | (v >> 2));
    if (n == 4) return (uint8_t)((v << 4) | v);
    if (n == 3) return (uint8_t)((v << 5) | (v << 2) | (v >> 1));
    return 0;
}

static const V2TexelFormat* texel_tables(void) {
    static V2TexelFormat fmt;
    static bool built = false;
    if (built) return &fmt;
    for (int i = 0; i < 256; i++) {
        fmt.rgb332[i] = 0xFF000000u
            | ((uint32_t)expand_n((i >> 5) & 7, 3) << 16)
            | ((uint32_t)expand_n((i >> 2) & 7, 3) << 8)
            | expand_n(i & 3, 2);
        fmt.alpha8[i] = ((uint32_t)i << 24) | 0xFFFFFF;
        fmt.int8[i] = 0xFF000000u | ((uint32_t)i << 16) | ((uint32_t)i << 8) | i;
        fmt.ai44[i] = ((uint32_t)expand_n((i >> 4) & 0xF, 4) << 24)
                    | ((uint32_t)expand_n(i & 0xF, 4) << 16)
                    | ((uint32_t)expand_n(i & 0xF, 4) << 8)
                    | expand_n(i & 0xF, 4);
    }
    for (int i = 0; i < 65536; i++) {
        fmt.rgb565[i] = 0xFF000000u
            | ((uint32_t)expand_n((i >> 11) & 0x1F, 5) << 16)
            | ((uint32_t)expand_n((i >> 5) & 0x3F, 6) << 8)
            | expand_n(i & 0x1F, 5);
        fmt.argb1555[i] = ((uint32_t)expand_n((i >> 15) & 1, 1) << 24)
            | ((uint32_t)expand_n((i >> 10) & 0x1F, 5) << 16)
            | ((uint32_t)expand_n((i >> 5) & 0x1F, 5) << 8)
            | expand_n(i & 0x1F, 5);
        fmt.argb4444[i] = ((uint32_t)expand_n((i >> 12) & 0xF, 4) << 24)
            | ((uint32_t)expand_n((i >> 8) & 0xF, 4) << 16)
            | ((uint32_t)expand_n((i >> 4) & 0xF, 4) << 8)
            | expand_n(i & 0xF, 4);
    }
    built = true;
    return &fmt;
}

/* Sample a texel from the unified SGRAM. texbase is a byte offset into SGRAM.
 * The texture is 2^lod texels per side; coordinates wrap within the LOD. */
static uint32_t sample_texel(const Voodoo2EC* v, uint32_t fmt, int lod,
                             uint32_t s, uint32_t t, uint32_t texbase) {
    const uint8_t* sgram = v->sgram;
    if (!sgram) return 0xFF000000u;
    uint32_t mask = v->sgram_mask;

    uint32_t size = 1u << lod;
    uint32_t sm = s & (size - 1);
    uint32_t tm = t & (size - 1);
    uint32_t bpp = (fmt <= 3) ? 1 : ((fmt == 4 || fmt == 5 || fmt == 7) ? 2 : 4);
    uint32_t texel = sm + tm * size;
    uint32_t addr = (texbase + texel * bpp) & mask;

    switch (fmt) {
    case 0: return texel_tables()->rgb332[sgram[addr]];
    case 1: return texel_tables()->alpha8[sgram[addr]];
    case 2: return texel_tables()->int8[sgram[addr]];
    case 3: return texel_tables()->ai44[sgram[addr]];
    case 4: {
        uint16_t w = (uint16_t)(sgram[addr] | (sgram[(addr + 1) & mask] << 8));
        return texel_tables()->rgb565[w];
    }
    case 5: {
        uint16_t w = (uint16_t)(sgram[addr] | (sgram[(addr + 1) & mask] << 8));
        return texel_tables()->argb1555[w];
    }
    case 7: {
        uint16_t w = (uint16_t)(sgram[addr] | (sgram[(addr + 1) & mask] << 8));
        return texel_tables()->argb4444[w];
    }
    case 6:
        return (uint32_t)sgram[addr]
             | ((uint32_t)sgram[(addr + 1) & mask] << 8)
             | ((uint32_t)sgram[(addr + 2) & mask] << 16)
             | ((uint32_t)sgram[(addr + 3) & mask] << 24);
    default:
        return 0xFF000000u;
    }
}

/* ------------------------------------------------------------------ */
/* device                                                              */
/* ------------------------------------------------------------------ */

Voodoo2EC* voodoo2ec_create(void) {
    Voodoo2EC* v = calloc(1, sizeof(Voodoo2EC));
    if (!v) return NULL;
    v2cmdfifo_init(&v->cmdfifo, v);
    voodoo2ec_reset(v);
    return v;
}

void voodoo2ec_destroy(Voodoo2EC* v) {
    if (!v) return;
    free(v->sgram);
    free(v);
}

static int chipmask_from_offset(uint32_t off) {
    switch ((off >> 10) & 3) {
    case 0: return 1;
    case 1: return 3;      /* FBI | TMU0 */
    case 2: return 5;      /* FBI | TMU1 */
    default: return 7;     /* all */
    }
}

void voodoo2ec_reset(Voodoo2EC* v) {
    if (!v->sgram)
        v->sgram = calloc(1, VOODOO2_EC_SGRAM_SIZE);
    memset(v->sgram, 0, VOODOO2_EC_SGRAM_SIZE);
    v->sgram_mask = VOODOO2_EC_SGRAM_SIZE - 1;

    memset(v->regs, 0, sizeof(v->regs));
    memset(v->tmu_regs, 0, sizeof(v->tmu_regs));
    v2cmdfifo_set_enable(&v->cmdfifo, false);

    v->sverts = 0;
    memset(v->svert, 0, sizeof(v->svert));

    v->rgboffs[0] = 0;
    v->rgboffs[1] = 0;
    v->rgboffs[2] = ~0u;
    v->auxoffs = ~0u;
    v->frontbuf = 0;
    v->backbuf = 1;
    v->swaps_pending = 0;
    v->video_changed = true;
    v->height = 480;
    v->xoffs = v->yoffs = 0;
    v->init_enable = 0;
    v->vblank = false;

    /* Display configuration defaults (x tiles = 20 * 32 = 640) */
    v->regs[V2_REG_FBIINIT1] = (20u << 1) | (1u << 8) | FBIINIT1_BLANK;
    v->regs[V2_REG_FBIINIT2] = 0;
    v->regs[V2_REG_FBIINIT4] = 1;
    v->regs[V2_REG_FBIINIT5] = 1u << 9;
    v->regs[V2_REG_FBIINIT6] = 0;
    v->regs[V2_REG_FBIINIT7] = 0;
    v->regs[V2_REG_CLIPLEFTRIGHT] = (1023u << 16);
    v->regs[V2_REG_CLIPLOWYHIGHY] = (1023u << 16);

    /* Init CLUT: expand 5-bit values to 8-bit */
    for (int i = 0; i < 32; i++) {
        uint32_t c = (uint32_t)((i << 3) | (i >> 2));
        v->clut[i] = 0xFF000000u | (c << 16) | (c << 8) | c;
    }
    v->clut[32] = 0x20FFFFFF;
    v->clut_dirty = true;

    /* video memory layout */
    uint32_t config = BITS(v->regs[V2_REG_FBIINIT2], 6, 1);
    if (config == 0) config = BITS(v->regs[V2_REG_FBIINIT5], 9, 2);
    uint32_t xtiles = BITS(v->regs[V2_REG_FBIINIT1], 1, 5)
                    | (BITS(v->regs[V2_REG_FBIINIT6], 0, 1) << 5);
    v->rowpixels = (int)(xtiles * 32);
    if (v->rowpixels < 640) v->rowpixels = 640;

    v->rgboffs[0] = 0;
    uint32_t buf_pages = BITS(v->regs[V2_REG_FBIINIT2], 19, 9);
    if (buf_pages == 0) buf_pages = 0x100;   /* 1 MB per buffer */
    v->rgboffs[1] = buf_pages * 0x1000;
    if (config <= 1) {
        v->rgboffs[2] = ~0u;
        v->auxoffs = 2 * buf_pages * 0x1000;
    } else {
        v->rgboffs[2] = 2 * buf_pages * 0x1000;
        v->auxoffs = (config == 2) ? 3 * buf_pages * 0x1000 : ~0u;
    }
    v->video_changed = true;
}

uint32_t voodoo2ec_reg_read(Voodoo2EC* v, int regnum) {
    if (regnum < 0 || regnum >= 256) return 0;
    if (regnum >= V2_REG_TMU_BASE) {
        int idx = regnum - V2_REG_TMU_BASE;
        if (idx < V2_REG_TMU_COUNT) return v->tmu_regs[0][idx];
        return 0;
    }
    if (regnum == V2_REG_STATUS)
        return (uint32_t)(v2cmdfifo_depth(&v->cmdfifo) & 0x3F)
             | (v->vblank ? (1u << 6) : 0)
             | ((uint32_t)v->frontbuf << 10);
    if (regnum == V2_REG_VRETRACE) return v->vblank ? 0 : 480;
    if (regnum == V2_REG_CMDFIFORDPTR) return v2cmdfifo_read_pointer(&v->cmdfifo);
    if (regnum == V2_REG_CMDFIFODEPTH) return v2cmdfifo_depth(&v->cmdfifo);
    if (regnum == V2_REG_CMDFIFOHOLES) return v2cmdfifo_holes(&v->cmdfifo);
    return v->regs[regnum];
}

void voodoo2ec_reg_write(Voodoo2EC* v, int regnum, uint32_t data) {
    v2_reg_write_chip(v, regnum, data, 7);   /* all chips */
}

static void v2_reg_write_chip(Voodoo2EC* v, int regnum, uint32_t data, int chipmask) {
    if (regnum < 0 || regnum >= 256) return;

    if (regnum >= V2_REG_TMU_BASE) {
        int idx = regnum - V2_REG_TMU_BASE;
        if (idx >= V2_REG_TMU_COUNT) return;
        if (chipmask & 2) v->tmu_regs[0][idx] = data;
        if (chipmask & 4) v->tmu_regs[1][idx] = data;
        return;
    }

    /* float -> fixed conversion for the float triangle registers */
    if (regnum >= V2_REG_FVERTEXAX && regnum <= V2_REG_FDWDY) {
        float f = as_float(data);
        int scale = (regnum <= V2_REG_FVERTEXCY) ? 16 : 4096;
        int target;
        if (regnum <= V2_REG_FVERTEXCY) {
            static const int map[6] = {
                V2_REG_VERTEXAX, V2_REG_VERTEXAY, V2_REG_VERTEXBX,
                V2_REG_VERTEXBY, V2_REG_VERTEXCX, V2_REG_VERTEXCY,
            };
            target = map[regnum - V2_REG_FVERTEXAX];
        } else {
            static const int map[24] = {
                V2_REG_STARTR, V2_REG_STARTG, V2_REG_STARTB, V2_REG_STARTZ,
                V2_REG_STARTA, V2_REG_STARTS, V2_REG_STARTT, V2_REG_STARTW,
                V2_REG_DRDX, V2_REG_DGDX, V2_REG_DBDX, V2_REG_DZDX,
                V2_REG_DADX, V2_REG_DSDX, V2_REG_DTDX, V2_REG_DWDX,
                V2_REG_DRDY, V2_REG_DGDY, V2_REG_DBDY, V2_REG_DZDY,
                V2_REG_DADY, V2_REG_DSDY, V2_REG_DTDY, V2_REG_DWDY,
            };
            target = map[regnum - V2_REG_FSTARTR];
        }
        v->regs[target] = (uint32_t)(int64_t)(f * (float)scale);
        return;
    }

    v->regs[regnum] = data;

    switch (regnum) {
    case V2_REG_FBIINIT0:
        if (data & 0x02000000) voodoo2ec_reset(v);
        break;
    case V2_REG_FBIINIT1:
    case V2_REG_FBIINIT2:
    case V2_REG_FBIINIT5:
    case V2_REG_FBIINIT6:
        voodoo2ec_recompute(v);
        break;
    case V2_REG_FBIINIT7:
        v2cmdfifo_set_enable(&v->cmdfifo, (data & 1) != 0);
        break;
    case V2_REG_CMDFIFOBASEADDR: {
        uint32_t base = BITS(data, 0, 10) << 12;
        uint32_t end = (BITS(data, 16, 10) + 1) << 12;
        if (end > VOODOO2_EC_SGRAM_SIZE) end = VOODOO2_EC_SGRAM_SIZE;
        v2cmdfifo_configure(&v->cmdfifo, v->sgram, base, end);
        break;
    }
    case V2_REG_CMDFIFORDPTR: v2cmdfifo_set_read_pointer(&v->cmdfifo, data); break;
    case V2_REG_CMDFIFOAMIN:  v2cmdfifo_set_address_min(&v->cmdfifo, data); break;
    case V2_REG_CMDFIFOAMAX:  v2cmdfifo_set_address_max(&v->cmdfifo, data); break;
    case V2_REG_CMDFIFODEPTH: v2cmdfifo_set_depth(&v->cmdfifo, data); break;
    case V2_REG_CMDFIFOHOLES: v2cmdfifo_set_holes(&v->cmdfifo, data); break;
    case V2_REG_CLUTDATA:
        if (!(v->regs[V2_REG_FBIINIT1] & (1u << 8))) {
            int idx = (data >> 24) & 0xFF;
            if (idx <= 32) {
                v->clut[idx] = data;
                v->clut_dirty = true;
            }
        }
        break;
    case V2_REG_SWAPBUFFERCMD:
        v->swaps_pending++;
        if (!(data & 1)) {
            v->frontbuf = (v->frontbuf + 1) % 2;
            v->backbuf = (v->backbuf + 1) % 2;
            v->video_changed = true;
            v->swaps_pending--;
        }
        break;
    case V2_REG_FASTFILLCMD: voodoo2ec_fastfill(v); break;
    case V2_REG_TRIANGLECMD:
    case V2_REG_FTRIANGLECMD: voodoo2ec_triangle_legacy(v); break;
    case V2_REG_SARGB:
        v->regs[V2_REG_SALPHA] = as_u32((float)((data >> 24) & 0xFF));
        v->regs[V2_REG_SRED]   = as_u32((float)((data >> 16) & 0xFF));
        v->regs[V2_REG_SGREEN] = as_u32((float)((data >> 8) & 0xFF));
        v->regs[V2_REG_SBLUE]  = as_u32((float)(data & 0xFF));
        break;
    case V2_REG_SBEGINTRI: voodoo2ec_begin_triangle(v); break;
    case V2_REG_SDRAWTRI:  voodoo2ec_draw_triangle_setup(v); break;
    case V2_REG_BLTCOMMAND: voodoo2ec_blit(v); break;
    default:
        break;
    }
}

uint32_t voodoo2ec_read(Voodoo2EC* v, uint32_t offset) {
    uint32_t region = offset >> 22;
    if (region == 0) {
        if (v2cmdfifo_enabled(&v->cmdfifo) && (offset & 0x200000)) return 0xFFFFFFFF;
        return voodoo2ec_reg_read(v, (int)((offset >> 2) & 0x3FF));
    }
    if (region == 1) return voodoo2ec_lfb_read(v, offset & 0x3FFFFF);
    return 0xFFFFFFFF;
}

void voodoo2ec_write(Voodoo2EC* v, uint32_t offset, uint32_t data, uint32_t mask) {
    v->regs[253]++;
    uint32_t region = offset >> 22;
    if (region == 0) {
        if (v2cmdfifo_enabled(&v->cmdfifo) && (offset & 0x200000)) {
            v2cmdfifo_write_direct(&v->cmdfifo, (offset >> 2) & 0xFFFF, data);
            return;
        }
        int chip = chipmask_from_offset(offset >> 2);
        v2_reg_write_chip(v, (int)((offset >> 2) & 0x3FF), data, chip);
        (void)mask;
        return;
    }
    if (region == 1) {
        voodoo2ec_lfb_write(v, offset & 0x3FFFFF, data, mask);
        return;
    }
    if (region == 2) {
        voodoo2ec_texture_write(v, offset & 0x7FFFFF, data);
        return;
    }
}

void voodoo2ec_recompute(Voodoo2EC* v) {
    uint32_t config = BITS(v->regs[V2_REG_FBIINIT2], 6, 1);
    if (config == 0) config = BITS(v->regs[V2_REG_FBIINIT5], 9, 2);
    uint32_t xtiles = BITS(v->regs[V2_REG_FBIINIT1], 1, 5)
                    | (BITS(v->regs[V2_REG_FBIINIT6], 0, 1) << 5);
    v->rowpixels = (int)(xtiles * 32);
    if (v->rowpixels < 640) v->rowpixels = 640;

    v->rgboffs[0] = 0;
    uint32_t buf_pages = BITS(v->regs[V2_REG_FBIINIT2], 19, 9);
    if (buf_pages == 0) buf_pages = 0x100;
    v->rgboffs[1] = buf_pages * 0x1000;
    if (config <= 1) {
        v->rgboffs[2] = ~0u;
        v->auxoffs = 2 * buf_pages * 0x1000;
    } else {
        v->rgboffs[2] = 2 * buf_pages * 0x1000;
        v->auxoffs = (config == 2) ? 3 * buf_pages * 0x1000 : ~0u;
    }
    v->video_changed = true;
}

/* ---------------- CMDFIFO callbacks ---------------- */

void voodoo2ec_cmdfifo_reg_write(Voodoo2EC* v, uint32_t regnum, uint32_t data) {
    int chip = chipmask_from_offset(regnum);
    v2_reg_write_chip(v, (int)(regnum & 0xFF), data, chip);
}

void voodoo2ec_cmdfifo_2d_write(Voodoo2EC* v, uint32_t index, uint32_t data) {
    uint32_t regnum = V2_REG_BLTSRCBASEADDR + index;
    if (regnum <= V2_REG_BLTDATA) voodoo2ec_reg_write(v, (int)regnum, data);
}

void voodoo2ec_cmdfifo_set_setup_mode(Voodoo2EC* v, uint32_t value) {
    v->regs[V2_REG_SSETUPMODE] = value;
}

void voodoo2ec_cmdfifo_triangle_vertex(Voodoo2EC* v, const V2SetupVertex* sv,
                                       uint32_t command, uint32_t index) {
    uint32_t code = BITS(command, 3, 3);
    bool fan = BITS(command, 22, 1) != 0;
    if ((code == 1 && index == 0) || (code == 0 && index % 3 == 0)) {
        v->sverts = 1;
        v->svert[0] = v->svert[1] = v->svert[2] = *sv;
    } else {
        if (!fan) v->svert[0] = v->svert[1];
        v->svert[1] = v->svert[2];
        v->svert[2] = *sv;
        if (++v->sverts >= 3) voodoo2ec_setup_draw(v);
    }
}

void voodoo2ec_cmdfifo_lfb_write(Voodoo2EC* v, uint32_t offset, uint32_t data) {
    voodoo2ec_lfb_write(v, offset, data, 0xFFFFFFFF);
}

void voodoo2ec_cmdfifo_texture_write(Voodoo2EC* v, uint32_t offset, uint32_t data) {
    voodoo2ec_texture_write(v, offset, data);
}

/* ---------------- buffer / LFB / texture ---------------- */

static uint16_t* front_buffer(Voodoo2EC* v) {
    if (v->rgboffs[v->frontbuf] == ~0u) return NULL;
    return (uint16_t*)(v->sgram + v->rgboffs[v->frontbuf]);
}

static uint16_t* back_buffer(Voodoo2EC* v) {
    if (v->rgboffs[v->backbuf] == ~0u) return NULL;
    return (uint16_t*)(v->sgram + v->rgboffs[v->backbuf]);
}

uint32_t voodoo2ec_lfb_read(Voodoo2EC* v, uint32_t offset) {
    uint32_t x = offset % (uint32_t)v->rowpixels;
    uint32_t y = offset / (uint32_t)v->rowpixels;
    uint16_t* buf = front_buffer(v);
    if (!buf) return 0xFFFFFFFF;
    if (y >= (uint32_t)v->height || y * (uint32_t)v->rowpixels + x + 1 >=
        VOODOO2_EC_SGRAM_SIZE / 2) return 0xFFFFFFFF;
    buf += y * v->rowpixels + x;
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 16);
}

void voodoo2ec_lfb_write(Voodoo2EC* v, uint32_t offset, uint32_t data, uint32_t mask) {
    uint32_t x = offset % (uint32_t)v->rowpixels;
    uint32_t y = offset / (uint32_t)v->rowpixels;
    uint16_t* buf = back_buffer(v);
    if (!buf) return;
    if (y >= (uint32_t)v->height || y * (uint32_t)v->rowpixels + x + 1 >=
        VOODOO2_EC_SGRAM_SIZE / 2) return;
    buf += y * v->rowpixels + x;
    if (mask & 0xFFFF) buf[0] = (uint16_t)data;
    if (mask & 0xFFFF0000) buf[1] = (uint16_t)(data >> 16);
}

void voodoo2ec_texture_write(Voodoo2EC* v, uint32_t offset, uint32_t data) {
    uint32_t addr = offset & (VOODOO2_EC_SGRAM_SIZE - 1);
    v->sgram[addr + 0] = (uint8_t)(data >> 0);
    v->sgram[addr + 1] = (uint8_t)(data >> 8);
    v->sgram[addr + 2] = (uint8_t)(data >> 16);
    v->sgram[addr + 3] = (uint8_t)(data >> 24);
}

/* ---------------- drawing helpers ---------------- */

static uint16_t pack565(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static uint16_t pack555(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

static inline int clamp8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

void voodoo2ec_fastfill(Voodoo2EC* v) {
    uint16_t* buf = back_buffer(v);
    if (!buf) return;
    uint32_t color = v->regs[V2_REG_COLOR1];
    int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    bool is555 = (v->regs[V2_REG_FBZCOLORPATH] & 1) != 0;
    uint16_t pix = is555 ? pack555(r, g, b) : pack565(r, g, b);

    int32_t left = BITS(v->regs[V2_REG_CLIPLEFTRIGHT], 0, 12);
    int32_t right = BITS(v->regs[V2_REG_CLIPLEFTRIGHT], 16, 12);
    int32_t top = BITS(v->regs[V2_REG_CLIPLOWYHIGHY], 0, 12);
    int32_t bottom = BITS(v->regs[V2_REG_CLIPLOWYHIGHY], 16, 12);

    if (left <= right && top <= bottom) {
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right >= v->rowpixels) right = v->rowpixels - 1;
        if (bottom >= v->height) bottom = v->height - 1;
        for (int y = top; y <= bottom; y++) {
            uint16_t* row = buf + (uint32_t)y * v->rowpixels;
            for (int x = left; x <= right; x++) row[x] = pix;
        }
    } else {
        for (int y = 0; y < v->height; y++) {
            uint16_t* row = buf + (uint32_t)y * v->rowpixels;
            for (int x = 0; x < v->rowpixels; x++) row[x] = pix;
        }
    }
    v->video_changed = true;
}

/* ---------------- triangle setup engine ---------------- */

static void extract_setup(Voodoo2EC* v, V2SetupVertex* sv) {
    sv->x = as_float(v->regs[V2_REG_SVX]);
    sv->y = as_float(v->regs[V2_REG_SVY]);
    sv->wb = as_float(v->regs[V2_REG_SWB]);
    sv->w0 = as_float(v->regs[V2_REG_SWTMU0]);
    sv->s0 = as_float(v->regs[V2_REG_SS_W0]);
    sv->t0 = as_float(v->regs[V2_REG_ST_W0]);
    sv->w1 = as_float(v->regs[V2_REG_SWTMU1]);
    sv->s1 = as_float(v->regs[V2_REG_SS_W1]);
    sv->t1 = as_float(v->regs[V2_REG_ST_W1]);
    sv->a = as_float(v->regs[V2_REG_SALPHA]);
    sv->r = as_float(v->regs[V2_REG_SRED]);
    sv->g = as_float(v->regs[V2_REG_SGREEN]);
    sv->b = as_float(v->regs[V2_REG_SBLUE]);
}

void voodoo2ec_begin_triangle(Voodoo2EC* v) {
    extract_setup(v, &v->svert[2]);
    v->svert[0] = v->svert[1] = v->svert[2];
    v->sverts = 1;
}

void voodoo2ec_draw_triangle_setup(Voodoo2EC* v) {
    if (!(v->regs[V2_REG_SSETUPMODE] & (1u << 16)))
        v->svert[0] = v->svert[1];
    v->svert[1] = v->svert[2];
    extract_setup(v, &v->svert[2]);
    if (++v->sverts >= 3) voodoo2ec_setup_draw(v);
}

void voodoo2ec_setup_draw(Voodoo2EC* v) {
    V2SetupVertex sv0 = v->svert[0];
    V2SetupVertex sv1 = v->svert[1];
    V2SetupVertex sv2 = v->svert[2];

    double divisor = (sv0.x - sv1.x) * (sv0.y - sv2.y) -
                     (sv0.x - sv2.x) * (sv0.y - sv1.y);

    uint32_t smode = v->regs[V2_REG_SSETUPMODE];
    if (smode & (1u << 17)) {   /* enable culling */
        int culling_sign = (smode >> 18) & 1;
        int divisor_sign = (divisor < 0);
        if (!(smode & (1u << 16)) && !(smode & (1u << 19)))
            culling_sign ^= (v->sverts - 3) & 1;
        if (divisor_sign == culling_sign) return;
    }

    bool use_tex0 = (smode & (1u << 5)) != 0;
    bool use_tex1 = (smode & (1u << 7)) != 0;
    voodoo2ec_rasterize(v, &sv0, &sv1, &sv2, use_tex0, use_tex1);
}

void voodoo2ec_triangle_legacy(Voodoo2EC* v) {
    int16_t ax = (int16_t)(v->regs[V2_REG_VERTEXAX] & 0xFFFF);
    int16_t ay = (int16_t)(v->regs[V2_REG_VERTEXAY] & 0xFFFF);
    int16_t bx = (int16_t)(v->regs[V2_REG_VERTEXBX] & 0xFFFF);
    int16_t by = (int16_t)(v->regs[V2_REG_VERTEXBY] & 0xFFFF);
    int16_t cx = (int16_t)(v->regs[V2_REG_VERTEXCX] & 0xFFFF);
    int16_t cy = (int16_t)(v->regs[V2_REG_VERTEXCY] & 0xFFFF);

    double r0 = (double)(int32_t)v->regs[V2_REG_STARTR];
    double g0 = (double)(int32_t)v->regs[V2_REG_STARTG];
    double b0 = (double)(int32_t)v->regs[V2_REG_STARTB];
    double drx = (double)(int32_t)v->regs[V2_REG_DRDX], dry = (double)(int32_t)v->regs[V2_REG_DRDY];
    double dgx = (double)(int32_t)v->regs[V2_REG_DGDX], dgy = (double)(int32_t)v->regs[V2_REG_DGDY];
    double dbx = (double)(int32_t)v->regs[V2_REG_DBDX], dby = (double)(int32_t)v->regs[V2_REG_DBDY];

    V2SetupVertex v0 = {}, v1 = {}, v2 = {};
    double dx, dy;
    int c;
    dx = 0; dy = 0;
    c = clamp8((int)((r0 + drx * dx / 16.0 + dry * dy / 16.0) / 4096.0));
    v0.r = (float)c;
    c = clamp8((int)((g0 + dgx * dx / 16.0 + dgy * dy / 16.0) / 4096.0));
    v0.g = (float)c;
    c = clamp8((int)((b0 + dbx * dx / 16.0 + dby * dy / 16.0) / 4096.0));
    v0.b = (float)c;
    v0.x = ax / 16.0; v0.y = ay / 16.0;

    dx = bx - ax; dy = by - ay;
    c = clamp8((int)((r0 + drx * dx / 16.0 + dry * dy / 16.0) / 4096.0));
    v1.r = (float)c;
    c = clamp8((int)((g0 + dgx * dx / 16.0 + dgy * dy / 16.0) / 4096.0));
    v1.g = (float)c;
    c = clamp8((int)((b0 + dbx * dx / 16.0 + dby * dy / 16.0) / 4096.0));
    v1.b = (float)c;
    v1.x = bx / 16.0; v1.y = by / 16.0;

    dx = cx - ax; dy = cy - ay;
    c = clamp8((int)((r0 + drx * dx / 16.0 + dry * dy / 16.0) / 4096.0));
    v2.r = (float)c;
    c = clamp8((int)((g0 + dgx * dx / 16.0 + dgy * dy / 16.0) / 4096.0));
    v2.g = (float)c;
    c = clamp8((int)((b0 + dbx * dx / 16.0 + dby * dy / 16.0) / 4096.0));
    v2.b = (float)c;
    v2.x = cx / 16.0; v2.y = cy / 16.0;

    voodoo2ec_rasterize(v, &v0, &v1, &v2, false, false);
}

void voodoo2ec_rasterize(Voodoo2EC* v, const V2SetupVertex* v0in,
                         const V2SetupVertex* v1in, const V2SetupVertex* v2in,
                         bool use_tex0, bool use_tex1) {
    uint16_t* buf = back_buffer(v);
    if (!buf) return;

    double x0 = v0in->x, y0 = v0in->y;
    double x1 = v1in->x, y1 = v1in->y;
    double x2 = v2in->x, y2 = v2in->y;

    double area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0.0) return;

    bool is555 = (v->regs[V2_REG_FBZCOLORPATH] & 1) != 0;
    bool constant_rgb = (v->regs[V2_REG_FBZCOLORPATH] & 2) != 0;

    /* TMU configuration from the per-TMU register banks */
    uint32_t texmode0 = v->tmu_regs[0][V2_REG_TEXTUREMODE - V2_REG_TMU_BASE];
    uint32_t texmode1 = v->tmu_regs[1][V2_REG_TEXTUREMODE - V2_REG_TMU_BASE];
    uint32_t tlod0 = v->tmu_regs[0][V2_REG_TLOD - V2_REG_TMU_BASE];
    uint32_t tlod1 = v->tmu_regs[1][V2_REG_TLOD - V2_REG_TMU_BASE];
    uint32_t tbase0 = v->tmu_regs[0][V2_REG_TEXBASEADDR - V2_REG_TMU_BASE] & 0xFFFFFF;
    uint32_t tbase1 = v->tmu_regs[1][V2_REG_TEXBASEADDR - V2_REG_TMU_BASE] & 0xFFFFFF;
    uint32_t fmt0 = use_tex0 ? ((texmode0 >> 2) & 7) : 0;
    uint32_t fmt1 = use_tex1 ? ((texmode1 >> 2) & 7) : 0;
    int lod0 = use_tex0 ? (int)((tlod0 >> 3) & 0x1F) : 0;
    int lod1 = use_tex1 ? (int)((tlod1 >> 3) & 0x1F) : 0;

    int minx = v2_max(0, (int)v2_min(x0, v2_min(x1, x2)));
    int maxx = v2_min(v->rowpixels - 1, (int)v2_max(x0, v2_max(x1, x2)));
    int miny = v2_max(0, (int)v2_min(y0, v2_min(y1, y2)));
    int maxy = v2_min(v->height - 1, (int)v2_max(y0, v2_max(y1, y2)));

    int32_t cl = BITS(v->regs[V2_REG_CLIPLEFTRIGHT], 0, 12);
    int32_t cr = BITS(v->regs[V2_REG_CLIPLEFTRIGHT], 16, 12);
    int32_t ct = BITS(v->regs[V2_REG_CLIPLOWYHIGHY], 0, 12);
    int32_t cb = BITS(v->regs[V2_REG_CLIPLOWYHIGHY], 16, 12);
    if (cl <= cr && ct <= cb) {
        minx = v2_max(minx, (int)cl);
        maxx = v2_min(maxx, (int)cr);
        miny = v2_max(miny, (int)ct);
        maxy = v2_min(maxy, (int)cb);
    }
    if (minx > maxx || miny > maxy) return;

    for (int y = miny; y <= maxy; y++) {
        uint16_t* row = buf + (uint32_t)y * v->rowpixels;
        double py = y + 0.5;
        for (int x = minx; x <= maxx; x++) {
            double px = x + 0.5;
            double w0 = ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) / area;
            double w1 = ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) / area;
            double w2 = ((x0 - px) * (y1 - py) - (y0 - py) * (x1 - px)) / area;
            if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;

            int r, g, b;
            if (constant_rgb) {
                uint32_t col = v->regs[V2_REG_COLOR1];
                r = (col >> 16) & 0xFF; g = (col >> 8) & 0xFF; b = col & 0xFF;
            } else {
                r = clamp8((int)(v0in->r * w0 + v1in->r * w1 + v2in->r * w2));
                g = clamp8((int)(v0in->g * w0 + v1in->g * w1 + v2in->g * w2));
                b = clamp8((int)(v0in->b * w0 + v1in->b * w1 + v2in->b * w2));
            }

            uint32_t col = 0xFF000000u | (r << 16) | (g << 8) | b;

            if (use_tex0) {
                double sw = v0in->s0 * v0in->w0 * w0 + v1in->s0 * v1in->w0 * w1 +
                            v2in->s0 * v2in->w0 * w2;
                double tw = v0in->t0 * v0in->w0 * w0 + v1in->t0 * v1in->w0 * w1 +
                            v2in->t0 * v2in->w0 * w2;
                double ww = v0in->w0 * w0 + v1in->w0 * w1 + v2in->w0 * w2;
                if (ww > 1e-6)
                    col = sample_texel(v, fmt0, lod0, (uint32_t)(sw / ww),
                                       (uint32_t)(tw / ww), tbase0);
            }
            if (use_tex1) {
                double sw = v0in->s1 * v0in->w1 * w0 + v1in->s1 * v1in->w1 * w1 +
                            v2in->s1 * v2in->w1 * w2;
                double tw = v0in->t1 * v0in->w1 * w0 + v1in->t1 * v1in->w1 * w1 +
                            v2in->t1 * v2in->w1 * w2;
                double ww = v0in->w1 * w0 + v1in->w1 * w1 + v2in->w1 * w2;
                if (ww > 1e-6) {
                    uint32_t tex1 = sample_texel(v, fmt1, lod1, (uint32_t)(sw / ww),
                                                 (uint32_t)(tw / ww), tbase1);
                    int cr2 = ((col >> 16) & 0xFF) * ((tex1 >> 16) & 0xFF) / 255;
                    int cg2 = ((col >> 8) & 0xFF) * ((tex1 >> 8) & 0xFF) / 255;
                    int cb2 = (col & 0xFF) * (tex1 & 0xFF) / 255;
                    col = 0xFF000000u | (cr2 << 16) | (cg2 << 8) | cb2;
                }
            }

            row[x] = is555 ? pack555((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF)
                           : pack565((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
        }
    }
    v->video_changed = true;
}

void voodoo2ec_blit(Voodoo2EC* v) {
    uint16_t* fb = (uint16_t*)v->sgram;
    uint32_t src = v->regs[V2_REG_BLTSRCBASEADDR];
    uint32_t dst = v->regs[V2_REG_BLTDSTBASEADDR];
    int xstride = v->regs[V2_REG_BLTXYSTRIDES] & 0xFFFF;
    int ystride = (v->regs[V2_REG_BLTXYSTRIDES] >> 16) & 0xFFFF;
    int sx = v->regs[V2_REG_BLTSRCXY] & 0xFFFF;
    int sy = (v->regs[V2_REG_BLTSRCXY] >> 16) & 0xFFF;
    int dx = v->regs[V2_REG_BLTDSTXY] & 0xFFFF;
    int dy = (v->regs[V2_REG_BLTDSTXY] >> 16) & 0xFFF;
    int w = v->regs[V2_REG_BLTSIZE] & 0xFFF;
    int h = (v->regs[V2_REG_BLTSIZE] >> 16) & 0xFFF;
    uint32_t color = v->regs[V2_REG_BLTCOLOR];
    uint32_t maxw = VOODOO2_EC_SGRAM_SIZE / 2;

    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            uint32_t xstep = (xstride != 0) ? (uint32_t)xstride : 2u;
            uint32_t saddr = (src + (uint32_t)(sy + yy) * ystride +
                              (uint32_t)(sx + xx) * xstep) / 2;
            uint32_t daddr = (dst + (uint32_t)(dy + yy) * ystride +
                              (uint32_t)(dx + xx) * xstep) / 2;
            if (saddr >= maxw || daddr >= maxw) continue;
            fb[daddr] = fb[saddr];
        }
    }
    (void)color;
    v->video_changed = true;
}

/* ---------------- display output ---------------- */

int voodoo2ec_update(Voodoo2EC* v, uint32_t* rgb, int width, int height) {
    if (!v->sgram || !rgb) return 0;

    memset(rgb, 0, (size_t)width * height * 4);   /* transparent */

    if (!(v->regs[V2_REG_FBIINIT1] & FBIINIT1_BLANK))
        voodoo2ec_draw_framebuffer(v, rgb, width, height);

    bool changed = v->video_changed;
    v->video_changed = false;
    return changed ? 1 : 0;
}

void voodoo2ec_draw_framebuffer(Voodoo2EC* v, uint32_t* out, int w, int h) {
    uint16_t* buf = front_buffer(v);
    if (!buf) return;

    if (v->clut_dirty) {
        uint8_t rt[32], gt[64], bt[32];
        for (int i = 0; i < 32; i++) {
            rt[i] = (v->clut[i] >> 16) & 0xFF;
            bt[i] = v->clut[i] & 0xFF;
        }
        for (int i = 0; i < 64; i++) gt[i] = (v->clut[i / 2] >> 8) & 0xFF;
        for (uint32_t pen = 0; pen < 65536; pen++) {
            int r = BITS(pen, 11, 5), g = BITS(pen, 5, 6), b = BITS(pen, 0, 5);
            v->pen[pen] = 0xFF000000u | ((uint32_t)rt[r] << 16) |
                          ((uint32_t)gt[g] << 8) | bt[b];
        }
        v->clut_dirty = false;
    }

    for (int y = 0; y < h && y < v->height; y++) {
        int sy = y + v->yoffs;
        if (sy < 0 || sy >= v->height) continue;
        uint16_t* src = buf + sy * v->rowpixels - v->xoffs;
        uint32_t* dst = out + y * w;
        for (int x = 0; x < w && x < v->rowpixels; x++)
            dst[x] = v->pen[src[x]];
    }
}

/* ---------------- accessors ---------------- */

uint32_t voodoo2ec_fifo_depth(Voodoo2EC* v) { return v2cmdfifo_depth(&v->cmdfifo); }
const uint8_t* voodoo2ec_sgram(Voodoo2EC* v) { return v->sgram; }
uint32_t voodoo2ec_sgram_size(Voodoo2EC* v) { (void)v; return VOODOO2_EC_SGRAM_SIZE; }
