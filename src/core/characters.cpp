#include "characters.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static void put32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> i * 8));
}
static void put16(std::vector<uint8_t> &v, int x) { v.push_back((uint8_t)x), v.push_back((uint8_t)(x >> 8)); }

static const char *SET_NAMES[] = {"", "Stand", "", "", "Walk"};
static const char *DIR_NAMES[5] = {"S", "SW", "W", "NW", "N"};
/* the directions a front and a back stand for, and the row each direction takes */
static int dir_row(const CharTemplate &t, int dir) { return t.full ? dir : dir < 3 ? 0 : 1; }

int CharTemplate::drawings(int a) const {
    int n = 0;
    for (int k : anims[a].order) n = std::max(n, k + 1);
    return n;
}

int CharTemplate::cols() const {
    int n = 0;
    for (int a = 0; a < (int)anims.size(); a++) n = std::max(n, drawings(a));
    return n;
}

CharTemplate standard_template() {
    CharTemplate t;
    t.anims = {{1, {0, 1, 2, 1}, {16, 10, 18, 10}}, {4, {0, 1, 2, 3, 4, 2}, {11, 12, 10, 11, 12, 10}}};
    return t;
}

std::string row_name(const CharTemplate &t, int row) {
    int a = row / t.dirs(), d = row % t.dirs();
    int set = t.anims[a].set;
    std::string name = set < 5 && *SET_NAMES[set] ? SET_NAMES[set] : "Set " + std::to_string(set);
    return name + ", " + (t.full ? DIR_NAMES[d] : d ? "back (NW, N)" : "front (S, SW, W)");
}

std::string CharTemplate::json() const {
    auto list = [](const std::vector<int> &v) {
        std::string s = "[";
        for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + std::to_string(v[i]);
        return s + "]";
    };
    std::string out = "{\"directions\":" + std::to_string(dirs()) + ",\"cell\":[" + std::to_string(cellW) + "," +
                      std::to_string(cellH) + "],\"anchor\":[" + std::to_string(anchorX) + "," + std::to_string(anchorY) +
                      "],\"width\":" + std::to_string(width) + ",\n\"animations\":[";
    for (size_t a = 0; a < anims.size(); a++)
        out += (a ? ",\n" : "\n") + std::string("{\"set\":") + std::to_string(anims[a].set) + ",\"order\":" + list(anims[a].order) +
               ",\"times\":" + list(anims[a].times) + "}";
    out += "],\n\"rows\":[";
    for (int r = 0; r < rows(); r++) out += (r ? "," : "") + std::string("\"") + row_name(*this, r) + "\"";
    return out + "]}\n";
}

bool CharTemplate::parse(const Json &j) {
    if (j.type != Json::Object) return false;
    full = j["directions"].i() == 5;
    cellW = j["cell"][0].i(), cellH = j["cell"][1].i(), anchorX = j["anchor"][0].i(), anchorY = j["anchor"][1].i();
    width = j["width"].i() ? j["width"].i() : 32;
    anims.clear();
    for (auto &a : j["animations"].items) {
        Anim t{a["set"].i(), {}, {}};
        for (auto &v : a["order"].items) t.order.push_back(v.i());
        for (auto &v : a["times"].items) t.times.push_back(v.i());
        if (t.order.empty() || t.order.size() != t.times.size()) return false;
        anims.push_back(t);
    }
    return !anims.empty() && cellW > 0 && cellW % 4 == 0 && cellH > 0 && cellW <= 256 && cellH <= 256;
}

