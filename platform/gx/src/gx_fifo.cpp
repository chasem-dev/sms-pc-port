// Register file, command-stream parser and vertex loader.
#include "gx_internal.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include <map>

namespace gx {

State g;
extern uint32_t g_traceLastVat;
void (*drawSyncCallback)(uint16_t) = nullptr;

// ------------------------------------------------------------------ utilities
void logmsg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[sms_gx] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

void fatal(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[sms_gx] fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    abort();
}

uint64_t hashBytes(const void* data, size_t n, uint64_t seed) {
    // 64-bit multiply/xor-shift hash over 8-byte words; not cryptographic.
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = seed ^ (0x9E3779B97F4A7C15ull * (n + 1));
    while (n >= 8) {
        uint64_t w;
        memcpy(&w, p, 8);
        h ^= w * 0xBF58476D1CE4E5B9ull;
        h = (h << 27 | h >> 37) * 0x94D049BB133111EBull;
        p += 8;
        n -= 8;
    }
    uint64_t w = 0;
    memcpy(&w, p, n);
    h ^= w * 0xBF58476D1CE4E5B9ull + n;
    h ^= h >> 31;
    h *= 0xD6E8FEB86659FD93ull;
    h ^= h >> 32;
    return h;
}

// Default window: MEM1 mapped at the GameCube's own cached base, which is what
// the unmodified OSCachedToPhysical macro (addr - 0x80000000) assumes.
static uint8_t* s_memBase = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(0x80000000u));
static uint32_t s_memSize = 0x01800000;

void* physToPtr(uint32_t phys) {
    phys &= 0x3FFFFFFF;
    if (!phys) return nullptr;
    return s_memBase + phys;
}

uint32_t ptrToPhys(const void* p) {
    if (!p) return 0;
    uintptr_t off = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(s_memBase);
    return static_cast<uint32_t>(off);
}

// ------------------------------------------------------------------ big-endian ranges
// Memory holding data in its on-disc byte order (resource files loaded in
// place).  Vertex arrays that start inside such a range are read big-endian.
static std::map<uintptr_t, uintptr_t> s_beRanges;  // start -> end

bool isBigEndianData(const void* p) {
    if (s_beRanges.empty() || !p) return false;
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    auto it = s_beRanges.upper_bound(a);
    if (it == s_beRanges.begin()) return false;
    --it;
    return a < it->second;
}

static inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
static inline uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

// ------------------------------------------------------------------ register writes
bool g_defaultArrayBE = false;
#define s_defaultArrayBE g_defaultArrayBE

