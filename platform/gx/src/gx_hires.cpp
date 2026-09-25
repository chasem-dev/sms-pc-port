// Custom ("HD") textures in Dolphin's texture pack format.
//
// A pack is a folder of images named after the GX texture they replace:
//   tex1_<w>x<h>[_m]_<data hash>[_<palette hash>]_<format>.png
// <w>x<h> and <format> are the GX texture's size and format number, _m marks a
// texture sampled with mipmaps, and the hashes are XXH64 (seed 0) of the base
// level's bytes as they sit in memory and, for colour-indexed formats, of the
// palette entries the texture uses ("$" in place of the palette hash matches
// any palette). A file ending in _mip<n> is level n of the texture named by
// the rest; one ending in _arb has hand-made ("arbitrary") mipmaps, so its
// levels are not regenerated. Every pack made for Dolphin's "Load Custom
// Textures" works unchanged (the folder is usually named after the game ID,
// GMS or GMSE01), since it depends only on the texture data the game loads.
//
// Packs are found under the directories in SMS_TEXTURE_PACKS (separated by ':'
// or ';'), else under mods/textures/ in the working directory (or two levels
// up, when started from build/<os>-<arch>/). SMS_TEXTURE_PACKS=0 disables
// them. Images decode on a worker thread: a texture shows its original until
// its replacement is ready.
#include "gx_internal.h"
#include "gl_funcs.h"
#include "gx_glcache.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO_OVERRIDE
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "third_party/stb_image.h"

namespace gx {

// ------------------------------------------------------------------ XXH64
// From the xxHash specification (Yann Collet): 64-bit lanes over 32-byte
// stripes, then the tail and the avalanche.
static const uint64_t P1 = 0x9E3779B185EBCA87ull, P2 = 0xC2B2AE3D27D4EB4Full, P3 = 0x165667B19E3779F9ull,
                      P4 = 0x85EBCA77C2B2AE63ull, P5 = 0x27D4EB2F165667C5ull;
static inline uint64_t rotl(uint64_t x, int r) { return x << r | x >> (64 - r); }
static inline uint64_t rd64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}
static inline uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
static inline uint64_t xround(uint64_t acc, uint64_t in) { return rotl(acc + in * P2, 31) * P1; }
static inline uint64_t xmerge(uint64_t acc, uint64_t v) { return (acc ^ xround(0, v)) * P1 + P4; }

uint64_t xxh64(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    const uint8_t* end = p + len;
    uint64_t h;
    if (len >= 32) {
        uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        do {
            v1 = xround(v1, rd64(p));
            v2 = xround(v2, rd64(p + 8));
            v3 = xround(v3, rd64(p + 16));
            v4 = xround(v4, rd64(p + 24));
            p += 32;
        } while (end - p >= 32);
        h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
        h = xmerge(h, v1);
        h = xmerge(h, v2);
        h = xmerge(h, v3);
        h = xmerge(h, v4);
    } else {
        h = seed + P5;
    }
    h += uint64_t(len);
    while (end - p >= 8) {
        h ^= xround(0, rd64(p));
        h = rotl(h, 27) * P1 + P4;
        p += 8;
    }
    if (end - p >= 4) {
        h ^= uint64_t(rd32(p)) * P1;
        h = rotl(h, 23) * P2 + P3;
        p += 4;
    }
    while (p < end) {
        h ^= uint64_t(*p++) * P5;
        h = rotl(h, 11) * P1;
    }
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    h *= P3;
    h ^= h >> 32;
    return h;
}

// ------------------------------------------------------------------ the index
struct PackFile {
    std::string path;               // level 0
    std::vector<std::string> mips;  // explicit levels 1.. (_mip<n>), may have gaps
    bool arbitrary = false;
};
static std::unordered_map<std::string, PackFile> s_index;  // key: the texture name
static int s_state = -1;                                   // -1 not scanned, 0 off, 1 on
static uint32_t s_uploaded = 0;                            // replacements in GL