/* the faint guides: a cell's border and its anchor */
static void guides(Image &im, const CharTemplate &t) {
    const uint32_t G = 0x40FFFFFFu, A = 0x500000FFu;
    auto set = [&](int x, int y, uint32_t c) {
        if (x >= 0 && y >= 0 && x < im.w && y < im.h) im.px[(size_t)y * im.w + x] = c;
    };
    for (int r = 0; r < t.rows(); r++)
        for (int c = 0; c < t.cols(); c++) {
            int x0 = c * t.cellW, y0 = r * t.cellH;
            for (int x = 0; x < t.cellW; x++) set(x0 + x, y0, G), set(x0 + x, y0 + t.cellH - 1, G);
            for (int y = 0; y < t.cellH; y++) set(x0, y0 + y, G), set(x0 + t.cellW - 1, y0 + y, G);
            for (int k = -2; k <= 2; k++)
                if (t.anchorX < t.cellW && t.anchorY < t.cellH)
                    set(x0 + t.anchorX + k, y0 + t.anchorY, A), set(x0 + t.anchorX, y0 + t.anchorY + k, A);
        }
}

Image blank_sheet(const CharTemplate &t) {
    Image im;
    im.resize(t.cols() * t.cellW, t.rows() * t.cellH);
    guides(im, t);
    return im;
}

/* an archive's entry by index, the file's own table */
static const uint8_t *entry(const std::vector<uint8_t> &f, int i, size_t *n) { return archive_entry(f, i, n); }

bool export_character(const Files &files, int animFile, int width, CharTemplate *out, Image *sheet, std::string *err) {
    return export_character(files.read(animFile >> 16), animFile & 0xFFFF, width, out, sheet, err);
}

bool export_character(const std::vector<uint8_t> &f, int setsEntry, int width, CharTemplate *out, Image *sheet, std::string *err) {
    size_t n;
    const uint8_t *sets = entry(f, setsEntry, &n);
    if (!sets) {
        if (err) *err = "the character has no animation file";
        return false;
    }
    std::map<int, std::vector<int>> rows; /* set -> its 5 directions' animations */
    for (size_t r = 0; r + 12 <= n && rd16(sets + r) != 0; r += 12) {
        std::vector<int> d;
        for (int k = 1; k <= 5; k++) d.push_back((uint16_t)rd16(sets + r + k * 2));
        rows[rd16(sets + r)] = d;
    }
    CharTemplate t;
    t.width = width;
    for (int set : {1, 4})
        if (rows.count(set)) {
            const auto &d = rows[set];
            /* a front and a back, as most NPCs, unless the directions differ otherwise */
            if (!(d[0] == d[1] && d[1] == d[2] && d[3] == d[4])) t.full = true;
        }
    struct Frame { int time, tim, x, y; };
    auto frames = [&](int e) {
        std::vector<Frame> v;
        const uint8_t *p = entry(f, e, &n);
        for (size_t k = 0; p && k + 16 <= n; k += 16) {
            Frame fr{(int)rd32(p + k), (int)(rd32(p + k + 4) & 0xFFFF), (int)rd32(p + k + 8), (int)rd32(p + k + 12)};
            if (fr.time == 0 && fr.tim == 0) break;
            v.push_back(fr);
        }
        return v;
    };
    /* the drawings of each row, and the cell that holds them all at one anchor */
    std::vector<std::vector<Frame>> drawn; /* by row: each drawing's first frame */
    int ax = 0, ay = 0, right = 0, bottom = 0;
    for (int set : {1, 4}) {
        if (!rows.count(set)) continue;
        CharTemplate::Anim anim{set, {}, {}};
        for (int r = 0; r < t.dirs(); r++) {
            auto v = frames(rows[set][t.full ? r : r ? 3 : 0]);
            std::vector<int> seen;
            std::vector<Frame> row;
            for (auto &fr : v) {
                auto it = std::find(seen.begin(), seen.end(), fr.tim);
                if (it == seen.end()) seen.push_back(fr.tim), row.push_back(fr);
                if (r == 0) anim.order.push_back((int)(std::find(seen.begin(), seen.end(), fr.tim) - seen.begin())), anim.times.push_back(fr.time);
                const uint8_t *tim = entry(f, fr.tim, &n);
                if (!tim || n < 20) continue;
                const uint8_t *img = tim + 8 + (rd32(tim + 4) & 8 ? rd32(tim + 8) : 0);
                ax = std::max(ax, -fr.x), ay = std::max(ay, -fr.y);
                right = std::max(right, fr.x + rd16(img + 8) * 4), bottom = std::max(bottom, fr.y + rd16(img + 10));
            }
            drawn.push_back(row);
        }
        if (!anim.order.empty()) t.anims.push_back(anim);
    }
    if (t.anims.empty()) {
        if (err) *err = "the character has neither a stand nor a walk";
        return false;
    }
    t.anchorX = ax, t.anchorY = ay, t.cellW = (ax + right + 3) & ~3, t.cellH = ay + bottom;
    if (t.cellW <= 0 || t.cellH <= 0 || t.cellW > 256 || t.cellH > 256) {
        if (err) *err = "the character's frames don't make a cell";
        return false;
    }
    Image im = blank_sheet(t);
    for (int r = 0; r < (int)drawn.size(); r++)
        for (int c = 0; c < (int)drawn[r].size() && c < t.drawings(r / t.dirs()); c++) {
            const Frame &fr = drawn[r][c];
            const uint8_t *tim = entry(f, fr.tim, &n);
            if (!tim || n < 20 || (rd32(tim + 4) & 7) != 0) continue;
            const uint8_t *clut = tim + 20, *img = tim + 8 + rd32(tim + 8);
            int w = rd16(img + 8) * 4, h = rd16(img + 10);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int idx = img[12 + y * (w / 2) + x / 2] >> ((x & 1) * 4) & 15;
                    uint32_t col = rgba15((uint16_t)rd16(clut + idx * 2));
                    int X = c * t.cellW + t.anchorX + fr.x + x, Y = r * t.cellH + t.anchorY + fr.y + y;
                    if (col && X >= 0 && Y >= 0 && X < im.w && Y < im.h) im.px[(size_t)Y * im.w + X] = col;
                }
        }
    *out = t;
    *sheet = im;
    return true;
}