void resetState() {
    bool be[16];
    memcpy(be, g.arrayBigEndian, sizeof(be));
    memset(&g, 0, sizeof(g));
    for (int i = 0; i < 16; i++) g.arrayBigEndian[i] = s_defaultArrayBE;
    g.bpMask = 0xFFFFFF;
    for (int i = 0; i < 256; i++) g.bp[i] = 0;
    // identity matrices where the SDK expects them: GX_IDENTITY (row 60) and
    // GX_PTIDENTITY (post matrix 61)
    const float ident[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    for (int i = 0; i < 12; i++) {
        memcpy(&g.xfMem[60 * 4 + i], &ident[i], 4);
        memcpy(&g.xfMem[0x500 + 61 * 4 + i], &ident[i], 4);
    }
    (void)be;
}

void writeBP(uint32_t value) {
    uint32_t reg = value >> 24;
    uint32_t data = value & 0xFFFFFF;
    if (reg == BP_MASK) {
        g.bpMask = data;
        return;
    }
    if (g.bpMask != 0xFFFFFF) {
        data = (g.bp[reg] & ~g.bpMask) | (data & g.bpMask);
        g.bpMask = 0xFFFFFF;
    }
    if (reg >= BP_TEV_REG && reg < BP_TEV_REG + 8 && (data & 0x800000)) {
        if (g.kreg[reg & 7] != data) {
            onStateChange();
            g.kreg[reg & 7] = data;
        }
        return;
    }
    switch (reg) {
    case BP_COPY_EXEC:
        onStateChange();
        g.bp[reg] = data;
        executeCopy(data);
        return;
    case BP_PE_TOKEN_INT:
    case BP_PE_TOKEN:
        g.bp[reg] = data;
        g.drawSyncToken = uint16_t(data & 0xFFFF);
        if (reg == BP_PE_TOKEN_INT && drawSyncCallback) {
            flushBatch();
            drawSyncCallback(g.drawSyncToken);
        }
        return;
    case BP_DRAWDONE:
        flushBatch();
        g.bp[reg] = data;
        return;
    case BP_LOADTLUT1: {
        g.bp[reg] = data;
        uint32_t tmemOff = (data & 0x3FF) << 9;
        uint32_t bytes = ((data >> 10) & 0x7FF) * 32;
        const uint8_t* src = static_cast<const uint8_t*>(physToPtr(g.bp[BP_LOADTLUT0] << 5));
        if (tmemOff + bytes > TMEM_TLUT_SIZE) bytes = TMEM_TLUT_SIZE - tmemOff;
        onStateChange();
        if (src && bytes) memcpy(g.tlutMem + tmemOff, src, bytes);
        return;
    }
    case BP_TEXINVALIDATE:
        g.bp[reg] = data;
        textureInvalidateAll();
        return;
    default:
        break;
    }
    if (g.bp[reg] != data) {
        onStateChange();
        g.bp[reg] = data;
    }
    if (reg == BP_COPY_DST) g.copyDest = physToPtr(data << 5);
    if ((reg >= BP_TX_IMAGE3 && reg < BP_TX_IMAGE3 + 4) || (reg >= BP_TX_IMAGE3 + 0x20 && reg < BP_TX_IMAGE3 + 0x24)) {
        int map = (reg & 3) + ((reg & 0x20) ? 4 : 0);
        g.texImage[map] = static_cast<const uint8_t*>(physToPtr(data << 5));
    }
}

void writeCP(uint8_t reg, uint32_t value) {
    switch (reg & 0xF0) {
    case CP_MATIDX_A: g.cpMatA = value; break;
    case CP_MATIDX_B: g.cpMatB = value; break;
    case CP_VCD_LO: g.cpVcdLo = value; break;
    case CP_VCD_HI: g.cpVcdHi = value; break;
    case CP_VAT_A: g.cpVatA[reg & 7] = value; break;
    case CP_VAT_B: g.cpVatB[reg & 7] = value; break;
    case CP_VAT_C: g.cpVatC[reg & 7] = value; break;
    case CP_ARRAY_BASE:
        g.arrayBase[reg & 15] = static_cast<const uint8_t*>(physToPtr(value));
        g.arrayBigEndian[reg & 15] = s_defaultArrayBE || isBigEndianData(g.arrayBase[reg & 15]);
        break;
    case CP_ARRAY_STRIDE: g.arrayStride[reg & 15] = value & 0xFF; break;
    default: break;
    }
}

void writeXF(uint16_t addr, uint32_t count, const uint32_t* words) {
    for (uint32_t i = 0; i < count; i++, addr++) {
        uint32_t v = words[i];
        if (addr < XF_MEM_WORDS) {
            if (g.xfMem[addr] != v) {
                onStateChange();
                g.xfMem[addr] = v;
                markXfMemDirty();
            }
        } else if (addr >= XF_REG_BASE && addr < XF_REG_BASE + XF_REG_COUNT) {
            uint32_t r = addr - XF_REG_BASE;
            if (g.xfReg[r] != v) {
                onStateChange();
                g.xfReg[r] = v;
            }
        }
    }
}

void writeXFf(uint16_t addr, uint32_t count, const float* f) {
    uint32_t w[16];
    while (count) {
        uint32_t n = count > 16 ? 16 : count;
        memcpy(w, f, n * 4);
        writeXF(addr, n, w);
        addr += n;
        f += n;
        count -= n;
    }
}

// ------------------------------------------------------------------ vertex loader
struct AttrFmt {
    uint8_t mode;   // 0 none, 1 direct, 2 idx8, 3 idx16
    uint8_t cnt, type, frac;
};

struct VtxLayout {
    AttrFmt pnmtx;          // direct u8 only
    AttrFmt texmtx[8];
    AttrFmt pos, nrm, clr[2], tex[8];
    bool nbt, nbt3;
    uint32_t size;
};

static const uint8_t kCompSize[5] = {1, 1, 2, 2, 4};
static const uint8_t kColorSize[6] = {2, 3, 4, 2, 3, 4};

static void buildLayout(int vat, VtxLayout& L) {
    uint32_t lo = g.cpVcdLo, hi = g.cpVcdHi;
    uint32_t A = g.cpVatA[vat], B = g.cpVatB[vat], C = g.cpVatC[vat];
    memset(&L, 0, sizeof(L));
    L.pnmtx.mode = lo & 1;
    for (int i = 0; i < 8; i++) L.texmtx[i].mode = (lo >> (1 + i)) & 1;
    L.pos = {uint8_t((lo >> 9) & 3), uint8_t(A & 1), uint8_t((A >> 1) & 7), uint8_t((A >> 4) & 31)};
    L.nrm = {uint8_t((lo >> 11) & 3), uint8_t((A >> 9) & 1), uint8_t((A >> 10) & 7), 0};
    L.clr[0] = {uint8_t((lo >> 13) & 3), uint8_t((A >> 13) & 1), uint8_t((A >> 14) & 7), 0};
    L.clr[1] = {uint8_t((lo >> 15) & 3), uint8_t((A >> 17) & 1), uint8_t((A >> 18) & 7), 0};
    L.tex[0] = {uint8_t(hi & 3), uint8_t((A >> 21) & 1), uint8_t((A >> 22) & 7), uint8_t((A >> 25) & 31)};
    L.tex[1] = {uint8_t((hi >> 2) & 3), uint8_t(B & 1), uint8_t((B >> 1) & 7), uint8_t((B >> 4) & 31)};
    L.tex[2] = {uint8_t((hi >> 4) & 3), uint8_t((B >> 9) & 1), uint8_t((B >> 10) & 7), uint8_t((B >> 13) & 31)};
    L.tex[3] = {uint8_t((hi >> 6) & 3), uint8_t((B >> 18) & 1), uint8_t((B >> 19) & 7), uint8_t((B >> 22) & 31)};
    L.tex[4] = {uint8_t((hi >> 8) & 3), uint8_t((B >> 27) & 1), uint8_t((B >> 28) & 7), uint8_t(C & 31)};
    L.tex[5] = {uint8_t((hi >> 10) & 3), uint8_t((C >> 5) & 1), uint8_t((C >> 6) & 7), uint8_t((C >> 9) & 31)};
    L.tex[6] = {uint8_t((hi >> 12) & 3), uint8_t((C >> 14) & 1), uint8_t((C >> 15) & 7), uint8_t((C >> 18) & 31)};
    L.tex[7] = {uint8_t((hi >> 14) & 3), uint8_t((C >> 23) & 1), uint8_t((C >> 24) & 7), uint8_t((C >> 27) & 31)};
    L.nbt = L.nrm.cnt == 1;
    L.nbt3 = L.nbt && (A >> 31) != 0;

    uint32_t s = 0;
    s += L.pnmtx.mode;
    for (int i = 0; i < 8; i++) s += L.texmtx[i].mode;
    auto idxSize = [](uint8_t mode) -> uint32_t { return mode == 2 ? 1 : mode == 3 ? 2 : 0; };
    auto comp = [](uint8_t type) -> uint32_t { return type < 5 ? kCompSize[type] : 4; };
    if (L.pos.mode == 1) s += comp(L.pos.type) * (L.pos.cnt ? 3 : 2);
    else s += idxSize(L.pos.mode);
    if (L.nrm.mode == 1) s += comp(L.nrm.type) * (L.nbt ? 9 : 3);
    else s += idxSize(L.nrm.mode) * (L.nbt3 ? 3 : 1);
    for (int c = 0; c < 2; c++) {
        if (L.clr[c].mode == 1) s += L.clr[c].type < 6 ? kColorSize[L.clr[c].type] : 4;
        else s += idxSize(L.clr[c].mode);
    }
    for (int t = 0; t < 8; t++) {
        if (L.tex[t].mode == 1) s += comp(L.tex[t].type) * (L.tex[t].cnt ? 2 : 1);
        else s += idxSize(L.tex[t].mode);
    }
    L.size = s;
}

// reads one component from p in the given byte order
static inline float readComp(const uint8_t* p, uint8_t type, float scale, bool be) {
    switch (type) {
    case 0: return p[0] * scale;
    case 1: return int8_t(p[0]) * scale;
    case 2: {
        uint16_t v = be ? be16(p) : uint16_t(p[0] | p[1] << 8);
        return v * scale;
    }
    case 3: {
        uint16_t v = be ? be16(p) : uint16_t(p[0] | p[1] << 8);
        return int16_t(v) * scale;
    }
    default: {
        uint32_t v = be ? be32(p) : (uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
        float f;
        memcpy(&f, &v, 4);
        return f;
    }
    }
}

static inline void readColor(const uint8_t* p, uint8_t type, bool be, uint8_t out[4]) {
    switch (type) {
    case 0: {  // RGB565
        uint16_t v = be ? be16(p) : uint16_t(p[0] | p[1] << 8);
        out[0] = uint8_t(((v >> 11) & 31) * 255 / 31);
        out[1] = uint8_t(((v >> 5) & 63) * 255 / 63);
        out[2] = uint8_t((v & 31) * 255 / 31);
        out[3] = 255;
        break;
    }
    case 1:  // RGB8
    case 2:  // RGBX8
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255;
        break;
    case 3: {  // RGBA4
        uint16_t v = be ? be16(p) : uint16_t(p[0] | p[1] << 8);
        out[0] = uint8_t(((v >> 12) & 15) * 17);
        out[1] = uint8_t(((v >> 8) & 15) * 17);
        out[2] = uint8_t(((v >> 4) & 15) * 17);
        out[3] = uint8_t((v & 15) * 17);
        break;
    }
    case 4: {  // RGBA6
        uint32_t v = uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
        if (!be) v = uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0];
        out[0] = uint8_t(((v >> 18) & 63) * 255 / 63);
        out[1] = uint8_t(((v >> 12) & 63) * 255 / 63);
        out[2] = uint8_t(((v >> 6) & 63) * 255 / 63);
        out[3] = uint8_t((v & 63) * 255 / 63);
        break;
    }
    default:  // RGBA8: bytes are R,G,B,A in memory order either way
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3];
        break;
    }
}

