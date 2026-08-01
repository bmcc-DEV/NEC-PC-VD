/*
 * glide.h - Glide 2.x/3.x API adapted to the PC-Viper Voodoo2 EC.
 *
 * Implements the subset used by 3dfx-era demos/games: context open/close,
 * buffer clear/swap, color combine, textured triangle strips via the
 * hardware setup engine, texture upload/bind and splash. Vertices are
 * supplied in screen space (x, y in pixels).
 */
#ifndef VIPER_GLIDE_H
#define VIPER_GLIDE_H

#include <stdint.h>
#include "voodoo2_ec.h"

typedef uint32_t GrColor;
typedef uint32_t GrChipID;
typedef uint32_t GrBuffer;
typedef uint32_t GrTextureFormat;
typedef uint32_t GrCombineFunction;
typedef uint32_t GrCombineFactor;

#define GR_TMU0 0
#define GR_TMU1 1

#define GR_BUFFER_FRONTBUFFER  0
#define GR_BUFFER_BACKBUFFER   1
#define GR_BUFFER_AUXBUFFER    2
#define GR_BUFFER_DEPTHBUFFER  3

/* texture formats map directly to the Voodoo format codes */
#define GR_TEXFMT_RGB_332     0
#define GR_TEXFMT_ALPHA_8     1
#define GR_TEXFMT_INTENSITY_8 2
#define GR_TEXFMT_AI_88       3
#define GR_TEXFMT_RGB_565     4
#define GR_TEXFMT_ARGB_1555   5
#define GR_TEXFMT_ARGB_8888   6
#define GR_TEXFMT_ARGB_4444   7

typedef struct {
    float x, y, z, w;        /* w = perspective weight (1/w) */
    float r, g, b, a;        /* 0..255 */
    float s0, t0;            /* TMU0 texcoords */
    float s1, t1;            /* TMU1 texcoords */
    float ooz;               /* 1/z */
    float oow;               /* 1/w */
    uint32_t c0, c1;         /* packed colors */
} GrVertex;

typedef struct {
    int smallLod;
    int largeLod;
    float aspectRatio;
    GrTextureFormat format;
    void* data;              /* texture pixels (LOD `largeLod` square) */
} GrTexInfo;

typedef struct {
    int width;
    int height;
    Voodoo2EC* voodoo;
} GrContext;

/* ---- lifecycle ---- */
int grGlideInit(void);
void grGlideShutdown(void);
void grSstSelect(int which_sst);
int grSstWinOpen(GrContext* ctx, uint32_t hwnd, int screen_width,
                 int screen_height, int num_buffers, int resolution);
void grSstWinClose(GrContext* ctx);

/* ---- frame ---- */
void grBufferClear(GrContext* ctx, GrColor color, GrBuffer buffer);
void grBufferSwap(GrContext* ctx, GrBuffer buffer);
void grSplash(GrContext* ctx, float x, float y, float width, float height,
              uint32_t frame, uint32_t* rgb_out);

/* ---- texture ---- */
void grTexUpload(GrContext* ctx, GrChipID tmu, GrTexInfo* info);
void grTexBind(GrContext* ctx, GrChipID tmu, GrTexInfo* info);
void grTexMipMap(GrContext* ctx, GrChipID tmu, int lod_min, int lod_max);

/* ---- triangles ---- */
void grColorCombine(GrContext* ctx, GrCombineFunction rgb, GrCombineFunction alpha);
void grBeginTriangles(GrContext* ctx);
void grVertex(GrContext* ctx, GrVertex* v);
void grEndTriangles(GrContext* ctx);

#endif /* VIPER_GLIDE_H */
