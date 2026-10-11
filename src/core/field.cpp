#include "field.h"
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstring>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static bool read_file(const char *path, std::vector<uint8_t> &out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    out.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    if (fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    fclose(f);
    return true;
}

std::vector<uint8_t> Files::read(int fid) const {
    std::vector<uint8_t> out;
    char path[1200];
    snprintf(path, sizeof path, "%s/mods/%04d.bin", dir.c_str(), fid);
    return read_file(path, out) ? out : readOriginal(fid);
}

std::vector<uint8_t> Files::readOriginal(int fid) const {
    std::vector<uint8_t> out;
    char path[1200];
    snprintf(path, sizeof path, "%s/files/%04d.bin", dir.c_str(), fid);
    if (!read_file(path, out) && disc) out = disc->file(fid);
    return out;
}

bool Files::write(int fid, const std::vector<uint8_t> &data) const {
    char path[1200];
    snprintf(path, sizeof path, "%s/mods/%04d.bin", dir.c_str(), fid);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return fclose(f) == 0 && ok;
}

bool Files::remove(int fid) const {
    char path[1200];
    snprintf(path, sizeof path, "%s/mods/%04d.bin", dir.c_str(), fid);
    return std::remove(path) == 0;
}

std::vector<uint8_t> rebuild_archive(const std::vector<uint8_t> &a, const std::map<int, std::vector<uint8_t>> &replace) {
    uint32_t count = 0;
    for (size_t k = 0; k + 4 <= a.size() && k < 4096 && !count; k += 4) count = rd32(&a[k]) / 4;
    std::vector<uint8_t> out(count * 4, 0);
    for (uint32_t i = 0; i < count; i++) {
        size_t n;
        const uint8_t *e = archive_entry(a, (int)i, &n);
        auto r = replace.find((int)i);
        if (!e && r == replace.end()) continue; /* an unused entry stays 0 */
        while (out.size() % 4) out.push_back(0);
        uint32_t off = (uint32_t)out.size();
        memcpy(&out[i * 4], &off, 4);
        if (r != replace.end()) out.insert(out.end(), r->second.begin(), r->second.end());
        else out.insert(out.end(), e, e + n);
    }
    out.resize((out.size() + 2047) / 2048 * 2048, 0);
    return out;
}

const uint8_t *archive_entry(const std::vector<uint8_t> &a, int i, size_t *size) {
    if (a.size() < 4 || i < 0 || (size_t)(i + 1) * 4 > a.size()) return nullptr;
    /* unused entries lead with offset 0: the table ends at the first entry's data */
    uint32_t count = 0;
    for (size_t k = 0; k + 4 <= a.size() && k < 4096 && !count; k += 4) count = rd32(&a[k]) / 4;
    uint32_t off = rd32(&a[i * 4]);
    if ((uint32_t)i >= count || off == 0 || off >= a.size()) return nullptr;
    if (size) {
        uint32_t next = (uint32_t)i + 1 < count ? rd32(&a[(i + 1) * 4]) : 0;
        *size = (next > off && next <= a.size() ? next : a.size()) - off;
    }
    return &a[off];
}

std::vector<uint8_t> unrlen(const uint8_t *p, size_t n) {
    if (n < 8 || memcmp(p, "RLEN", 4) != 0) return std::vector<uint8_t>(p, p + n);
    std::vector<uint8_t> out;
    out.reserve(rd32(p + 4));
    for (size_t s = 8; s < n && p[s] != 0;) {
        int c = p[s++];
        if (c & 0x80) {
            if (s >= n) break;
            out.insert(out.end(), c & 0x7F, p[s++]);
        } else {
            if (s + c > n) break;
            out.insert(out.end(), p + s, p + s + c);
            s += c;
        }
    }
    return out;
}

uint32_t rgba15(uint16_t c, int r, int g, int b) {
    if (c == 0) return 0;
    auto ch = [](int v, int k) {
        v = (v << 3 | v >> 2) * k / 128;
        return (uint32_t)(v > 255 ? 255 : v);
    };
    return ch(c & 31, r) | ch(c >> 5 & 31, g) << 8 | ch(c >> 10 & 31, b) << 16 | 0xFFu << 24;
}

/* timLoaderLoad: the CLUT (4 and 8-bit images) to (cx, cy), the image to (ix, iy) */
void Vram::loadTim(const uint8_t *tim, int ix, int iy, int cx, int cy) {
    uint32_t flag = rd32(tim + 4);
    const uint8_t *p = tim + 8;
    if (flag & 8) {
        int w = rd16(p + 8), h = rd16(p + 10);
        if ((flag & 7) <= 1)
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) at(cx + x, cy + y) = (uint16_t)rd16(p + 12 + (y * w + x) * 2);
        p += rd32(p);
    }
    int w = rd16(p + 8), h = rd16(p + 10);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) at(ix + x, iy + y) = (uint16_t)rd16(p + 12 + (y * w + x) * 2);
}

