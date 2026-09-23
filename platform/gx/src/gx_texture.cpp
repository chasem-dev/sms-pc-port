// Texture decoding (GameCube tiled formats -> RGBA8) and the GL texture cache.
#include "gx_internal.h"
#include "gl_funcs.h"

#include <string.h>
#include <unordered_map>

namespace gx {

extern uint32_t g_statTexUploads;

static inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

static inline void rgb565(uint16_t v, uint8_t* o) {
    o[0] = uint8_t((v >> 11) << 3 | (v >> 13));
    o[1] = uint8_t(((v >> 5) & 63) << 2 | ((v >> 9) & 3));
    o[2] = uint8_t((v & 31) << 3 | ((v >> 2) & 7));
    o[3] = 255;
}
static inline void rgb5a3(uint16_t v, uint8_t* o) {
    if (v & 0x8000) {
        o[0] = uint8_t(((v >> 10) & 31) << 3 | ((v >> 12) & 7));
        o[1] = uint8_t(((v >> 5) & 31) << 3 | ((v >> 7) & 7));
        o[2] = uint8_t((v & 31) << 3 | ((v >> 2) & 7));
        o[3] = 255;
    } else {
        o[0] = uint8_t(((v >> 8) & 15) * 17);
        o[1] = uint8_t(((v >> 4) & 15) * 17);
        o[2] = uint8_t((v & 15) * 17);
        o[3] = uint8_t(((v >> 12) & 7) * 255 / 7);
    }
}
static inline void ia8(uint16_t v, uint8_t* o) {
    o[0] = o[1] = o[2] = uint8_t(v & 0xFF);
    o[3] = uint8_t(v >> 8);
}

static const uint8_t kTileW[16] = {8, 8, 8, 4, 4, 4, 4, 0, 8, 8, 4, 0, 0, 0, 8, 0};
static const uint8_t kTileH[16] = {8, 4, 4, 4, 4, 4, 4, 0, 8, 4, 4, 0, 0, 0, 8, 0};
static const uint8_t kTileBytes[16] = {32, 32, 32, 32, 32, 32, 64, 0, 32, 32, 32, 0, 0, 0, 32, 0};

uint32_t texLevelBytes(uint32_t fmt, uint32_t w, uint32_t h) {
    fmt &= 15;
    if (!kTileW[fmt]) return 0;
    uint32_t cols = (w + kTileW[fmt] - 1) / kTileW[fmt];
    uint32_t rows = (h + kTileH[fmt] - 1) / kTileH[fmt];
    return cols * rows * kTileBytes[fmt];
}

static void palette(const uint8_t* tlut, uint32_t tlutFmt, uint32_t idx, uint8_t* o) {
    uint16_t v = be16(tlut + 2 * idx);
    if (tlutFmt == 0) ia8(v, o);
    else if (tlutFmt == 1) rgb565(v, o);
    else rgb5a3(v, o);
}

// Decodes one mip level into out (w*h*4 bytes, row-major, top row first).
void decodeTexture(const uint8_t* src, uint32_t fmt, uint32_t w, uint32_t h, const uint8_t* tlut, uint32_t tlutFmt,
                   uint8_t* out) {
    fmt &= 15;
    uint32_t tw = kTileW[fmt], th = kTileH[fmt];
    if (!tw) {
        memset(out, 0xFF, size_t(w) * h * 4);
        return;
    }
    uint32_t cols = (w + tw - 1) / tw, rows = (h + th - 1) / th;
    const uint8_t* p = src;
    for (uint32_t ty = 0; ty < rows; ty++) {
        for (uint32_t tx = 0; tx < cols; tx++) {
            uint32_t x0 = tx * tw, y0 = ty * th;
            auto put = [&](uint32_t x, uint32_t y, const uint8_t* c) {
                if (x < w && y < h) memcpy(out + (size_t(y) * w + x) * 4, c, 4);
            };
            uint8_t c[4];
            switch (fmt) {
            case 0:  // I4
                for (uint32_t i = 0; i < 64; i++) {
                    uint8_t b = p[i >> 1];
                    uint8_t v = (i & 1) ? (b & 15) : (b >> 4);
                    c[0] = c[1] = c[2] = c[3] = uint8_t(v * 17);
                    put(x0 + (i & 7), y0 + (i >> 3), c);
                }
                break;
            case 1:  // I8
                for (uint32_t i = 0; i < 32; i++) {
                    c[0] = c[1] = c[2] = c[3] = p[i];
                    put(x0 + (i & 7), y0 + (i >> 3), c);
                }
                break;
            case 2:  // IA4
                for (uint32_t i = 0; i < 32; i++) {
                    c[0] = c[1] = c[2] = uint8_t((p[i] & 15) * 17);
                    c[3] = uint8_t((p[i] >> 4) * 17);
                    put(x0 + (i & 7), y0 + (i >> 3), c);
                }
                break;
            case 3:  // IA8
                for (uint32_t i = 0; i < 16; i++) {
                    ia8(be16(p + 2 * i), c);
                    put(x0 + (i & 3), y0 + (i >> 2), c);
                }
                break;
            case 4:  // RGB565
                for (uint32_t i = 0; i < 16; i++) {
                    rgb565(be16(p + 2 * i), c);
                    put(x0 + (i & 3), y0 + (i >> 2), c);
                }
                break;
            case 5:  // RGB5A3
                for (uint32_t i = 0; i < 16; i++) {
                    rgb5a3(be16(p + 2 * i), c);
                    put(x0 + (i & 3), y0 + (i >> 2), c);
                }
                break;
            case 6:  // RGBA8: AR plane then GB plane
                for (uint32_t i = 0; i < 16; i++) {
                    c[3] = p[2 * i];
                    c[0] = p[2 * i + 1];
                    c[1] = p[32 + 2 * i];
                    c[2] = p[32 + 2 * i + 1];
                    put(x0 + (i & 3), y0 + (i >> 2), c);
                }
                break;
            case 8:  // C4
                for (uint32_t i = 0; i < 64; i++) {
                    uint8_t b = p[i >> 1];
                    palette(tlut, tlutFmt, (i & 1) ? (b & 15) : (b >> 4), c);
                    put(x0 + (i & 7), y0 + (i >> 3), c);
                }
                break;
            case 9:  // C8
                for (uint32_t i = 0; i < 32; i++) {
                    palette(tlut, tlutFmt, p[i], c);
                    put(x0 + (i & 7), y0 + (i >> 3), c);
                }
                break;
            case 10:  // C14X2
                for (uint32_t i = 0; i < 16; i++) {
                    palette(tlut, tlutFmt, be16(p + 2 * i) & 0x3FFF, c);
                    put(x0 + (i & 3), y0 + (i >> 2), c);
                }
                break;
            case 14:  // CMPR: 2x2 DXT1 sub-blocks
                for (uint32_t sb = 0; sb < 4; sb++) {
                    const uint8_t* b = p + sb * 8;
                    uint16_t c0 = be16(b), c1 = be16(b + 2);
                    uint8_t pal[4][4];
                    rgb565(c0, pal[0]);
                    rgb565(c1, pal[1]);
                    if (c0 > c1) {
                        for (int k = 0; k < 3; k++) {
                            pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3);
                            pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3);
                        }
                        pal[2][3] = pal[3][3] = 255;
                    } else {
                        for (int k = 0; k < 3; k++) {
                            pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2);
                            pal[3][k] = 0;
                        }
                        pal[2][3] = 255;
                        pal[3][3] = 0;
                    }
                    uint32_t sx = x0 + (sb & 1) * 4, sy = y0 + (sb >> 1) * 4;
                    for (uint32_t r = 0; r < 4; r++) {
                        uint8_t bits = b[4 + r];
                        for (uint32_t q = 0; q < 4; q++) put(sx + q, sy + r, pal[(bits >> (6 - 2 * q)) & 3]);
                    }
                }
                break;
            default:
                break;
            }
            p += kTileBytes[fmt];
        }
    }
}

