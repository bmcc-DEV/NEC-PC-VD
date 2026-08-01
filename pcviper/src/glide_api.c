/*
 * glide_api.c - Glide 2.x/3.x wrapper over the Voodoo2 EC.
 *
 * Vertices are written to the hardware triangle setup engine (sVx..sARGB,
 * sBeginTriCMD / sDrawTriCMD) in strip order; textures are placed in the
 * unified SGRAM texture heap and bound through texBaseAddr.
 */
#include "glide.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* setup mode flags (see voodoo2_ec.h / setup engine) */
#define SM_RGB  0x01
#define SM_W0   0x10
#define SM_ST0  0x20
#define SM_W1   0x40
#define SM_ST1  0x80

typedef struct {
    int active;
    uint32_t texbase;   /* SGRAM byte offset */
    int lod;
    GrTextureFormat fmt;
} GlideTmu;

typedef struct {
    int initialised;
    int sst;
    int width, height;
    int nverts;
    int setup_mode;
    bool tex_enabled[2];
    GlideTmu tmu[2];
    uint32_t tex_heap;   /* next free SGRAM offset */
    Voodoo2EC* voodoo;
} GlideState;

static GlideState s_glide;

static inline uint32_t fbits(float v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b;
}

static void set_tmu_regs(Voodoo2EC* v, int tmu, const GlideTmu* t) {
    int chipsel = (tmu == 0) ? 1 : 2;
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (chipsel << 12)),
                    (uint32_t)(t->fmt << 2), ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (chipsel << 12)),
                    (uint32_t)(t->lod << 3), ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (chipsel << 12)),
                    t->texbase, ~0u);
}

int grGlideInit(void) {
    memset(&s_glide, 0, sizeof(s_glide));
    s_glide.initialised = 1;
    return 0;
}

void grGlideShutdown(void) {
    s_glide.initialised = 0;
}

void grSstSelect(int which_sst) {
    s_glide.sst = which_sst;
}

int grSstWinOpen(GrContext* ctx, uint32_t hwnd, int screen_width,
                 int screen_height, int num_buffers, int resolution) {
    (void)hwnd;
    (void)num_buffers;
    (void)resolution;
    if (!s_glide.initialised) return 0;
    s_glide.width = screen_width;
    s_glide.height = screen_height;
    s_glide.nverts = 0;
    s_glide.setup_mode = SM_RGB | SM_W0;
    s_glide.tex_enabled[0] = s_glide.tex_enabled[1] = false;
    s_glide.tmu[0].active = s_glide.tmu[1].active = 0;
    s_glide.tex_heap = 0x300000;      /* past the framebuffer buffers */
    s_glide.voodoo = ctx->voodoo;
    voodoo2ec_reset(s_glide.voodoo);
    /* unblank */
    voodoo2ec_reg_write(s_glide.voodoo, V2_REG_FBIINIT1,
                        (20u << 1) | (1u << 8));
    return 1;
}

void grSstWinClose(GrContext* ctx) {
    (void)ctx;
    s_glide.voodoo = NULL;
}

void grBufferClear(GrContext* ctx, GrColor color, GrBuffer buffer) {
    (void)ctx; (void)buffer;
    voodoo2ec_reg_write(s_glide.voodoo, V2_REG_COLOR1, color);
    voodoo2ec_reg_write(s_glide.voodoo, V2_REG_FASTFILLCMD, 0);
}

void grBufferSwap(GrContext* ctx, GrBuffer buffer) {
    (void)ctx; (void)buffer;
    voodoo2ec_reg_write(s_glide.voodoo, V2_REG_SWAPBUFFERCMD, 0);
}

void grSplash(GrContext* ctx, float x, float y, float width, float height,
              uint32_t frame, uint32_t* rgb_out) {
    (void)ctx; (void)x; (void)y; (void)width; (void)height; (void)frame;
    if (rgb_out)
        voodoo2ec_update(s_glide.voodoo, rgb_out, s_glide.width, s_glide.height);
}