void Vram::loadArchive(const uint8_t *archive, size_t size, int ix, int iy, int cx, int cy) {
    std::vector<uint8_t> a(archive, archive + size);
    for (int i = 0;; i++) {
        size_t n;
        const uint8_t *e = archive_entry(a, i, &n);
        if (!e) break;
        std::vector<uint8_t> tim = unrlen(e, n);
        if (tim.size() >= 20) loadTim(tim.data(), ix, iy, cx, cy);
        ix += 0x40;
    }
}

bool LogicMap::load(const uint8_t *e, size_t size) {
    if (size < 24) return false;
    uint32_t part[6];
    for (int i = 0; i < 6; i++)
        if ((part[i] = rd32(e + i * 4)) >= size) return false;
    const uint8_t *grid = e + part[0], *c64 = e + part[1], *pix = e + part[5];
    int gw = grid[0], gh = grid[1];
    auto s16 = [&](int k, int i) { return rd16(e + part[k] + i * 2); };
    w = gw * 128, h = gh * 128;
    px.assign((size_t)w * h, 0);
    /* down the tree, a block of 8x8 pixels at a time */
    for (int y = 0; y < h; y += 8)
        for (int x = 0; x < w; x += 8) {
            int i = grid[2 + y / 128 * gw + x / 128] * 4 + (y & 64 ? 2 : 0) + (x & 64 ? 1 : 0);
            i = c64[i] * 4 + (y & 32 ? 2 : 0) + (x & 32 ? 1 : 0);
            i = s16(2, i) * 4 + (y & 16 ? 2 : 0) + (x & 16 ? 1 : 0);
            i = s16(3, i) * 4 + (y & 8 ? 2 : 0) + (x & 8 ? 1 : 0);
            i = s16(4, i);
            if (part[5] + (size_t)i * 64 + 64 > size) return false;
            for (int r = 0; r < 8; r++) memcpy(&px[(size_t)(y + r) * w + x], pix + i * 64 + r * 8, 8);
        }
    return true;
}