// attribute array slot for CP array registers
enum { ARR_POS = 0, ARR_NRM = 1, ARR_CLR0 = 2, ARR_TEX0 = 4 };

static const uint8_t* arrayElem(int slot, uint32_t idx) {
    const uint8_t* base = g.arrayBase[slot];
    if (!base) return nullptr;
    return base + size_t(idx) * g.arrayStride[slot];
}

static std::vector<HostVertex> s_verts;
static uint32_t s_vertsLoaded = 0;

static const uint8_t* decodeVertices(uint8_t opcode, const uint8_t* p, uint32_t count, const VtxLayout& L) {
    s_verts.resize(count);
    uint32_t matA = g.xfReg[XFR_MATIDX_A], matB = g.xfReg[XFR_MATIDX_B];
    uint8_t defMtx[9] = {
        uint8_t(matA & 63), uint8_t((matA >> 6) & 63), uint8_t((matA >> 12) & 63), uint8_t((matA >> 18) & 63),
        uint8_t((matA >> 24) & 63), uint8_t(matB & 63), uint8_t((matB >> 6) & 63), uint8_t((matB >> 12) & 63),
        uint8_t((matB >> 18) & 63),
    };
    auto readIndex = [&](uint8_t mode) -> uint32_t {
        uint32_t idx;
        if (mode == 2) { idx = p[0]; p += 1; }
        else { idx = be16(p); p += 2; }
        return idx;
    };
    for (uint32_t v = 0; v < count; v++) {
        HostVertex& hv = s_verts[v];
        memset(&hv, 0, sizeof(hv));
        hv.nrm[2] = 1.0f;
        hv.clr[0][0] = hv.clr[0][1] = hv.clr[0][2] = hv.clr[0][3] = 255;
        hv.clr[1][0] = hv.clr[1][1] = hv.clr[1][2] = hv.clr[1][3] = 255;
        memcpy(hv.mtx, defMtx, 9);
        if (L.pnmtx.mode) hv.mtx[0] = *p++ & 63;
        for (int i = 0; i < 8; i++)
            if (L.texmtx[i].mode) hv.mtx[1 + i] = *p++ & 63;
        // position
        if (L.pos.mode) {
            const uint8_t* src = p;
            bool be = true;
            int n = L.pos.cnt ? 3 : 2;
            uint32_t cs = L.pos.type < 5 ? kCompSize[L.pos.type] : 4;
            if (L.pos.mode == 1) p += cs * n;
            else { src = arrayElem(ARR_POS, readIndex(L.pos.mode)); be = g.arrayBigEndian[ARR_POS]; }
            if (src) {
                float sc = 1.0f / float(1u << L.pos.frac);
                for (int i = 0; i < n; i++) hv.pos[i] = readComp(src + i * cs, L.pos.type, sc, be);
            }
        }
        // normal / NBT
        if (L.nrm.mode) {
            uint32_t cs = L.nrm.type < 5 ? kCompSize[L.nrm.type] : 4;
            float sc = L.nrm.type == 1 ? 1.0f / 64 : L.nrm.type == 3 ? 1.0f / 16384 : L.nrm.type == 0 ? 1.0f / 128 : L.nrm.type == 2 ? 1.0f / 32768 : 1.0f;
            float* dst[3] = {hv.nrm, hv.bin, hv.tan};
            int nvec = L.nbt ? 3 : 1;
            if (L.nrm.mode == 1) {
                for (int k = 0; k < nvec; k++)
                    for (int i = 0; i < 3; i++) dst[k][i] = readComp(p + (k * 3 + i) * cs, L.nrm.type, sc, true);
                p += cs * 3 * nvec;
            } else if (L.nbt3) {
                for (int k = 0; k < 3; k++) {
                    const uint8_t* src = arrayElem(ARR_NRM, readIndex(L.nrm.mode));
                    if (src)
                        for (int i = 0; i < 3; i++)
                            dst[k][i] = readComp(src + i * cs, L.nrm.type, sc, g.arrayBigEndian[ARR_NRM]);
                }
            } else {
                const uint8_t* src = arrayElem(ARR_NRM, readIndex(L.nrm.mode));
                if (src)
                    for (int k = 0; k < nvec; k++)
                        for (int i = 0; i < 3; i++)
                            dst[k][i] = readComp(src + (k * 3 + i) * cs, L.nrm.type, sc, g.arrayBigEndian[ARR_NRM]);
            }
        }
        for (int c = 0; c < 2; c++) {
            if (!L.clr[c].mode) continue;
            if (L.clr[c].mode == 1) {
                readColor(p, L.clr[c].type, true, hv.clr[c]);
                p += L.clr[c].type < 6 ? kColorSize[L.clr[c].type] : 4;
            } else {
                const uint8_t* src = arrayElem(ARR_CLR0 + c, readIndex(L.clr[c].mode));
                if (src) readColor(src, L.clr[c].type, g.arrayBigEndian[ARR_CLR0 + c], hv.clr[c]);
            }
        }
        for (int t = 0; t < 8; t++) {
            const AttrFmt& a = L.tex[t];
            if (!a.mode) continue;
            int n = a.cnt ? 2 : 1;
            uint32_t cs = a.type < 5 ? kCompSize[a.type] : 4;
            const uint8_t* src = p;
            bool be = true;
            if (a.mode == 1) p += cs * n;
            else { src = arrayElem(ARR_TEX0 + t, readIndex(a.mode)); be = g.arrayBigEndian[ARR_TEX0 + t]; }
            if (src) {
                float sc = 1.0f / float(1u << a.frac);
                for (int i = 0; i < n; i++) hv.tex[t][i] = readComp(src + i * cs, a.type, sc, be);
            }
        }
    }
    s_vertsLoaded += count;
    addPrimitive(opcode, s_verts.data(), count);
    return p;
}