static bool endsWith(const std::string& s, const char* suf) {
    size_t n = strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

static void indexFile(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(tolower(c));
    if (ext != ".png") return;  // DDS is not read yet
    std::string stem = p.stem().string();
    if (stem.compare(0, 5, "tex1_") != 0) return;
    bool arb = false;
    if (endsWith(stem, "_arb")) {
        arb = true;
        stem.resize(stem.size() - 4);
    }
    int level = 0;
    size_t m = stem.rfind("_mip");
    if (m != std::string::npos && m + 4 < stem.size() &&
        stem.find_first_not_of("0123456789", m + 4) == std::string::npos) {
        level = atoi(stem.c_str() + m + 4);
        stem.resize(m);
    }
    PackFile& f = s_index[stem];
    if (level == 0) {
        if (f.path.empty()) f.path = p.string();
        f.arbitrary = f.arbitrary || arb;
    } else {
        if (f.mips.size() < size_t(level)) f.mips.resize(size_t(level));
        if (f.mips[size_t(level) - 1].empty()) f.mips[size_t(level) - 1] = p.string();
    }
}

static void scan() {
    s_state = 0;
    std::vector<std::string> dirs;
    const char* e = getenv("SMS_TEXTURE_PACKS");
    if (e && strcmp(e, "0") == 0) return;
    if (e && *e) {
        std::string all = e, cur;
        for (char c : all + ";") {
            if (c == ';' || (c == ':' && !(cur.size() == 1 && isalpha(uint8_t(cur[0]))))) {
                if (!cur.empty()) dirs.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
    } else {
        dirs = {"mods/textures", "../../mods/textures"};
    }
    namespace fs = std::filesystem;
    for (const std::string& d : dirs) {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        size_t before = s_index.size();
        for (auto it = fs::recursive_directory_iterator(d, fs::directory_options::follow_directory_symlink, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec)) indexFile(it->path());
        logmsg("texture pack: %zu textures under %s", s_index.size() - before, d.c_str());
    }
    for (auto it = s_index.begin(); it != s_index.end();)  // mips without a level 0
        it = it->second.path.empty() ? s_index.erase(it) : std::next(it);
    s_state = s_index.empty() ? 0 : 1;
}

bool hiresEnabled() {
    if (s_state < 0) scan();
    return s_state == 1;
}

// SMS_TEXTURE_PACK_LOG=1 logs the pack name of every texture the game loads,
// and whether the pack replaced it (for checking or making a pack).
bool hiresLog() {
    static int on = -1;
    if (on < 0) on = getenv("SMS_TEXTURE_PACK_LOG") && atoi(getenv("SMS_TEXTURE_PACK_LOG")) != 0;
    return on != 0;
}

// ------------------------------------------------------------------ names
// The palette entries a colour-indexed texture's base level uses.
static void paletteRange(const uint8_t* data, size_t bytes, uint32_t fmt, uint32_t* lo, uint32_t* hi) {
    uint32_t mn = 0xFFFF, mx = 0;
    if (fmt == 8) {  // C4: two indices per byte
        for (size_t i = 0; i < bytes; i++) {
            uint32_t a = data[i] & 15, b = data[i] >> 4;
            mn = std::min(mn, std::min(a, b));
            mx = std::max(mx, std::max(a, b));
        }
    } else if (fmt == 9) {  // C8
        for (size_t i = 0; i < bytes; i++) {
            mn = std::min<uint32_t>(mn, data[i]);
            mx = std::max<uint32_t>(mx, data[i]);
        }
    } else {  // C14X2: big-endian halfwords
        for (size_t i = 0; i + 1 < bytes; i += 2) {
            uint32_t v = (uint32_t(data[i]) << 8 | data[i + 1]) & 0x3FFF;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
    }
    if (mn > mx) mn = mx = 0;
    *lo = mn;
    *hi = mx;
}

std::string hiresName(const uint8_t* data, uint32_t fmt, uint32_t w, uint32_t h, bool mipmapped, const uint8_t* tlut,
                      uint32_t tlutBytes, bool onlyIfPresent) {
    size_t bytes = texLevelBytes(fmt, w, h);
    char base[96];
    snprintf(base, sizeof base, "tex1_%ux%u%s_%016llx", w, h, mipmapped ? "_m" : "",
             (unsigned long long)xxh64(data, bytes, 0));
    char fmtPart[8];
    snprintf(fmtPart, sizeof fmtPart, "_%u", fmt);
    bool ci = fmt == 8 || fmt == 9 || fmt == 10;
    if (ci && tlut) {
        uint32_t lo, hi;
        paletteRange(data, bytes, fmt, &lo, &hi);
        uint32_t off = 2 * lo, len = 2 * (hi + 1 - lo);
        if (off + len > tlutBytes) len = off < tlutBytes ? tlutBytes - off : 0;
        char tl[24];
        snprintf(tl, sizeof tl, "_%016llx", (unsigned long long)xxh64(tlut + off, len, 0));
        std::string full = std::string(base) + tl + fmtPart;
        if (!onlyIfPresent || s_index.count(full)) return full;
        std::string wild = std::string(base) + "_$" + fmtPart;
        if (s_index.count(wild)) return wild;
    }
    std::string plain = std::string(base) + fmtPart;
    if (!onlyIfPresent || s_index.count(plain)) return plain;
    return std::string();
}

// ------------------------------------------------------------------ loading
struct Loaded {
    int w = 0, h = 0;
    std::vector<std::vector<uint8_t>> levels;  // RGBA8; levels[0] always present
    bool arbitrary = false;
    bool failed = false;
};

// Each replacement is decoded once, on a worker thread, uploaded into its own
// GL texture on the render thread and its pixels freed; every cache entry
// whose data has that name samples the one texture.
struct Replacement {
    enum { QUEUED, READY, FAILED } state = QUEUED;
    GLuint tex = 0;
    int scale = 0;  // log2 of its size over the GX size
};
static std::unordered_map<std::string, Replacement> s_repl;  // render thread only
static std::mutex s_mu;
static std::condition_variable s_cv;
static std::deque<std::string> s_queue;                        // to decode
static std::vector<std::pair<std::string, Loaded*>> s_decoded;  // decoded, to upload
static bool s_workerStarted = false;

static Loaded* decode(const PackFile& f) {
    Loaded* L = new Loaded;
    L->arbitrary = f.arbitrary;
    int n = 0;
    uint8_t* px = stbi_load(f.path.c_str(), &L->w, &L->h, &n, 4);
    if (!px) {
        logmsg("texture pack: cannot read %s (%s)", f.path.c_str(), stbi_failure_reason());
        L->failed = true;
        return L;
    }
    L->levels.emplace_back(px, px + size_t(L->w) * L->h * 4);
    stbi_image_free(px);
    int lw = L->w, lh = L->h;
    for (const std::string& mp : f.mips) {  // explicit levels, in order, while their sizes fit
        lw = std::max(1, lw / 2);
        lh = std::max(1, lh / 2);
        int mw, mh;
        uint8_t* m = mp.empty() ? nullptr : stbi_load(mp.c_str(), &mw, &mh, &n, 4);
        if (!m || mw != lw || mh != lh) {
            if (m) stbi_image_free(m);
            break;
        }
        L->levels.emplace_back(m, m + size_t(mw) * mh * 4);
        stbi_image_free(m);
    }
    return L;
}

static void worker() {
    for (;;) {
        std::string name;
        {
            std::unique_lock<std::mutex> lk(s_mu);
            s_cv.wait(lk, [] { return !s_queue.empty(); });
            name = s_queue.front();
            s_queue.pop_front();
        }
        Loaded* L = decode(s_index.at(name));  // the index is read-only after the scan
        std::lock_guard<std::mutex> lk(s_mu);
        s_decoded.emplace_back(name, L);
    }
}

static void upload(Replacement& r, Loaded* L, int unit, uint32_t gxW, uint32_t gxH) {
    if (L->failed) {
        r.state = Replacement::FAILED;
        return;
    }
    int scale = 0;
    while (scale < 6 && (gxW << (scale + 1)) <= uint32_t(L->w) && (gxH << (scale + 1)) <= uint32_t(L->h)) scale++;
    glGenTextures(1, &r.tex);
    glcBindTexture(unit, r.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    int lw = L->w, lh = L->h, n = 0;
    for (const auto& lv : L->levels) {
        glTexImage2D(GL_TEXTURE_2D, n, GL_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, lv.data());
        lw = std::max(1, lw / 2);
        lh = std::max(1, lh / 2);
        n++;
    }
    // A full chain: the sampler picks the levels the original would use (and
    // stops a plain texture's upscale at its original size); the pack's own
    // levels are kept when it has them.
    int full = 1;
    for (int m = std::max(L->w, L->h); m > 1; m >>= 1) full++;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    if (L->levels.size() == 1 && full > 1) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, full - 1);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, int(L->levels.size()) - 1);
    }
    r.scale = scale;
    r.state = Replacement::READY;
    s_uploaded++;
}

// The GL texture replacing `name` (a name hiresName found in the pack), or 0
// while it is still decoding or if it cannot be read. Uploading binds it on
// `unit`. SMS_TEXTURE_PACK_SYNC=1 decodes on the spot (repeatable captures).
GLuint hiresTexture(const std::string& name, int unit, uint32_t gxW, uint32_t gxH, int* scale) {
    static int sync = -1;
    if (sync < 0) sync = getenv("SMS_TEXTURE_PACK_SYNC") && atoi(getenv("SMS_TEXTURE_PACK_SYNC")) != 0;
    auto it = s_repl.find(name);
    if (it == s_repl.end()) {
        Replacement& r = s_repl[name];
        if (sync) {
            Loaded* L = decode(s_index.at(name));
            upload(r, L, unit, gxW, gxH);
            delete L;
        } else {
            std::lock_guard<std::mutex> lk(s_mu);
            s_queue.push_back(name);
            if (!s_workerStarted) {
                s_workerStarted = true;
                std::thread(worker).detach();
            }
            s_cv.notify_one();
        }
        it = s_repl.find(name);
    } else if (it->second.state == Replacement::QUEUED) {
        std::vector<std::pair<std::string, Loaded*>> done;
        {
            std::lock_guard<std::mutex> lk(s_mu);
            for (size_t i = 0; i < s_decoded.size(); i++)
                if (s_decoded[i].first == name) {
                    done.push_back(s_decoded[i]);
                    s_decoded.erase(s_decoded.begin() + long(i));
                    break;
                }
        }
        for (auto& d : done) {
            upload(it->second, d.second, unit, gxW, gxH);
            delete d.second;
        }
    }
    if (it->second.state != Replacement::READY) return 0;
    *scale = it->second.scale;
    return it->second.tex;
}

void hiresShutdown() {
    for (auto& kv : s_repl)
        if (kv.second.tex) {
            glcForgetTexture(kv.second.tex);
            glDeleteTextures(1, &kv.second.tex);
        }
    s_repl.clear();
}

uint32_t hiresUploadedCount() { return s_uploaded; }

}  // namespace gx