std::vector<uint8_t> LogicMap::encode(std::string *err) const {
    if (w <= 0 || h <= 0 || w % 128 || h % 128 || w / 128 > 255 || h / 128 > 255) {
        if (err) *err = "the map's size isn't a whole number of 128-pixel cells";
        return {};
    }
    /* each level's distinct blocks, by their content, in order of first use */
    std::map<std::vector<uint8_t>, int> pixIndex;
    std::map<std::array<int, 4>, int> idx[4]; /* 16, 32, 64, 128-pixel cells */
    std::vector<uint8_t> pixels;
    std::vector<std::array<int, 4>> lists[4];
    auto block = [&](int x, int y) {
        std::vector<uint8_t> b(64);
        for (int r = 0; r < 8; r++) memcpy(&b[r * 8], &px[(size_t)(y + r) * w + x], 8);
        auto it = pixIndex.find(b);
        if (it != pixIndex.end()) return it->second;
        int k = (int)pixIndex.size();
        pixIndex[b] = k;
        pixels.insert(pixels.end(), b.begin(), b.end());
        return k;
    };
    /* a cell of level l (0: 16 pixels ... 3: 128) at x, y: its four quarters,
       top left, top right, bottom left, bottom right, as getMapCell reads them */
    std::function<int(int, int, int)> cell = [&](int l, int x, int y) {
        int half = 8 << l;
        std::array<int, 4> q;
        for (int k = 0; k < 4; k++) {
            int qx = x + (k & 1) * half, qy = y + (k >> 1) * half;
            q[k] = l == 0 ? block(qx, qy) : cell(l - 1, qx, qy);
        }
        auto it = idx[l].find(q);
        if (it != idx[l].end()) return it->second;
        int k = (int)idx[l].size();
        idx[l][q] = k;
        lists[l].push_back(q);
        return k;
    };
    int gw = w / 128, gh = h / 128;
    std::vector<uint8_t> grid = {(uint8_t)gw, (uint8_t)gh};
    for (int gy = 0; gy < gh; gy++)
        for (int gx = 0; gx < gw; gx++) {
            int g = cell(3, gx * 128, gy * 128);
            if (g > 255) {
                if (err) *err = "the map has more than 256 different 128-pixel cells";
                return {};
            }
            grid.push_back((uint8_t)g);
        }
    if (idx[2].size() > 256) {
        if (err) *err = "the map has more than 256 different 64-pixel cells";
        return {};
    }
    if (pixIndex.size() > 32767 || idx[0].size() > 32767 || idx[1].size() > 32767) {
        if (err) *err = "the map has too many different small cells";
        return {};
    }
    /* the entry: six offsets, then the grid, cells64 (bytes), cells32,
       cells16, cells8 (halfwords) and the pixels, each 4-aligned */
    std::vector<uint8_t> out(24, 0);
    auto part = [&](int k, const std::vector<uint8_t> &bytes) {
        while (out.size() % 4) out.push_back(0);
        uint32_t off = (uint32_t)out.size();
        memcpy(&out[k * 4], &off, 4);
        out.insert(out.end(), bytes.begin(), bytes.end());
    };
    auto halves = [&](const std::vector<std::array<int, 4>> &l) {
        std::vector<uint8_t> b;
        for (auto &q : l)
            for (int v : q) b.push_back((uint8_t)v), b.push_back((uint8_t)(v >> 8));
        return b;
    };
    std::vector<uint8_t> c64;
    for (auto &q : lists[3])
        for (int v : q) c64.push_back((uint8_t)v);
    part(0, grid);
    part(1, c64);
    part(2, halves(lists[2]));
    part(3, halves(lists[1]));
    part(4, halves(lists[0]));
    part(5, pixels);
    while (out.size() % 4) out.push_back(0);
    return out;
}

static std::vector<std::pair<int, int>> codes(const Json &j) {
    std::vector<std::pair<int, int>> out;
    for (auto &c : j.items) out.push_back({c[0].i(), c[1].i()});
    return out;
}

bool Stage::load(const Json &j) {
    if (j.type != Json::Object) return false;
    name = j["name"].str;
    templ = j["template"].str;
    for (auto &m : j["euModes"].items) modes.push_back({m[0].i(), m[1].i(), m[2].i(), m[3].i(), false});
    for (auto &m : j["modes"].items) {
        bool seen = false;
        for (auto &k : modes) seen |= k.mode == m[0].i();
        if (!seen) modes.push_back({m[0].i(), m[1].i(), m[2].i(), m[3].i(), true});
    }
    mapFile = j["mapFile"].i(), sheetEntry = j["sheetEntry"].i(), imageEntry = j["imageEntry"].i();
    imageFile = j["imageFile"].i(), textFile = j["textFile"].i(), soundBank = j["soundBank"].i(), music = j["music"].i();
    for (int i = 0; i < 4; i++) color[i] = j["spriteColor"][i].i();
    for (int i = 0; i < 8; i++) mapFiles[i] = j["mapFiles"][i].i();
    loadSections(j);
    return true;
}

