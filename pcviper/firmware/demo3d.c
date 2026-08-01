/*
 * demo3d.c - Quake-style textured 3D demo (MIPS IV, freestanding C).
 *
 * A rotating textured cube rendered on the VR5432: the CPU does the
 * geometry (rotation + perspective projection with the FPU) and drives
 * the Voodoo2 EC hardware setup engine over MMIO for perspective-correct
 * textured triangle rasterization.
 *
 * Cross-compile: mips64-elf-gcc -EL -mips4 -mhard-float -ffreestanding
 *   -nostdlib -mno-abicalls -G0 -O2, link at 0x1FC00000.
 */

typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;

/* ---- MMIO access to the Voodoo2 EC registers ----
 * V2_R(r) writes register dword index r (r is DWORD, not byte).
 * V2_TMU(r) writes TMU0 register r (chipsel 1 via the high bits).
 * V2_TEX(ofs) writes texture port at SGRAM byte offset. */
#define V2_R(r)    (*(volatile uint32_t*)(0xB0000000ull + ((uint32_t)(r) << 2)))
#define V2_TMU(r)  (*(volatile uint32_t*)(0xB0000000ull + ((uint32_t)(r) << 2) + 0x1000))
#define V2_TEX(o)  (*(volatile uint32_t*)(0xB0800000ull + (o)))

/* Register DWORD indices (from voodoo2_ec.h / voodoo_defs.h) */
#define R_FBIINIT1    0x214/4   /* 133 */
#define R_FBIINIT2    0x218/4   /* 134 */
#define R_FBIINIT4    0x200/4   /* 128 */
#define R_SETUPMODE   0x260/4   /* 152 */
#define R_SVX         0x264/4   /* 153 */
#define R_SVY         0x268/4   /* 154 */
#define R_SARGB       0x26C/4   /* 155 */
#define R_SWB         0x284/4   /* 161 */
#define R_SWTMU0      0x288/4   /* 162 */
#define R_SS_W0       0x28C/4   /* 163 */
#define R_ST_W0       0x290/4   /* 164 */
#define R_SBTTRI      0x2A4/4   /* 169 */
#define R_SDRAWTRI    0x2A0/4   /* 168 */
#define R_COLOR1      0x148/4   /* 82 */
#define R_FASTFILL    0x124/4   /* 73 */
#define R_SWAP        0x128/4   /* 74 */
/* TMU0: dword index (e.g., texmode = 192) with chipsel 1 */
#define TMU0_REG(byte)  ((byte)/4)   /* regnum for chip mask from the offset */

#define SX 640
#define SY 480

static inline uint32_t fbits(float f) {
    union { float f; uint32_t u; } c; c.f = f; return c.u;
}

static const int uv[6][4][2] = {
    {{0,0},{63,0},{63,63},{0,63}},
    {{0,0},{63,0},{63,63},{0,63}},
    {{0,0},{63,0},{63,63},{0,63}},
    {{0,0},{63,0},{63,63},{0,63}},
    {{0,0},{63,0},{63,63},{0,63}},
    {{0,0},{63,0},{63,63},{0,63}},
};
static const float face[6][4][3] = {
    {{-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f}},
    {{ 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f}},
    {{-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f}},
    {{ 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f}},
    {{-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f}},
    {{-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f}},
};

static void voodoo_setup(void) {
    /* unblank */
    V2_R(R_FBIINIT1) = (20u << 1) | (1u << 8);
    /* TMU0: RGB565 (fmt 4), lod 6 (64x64), base 0x300000 */
    V2_TMU(TMU0_REG(0x300)) = 4u << 2;
    V2_TMU(TMU0_REG(0x304)) = 6u << 3;
    V2_TMU(TMU0_REG(0x30C)) = 0x300000u;
    /* upload 64x64 checkerboard RGB565 into SGRAM */
    for (int i = 0; i < 64 * 64; i += 2) {
        int x0 = (i & 63), y0 = (i >> 6);
        int x1 = x0 + 1;
        uint16_t c0 = ((x0 >> 3) + (y0 >> 3)) & 1 ? 0x7FFF : 0x0000;
        uint16_t c1 = ((x1 >> 3) + (y0 >> 3)) & 1 ? 0x7FFF : 0x0000;
        V2_TEX(0x300000u + (uint32_t)i * 2) = c0 | ((uint32_t)c1 << 16);
    }
}

static void put_vertex(float sx, float sy, float s, float t, float w) {
    V2_R(R_SVX)   = fbits(sx);
    V2_R(R_SVY)   = fbits(sy);
    V2_R(R_SWB)   = fbits(1.0f);
    V2_R(R_SWTMU0) = fbits(w);
    V2_R(R_SS_W0) = fbits(s);
    V2_R(R_ST_W0) = fbits(t);
    V2_R(R_SARGB) = 0xFFFFFFFFu;
}

static void triangle(const float p0[3], const float p1[3], const float p2[3],
                     int u0, int v0, int u1, int v1, int u2, int v2,
                     float ca, float sa) {
    const float focal = (float)SY / 2.0f;
    const float camz = 3.0f;
    float sx[3], sy[3], w[3];
    for (int i = 0; i < 3; i++) {
        const float* p = i == 0 ? p0 : (i == 1 ? p1 : p2);
        float x = p[0] * ca + p[2] * sa;
        float z = -p[0] * sa + p[2] * ca;
        float y = p[1];
        float zc = z + camz;
        if (zc < 0.1f) zc = 0.1f;
        float inv = 1.0f / zc;
        w[i] = inv;
        sx[i] = x * inv * focal + (float)SX / 2.0f;
        sy[i] = -(y * inv * focal) + (float)SY / 2.0f;
    }
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) -
                 (sy[1] - sy[0]) * (sx[2] - sx[0]);
    
    V2_R(R_SETUPMODE) = 0x31u;
    put_vertex(sx[0], sy[0], (float)u0, (float)v0, w[0]);
    V2_R(R_SBTTRI) = 1;
    put_vertex(sx[1], sy[1], (float)u1, (float)v1, w[1]);
    V2_R(R_SDRAWTRI) = 1;
    put_vertex(sx[2], sy[2], (float)u2, (float)v2, w[2]);
    V2_R(R_SDRAWTRI) = 1;
}

void main_c(void) {
    const float ca = 0.9396926f, sa = 0.3420201f;
    voodoo_setup();
    while (1) {
        V2_R(R_COLOR1) = 0x00000000u;
        V2_R(R_FASTFILL) = 1;
        V2_R(R_SETUPMODE) = 0x31u;
        for (int f = 0; f < 6; f++) {
            triangle(face[f][0], face[f][1], face[f][2],
                     uv[f][0][0], uv[f][0][1], uv[f][1][0], uv[f][1][1],
                     uv[f][2][0], uv[f][2][1], ca, sa);
            triangle(face[f][0], face[f][2], face[f][3],
                     uv[f][0][0], uv[f][0][1], uv[f][2][0], uv[f][2][1],
                     uv[f][3][0], uv[f][3][1], ca, sa);
        }
        V2_R(R_SWAP) = 0;
    }
}