// ------------------------------------------------------------------ cache
struct TexKey {
    const void* ptr;
    uint32_t fmt, w, h, levels, tlutOff, tlutFmt;
    bool operator==(const TexKey& o) const {
        return ptr == o.ptr && fmt == o.fmt && w == o.w && h == o.h && levels == o.levels && tlutOff == o.tlutOff &&
               tlutFmt == o.tlutFmt;
    }
};
struct TexKeyHash {
    size_t operator()(const TexKey& k) const { return size_t(hashBytes(&k, sizeof(k))); }
};
struct TexEntry {
    GLuint tex = 0;
    uint64_t dataHash = 0, tlutHash = 0;
    uint32_t checkedGen = 0;
    uint32_t bytes = 0;
};

static std::unordered_map<TexKey, TexEntry, TexKeyHash> s_cache;
static uint32_t s_gen = 1;
static std::vector<uint8_t> s_decodeBuf;

struct CopyEntry {
    GLuint tex;
    int w, h;
    uint32_t fmt;
};
static std::unordered_map<const void*, CopyEntry> s_copies;

void textureInvalidateAll() { s_gen++; }

void textureInvalidateRange(const void* p, uint32_t size) {
    const uint8_t* lo = static_cast<const uint8_t*>(p);
    const uint8_t* hi = lo + size;
    for (auto& kv : s_cache) {
        const uint8_t* a = static_cast<const uint8_t*>(kv.first.ptr);
        if (a < hi && a + kv.second.bytes > lo) kv.second.checkedGen = 0;
    }
}