void Stage::loadSections(const Json &j) {
    if (j["mapFile"].type == Json::Number) mapFile = j["mapFile"].i();
    if (j["sheetEntry"].type == Json::Number) sheetEntry = j["sheetEntry"].i();
    if (j["imageFile"].type == Json::Number) imageFile = j["imageFile"].i();
    if (j["imageEntry"].type == Json::Number) imageEntry = j["imageEntry"].i();
    if (j["mapFiles"].type == Json::Array)
        for (int i = 0; i < 8; i++) mapFiles[i] = j["mapFiles"][i].i();
    if (j["start"].type == Json::Array) startX = j["start"][0].i(), startY = j["start"][1].i();
    if (j["startDir"].type == Json::Number) startDir = j["startDir"].i();
    if (j["objects"].type == Json::Array) objects.clear();
    for (auto &o : j["objects"].items) {
        int v[14];
        for (int i = 0; i < 14; i++) v[i] = o[i].i();
        objects.push_back({v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13]});
    }
    if (j["slots"].type == Json::Array) slots.clear();
    for (auto &s : j["slots"].items) {
        Slot t;
        t.cond[0][0] = s[0].i(), t.cond[0][1] = s[1].i(), t.cond[1][0] = s[2].i(), t.cond[1][1] = s[3].i();
        t.type = s[4].i(), t.arg = s[5].i(), t.x = s[6].i(), t.y = s[7].i(), t.dir = s[8].i();
        t.hideAnim = s[9].i(), t.place = s[10].i(), t.placeArg = s[11].i();
        slots.push_back(t);
    }
    if (j["actors"].type == Json::Array) actors.clear();
    for (auto &a : j["actors"].items) {
        Actor t{a["id"].i(), a["image"].i(), a["x"].i(), a["y"].i(), a["dir"].i(), codes(a["conditions"]), {}};
        for (auto &k : a["talks"].items) t.talks.push_back({k["entry"].i(), codes(k["conditions"]), codes(k["actions"])});
        actors.push_back(t);
    }
    if (j["images"].type == Json::Array) images.clear();
    for (auto &m : j["images"].items) {
        std::array<int, 8> v;
        for (int i = 0; i < 8; i++) v[i] = m[i].i();
        images.push_back(v);
    }
    if (j["events"].type == Json::Array) events.clear();
    for (auto &e : j["events"].items) {
        Event t;
        if (e.type == Json::Array) { /* an older dump: [id, scripted, text], without the script */
            t.id = e[0].i(), t.text = e[2].i();
        } else {
            t.id = e["id"].i(), t.text = e["text"].i(), t.start = e["start"].i(), t.end = e["end"].i();
            t.scripted = e["script"].type == Json::Array;
            for (auto &w : e["script"].items) t.script.push_back(w.i());
        }
        events.push_back(t);
    }
    if (j["places"].type == Json::Array) places.clear();
    for (auto &p : j["places"].items) {
        Place t{p["serial"].i(), p["id"].i(), {}};
        for (int a = 0; a < 4; a++) {
            const Json &ar = p["areas"][a];
            t.areas[a].rate = ar["rate"].i();
            for (int k = 0; k < 8; k++) {
                const Json &b = ar["battles"][k];
                t.areas[a].battles[k] = {b[0].i(), b[1].i(), b[2].i()};
            }
        }
        places.push_back(t);
    }
}

static std::string codes_json(const std::vector<std::pair<int, int>> &c) {
    std::string out = "[";
    for (size_t i = 0; i < c.size(); i++)
        out += (i ? ",[" : "[") + std::to_string(c[i].first) + "," + std::to_string(c[i].second) + "]";
    return out + "]";
}