void character_size(const Files &files, int animFile, int *w, int *h) {
    *w = 8, *h = 40; /* a game NPC's, when it can't be read */
    std::vector<uint8_t> f = files.read(animFile >> 16);
    size_t n, fn, tn;
    const uint8_t *sets = archive_entry(f, animFile & 0xFFFF, &n);
    if (!sets) return;
    int mw = 0, mh = 0;
    for (size_t r = 0; r + 12 <= n && rd16(sets + r) != 0; r += 12)
        for (int d = 1; d <= 5; d++) {
            const uint8_t *fr = archive_entry(f, (uint16_t)rd16(sets + r + d * 2), &fn);
            for (size_t k = 0; fr && k + 16 <= fn && !(rd32(fr + k) == 0 && rd32(fr + k + 4) == 0); k += 16) {
                const uint8_t *tim = archive_entry(f, rd32(fr + k + 4) & 0xFFFF, &tn);
                if (!tim || tn < 20) continue;
                const uint8_t *img = tim + 8 + (rd32(tim + 4) & 8 ? rd32(tim + 8) : 0);
                mw = std::max(mw, (int)rd16(img + 8)), mh = std::max(mh, (int)rd16(img + 10));
            }
        }
    if (mw && mh) *w = mw, *h = mh;
}

std::vector<bool> field_vram(const Files &files, const Stage &st, const std::vector<uint8_t> &imagesData, const std::vector<int> &animFiles,
                             bool objectPages) {
    std::vector<bool> taken(320 * 256, false);
    auto mark = [&](int x, int y, int w, int h) {
        for (int yy = std::max(y, 256); yy < y + h && yy < 512; yy++)
            for (int xx = std::max(x, 320); xx < x + w && xx < 640; xx++) taken[(size_t)(yy - 256) * 320 + xx - 320] = true;
    };
    size_t n;
    /* the objects' pages, side by side from 0x140 */
    for (int i = 0; objectPages && archive_entry(imagesData, i, &n); i++) mark(0x140 + i * 0x40, 256, 64, 256);
    /* the field's sprites: FIELD_SPRITES_FILE's entries 2 and 3, each an archive of TIMs 0x40 apart */
    std::vector<uint8_t> sprites = files.read(0x160);
    for (int e = 2; e <= 3; e++) {
        const uint8_t *a = archive_entry(sprites, e, &n);
        if (!a) continue;
        std::vector<uint8_t> arch(a, a + n);
        for (int i = 0;; i++) {
            size_t tn;
            const uint8_t *t = archive_entry(arch, i, &tn);
            if (!t) break;
            std::vector<uint8_t> tim = unrlen(t, tn);
            if (tim.size() < 20) continue;
            const uint8_t *img = tim.data() + 8 + (rd32(&tim[4]) & 8 ? rd32(&tim[8]) : 0);
            mark((e == 2 ? 0x200 : 0x240) + i * 0x40, 0x100, rd16(img + 8), rd16(img + 10));
        }
    }
    /* the character slots: as big as their characters' biggest frame, else a game NPC's; their palettes */
    for (size_t k = 2; k < st.images.size(); k++) {
        const auto &m = st.images[k];
        if (!m[2] && !m[3]) continue;
        int w = 0, h = 0;
        bool used = false;
        for (const Actor &a : st.actors) {
            if (a.image != (int)k - 2) continue;
            used = true;
            if (a.id >= 0 && a.id < (int)animFiles.size() && animFiles[a.id]) {
                int cw, ch;
                character_size(files, animFiles[a.id], &cw, &ch);
                w = std::max(w, cw), h = std::max(h, ch);
            }
        }
        if (!used) continue; /* no character takes it: its room is free */
        if (!w) w = 8, h = 40;
        mark(m[2], m[3], w, h);
        mark(m[6], m[7], 16, 1);
    }
    return taken;
}