void textureShutdown() {
    for (auto& kv : s_cache) glDeleteTextures(1, &kv.second.tex);
    s_cache.clear();
    for (auto& kv : s_copies) glDeleteTextures(1, &kv.second.tex);
    s_copies.clear();
}

unsigned efbCopyLookup(const void* addr, int* w, int* h) {
    auto it = s_copies.find(addr);
    if (it == s_copies.end()) return 0;
    if (w) *w = it->second.w;
    if (h) *h = it->second.h;
    return it->second.tex;
}

void efbCopyRegister(const void* addr, unsigned tex, int w, int h, uint32_t fmt) {
    auto it = s_copies.find(addr);
    if (it != s_copies.end() && it->second.tex != tex) glDeleteTextures(1, &it->second.tex);
    s_copies[addr] = CopyEntry{tex, w, h, fmt};
}

static void applySampler(uint32_t mode0, uint32_t mode1, uint32_t levels) {
    static const GLint wrap[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap[mode0 & 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap[(mode0 >> 2) & 3]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (mode0 >> 4) & 1 ? GL_LINEAR : GL_NEAREST);
    uint32_t mf = (mode0 >> 5) & 7;
    bool lin = (mf & 4) != 0;
    uint32_t mip = mf & 3;  // 0 none, 1 nearest mip, 2 linear mip
    GLint minf;
    if (levels <= 1 || mip == 0) minf = lin ? GL_LINEAR : GL_NEAREST;
    else if (mip == 1) minf = lin ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    else minf = lin ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, float(int8_t((mode0 >> 9) & 0xFF)) / 32.0f);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, float(mode1 & 0xFF) / 16.0f);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, float((mode1 >> 8) & 0xFF) / 16.0f);
}

static GLuint s_whiteTex = 0;