std::string section_json(const Stage &s, const std::string &section) {
    std::string out;
    auto ints = [&](const int *v, int n) {
        std::string r = "[";
        for (int i = 0; i < n; i++) r += (i ? "," : "") + std::to_string(v[i]);
        return r + "]";
    };
    if (section == "start") return "[" + std::to_string(s.startX) + "," + std::to_string(s.startY) + "]";
    if (section == "startDir") return std::to_string(s.startDir);
    if (section == "mapFile") return std::to_string(s.mapFile);
    if (section == "imageFile") return std::to_string(s.imageFile);
    if (section == "imageEntry") return std::to_string(s.imageEntry);
    out = "[";
    if (section == "objects")
        for (size_t i = 0; i < s.objects.size(); i++) out += (i ? ",\n" : "\n") + ints(&s.objects[i].visible, 14);
    if (section == "slots")
        for (size_t i = 0; i < s.slots.size(); i++) {
            const Slot &t = s.slots[i];
            int v[12] = {t.cond[0][0], t.cond[0][1], t.cond[1][0], t.cond[1][1], t.type, t.arg, t.x, t.y, t.dir, t.hideAnim, t.place, t.placeArg};
            out += (i ? ",\n" : "\n") + ints(v, 12);
        }
    if (section == "images")
        for (size_t i = 0; i < s.images.size(); i++) out += (i ? "," : "") + ints(s.images[i].data(), 8);
    if (section == "actors")
        for (size_t i = 0; i < s.actors.size(); i++) {
            const Actor &a = s.actors[i];
            out += (i ? ",\n" : "\n") + std::string("{\"id\":") + std::to_string(a.id) + ",\"image\":" + std::to_string(a.image) +
                   ",\"x\":" + std::to_string(a.x) + ",\"y\":" + std::to_string(a.y) + ",\"dir\":" + std::to_string(a.dir) +
                   ",\"conditions\":" + codes_json(a.conditions) + ",\"talks\":[";
            for (size_t k = 0; k < a.talks.size(); k++)
                out += (k ? ",{" : "{") + std::string("\"entry\":") + std::to_string(a.talks[k].entry) + ",\"conditions\":" +
                       codes_json(a.talks[k].conditions) + ",\"actions\":" + codes_json(a.talks[k].actions) + "}";
            out += "]}";
        }
    if (section == "events")
        for (size_t i = 0; i < s.events.size(); i++) {
            const Event &e = s.events[i];
            out += (i ? ",\n" : "\n") + std::string("{\"id\":") + std::to_string(e.id) + ",\"text\":" + std::to_string(e.text) +
                   ",\"start\":" + std::to_string(e.start) + ",\"end\":" + std::to_string(e.end) + ",\"script\":";
            if (!e.scripted) {
                out += "null}";
                continue;
            }
            for (size_t k = 0; k < e.script.size(); k++) out += (k ? "," : "[") + std::to_string(e.script[k]);
            out += e.script.empty() ? "[]}" : "]}";
        }
    if (section == "places")
        for (size_t i = 0; i < s.places.size(); i++) {
            const Place &p = s.places[i];
            out += (i ? ",\n" : "\n") + std::string("{\"serial\":") + std::to_string(p.serial) + ",\"id\":" + std::to_string(p.id) + ",\"areas\":[";
            for (int a = 0; a < 4; a++) {
                out += (a ? ",{" : "{") + std::string("\"rate\":") + std::to_string(p.areas[a].rate) + ",\"battles\":[";
                for (int k = 0; k < 8; k++) {
                    const Battle &b = p.areas[a].battles[k];
                    out += (k ? ",[" : "[") + std::to_string(b.encounter) + "," + std::to_string(b.stage) + "," + std::to_string(b.music) + "]";
                }
                out += "]}";
            }
            out += "]}";
        }
    return out + "]";
}

std::string stage_mod_json(const Stage &edited, const Stage &original) {
    static const char *SECTIONS[] = {"objects", "slots", "images", "actors", "start", "startDir", "places", "mapFile", "imageFile", "imageEntry", "events"};
    std::string out;
    for (const char *sec : SECTIONS) {
        std::string e = section_json(edited, sec);
        if (e == section_json(original, sec)) continue;
        out += (out.empty() ? "{" : ",\n") + std::string("\"") + sec + "\":" + e;
    }
    return out.empty() ? "" : out + "}\n";
}

int command_args(int op) {
    int kind = op >> 8, sub = op & 0xFF;
    switch (kind) {
    case 1: return sub == 2 ? 4 : 3;
    case 2: return 4;
    case 3: return sub == 1 ? 0 : sub == 4 ? 4 : 1;
    case 6: return sub == 1 ? 3 : 2;
    default: return -1;
    }
}

std::vector<Command> parse_script(const std::vector<int> &script) {
    std::vector<Command> out;
    for (size_t i = 0; i < script.size();) {
        int n = command_args(script[i]);
        if (n < 0 || i + 1 + n > script.size()) break;
        out.push_back({script[i], std::vector<int>(script.begin() + i + 1, script.begin() + i + 1 + n)});
        i += 1 + n;
    }
    return out;
}

std::vector<int> build_script(const std::vector<Command> &commands) {
    std::vector<int> out;
    for (auto &c : commands) {
        out.push_back(c.op);
        for (int k = 0; k < command_args(c.op); k++) out.push_back(k < (int)c.args.size() ? c.args[k] : 0);
    }
    out.push_back(0);
    return out;
}

int own_images_fid(const Stage &s) {
    if (s.isNew()) return 6000 + ((s.modes.empty() ? 0x200 : s.modes[0].mode) - 0x200);
    return 5000 + atoi(s.name.c_str() + 5);
}

int own_tiles_fid(const Stage &s) {
    if (s.isNew()) return 4000 + 2 * ((s.modes.empty() ? 0x200 : s.modes[0].mode) - 0x200);
    return 3000 + atoi(s.name.c_str() + 5);
}