// ------------------------------------------------------------------ command parser
static uint32_t s_dlDepth = 0;

// Executes complete commands from [p, p+n).  Returns bytes consumed; if the
// tail holds an incomplete command, *need receives its total length.
static uint32_t parse(const uint8_t* p, uint32_t n, uint32_t* need) {
    const uint8_t* start = p;
    const uint8_t* end = p + n;
    while (p < end) {
        uint8_t op = p[0];
        uint32_t avail = uint32_t(end - p);
        uint32_t len;
        if (op == 0x00 || op == 0x48 || op == 0x44) {  // NOP, invalidate vtx cache, unknown sync
            p++;
            continue;
        } else if (op == 0x08) {
            len = 6;
            if (avail < len) break;
            writeCP(p[1], be32(p + 2));
        } else if (op == 0x10) {
            if (avail < 5) { len = 5; break; }
            uint32_t hdr = be32(p + 1);
            uint32_t cnt = (hdr >> 16) + 1;
            len = 5 + cnt * 4;
            if (avail < len) break;
            uint32_t words[64];
            uint16_t addr = uint16_t(hdr & 0xFFFF);
            for (uint32_t i = 0; i < cnt;) {
                uint32_t m = cnt - i > 64 ? 64 : cnt - i;
                for (uint32_t k = 0; k < m; k++) words[k] = be32(p + 5 + (i + k) * 4);
                writeXF(uint16_t(addr + i), m, words);
                i += m;
            }
        } else if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) {
            len = 5;
            if (avail < len) break;
            uint32_t idx = be16(p + 1);
            uint32_t v = be16(p + 3);
            uint32_t addr = v & 0xFFF, cnt = (v >> 12) + 1;
            int slot = 12 + ((op - 0x20) >> 3);
            const uint8_t* src = arrayElem(slot, idx);
            if (src) {
                uint32_t words[16];
                bool be = g.arrayBigEndian[slot];
                for (uint32_t k = 0; k < cnt; k++)
                    words[k] = be ? be32(src + k * 4)
                                  : (uint32_t(src[k * 4]) | uint32_t(src[k * 4 + 1]) << 8 |
                                     uint32_t(src[k * 4 + 2]) << 16 | uint32_t(src[k * 4 + 3]) << 24);
                writeXF(uint16_t(addr), cnt, words);
            }
        } else if (op == 0x40) {
            len = 9;
            if (avail < len) break;
            const uint8_t* dl = static_cast<const uint8_t*>(physToPtr(be32(p + 1)));
            uint32_t sz = be32(p + 5);
            if (s_dlDepth < 4 && dl) {
                s_dlDepth++;
                runCommands(dl, sz);
                s_dlDepth--;
            }
        } else if (op == 0x61) {
            len = 5;
            if (avail < len) break;
            writeBP(be32(p + 1));
        } else if (op >= 0x80 && op < 0xC0) {
            if (avail < 3) { len = 3; break; }
            VtxLayout L;
            buildLayout(op & 7, L);
            uint32_t cnt = be16(p + 1);
            len = 3 + cnt * L.size;
            if (avail < len) break;
            g_traceLastVat = op & 7;
            decodeVertices(op & 0xF8, p + 3, cnt, L);
        } else {
            logmsg("unknown FIFO opcode 0x%02X, skipping byte", op);
            p++;
            continue;
        }
        p += len;
        continue;
    }
    if (p < end && need) {
        // recompute the length of the incomplete command for the caller
        uint8_t op = p[0];
        uint32_t avail = uint32_t(end - p);
        uint32_t len = 1;
        if (op == 0x08) len = 6;
        else if (op == 0x10) len = avail < 5 ? 5 : 5 + ((be32(p + 1) >> 16) + 1) * 4;
        else if (op >= 0x20 && op <= 0x38) len = 5;
        else if (op == 0x40) len = 9;
        else if (op == 0x61) len = 5;
        else if (op >= 0x80 && op < 0xC0) {
            if (avail < 3) len = 3;
            else {
                VtxLayout L;
                buildLayout(op & 7, L);
                len = 3 + be16(p + 1) * L.size;
            }
        }
        *need = len;
    }
    return uint32_t(p - start);
}