unsigned bindTextureMap(int map, float* outW, float* outH) {
    uint32_t mode0 = g.bp[bpTexReg(BP_TX_MODE0, map)];
    uint32_t mode1 = g.bp[bpTexReg(BP_TX_MODE1, map)];
    uint32_t img0 = g.bp[bpTexReg(BP_TX_IMAGE0, map)];
    uint32_t tlutReg = g.bp[bpTexReg(BP_TX_TLUT, map)];
    uint32_t w = (img0 & 0x3FF) + 1, h = ((img0 >> 10) & 0x3FF) + 1, fmt = (img0 >> 20) & 15;
    const uint8_t* ptr = g.texImage[map];
    *outW = float(w);
    *outH = float(h);

    int cw, ch;
    if (GLuint ct = efbCopyLookup(ptr, &cw, &ch)) {
        glBindTexture(GL_TEXTURE_2D, ct);
        applySampler(mode0 & ~(7u << 5) | ((mode0 & (1u << 7)) ? (4u << 5) : 0), 0, 1);
        return ct;
    }
    if (!ptr) {
        if (!s_whiteTex) {
            glGenTextures(1, &s_whiteTex);
            glBindTexture(GL_TEXTURE_2D, s_whiteTex);
            uint32_t px = 0xFFFFFFFFu;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px);
        }
        glBindTexture(GL_TEXTURE_2D, s_whiteTex);
        return s_whiteTex;
    }

    // mip levels present: GX has no explicit count, derive it from max LOD
    uint32_t levels = 1;
    if (((mode0 >> 5) & 3) != 0) {
        uint32_t maxLod = ((mode1 >> 8) & 0xFF) / 16;
        uint32_t m = w > h ? w : h;
        while (levels <= maxLod && (m >> levels) >= 1) levels++;
        if (levels > 11) levels = 11;
    }
    bool ci = fmt == 8 || fmt == 9 || fmt == 10;
    TexKey key{ptr, fmt, w, h, levels, ci ? (tlutReg & 0x3FF) : 0, ci ? ((tlutReg >> 10) & 3) : 0};
    const uint8_t* tlut = ci ? g.tlutMem + ((tlutReg & 0x3FF) << 9) : nullptr;
    uint32_t tlutBytes = fmt == 8 ? 32 : fmt == 9 ? 512 : 0x8000;
    if (tlut && ((tlutReg & 0x3FF) << 9) + tlutBytes > TMEM_TLUT_SIZE) tlutBytes = TMEM_TLUT_SIZE - ((tlutReg & 0x3FF) << 9);

    TexEntry& e = s_cache[key];
    bool upload = false;
    uint32_t total = 0;
    {
        uint32_t lw = w, lh = h;
        for (uint32_t l = 0; l < levels; l++) {
            total += texLevelBytes(fmt, lw, lh);
            lw = lw > 1 ? lw / 2 : 1;
            lh = lh > 1 ? lh / 2 : 1;
        }
    }
    if (!e.tex) {
        glGenTextures(1, &e.tex);
        upload = true;
    }
    if (e.checkedGen != s_gen || upload) {
        uint64_t dh = hashBytes(ptr, total);
        uint64_t th = tlut ? hashBytes(tlut, tlutBytes) : 0;
        if (upload || dh != e.dataHash || th != e.tlutHash) upload = true;
        e.dataHash = dh;
        e.tlutHash = th;
        e.checkedGen = s_gen;
        e.bytes = total;
    }
    glBindTexture(GL_TEXTURE_2D, e.tex);
    if (upload) {
        g_statTexUploads++;
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const uint8_t* src = ptr;
        uint32_t lw = w, lh = h;
        for (uint32_t l = 0; l < levels; l++) {
            s_decodeBuf.resize(size_t(lw) * lh * 4);
            decodeTexture(src, fmt, lw, lh, tlut, (tlutReg >> 10) & 3, s_decodeBuf.data());
            glTexImage2D(GL_TEXTURE_2D, GLint(l), GL_RGBA8, GLsizei(lw), GLsizei(lh), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         s_decodeBuf.data());
            src += texLevelBytes(fmt, lw, lh);
            lw = lw > 1 ? lw / 2 : 1;
            lh = lh > 1 ? lh / 2 : 1;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(levels - 1));
    }
    applySampler(mode0, mode1, levels);
    return e.tex;
}

}  // namespace gx
