// The radar's stage maps (src/radar.cpp): top-down pictures of each stage,
// made from the disc by tools/radar/gen_maps.py.
//
// The folder (SMS_RADAR_DIR, else assets/maps beside or two folders above
// the working directory, as the game runs from build/<os>-<arch>/) holds
// maps.txt, one line per map:
//     <col key> <file> <x0> <z0> <pixels per unit> <width> <height> <scenes>
// The game hashes the stage's loaded collision (/scene/map/map.col) and asks
// for that map; the picture is decoded on a thread of its own, so a stage
// never waits for it, and only the last one asked for is kept.
#include "gx_internal.h"

#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <vector>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "third_party/stb_image.h"

// as src/port_include/port_radar.h
struct PortRadarMap {
    const uint8_t* rgba;  // row-major RGBA, w x h
    int w, h;
    float x0, z0;   // world x and z of the picture's top left corner
    float density;  // pixels per world unit
};

namespace {

struct Entry {
    unsigned long long key;
    std::string file;
    float x0, z0, density;
};

std::once_flag s_once;
std::string s_dir;
std::vector<Entry> s_entries;

std::mutex s_lock;
unsigned long long s_want;  // the map asked for last
unsigned long long s_have;  // the map in s_pixels (or that failed)
const Entry* s_haveEntry;   // null if it failed
std::vector<uint8_t> s_pixels;
int s_w, s_h;
bool s_loading;

bool readable(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

void init() {
    const char* env = getenv("SMS_RADAR_DIR");
    const char* dirs[] = {env, "assets/maps", "../../assets/maps"};
    for (const char* d : dirs) {
        if (d && *d && readable(std::string(d) + "/maps.txt")) {
            s_dir = d;
            break;
        }
    }
    if (s_dir.empty()) {
        gx::logmsg("radar: no stage maps (assets/maps/maps.txt; SMS_RADAR_DIR sets the folder)");
        return;
    }
    FILE* f = fopen((s_dir + "/maps.txt").c_str(), "r");
    char line[1024];
    while (f && fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char file[256];
        unsigned long long key;
        int w, h;
        Entry e;
        if (sscanf(line, "%llx %255s %f %f %f %d %d", &key, file, &e.x0, &e.z0, &e.density, &w, &h) != 7) continue;
        e.key = key;
        e.file = file;
        s_entries.push_back(e);
    }
    if (f) fclose(f);
    gx::logmsg("radar: %zu stage maps in %s", s_entries.size(), s_dir.c_str());
}

const Entry* find(unsigned long long key) {
    for (const Entry& e : s_entries)
        if (e.key == key) return &e;
    return nullptr;
}

void load(const Entry* e) {
    int w = 0, h = 0, n = 0;
    uint8_t* px = stbi_load((s_dir + "/" + e->file).c_str(), &w, &h, &n, 4);
    std::lock_guard<std::mutex> g(s_lock);
    s_loading = false;
    if (s_want != e->key) {  // another stage by now: that one starts on the next ask
        if (px) stbi_image_free(px);
        return;
    }
    s_have = e->key;
    if (!px) {
        gx::logmsg("radar: cannot read %s/%s", s_dir.c_str(), e->file.c_str());
        s_haveEntry = nullptr;
        return;
    }
    s_pixels.assign(px, px + size_t(w) * h * 4);
    s_w = w;
    s_h = h;
    s_haveEntry = e;
    stbi_image_free(px);
}

}  // namespace

// 1 with the stage's map in *out, 0 while it loads, -1 with no map for it.
// The pixels stay until another map is asked for.
extern "C" int port_radar_map(unsigned long long colKey, PortRadarMap* out) {
    std::call_once(s_once, init);
    const Entry* e = find(colKey);
    if (!e) return -1;
    std::lock_guard<std::mutex> g(s_lock);
    if (s_have == colKey) {
        if (!s_haveEntry) return -1;
        out->rgba = s_pixels.data();
        out->w = s_w;
        out->h = s_h;
        out->x0 = s_haveEntry->x0;
        out->z0 = s_haveEntry->z0;
        out->density = s_haveEntry->density;
        return 1;
    }
    s_want = colKey;
    if (!s_loading) {  // one decode at a time
        s_loading = true;
        s_have = 0;
        s_haveEntry = nullptr;
        s_pixels.clear();
        s_pixels.shrink_to_fit();
        std::thread(load, e).detach();
    }
    return 0;
}