void runCommands(const uint8_t* data, uint32_t size) {
    uint32_t need = 0;
    uint32_t used = parse(data, size, &need);
    if (used < size) logmsg("display list ends inside a command (%u of %u bytes used)", used, size);
}

// ------------------------------------------------------------------ write-gather pipe
static std::vector<uint8_t> s_pipe;
static uint32_t s_pipeNeed = 1;
static uint8_t* s_redirect = nullptr;

void setPipeRedirect(uint8_t* dest) { s_redirect = dest; }
uint8_t* getPipeRedirect() { return s_redirect; }
bool pipeIdle() { return s_pipe.empty(); }

void pipeWrite(const uint8_t* bytes, uint32_t n) {
    if (s_redirect) {
        memcpy(s_redirect, bytes, n);
        s_redirect += n;
        return;
    }
    s_pipe.insert(s_pipe.end(), bytes, bytes + n);
    if (s_pipe.size() < s_pipeNeed) return;
    uint32_t need = 1;
    uint32_t used = parse(s_pipe.data(), uint32_t(s_pipe.size()), &need);
    if (used == s_pipe.size()) {
        s_pipe.clear();
        s_pipeNeed = 1;
    } else {
        s_pipe.erase(s_pipe.begin(), s_pipe.begin() + used);
        s_pipeNeed = need;
    }
}

}  // namespace gx