int add_character_slot(Stage &st, const std::vector<bool> &taken, int w, int h) {
    auto freeAt = [&](int x, int y, int ww, int hh) {
        for (int yy = y; yy < y + hh; yy++)
            for (int xx = x; xx < x + ww; xx++)
                if (yy < 256 || yy >= 512 || xx < 320 || xx >= 640 || taken[(size_t)(yy - 256) * 320 + xx - 320]) return false;
        return true;
    };
    /* the frames inside one texture page, from the bottom up: the sprite's
       u + width and v + height are bytes, at most 255 */
    int ix = -1, iy = -1;
    for (int page = 320; page < 640 && ix < 0; page += 64)
        for (int y = 511 - h; y >= 256 && ix < 0; y--)
            for (int x = page; x + w < page + 64 && ix < 0; x++)
                if (freeAt(x, y, w, h)) ix = x, iy = y;
    if (ix < 0) return -1;
    std::vector<bool> t2 = taken;
    for (int yy = iy; yy < iy + h; yy++)
        for (int xx = ix; xx < ix + w; xx++) t2[(size_t)(yy - 256) * 320 + xx - 320] = true;
    /* the palette: 16 halfwords at a multiple of 16 */
    int cx = -1, cy = -1;
    for (int y = 511; y >= 256 && cx < 0; y--)
        for (int x = 320; x + 16 <= 640 && cx < 0; x += 16) {
            bool ok = true;
            for (int k = 0; k < 16 && ok; k++) ok = !t2[(size_t)(y - 256) * 320 + x + k - 320];
            if (ok) cx = x, cy = y;
        }
    if (cx < 0) return -1;
    while (st.images.size() < 2) st.images.push_back({});
    int page = (ix - 320) / 64 * 64 + 320;
    st.images.push_back({page, 256, ix, iy, (ix - page) * 4, iy - 256, cx, cy});
    return (int)st.images.size() - 3; /* Actor.image is 2 short of its index (FIELDSTG_createActor) */
}