std::string new_field_json(const Stage &s) {
    auto n = [](int v) { return std::to_string(v); };
    const Stage::Mode m = s.modes.empty() ? Stage::Mode{0, 0, 0, 0, false} : s.modes[0];
    std::string mode = "[[" + n(m.mode) + "," + n(m.file) + "," + n(m.area) + "," + n(m.place) + "]]";
    std::string out = "{\"name\":\"" + s.name + "\",\"template\":\"" + s.templ + "\",\n\"modes\":" + mode + ",\"euModes\":" + mode +
                      ",\n\"mapFile\":" + n(s.mapFile) + ",\"sheetEntry\":" + n(s.sheetEntry) + ",\"imageEntry\":" + n(s.imageEntry) +
                      ",\"imageFile\":" + n(s.imageFile) + ",\"textFile\":" + n(s.textFile) + ",\"soundBank\":" + n(s.soundBank) +
                      ",\"music\":" + n(s.music) + ",\n\"spriteColor\":[" + n(s.color[0]) + "," + n(s.color[1]) + "," +
                      n(s.color[2]) + "," + n(s.color[3]) + "],\"mapFiles\":[";
    for (int i = 0; i < 8; i++) out += (i ? "," : "") + n(s.mapFiles[i]);
    out += "],\n\"start\":" + section_json(s, "start") + ",\"startDir\":" + section_json(s, "startDir");
    for (const char *sec : {"objects", "slots", "images", "actors", "places", "events"}) out += ",\n\"" + std::string(sec) + "\":" + section_json(s, sec);
    return out + "}\n";
}

Sprite draw_sheet_frame(const uint8_t *sheet, size_t size, int frame, Vram &vram, int tx, int ty, int ax, int ay,
                        int clutRow, const int color[3]) {
    Sprite out;
    if (size < 12) return out;
    uint32_t partsOff = rd32(sheet), idsOff = rd32(sheet + 4);
    if (partsOff >= size || idsOff >= size) return out;
    int i = 0;
    while (idsOff + i < size && sheet[idsOff + i] != frame && i < 255) i++;
    if (idsOff + i >= size || sheet[idsOff + i] != frame || (size_t)(i + 3) * 4 > size) return out;
    uint32_t frameOff = rd32(sheet + (i + 2) * 4);
    if (frameOff + 6 > size) return out;
    const uint8_t *p = sheet + frameOff;
    int count = rd16(p), frameRow = rd16(p + 2), abr = rd16(p + 4);
    if (count <= 0 || frameOff + 6 + (size_t)count * 6 > size) return out;
    struct Part { int x, y, u, v, w, h, cx, cy, mode; };
    std::vector<Part> parts;
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30);
    for (int k = 0; k < count; k++) {
        const uint8_t *e = p + 6 + k * 6;
        size_t po = partsOff + (size_t)(uint16_t)rd16(e) * 14;
        if (po + 14 > size) return out;
        const uint8_t *q = sheet + po;
        Part t{rd16(e + 2), rd16(e + 4), rd16(q), rd16(q + 2), rd16(q + 4), rd16(q + 6), rd16(q + 8), rd16(q + 10), rd16(q + 12)};
        parts.push_back(t);
        x0 = std::min(x0, t.x), y0 = std::min(y0, t.y), x1 = std::max(x1, t.x + t.w), y1 = std::max(y1, t.y + t.h);
    }
    if (x1 <= x0 || y1 <= y0 || x1 - x0 > 1024 || y1 - y0 > 1024) return out;
    out.img.resize(x1 - x0, y1 - y0);
    out.ox = x0, out.oy = y0;
    /* drawn from the last part to the first, so the first ends on top */
    for (int k = count - 1; k >= 0; k--) {
        const Part &t = parts[k];
        int cx = (t.mode ? ax : tx) + t.cx, cy = (t.mode ? ay - 0x100 : ty) + t.cy + frameRow + clutRow;
        cx &= 0x3F0, cy &= 0x1FF;
        int pageX = (tx + (t.mode ? t.u / 2 : t.u / 4)) & 0x3C0, pageY = ty & 0x100;
        int u0 = t.mode ? t.u & 0x7F : t.u & 0xFF, v0 = t.v & 0xFF;
        for (int yy = 0; yy < t.h; yy++)
            for (int xx = 0; xx < t.w; xx++) {
                int u = (u0 + xx) & 0xFF, v = (v0 + yy) & 0xFF, idx;
                if (t.mode) idx = vram.at(pageX + u / 2, pageY + v) >> ((u & 1) * 8) & 0xFF;
                else idx = vram.at(pageX + u / 4, pageY + v) >> ((u & 3) * 4) & 0xF;
                uint32_t c = rgba15(vram.at(cx + idx, cy), color[0], color[1], color[2]);
                if (!c) continue;
                if (abr != -1) c = (c & 0xFFFFFF) | 0x80u << 24; /* semi-transparent: shown at half */
                out.img.px[(size_t)(t.y - y0 + yy) * out.img.w + (t.x - x0 + xx)] = c;
            }
    }
    return out;
}