// ------------------------------------------------------------------ C entry points
using namespace gx;

extern "C" {

void GXPC_SetMemoryWindow(void* base, uint32_t size) {
    s_memBase = static_cast<uint8_t*>(base);
    s_memSize = size;
}

void GXPC_AddBigEndianRange(const void* p, uint32_t size) {
    if (!p || !size) return;
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    s_beRanges[a] = a + size;
}
void GXPC_RemoveBigEndianRange(const void* p) { s_beRanges.erase(reinterpret_cast<uintptr_t>(p)); }
uint32_t GXPC_PtrToPhys(const void* p) { return ptrToPhys(p); }
void* GXPC_PhysToPtr(uint32_t phys) { return physToPtr(phys); }

void GXPC_SetArrayBigEndian(int attr, int bigEndian) {
    int slot;
    if (attr == 25) slot = 1;           // GX_VA_NBT shares the normal array
    else if (attr >= 9 && attr <= 24) slot = attr - 9;
    else return;
    g.arrayBigEndian[slot] = bigEndian != 0;
}
void GXPC_SetDefaultArrayBigEndian(int bigEndian) {
    s_defaultArrayBE = bigEndian != 0;
    for (int i = 0; i < 16; i++) g.arrayBigEndian[i] = s_defaultArrayBE;
}

void GXPC_Write8(uint8_t v) { pipeWrite(&v, 1); }
void GXPC_Write16(uint16_t v) {
    uint8_t b[2] = {uint8_t(v >> 8), uint8_t(v)};
    pipeWrite(b, 2);
}
void GXPC_Write32(uint32_t v) {
    uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    pipeWrite(b, 4);
}
void GXPC_WriteF32(float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    GXPC_Write32(v);
}

}  // extern "C"

#include "sms_gx/gx_pc.h"
const GXPCWGPipe GXPC_WGPipe = {};