std::vector<OwnCharacter> read_own_characters(const Files &files) {
    std::vector<OwnCharacter> out;
    std::ifstream in(files.dir + "/mods/characters.json", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    Json j;
    if (in && Json::parse(ss.str(), j))
        for (auto &c : j["characters"].items)
            if (c[0].i() >= GAME_CHARACTERS && c[0].i() < 4096) out.push_back({c[0].i(), c[1].i(), c[2].i(), c[3].str});
    return out;
}

bool write_own_characters(const Files &files, const std::vector<OwnCharacter> &list, std::string *err) {
    auto quoted = [](const std::string &s) {
        std::string q = "\"";
        for (char c : s) q += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
        return q + "\"";
    };
    /* [id, animation file entry, width, name]: the port reads the first three */
    std::string out = "{\"characters\":[";
    for (size_t i = 0; i < list.size(); i++)
        out += (i ? ",\n[" : "\n[") + std::to_string(list[i].id) + "," + std::to_string(list[i].animFile) + "," +
               std::to_string(list[i].width) + "," + quoted(list[i].name) + "]";
    out += "]}\n";
    std::error_code ec;
    std::filesystem::create_directories(files.dir + "/mods", ec);
    std::string path = files.dir + "/mods/characters.json";
    std::ofstream o(path, std::ios::binary);
    if (!o.write(out.data(), (std::streamsize)out.size())) {
        if (err) *err = "can't write " + path;
        return false;
    }
    return true;
}

int register_character(const Files &files, const std::vector<uint8_t> &file, int width, int first, const std::string &name,
                       std::string *err) {
    std::vector<OwnCharacter> chars = read_own_characters(files);
    int id = std::max(GAME_CHARACTERS, first);
    for (auto &c : chars) id = std::max(id, c.id + 1);
    int fid = CHARACTER_FILES + id - GAME_CHARACTERS;
    chars.push_back({id, fid << 16, width, name});
    if (!files.write(fid, file)) {
        if (err) *err = "can't write mods/" + std::to_string(fid) + ".bin";
        return -1;
    }
    return write_own_characters(files, chars, err) ? id : -1;
}

bool delete_character(const Files &files, int id, std::string *err) {
    std::vector<OwnCharacter> chars = read_own_characters(files);
    auto it = std::find_if(chars.begin(), chars.end(), [id](const OwnCharacter &c) { return c.id == id; });
    if (it == chars.end()) return true;
    files.remove(it->animFile >> 16);
    chars.erase(it);
    return write_own_characters(files, chars, err);
}

std::vector<uint8_t> import_character(const Image &sheet, const CharTemplate &t, bool reuse, std::vector<std::string> *problems) {
    auto problem = [&](const std::string &p) {
        if (problems && problems->size() < 40) problems->push_back(p);
    };
    if (sheet.w != t.cols() * t.cellW || sheet.h != t.rows() * t.cellH) {
        problem("the sheet is " + std::to_string(sheet.w) + " x " + std::to_string(sheet.h) + " pixels; the template's is " +
                std::to_string(t.cols() * t.cellW) + " x " + std::to_string(t.rows() * t.cellH));
        return {};
    }
    auto opaque = [](uint32_t c) { return c >> 24 >= 128; };
    /* each drawing: its cell, or (reuse) the one before it in its row */
    std::vector<std::vector<int>> source(t.rows()); /* the column a drawing takes its pixels from */
    bool ok = true;
    for (int r = 0; r < t.rows(); r++) {
        int a = r / t.dirs();
        for (int c = 0; c < t.drawings(a); c++) {
            bool empty = true;
            std::set<uint32_t> colors;
            for (int y = 0; y < t.cellH; y++)
                for (int x = 0; x < t.cellW; x++) {
                    uint32_t col = sheet.px[(size_t)(r * t.cellH + y) * sheet.w + c * t.cellW + x];
                    if (opaque(col)) empty = false, colors.insert((col & 0xF8F8F8u));
                }
            if (empty && reuse && c > 0) source[r].push_back(source[r][c - 1]);
            else if (empty) problem(row_name(t, r) + ": drawing " + std::to_string(c + 1) + " is empty"), ok = false, source[r].push_back(c);
            else source[r].push_back(c);
            if (colors.size() > 15)
                problem(row_name(t, r) + ": drawing " + std::to_string(c + 1) + " has " + std::to_string(colors.size()) +
                        " colors; the game's take 15 and clear"),
                    ok = false;
        }
    }
    if (!ok) return {};
    /* the file: the sets, then each row's animation, then each drawing's picture */
    std::vector<std::vector<uint8_t>> entries(1);
    std::vector<int> animEntry(t.rows());
    std::map<std::pair<int, int>, int> timOf; /* (row, column) -> entry */
    for (int r = 0; r < t.rows(); r++)
        for (int c : std::set<int>(source[r].begin(), source[r].end())) {
            /* a 4-bit TIM of the cell, its 15 colors and clear */
            std::vector<uint16_t> pal;
            std::vector<uint8_t> idx((size_t)t.cellW * t.cellH, 0);
            for (int y = 0; y < t.cellH; y++)
                for (int x = 0; x < t.cellW; x++) {
                    uint32_t col = sheet.px[(size_t)(r * t.cellH + y) * sheet.w + c * t.cellW + x];
                    if (!opaque(col)) continue;
                    uint16_t v = (uint16_t)((col & 255) >> 3 | (col >> 8 & 255) >> 3 << 5 | (col >> 16 & 255) >> 3 << 10);
                    if (!v) v = 0x8000;
                    auto it = std::find(pal.begin(), pal.end(), v);
                    if (it == pal.end()) pal.push_back(v), it = pal.end() - 1;
                    idx[(size_t)y * t.cellW + x] = (uint8_t)(it - pal.begin() + 1);
                }
            std::vector<uint8_t> tim;
            put32(tim, 0x10), put32(tim, 8);
            put32(tim, 12 + 32), put16(tim, 0x280), put16(tim, 0x100), put16(tim, 16), put16(tim, 1);
            for (int i = 0; i < 16; i++) put16(tim, i == 0 || i > (int)pal.size() ? 0 : pal[i - 1]);
            put32(tim, 12 + (uint32_t)idx.size() / 2), put16(tim, 0x280), put16(tim, 0), put16(tim, t.cellW / 4), put16(tim, t.cellH);
            for (size_t i = 0; i < idx.size(); i += 2) tim.push_back((uint8_t)(idx[i] | idx[i + 1] << 4));
            timOf[{r, c}] = (int)entries.size();
            entries.push_back(tim);
        }
    for (int r = 0; r < t.rows(); r++) {
        const auto &anim = t.anims[r / t.dirs()];
        std::vector<uint8_t> fr;
        for (size_t k = 0; k < anim.order.size(); k++) {
            put32(fr, (uint32_t)anim.times[k]), put32(fr, (uint32_t)timOf[{r, source[r][anim.order[k]]}]);
            put32(fr, (uint32_t)-t.anchorX), put32(fr, (uint32_t)-t.anchorY);
        }
        put32(fr, 0);
        animEntry[r] = (int)entries.size();
        entries.push_back(fr);
    }
    std::vector<uint8_t> sets;
    for (int a = 0; a < (int)t.anims.size(); a++) {
        put16(sets, t.anims[a].set);
        for (int d = 0; d < 5; d++) put16(sets, animEntry[a * t.dirs() + dir_row(t, d)]);
    }
    for (int k = 0; k < 6; k++) put16(sets, 0);
    entries[0] = sets;
    /* the archive: the table with a 0 after it, each entry 4-aligned, whole sectors */
    std::vector<uint8_t> out((entries.size() + 1) * 4, 0);
    for (size_t i = 0; i < entries.size(); i++) {
        uint32_t o = (uint32_t)out.size();
        memcpy(&out[i * 4], &o, 4);
        out.insert(out.end(), entries[i].begin(), entries[i].end());
        while (out.size() % 4) out.push_back(0);
    }
    out.resize((out.size() + 2047) / 2048 * 2048, 0);
    return out;
}