/* The European version's FIELD_SPRITES_FILE: the field's own sprites (the
   hidden spots' among them), entries 0 and 2 */
static const int FIELD_SPRITES_FILE = 0x160;

Sprite actor_sprite(const Files &files, int animFile, int dir) {
    Sprite out;
    if (!animFile) return out;
    std::vector<uint8_t> f = files.read(animFile >> 16);
    size_t n;
    /* the sets: rows of six s16, the set then its five directions' animations, up to set 0 */
    const uint8_t *sets = archive_entry(f, animFile & 0xFFFF, &n);
    if (!sets || n < 12) return out;
    const uint8_t *row = sets;
    for (const uint8_t *r = sets; r + 12 <= sets + n && rd16(r) != 0; r += 12)
        if (rd16(r) == 1) { row = r; break; }
    bool mirror = dir >= 5;
    int d = dir < 5 ? (dir < 0 ? 0 : dir) : 8 - dir;
    /* the animation: (time, frame entry, x, y) words; its first frame */
    const uint8_t *frames = archive_entry(f, (uint16_t)rd16(row + 2 + d * 2), &n);
    if (!frames || n < 16) return out;
    int entry = (int)rd32(frames + 4), x = (int)rd32(frames + 8), y = (int)rd32(frames + 12);
    const uint8_t *tim = archive_entry(f, entry & 0xFFFF, &n);
    if (!tim || n < 20) return out;
    /* a 4-bit TIM with its CLUT */
    uint32_t flag = rd32(tim + 4);
    if ((flag & 7) != 0 || !(flag & 8)) return out;
    const uint8_t *clut = tim + 8 + 12, *img = tim + 8 + rd32(tim + 8);
    int w = rd16(img + 8) * 4, h = rd16(img + 10);
    if (w <= 0 || h <= 0 || w > 512 || h > 512 || (size_t)(img + 12 - tim) + (size_t)w / 2 * h > n) return out;
    out.img.resize(w, h);
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            int idx = img[12 + yy * (w / 2) + xx / 2] >> ((xx & 1) * 4) & 15;
            out.img.px[(size_t)yy * w + (mirror ? w - 1 - xx : xx)] = rgba15((uint16_t)rd16(clut + idx * 2));
        }
    out.ox = mirror ? -(w + x) : x, out.oy = y;
    return out;
}