void grTexUpload(GrContext* ctx, GrChipID tmu, GrTexInfo* info) {
    (void)ctx;
    if (tmu > 1 || !info || !info->data) return;
    GlideTmu* t = &s_glide.tmu[tmu];
    int texels = 1 << info->largeLod;
    int bpp = (info->format <= 3) ? 1 :
              ((info->format == 4 || info->format == 5 || info->format == 7) ? 2 : 4);
    uint32_t bytes = (uint32_t)texels * texels * (uint32_t)bpp;
    uint8_t* src = (uint8_t*)info->data;

    /* upload into the unified SGRAM via the texture port */
    for (uint32_t off = 0; off < bytes; off += 4) {
        uint32_t dword = (uint32_t)src[off] | ((uint32_t)src[off + 1] << 8) |
                         ((uint32_t)src[off + 2] << 16) | ((uint32_t)src[off + 3] << 24);
        voodoo2ec_write(s_glide.voodoo, 0x800000u + s_glide.tex_heap + off,
                        dword, ~0u);
    }
    t->active = 1;
    t->texbase = s_glide.tex_heap;
    t->lod = info->largeLod;
    t->fmt = info->format;
    s_glide.tex_heap += (bytes + 15) & ~15u;
    set_tmu_regs(s_glide.voodoo, (int)tmu, t);
}

void grTexBind(GrContext* ctx, GrChipID tmu, GrTexInfo* info) {
    (void)ctx;
    (void)info;
    if (tmu <= 1) {
        s_glide.tex_enabled[tmu] = true;
        set_tmu_regs(s_glide.voodoo, (int)tmu, &s_glide.tmu[tmu]);
    }
}

void grTexMipMap(GrContext* ctx, GrChipID tmu, int lod_min, int lod_max) {
    (void)ctx; (void)lod_min;
    if (tmu <= 1) {
        s_glide.tmu[tmu].lod = lod_max;
        set_tmu_regs(s_glide.voodoo, (int)tmu, &s_glide.tmu[tmu]);
    }
}

void grColorCombine(GrContext* ctx, GrCombineFunction rgb, GrCombineFunction alpha) {
    (void)ctx;
    (void)alpha;
    /* rgb == 0: constant only; otherwise texture is used */
    s_glide.setup_mode = SM_RGB | SM_W0;
    if (rgb != 0) s_glide.setup_mode |= SM_ST0;
    if (s_glide.tex_enabled[1]) s_glide.setup_mode |= SM_W1 | SM_ST1;
}

void grBeginTriangles(GrContext* ctx) {
    (void)ctx;
    s_glide.nverts = 0;
    voodoo2ec_reg_write(s_glide.voodoo, V2_REG_SSETUPMODE, s_glide.setup_mode);
}

void grVertex(GrContext* ctx, GrVertex* v) {
    (void)ctx;
    Voodoo2EC* vd = s_glide.voodoo;
    float w = (v->w != 0.0f) ? v->w : 1.0f;

    voodoo2ec_reg_write(vd, V2_REG_SVX, fbits(v->x));
    voodoo2ec_reg_write(vd, V2_REG_SVY, fbits(v->y));
    voodoo2ec_reg_write(vd, V2_REG_SWB, fbits(v->ooz != 0.0f ? v->ooz : 1.0f));
    voodoo2ec_reg_write(vd, V2_REG_SWTMU0, fbits(w));
    voodoo2ec_reg_write(vd, V2_REG_SS_W0, fbits(v->s0));
    voodoo2ec_reg_write(vd, V2_REG_ST_W0, fbits(v->t0));
    voodoo2ec_reg_write(vd, V2_REG_SWTMU1, fbits(w));
    voodoo2ec_reg_write(vd, V2_REG_SS_W1, fbits(v->s1));
    voodoo2ec_reg_write(vd, V2_REG_ST_W1, fbits(v->t1));

    uint32_t argb = ((uint32_t)(int)(v->a + 0.5f) << 24) |
                    ((uint32_t)(int)(v->r + 0.5f) << 16) |
                    ((uint32_t)(int)(v->g + 0.5f) << 8) |
                    (uint32_t)(int)(v->b + 0.5f);
    voodoo2ec_reg_write(vd, V2_REG_SARGB, argb);

    if (s_glide.nverts == 0)
        voodoo2ec_reg_write(vd, V2_REG_SBEGINTRI, 1);
    else
        voodoo2ec_reg_write(vd, V2_REG_SDRAWTRI, 1);
    s_glide.nverts++;
}

void grEndTriangles(GrContext* ctx) {
    (void)ctx;
}
