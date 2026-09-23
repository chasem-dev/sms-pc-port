/* Host-side entry points of sms_gx that have no GameCube equivalent.
 *
 * The GX and GD API itself is the one declared by decomp/include/dolphin/gx*.h
 * and gd*.h; sms_gx implements those symbols with C linkage.  This header only
 * adds what a PC host needs: context bring-up, the physical-address window,
 * cache-flush hooks, the write-gather pipe and XFB presentation.
 */
#ifndef SMS_GX_GX_PC_H
#define SMS_GX_GX_PC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* (*GXPCGetProcFn)(const char* name);

/* Call once with an OpenGL 3.3 core context current (SDL, EGL, ...), before the
 * game calls GXInit.  efbScale multiplies the 640x528 EFB (1 = native).
 * Returns 0 on failure (missing entry points, FBO creation failed). */
int GXPC_Init(GXPCGetProcFn getProc, int efbScale);
void GXPC_Shutdown(void);

/* 32-bit "physical" addresses appear inside command streams: display lists
 * built by GD/J3D carry texture, TLUT, vertex-array and EFB-copy addresses.
 * sms_gx maps them as phys = ptr - base.  The default base is 0x80000000,
 * matching the unmodified OSCachedToPhysical macro and a MEM1 arena mapped at
 * the GameCube address; call this only if emulated MEM1 lives elsewhere, and
 * keep OSCachedToPhysical consistent with it.  API calls (GXInitTexObj,
 * GXSetArray, GXCopyTex, ...) keep full host pointers and work for memory
 * outside the window too. */
void GXPC_SetMemoryWindow(void* base, uint32_t size);
uint32_t GXPC_PtrToPhys(const void* ptr);
void* GXPC_PhysToPtr(uint32_t phys);

/* Cache hooks: call from DCFlushRange/DCStoreRange (and DCInvalidateRange) so
 * textures the CPU rewrote are re-decoded.  GXInvalidateTexAll re-checks every
 * cached texture's contents as well. */
void GXPC_InvalidateRange(const void* ptr, uint32_t size);

/* Byte order of indexed vertex / matrix arrays (GXSetArray).  Display lists and
 * pipe data are always big-endian; arrays default to host order because most
 * are written by host code.  Arrays kept in their on-disc byte order (e.g. raw
 * BMD vertex data) must be flagged big-endian.  attr is a GXAttr value
 * (GX_VA_POS..GX_VA_TEX7, GX_POS_MTX_ARRAY..GX_LIGHT_ARRAY, GX_VA_NBT). */
void GXPC_SetArrayBigEndian(int attr, int bigEndian);
void GXPC_SetDefaultArrayBigEndian(int bigEndian);
/* Preferred: tell sms_gx which memory holds on-disc (big-endian) data, e.g.
 * every resource file loaded in place.  GXSetArray / CP array bases that point
 * into a registered range are then read big-endian automatically. */
void GXPC_AddBigEndianRange(const void* ptr, uint32_t size);
void GXPC_RemoveBigEndianRange(const void* ptr);

/* The write-gather pipe (0xCC008000).  GXVert.h's inline writers and direct
 * `GXWGFifo.u8 = ...` stores must end up here on PC; see README.md. */
void GXPC_Write8(uint8_t v);
void GXPC_Write16(uint16_t v);
void GXPC_Write32(uint32_t v);
void GXPC_WriteF32(float v);

/* Presentation.  GXCopyDisp(dest, ...) records the EFB region as the XFB at
 * `dest`.  PresentXFB draws that XFB into the currently bound default
 * framebuffer (window size winW x winH, letterboxed) - the VI layer calls it
 * on retrace for the buffer given to VISetNextFrameBuffer, then swaps.
 * xfb == NULL presents the most recent display copy.  Returns 0 if unknown. */
int GXPC_PresentXFB(const void* xfb, int winW, int winH);

/* Debug/test access: read back the EFB (RGBA8, top row first) or the XFB
 * texture at `xfb` (NULL = most recent).  Buffers are w*h*4 bytes; pass NULL to
 * query the size only. */
void GXPC_ReadEFB(uint8_t* rgba, int* w, int* h);
int GXPC_ReadXFB(const void* xfb, uint8_t* rgba, int* w, int* h);

/* Statistics for the current frame (reset by GXCopyDisp). */
typedef struct GXPCStats {
    uint32_t draws, vertices, shaderCompiles, textureUploads, efbCopies;
} GXPCStats;
void GXPC_GetStats(GXPCStats* out);

#ifdef __cplusplus
}

/* C++ replacement for the write-gather pipe union: `GXWGFifo.f32 = x` becomes
 * a call into the pipe.  To use it, GXVert.h (TARGET_PC) does
 *     #define GXWGFifo GXPC_WGPipe
 * instead of the 0xCC008000 cast. */
struct GXPCWGPipe {
    struct U8 { void operator=(uint8_t v) const { GXPC_Write8(v); } } u8;
    struct S8 { void operator=(int8_t v) const { GXPC_Write8((uint8_t)v); } } s8;
    struct U16 { void operator=(uint16_t v) const { GXPC_Write16(v); } } u16;
    struct S16 { void operator=(int16_t v) const { GXPC_Write16((uint16_t)v); } } s16;
    struct U32 { void operator=(uint32_t v) const { GXPC_Write32(v); } } u32;
    struct S32 { void operator=(int32_t v) const { GXPC_Write32((uint32_t)v); } } s32;
    struct F32 { void operator=(float v) const { GXPC_WriteF32(v); } } f32;
};
extern const GXPCWGPipe GXPC_WGPipe;
#endif

#endif