bool Field::load(const Files &files, const Stage &st, const std::vector<int> *animFiles) {
    stage = st;
    error.clear();

    /* the background: a header sector, then each tile's RLEN data in tileBytes */
    /* a stage without one (its setup sets no mapFile) has only its logic maps */
    std::vector<uint8_t> map = stage.mapFile ? files.read(stage.mapFile) : std::vector<uint8_t>(2048);
    if (map.size() < 2048) return error = "the map's file " + std::to_string(stage.mapFile) + " is missing", false;
    tilesW = (int)rd32(&map[4]), tilesH = (int)rd32(&map[8]);
    int tileBytes = (int)rd32(&map[12]);
    if (stage.mapFile && (tilesW <= 0 || tilesH <= 0 || tilesW * tilesH > 1000 || tileBytes <= 0))
        return error = "the map's file has an unknown header", false;
    this->tileBytes = stage.mapFile ? tileBytes : 2048;
    for (int d = 0; d < 3; d++) tiles[d].assign((size_t)tilesW * tilesH, Image());
    tileData.assign((size_t)tilesW * tilesH, {});
    for (int t = 0; t < tilesW * tilesH && stage.mapFile; t++) {
        int size = (uint16_t)rd16(&map[16 + t * 2]);
        size_t off = (size_t)(t * (tileBytes / 2048) + 1) * 2048;
        if (!size || off + 8 > map.size()) continue;
        tileData[t].assign(map.begin() + off, map.begin() + std::min(off + size, map.size()));
        Image layers[3];
        decode_tile(tileData[t], stage.color, layers);
        for (int d = 0; d < 3; d++) tiles[d][t] = std::move(layers[d]);
    }

    /* the logic maps: a file entry each, fid << 16 | entry */
    for (int i = 0; i < 8; i++) {
        maps[i] = LogicMap();
        if (!stage.mapFiles[i]) continue;
        std::vector<uint8_t> f = files.read(stage.mapFiles[i] >> 16);
        size_t n;
        if (const uint8_t *e = archive_entry(f, stage.mapFiles[i] & 0xFFFF, &n)) maps[i].load(e, n);
        if (!stage.mapFile) tilesW = std::max(tilesW, maps[i].w / 128), tilesH = std::max(tilesH, maps[i].h / 128);
    }
    if (!stage.mapFile) {
        for (auto &d : tiles) d.assign((size_t)tilesW * tilesH, Image());
        tileData.assign((size_t)tilesW * tilesH, {});
    }

    /* the objects' textures: the field's sprites at (0x200, 0x100) and
       (0x240, 0x100), the stage's images at (0x140, 0x100) with their CLUTs
       at (0, 0x1F0) (FIELDSTG_loadFieldFiles) */
    /* no field has both: an archive of its own, or an entry of one */
    imagesData.clear();
    size_t n;
    if (stage.imageFile) imagesData = files.read(stage.imageFile);
    else if (stage.imageEntry) {
        std::vector<uint8_t> f = files.read(stage.imageEntry >> 16);
        if (const uint8_t *e = archive_entry(f, stage.imageEntry & 0xFFFF, &n)) imagesData.assign(e, e + n);
    }
    spritesFile_ = files.read(FIELD_SPRITES_FILE);
    sheetFile_ = stage.sheetEntry ? files.read(stage.sheetEntry >> 16) : std::vector<uint8_t>();
    reloadImages(files, animFiles);
    return true;
}

void Field::reloadImages(const Files &files, const std::vector<int> *animFiles) {
    vram = Vram();
    size_t n;
    if (const uint8_t *e = archive_entry(spritesFile_, 2, &n)) vram.loadArchive(e, n, 0x200, 0x100, 0, 0);
    if (const uint8_t *e = archive_entry(spritesFile_, 3, &n)) vram.loadArchive(e, n, 0x240, 0x100, 0, 0);
    if (!imagesData.empty()) vram.loadArchive(imagesData.data(), imagesData.size(), 0x140, 0x100, 0, 0x1F0);
    refreshSprites(files, animFiles);
}

void Field::refreshSprites(const Files &files, const std::vector<int> *animFiles) {
    /* the characters': each (character, facing) read once */
    std::map<std::pair<int, int>, Sprite> cache;
    actors.clear();
    for (const Actor &a : stage.actors) {
        auto key = std::make_pair(a.id, a.dir);
        auto it = cache.find(key);
        if (it == cache.end())
            it = cache.emplace(key, animFiles && a.id >= 0 && a.id < (int)animFiles->size() ? actor_sprite(files, (*animFiles)[a.id], a.dir)
                                                                                          : Sprite()).first;
        actors.push_back(it->second);
    }
    /* the objects': the stage's sheet, or the field's sprites for the hidden spots */
    size_t sheetSize = 0, spriteSheetSize = 0;
    const uint8_t *sheet = archive_entry(sheetFile_, stage.sheetEntry & 0xFFFF, &sheetSize);
    const uint8_t *spriteSheet = archive_entry(spritesFile_, 0, &spriteSheetSize);
    bool tinted = stage.color[3] != 0;
    objects.clear();
    for (const Object &o : stage.objects) {
        int col[3] = {128, 128, 128};
        if (tinted) col[0] = stage.color[0], col[1] = stage.color[1], col[2] = stage.color[2];
        if (o.anim == 0xFF) {
            int neutral[3] = {128, 128, 128};
            objects.push_back(spriteSheet ? draw_sheet_frame(spriteSheet, spriteSheetSize, o.frame, vram, 0x200, 0x100,
                                                             0, 0x1F0, o.clutRow, neutral)
                                          : Sprite());
        } else {
            objects.push_back(sheet ? draw_sheet_frame(sheet, sheetSize, o.frame, vram, 0x140, 0x100, 0, 0x1F0,
                                                       o.clutRow, col)
                                    : Sprite());
        }
    }
}
