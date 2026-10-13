/* The background's tiles, read and written as the map file has them, and
   what changes the map as a whole: its size, its pictures, a new field. */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "field.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t rd16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static void put32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> i * 8));
}
static void put16(std::vector<uint8_t> &v, int x) { v.push_back((uint8_t)x), v.push_back((uint8_t)(x >> 8)); }

/* depth 2's sprites always take the field's color, the others only when it is set */
static bool tinted(int depth, const int color[4]) { return depth == 2 || color[3] != 0; }

void decode_tile(const std::vector<uint8_t> &chunk, const int color[4], Image out[3]) {
    for (int dep = 0; dep < 3; dep++) out[dep] = Image();
    if (chunk.size() < 8) return;
    std::vector<uint8_t> d = unrlen(chunk.data(), chunk.size());
    if (d.size() < 8 || rd32(d.data()) + 20 > d.size()) return;
    /* FIELDSTG_loadStreamSprites: the sprites of the three depths, then the 8-bit TIM */
    const uint8_t *tim = d.data() + rd32(d.data());
    uint32_t flag = rd32(tim + 4);
    const uint8_t *clut = tim + 8 + 12, *img = tim + 8;
    if (flag & 8) img = tim + 8 + rd32(tim + 8);
    int tw = rd16(img + 8) * 2, th = rd16(img + 10);
    const uint8_t *pix = img + 12;
    size_t p = 4;
    for (int dep = 0; dep < 3; dep++) {
        if (p + 4 > d.size()) break;
        int n = (int)rd32(&d[p]);
        p += 4;
        bool k = tinted(dep, color);
        int r = k ? color[0] : 128, g = k ? color[1] : 128, b = k ? color[2] : 128;
        for (int s = 0; s < n && p + 12 <= d.size(); s++, p += 12) {
            int x = rd16(&d[p]), y = rd16(&d[p + 2]), u = rd16(&d[p + 4]), v = rd16(&d[p + 6]);
            int w = rd16(&d[p + 8]), h = rd16(&d[p + 10]);
            Image &im = out[dep];
            if (!im.w) im.resize(128, 128);
            for (int yy = 0; yy < h; yy++)
                for (int xx = 0; xx < w; xx++) {
                    int px = x + xx, py = y + yy, tu = u + xx, tv = v + yy;
                    if (px < 0 || py < 0 || px >= 128 || py >= 128 || tu >= tw || tv >= th) continue;
                    if ((size_t)(pix - d.data()) + (size_t)tv * tw + tu >= d.size()) continue;
                    uint32_t c = rgba15((uint16_t)rd16(clut + pix[tv * tw + tu] * 2), r, g, b);
                    if (c) im.px[py * 128 + px] = c;
                }
        }
    }
}

/* RLEN: "RLEN", the unpacked size, then runs (0x80 | n, the byte) and
   literals (n, n bytes), up to a 0 (decompressorStep) */
static std::vector<uint8_t> rlen(const std::vector<uint8_t> &in) {
    std::vector<uint8_t> out = {'R', 'L', 'E', 'N'};
    put32(out, (uint32_t)in.size());
    size_t i = 0, lit = 0; /* lit: where the pending literal starts */
    auto flush = [&](size_t end) {
        while (lit < end) {
            size_t n = std::min<size_t>(127, end - lit);
            out.push_back((uint8_t)n);
            out.insert(out.end(), in.begin() + lit, in.begin() + lit + n);
            lit += n;
        }
    };
    while (i < in.size()) {
        size_t run = 1;
        while (i + run < in.size() && run < 127 && in[i + run] == in[i]) run++;
        if (run >= 3) {
            flush(i);
            out.push_back((uint8_t)(0x80 | run));
            out.push_back(in[i]);
            i += run, lit = i;
        } else i += run;
    }
    flush(in.size());
    out.push_back(0);
    return out;
}

/* Up to n colors for the 15-bit colors counted: all of them when they are
   few enough, else a median cut, each box its colors' weighted mean */
static std::vector<uint16_t> palette(const std::vector<std::pair<uint16_t, int>> &counts, size_t n) {
    if (counts.size() <= n) {
        std::vector<uint16_t> out;
        for (auto &c : counts) out.push_back(c.first);
        return out;
    }
    auto ch = [](uint16_t c, int k) { return c >> (k * 5) & 31; };
    struct Box { std::vector<std::pair<uint16_t, int>> c; };
    std::vector<Box> boxes{{counts}};
    while (boxes.size() < n) {
        /* the box with the widest channel splits at its weighted median */
        int best = -1, bestRange = 0, bestCh = 0;
        for (int i = 0; i < (int)boxes.size(); i++) {
            if (boxes[i].c.size() < 2) continue;
            for (int k = 0; k < 3; k++) {
                int lo = 31, hi = 0;
                for (auto &c : boxes[i].c) lo = std::min(lo, ch(c.first, k)), hi = std::max(hi, ch(c.first, k));
                if (hi - lo > bestRange) bestRange = hi - lo, best = i, bestCh = k;
            }
        }
        if (best < 0) break;
        auto &c = boxes[best].c;
        std::sort(c.begin(), c.end(), [&](auto &a, auto &b) { return ch(a.first, bestCh) < ch(b.first, bestCh); });
        long total = 0, acc = 0;
        for (auto &e : c) total += e.second;
        size_t cut = 1;
        for (; cut < c.size() - 1; cut++)
            if ((acc += c[cut - 1].second) * 2 >= total) break;
        Box other{std::vector<std::pair<uint16_t, int>>(c.begin() + cut, c.end())};
        c.resize(cut);
        boxes.push_back(std::move(other));
    }
    std::vector<uint16_t> out;
    for (auto &b : boxes) {
        long sum[3] = {}, w = 0;
        for (auto &c : b.c)
            for (int k = 0; k < 3; k++) sum[k] += (long)ch(c.first, k) * c.second;
        for (auto &c : b.c) w += c.second;
        uint16_t v = 0;
        for (int k = 0; k < 3; k++) v |= (uint16_t)((sum[k] + w / 2) / w) << (k * 5);
        out.push_back(v ? v : 0x8000);
    }
    return out;
}

std::vector<uint8_t> encode_tile(const Image layers[3], const int color[4], std::string *err) {
    /* what shows of each depth: the nearest first (3, then 7, then 11),
       each hiding what is under its opaque pixels */
    static const int NEAR_FIRST[3] = {1, 0, 2};
    std::vector<uint16_t> keep[3]; /* 15-bit, 0 for nothing */
    std::vector<bool> covered(128 * 128);
    for (int d : NEAR_FIRST) {
        keep[d].assign(128 * 128, 0);
        const Image &im = layers[d];
        if (im.w != 128 || im.h != 128) continue;
        bool k = tinted(d, color);
        for (int i = 0; i < 128 * 128; i++) {
            uint32_t c = im.px[i];
            if (c >> 24 < 128 || covered[i]) continue;
            covered[i] = true;
            int v[3] = {(int)(c & 255), (int)(c >> 8 & 255), (int)(c >> 16 & 255)};
            uint16_t c15 = 0;
            for (int ch = 0; ch < 3; ch++) {
                /* the field's color taken off (rgba15 multiplies by it / 128) */
                int x = k ? (color[ch] ? v[ch] * 128 / color[ch] : v[ch]) : v[ch];
                c15 |= (uint16_t)(std::min(255, x) >> 3) << (ch * 5);
            }
            keep[d][i] = c15 ? c15 : 0x8000; /* 0 is clear: black is 0x8000 */
        }
    }
    /* the sprites: each depth in strips of rows (four, else five to one: at
       most five sprites a depth), each strip's bounds, packed in shelves,
       the tallest first, in the 256x128 the tile's VRAM slot holds */
    struct Rect { int depth, x, y, w, h, u = 0, v = 0; };
    std::vector<Rect> rects;
    int usedW = 0, usedH = 0;
    bool packed = false;
    for (int bands : {4, 5, 3, 2, 1}) {
        rects.clear();
        for (int d = 0; d < 3; d++)
            for (int band = 0; band < bands; band++) {
                int x0 = 128, y0 = 128, x1 = -1, y1 = -1;
                for (int y = band * 128 / bands; y < (band + 1) * 128 / bands; y++)
                    for (int x = 0; x < 128; x++)
                        if (keep[d][y * 128 + x]) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
                if (x1 >= 0) rects.push_back({d, x0, y0, x1 - x0 + 1, y1 - y0 + 1});
            }
        if (rects.empty()) return {};
        std::vector<int> order(rects.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return rects[a].h > rects[b].h; });
        int sx = 0, sy = 0, sh = 0;
        usedW = usedH = 0;
        packed = true;
        for (int i : order) {
            Rect &r = rects[i];
            if (sx + r.w > 256) sy += sh, sx = 0, sh = 0;
            if (sy + r.h > 128) {
                packed = false;
                break;
            }
            r.u = sx, r.v = sy;
            sx += r.w, sh = std::max(sh, r.h);
            usedW = std::max(usedW, r.u + r.w), usedH = std::max(usedH, r.v + r.h);
        }
        if (packed) break;
    }
    if (!packed) {
        if (err) *err = "too much to show for the game's tile memory";
        return {};
    }
    int W = (usedW + 3) & ~3, H = usedH;
    /* the colors: index 0 clear, then up to 255 */
    std::map<uint16_t, int> count;
    for (auto &r : rects)
        for (int y = 0; y < r.h; y++)
            for (int x = 0; x < r.w; x++)
                if (uint16_t c = keep[r.depth][(r.y + y) * 128 + r.x + x]) count[c]++;
    std::vector<uint16_t> pal = palette(std::vector<std::pair<uint16_t, int>>(count.begin(), count.end()), 255);
    std::map<uint16_t, uint8_t> index;
    auto nearest = [&](uint16_t c) {
        auto it = index.find(c);
        if (it != index.end()) return it->second;
        int best = 0, bestD = 1 << 30;
        for (int i = 0; i < (int)pal.size(); i++) {
            int dist = 0;
            for (int k = 0; k < 3; k++) {
                int a = c >> (k * 5) & 31, b = pal[i] >> (k * 5) & 31;
                dist += (a - b) * (a - b);
            }
            if (dist < bestD) bestD = dist, best = i;
        }
        return index[c] = (uint8_t)(best + 1);
    };
    std::vector<uint8_t> pix((size_t)W * H, 0);
    for (auto &r : rects)
        for (int y = 0; y < r.h; y++)
            for (int x = 0; x < r.w; x++)
                if (uint16_t c = keep[r.depth][(r.y + y) * 128 + r.x + x]) pix[(size_t)(r.v + y) * W + r.u + x] = nearest(c);
    /* the tile: the TIM's offset, each depth's sprites, the TIM (8-bit, its CLUT) */
    std::vector<uint8_t> d(4, 0);
    for (int dep = 0; dep < 3; dep++) {
        int n = 0;
        for (auto &r : rects) n += r.depth == dep;
        put32(d, n);
        for (auto &r : rects)
            if (r.depth == dep) put16(d, r.x), put16(d, r.y), put16(d, r.u), put16(d, r.v), put16(d, r.w), put16(d, r.h);
    }
    while (d.size() % 4) d.push_back(0);
    uint32_t timOff = (uint32_t)d.size();
    memcpy(d.data(), &timOff, 4);
    put32(d, 0x10), put32(d, 9);
    put32(d, 12 + 512), put16(d, 0), put16(d, 0), put16(d, 256), put16(d, 1);
    for (int i = 0; i < 256; i++) put16(d, i == 0 || i > (int)pal.size() ? 0 : pal[i - 1]);
    put32(d, 12 + (uint32_t)pix.size()), put16(d, 0), put16(d, 0), put16(d, W / 2), put16(d, H);
    d.insert(d.end(), pix.begin(), pix.end());
    std::vector<uint8_t> out = rlen(d);
    out.resize((out.size() + 2047) / 2048 * 2048, 0);
    if (out.size() > (size_t)MAX_TILE_BYTES) {
        if (err) *err = "too detailed to fit the game's tile size";
        return {};
    }
    return out;
}

std::vector<uint8_t> Field::mapFileData() const {
    size_t room = 2048;
    int count = 0;
    for (auto &t : tileData) room = std::max(room, (t.size() + 2047) / 2048 * 2048), count += !t.empty();
    std::vector<uint8_t> out(2048 + (size_t)tilesW * tilesH * room, 0);
    int head[4] = {count, tilesW, tilesH, (int)room};
    memcpy(out.data(), head, 16);
    for (int t = 0; t < tilesW * tilesH; t++) {
        uint16_t size = (uint16_t)((tileData[t].size() + 2047) / 2048 * 2048);
        memcpy(&out[16 + t * 2], &size, 2);
        if (!tileData[t].empty()) memcpy(&out[2048 + t * room], tileData[t].data(), tileData[t].size());
    }
    return out;
}

void Field::resize(int left, int top, int right, int bottom) {
    int W = tilesW + left + right, H = tilesH + top + bottom;
    std::vector<Image> nt[3];
    std::vector<std::vector<uint8_t>> nd((size_t)W * H);
    for (auto &v : nt) v.assign((size_t)W * H, Image());
    for (int y = 0; y < tilesH; y++)
        for (int x = 0; x < tilesW; x++) {
            int nx = x + left, ny = y + top;
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            for (int d = 0; d < 3; d++) nt[d][ny * W + nx] = std::move(tiles[d][y * tilesW + x]);
            nd[ny * W + nx] = std::move(tileData[y * tilesW + x]);
        }
    for (int d = 0; d < 3; d++) tiles[d] = std::move(nt[d]);
    tileData = std::move(nd);
    tilesW = W, tilesH = H;
    int dx = left * 128, dy = top * 128;
    for (LogicMap &m : maps) {
        if (!m.w) continue;
        int nw = std::max(128, m.w + (left + right) * 128), nh = std::max(128, m.h + (top + bottom) * 128);
        std::vector<uint8_t> px((size_t)nw * nh, 0);
        for (int y = 0; y < m.h; y++)
            for (int x = 0; x < m.w; x++)
                if (x + dx >= 0 && y + dy >= 0 && x + dx < nw && y + dy < nh) px[(size_t)(y + dy) * nw + x + dx] = m.px[(size_t)y * m.w + x];
        m.w = nw, m.h = nh, m.px = std::move(px);
    }
    /* what stands on the map moves with it; the characters at 0, 0 are the events' */
    for (Actor &a : stage.actors)
        if (a.x || a.y) a.x += dx, a.y += dy;
    for (Object &o : stage.objects) {
        o.x += dx, o.y += dy;
        if (o.sortY) o.sortY += dy;
    }
    stage.startX += dx * 256, stage.startY += dy * 256;
    /* the exits that lead here from here arrive moved too; the other
       fields' move on saving (shiftX, shiftY) */
    for (Slot &s : stage.slots)
        for (auto &m : stage.modes)
            if (slot_leaves(s.type) && (int16_t)s.arg == m.mode) {
                s.x = (s.x + dx) & 0xFFFF, s.y = (s.y + dy) & 0xFFFF;
                break;
            }
    shiftX += dx, shiftY += dy;
    bgEdited = true;
}

/* Whether two of a tile's pictures show the same: an empty one is all clear */
static bool same_tile(const Image &a, const Image &b) {
    for (int i = 0; i < 128 * 128; i++) {
        uint32_t x = a.w ? a.px[i] : 0, y = b.w ? b.px[i] : 0;
        if (x >> 24 < 128) x = 0;
        if (y >> 24 < 128) y = 0;
        if (x != y) return false;
    }
    return true;
}

bool Field::importLayers(const Image *layers[3], std::string *err, int *changed) {
    for (int d = 0; d < 3; d++)
        if (layers[d] && (layers[d]->w != width() || layers[d]->h != height())) {
            if (err) *err = "the picture isn't the map's size";
            return false;
        }
    std::map<int, std::vector<uint8_t>> done;
    std::string bad;
    int badCount = 0;
    for (int t = 0; t < tilesW * tilesH; t++) {
        Image cand[3];
        bool same = true;
        for (int d = 0; d < 3; d++) {
            if (!layers[d]) {
                cand[d] = tiles[d][t];
                continue;
            }
            cand[d].resize(128, 128);
            int x0 = t % tilesW * 128, y0 = t / tilesW * 128;
            for (int y = 0; y < 128; y++)
                memcpy(&cand[d].px[y * 128], &layers[d]->px[(size_t)(y0 + y) * layers[d]->w + x0], 128 * 4);
            same &= same_tile(cand[d], tiles[d][t]);
        }
        if (same) continue;
        std::string e;
        std::vector<uint8_t> chunk = encode_tile(cand, stage.color, &e);
        if (!e.empty()) {
            if (badCount++ < 6) bad += (bad.empty() ? "" : "; ") + std::to_string(t % tilesW) + ", " + std::to_string(t / tilesW) + ": " + e;
            continue;
        }
        done[t] = std::move(chunk);
    }
    if (badCount) {
        if (err) *err = std::to_string(badCount) + " tiles can't be made (column, row): " + bad + (badCount > 6 ? "..." : "");
        return false;
    }
    for (auto &[t, chunk] : done) {
        Image back[3];
        decode_tile(chunk, stage.color, back);
        for (int d = 0; d < 3; d++) tiles[d][t] = std::move(back[d]);
        tileData[t] = std::move(chunk);
    }
    if (changed) *changed = (int)done.size();
    if (!done.empty()) bgEdited = true;
    return true;
}

Image Field::layerImage(int depth) const {
    Image out;
    out.resize(width(), height());
    for (int t = 0; t < tilesW * tilesH; t++) {
        const Image &im = tiles[depth][t];
        if (!im.w) continue;
        int x0 = t % tilesW * 128, y0 = t / tilesW * 128;
        for (int y = 0; y < 128; y++) memcpy(&out.px[(size_t)(y0 + y) * out.w + x0], &im.px[y * 128], 128 * 4);
    }
    return out;
}

bool make_new_field(const Files &files, const Field &from, int mode, int area, int place, int w, int h, NewField *out,
                    std::string *err, const std::vector<uint8_t> *floor) {
    Field f = from;
    Stage &s = f.stage;
    char name[16];
    snprintf(name, sizeof name, "new%03x", mode);
    s.templ = from.stage.isNew() ? from.stage.templ : from.stage.name;
    s.name = name;
    s.modes = {{mode, from.stage.modes.empty() ? 0 : from.stage.modes[0].file, area, place, false}};
    if (w > 0) {
        /* an empty map, walkable all over */
        f.tilesW = w, f.tilesH = h;
        for (auto &d : f.tiles) d.assign((size_t)w * h, Image());
        f.tileData.assign((size_t)w * h, {});
        for (int i = 0; i < 8; i++) {
            LogicMap &m = f.maps[i];
            if (!m.w) continue;
            m.w = w * 128, m.h = h * 128;
            m.px.assign((size_t)m.w * m.h, i == MAP_FLOOR0 ? 1 : 0);
            if (i == MAP_FLOOR0 && floor && floor->size() == m.px.size()) m.px = *floor;
        }
        s.objects.clear(), s.actors.clear(), s.slots.clear();
        s.startX = w * 64 * 256, s.startY = h * 64 * 256, s.startDir = 0;
    }
    /* its files: the background, and a copy of the logic maps' file (the
       sprite sheet's too, whose entries the stage's code may read) */
    int tilesFid = own_tiles_fid(s), mapsFid = tilesFid + 1, src = 0;
    for (int i = 0; i < 8 && !src; i++) src = s.mapFiles[i] >> 16;
    if (!src) src = s.sheetEntry >> 16;
    std::map<int, std::vector<uint8_t>> repl;
    for (int i = 0; i < 8; i++) {
        if (!s.mapFiles[i]) continue;
        int entry = s.mapFiles[i] & 0xFFFF;
        if (f.maps[i].w) {
            std::string e;
            repl[entry] = f.maps[i].encode(&e);
            if (repl[entry].empty()) {
                if (err) *err = "the logic map " + std::to_string(i) + " can't be written: " + e;
                return false;
            }
        }
        s.mapFiles[i] = mapsFid << 16 | entry;
    }
    if (src && (s.sheetEntry >> 16) == src) s.sheetEntry = mapsFid << 16 | (s.sheetEntry & 0xFFFF);
    std::vector<uint8_t> archive = src ? files.read(src) : std::vector<uint8_t>();
    if (src && archive.empty()) {
        if (err) *err = "the file " + std::to_string(src) + " is missing";
        return false;
    }
    if (src) out->files[mapsFid] = rebuild_archive(archive, repl);
    s.mapFile = tilesFid;
    out->files[tilesFid] = f.mapFileData();
    out->stage = s;
    return true;
}

/* --- imported sprites --- */

/* A page of the objects' images the editor made: 64 halfwords by 256 lines
   of 4-bit pixels, the palettes in rows 252-255 (4 a row), and this tag in
   the last palette's place */
static const uint16_t PAGE_TAG[4] = {0x5744, 0x4533, 0x5053, 0x0001}; /* "DW3ESP" */
static const int PAGE_W = 256, PAGE_H = 252, CLUT_ROW = 252; /* in pixels; palettes from row 252 */

static std::vector<uint8_t> tim_page(const std::vector<uint16_t> &px) {
    std::vector<uint8_t> t;
    put32(t, 0x10), put32(t, 2); /* 16-bit: the page as it is in VRAM */
    put32(t, 12 + 64 * 256 * 2), put16(t, 0x140), put16(t, 0x100), put16(t, 64), put16(t, 256);
    for (uint16_t h : px) put16(t, h);
    return t;
}

int Field::importSprite(const std::vector<Image> &frames, std::string *err) {
    auto fail = [&](const std::string &e) {
        if (err) *err = e;
        return -1;
    };
    size_t n;
    const uint8_t *sp = archive_entry(sheetFile_, stage.sheetEntry & 0xFFFF, &n);
    if (!stage.sheetEntry || !sp || n < 12) return fail("the field has no sprite sheet");
    std::vector<uint8_t> sheet(sp, sp + n);
    uint32_t partsOff = rd32(&sheet[0]), idsOff = rd32(&sheet[4]);
    if (partsOff >= n || idsOff >= n || idsOff < partsOff) return fail("the sprite sheet is unknown");
    std::vector<int> ids;
    for (size_t i = idsOff; i < n && sheet[i] != 0xFF; i++) ids.push_back(sheet[i]);
    if ((size_t)(ids.size() + 2) * 4 > partsOff) return fail("the sprite sheet is unknown");
    int nextId = 0;
    for (int id : ids) nextId = std::max(nextId, id + 1);
    if (nextId + (int)frames.size() > 0xFF) return fail("the sheet has no frame numbers left");

    /* the pages: each archive entry, a TIM */
    std::vector<std::vector<uint8_t>> pages;
    for (int i = 0;; i++) {
        const uint8_t *e = archive_entry(imagesData, i, &n);
        if (!e) break;
        pages.push_back(unrlen(e, n));
    }
    auto tagged = [&](const std::vector<uint8_t> &t) {
        return t.size() >= 20 + 64 * 256 * 2 && rd32(&t[4]) == 2 && memcmp(&t[20 + (255 * 64 + 60) * 2], PAGE_TAG, 8) == 0;
    };
    /* the colors: 15 and clear, for all the frames */
    std::map<uint16_t, int> count;
    struct Crop { int x0, y0, w, h; };
    std::vector<Crop> crops;
    for (const Image &im : frames) {
        int x0 = im.w, y0 = im.h, x1 = -1, y1 = -1;
        for (int y = 0; y < im.h; y++)
            for (int x = 0; x < im.w; x++) {
                uint32_t c = im.px[(size_t)y * im.w + x];
                if (c >> 24 < 128) continue;
                x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
                uint16_t v = (uint16_t)((c & 255) >> 3 | (c >> 8 & 255) >> 3 << 5 | (c >> 16 & 255) >> 3 << 10);
                count[v ? v : 0x8000]++;
            }
        if (x1 < 0) return fail("a frame is all clear");
        if (x1 - x0 + 1 > PAGE_W || y1 - y0 + 1 > PAGE_H) return fail("a frame is bigger than a page (256 x 252)");
        crops.push_back({x0, y0, x1 - x0 + 1, y1 - y0 + 1});
    }
    std::vector<uint16_t> pal = palette(std::vector<std::pair<uint16_t, int>>(count.begin(), count.end()), 15);
    auto nearest = [&](uint16_t c) {
        int best = 0, bestD = 1 << 30;
        for (int i = 0; i < (int)pal.size(); i++) {
            int dist = 0;
            for (int k = 0; k < 3; k++) {
                int a = c >> (k * 5) & 31, b = pal[i] >> (k * 5) & 31;
                dist += (a - b) * (a - b);
            }
            if (dist < bestD) bestD = dist, best = i;
        }
        return best + 1;
    };
    /* the existing parts: what they take of the editor's pages, pixels and palettes */
    struct Part { int16_t v[7]; };
    std::vector<Part> parts;
    for (uint32_t o = partsOff; o + 14 <= idsOff; o += 14) {
        Part p;
        memcpy(p.v, &sheet[o], 14);
        parts.push_back(p);
    }
    auto occupancy = [&](int page, std::vector<bool> &used, std::vector<bool> &cluts) {
        used.assign(PAGE_W * PAGE_H, false);
        cluts.assign(16, false);
        cluts[15] = true; /* the tag's place */
        for (const Part &p : parts) {
            int u = p.v[0], v = p.v[1], w = p.v[2], h = p.v[3], cx = p.v[4], cy = p.v[5], mode = p.v[6];
            if (mode == 0 && u / 256 == page)
                for (int y = v; y < v + h && y < PAGE_H; y++)
                    for (int x = u % 256; x < u % 256 + w && x < PAGE_W; x++) used[y * PAGE_W + x] = true;
            if (mode == 0 && cx / 64 == page && cy >= CLUT_ROW && cy < 256) cluts[(cy - CLUT_ROW) * 4 + cx % 64 / 16] = true;
        }
        /* and what the characters' frames and palettes take of it (a halfword, 4 pixels) */
        for (int y = 0; y < 256 && !vramTaken.empty(); y++)
            for (int x = 0; x < 64; x++)
                if (vramTaken[(size_t)y * 320 + page * 64 + x]) {
                    for (int k = 0; k < 4 && y < PAGE_H; k++) used[y * PAGE_W + x * 4 + k] = true;
                    if (y >= CLUT_ROW) cluts[(y - CLUT_ROW) * 4 + x / 16] = true;
                }
    };
    /* a page with room for every frame and a palette: one the editor made, else a new one */
    int page = -1, slot = -1;
    std::vector<std::pair<int, int>> at;
    for (int p = 0; p <= (int)pages.size() && page < 0; p++) {
        if (p == (int)pages.size()) {
            if (p >= 3) break;
            /* the characters' images and palettes share these pages: only a free one */
            bool free = true;
            for (int y = 0; y < 256 && free && !vramTaken.empty(); y++)
                for (int x = 0; x < 64 && free; x++) free = !vramTaken[(size_t)y * 320 + p * 64 + x];
            if (!free) break;
            std::vector<uint16_t> blank(64 * 256, 0);
            memcpy(&blank[255 * 64 + 60], PAGE_TAG, 8);
            pages.push_back(tim_page(blank));
        } else if (!tagged(pages[p])) continue;
        std::vector<bool> used, cluts;
        occupancy(p, used, cluts);
        int s = -1;
        for (int k = 0; k < 16 && s < 0; k++)
            if (!cluts[k]) s = k;
        if (s < 0) continue;
        std::vector<std::pair<int, int>> places;
        bool fits = true;
        for (const Crop &c : crops) {
            bool found = false;
            for (int y = 0; y + c.h <= PAGE_H && !found; y++)
                for (int x = 0; x + c.w <= PAGE_W && !found; x += 4) {
                    bool free = true;
                    for (int yy = 0; yy < c.h && free; yy++)
                        for (int xx = 0; xx < c.w && free; xx++) free = !used[(y + yy) * PAGE_W + x + xx];
                    if (!free) continue;
                    for (int yy = 0; yy < c.h; yy++)
                        for (int xx = 0; xx < c.w; xx++) used[(y + yy) * PAGE_W + x + xx] = true;
                    places.push_back({x, y});
                    found = true;
                }
            fits &= found;
        }
        if (fits) page = p, slot = s, at = places;
        else if (p == (int)pages.size() - 1 && !tagged(pages[p])) pages.pop_back();
    }
    if (page < 0) return fail("the field's VRAM has no page left for it: the objects' and the characters' images take them");

    /* the pixels and the palette into the page */
    std::vector<uint8_t> &t = pages[page];
    uint16_t *px = (uint16_t *)&t[20];
    for (size_t f = 0; f < frames.size(); f++) {
        const Image &im = frames[f];
        const Crop &c = crops[f];
        for (int y = 0; y < c.h; y++)
            for (int x = 0; x < c.w; x++) {
                uint32_t col = im.px[(size_t)(c.y0 + y) * im.w + c.x0 + x];
                if (col >> 24 < 128) continue;
                uint16_t v = (uint16_t)((col & 255) >> 3 | (col >> 8 & 255) >> 3 << 5 | (col >> 16 & 255) >> 3 << 10);
                int idx = nearest(v ? v : 0x8000), X = at[f].first + x, Y = at[f].second + y;
                uint16_t &h = px[Y * 64 + X / 4];
                h = (uint16_t)((h & ~(0xF << (X % 4 * 4))) | idx << (X % 4 * 4));
            }
    }
    int clutX = slot % 4 * 16, clutY = CLUT_ROW + slot / 4;
    for (int i = 0; i < 16; i++) px[clutY * 64 + clutX + i] = i == 0 || i > (int)pal.size() ? 0 : pal[i - 1];

    /* the sheet again: the parts, the ids, the frames */
    std::vector<std::vector<uint8_t>> frameData;
    for (size_t k = 0; k < ids.size(); k++) {
        uint32_t o = rd32(&sheet[(k + 2) * 4]);
        int cnt = o + 2 <= sheet.size() ? rd16(&sheet[o]) : 0;
        frameData.emplace_back(sheet.begin() + o, sheet.begin() + std::min(sheet.size(), (size_t)o + 6 + cnt * 6));
    }
    int firstPart = (int)parts.size();
    for (size_t f = 0; f < frames.size(); f++) {
        const Crop &c = crops[f];
        Part p = {{(int16_t)(page * 256 + at[f].first), (int16_t)at[f].second, (int16_t)c.w, (int16_t)c.h,
                   (int16_t)(page * 64 + clutX), (int16_t)clutY, 0}};
        parts.push_back(p);
        std::vector<uint8_t> fd;
        put16(fd, 1), put16(fd, 0), put16(fd, -1);
        /* anchored at the frame's bottom middle */
        put16(fd, firstPart + (int)f), put16(fd, c.x0 - frames[f].w / 2), put16(fd, c.y0 - frames[f].h);
        frameData.push_back(fd);
        ids.push_back(nextId + (int)f);
    }
    std::vector<uint8_t> out((ids.size() + 2) * 4, 0);
    uint32_t pOff = (uint32_t)out.size();
    for (const Part &p : parts) out.insert(out.end(), (const uint8_t *)p.v, (const uint8_t *)p.v + 14);
    while (out.size() % 4) out.push_back(0);
    uint32_t iOff = (uint32_t)out.size();
    for (int id : ids) out.push_back((uint8_t)id);
    out.push_back(0xFF);
    while (out.size() % 4) out.push_back(0);
    memcpy(&out[0], &pOff, 4), memcpy(&out[4], &iOff, 4);
    for (size_t k = 0; k < frameData.size(); k++) {
        uint32_t o = (uint32_t)out.size();
        memcpy(&out[(k + 2) * 4], &o, 4);
        out.insert(out.end(), frameData[k].begin(), frameData[k].end());
        while (out.size() % 4) out.push_back(0);
    }
    sheetFile_ = rebuild_archive(sheetFile_, {{stage.sheetEntry & 0xFFFF, out}});
    /* the images: every page, as an archive whose table ends with a 0
       (timLoaderLoadArchive reads up to it) */
    std::vector<uint8_t> arch((pages.size() + 1) * 4, 0);
    for (size_t i = 0; i < pages.size(); i++) {
        uint32_t o = (uint32_t)arch.size();
        memcpy(&arch[i * 4], &o, 4);
        arch.insert(arch.end(), pages[i].begin(), pages[i].end());
        while (arch.size() % 4) arch.push_back(0);
    }
    imagesData = arch;
    sheetEdited = imagesEdited = true;
    return nextId;
}

int free_field_mode(const std::vector<Stage> &stages) {
    for (int m = 0x2F0; m < 0x400; m++) {
        bool used = m == 0x300;
        for (const Stage &s : stages)
            for (auto &k : s.modes) used |= k.mode == m;
        if (!used) return m;
    }
    return 0;
}

bool write_new_field(const Files &files, const NewField &nf, std::string *err) {
    std::error_code ec;
    std::filesystem::create_directories(files.dir + "/mods/stages", ec);
    for (auto &[fid, data] : nf.files)
        if (!files.write(fid, data)) {
            if (err) *err = "can't write mods/" + std::to_string(fid) + ".bin";
            return false;
        }
    std::string json = new_field_json(nf.stage);
    std::ofstream f(files.stageModPath(nf.stage.name), std::ios::binary);
    if (!f.write(json.data(), (std::streamsize)json.size())) {
        if (err) *err = "can't write " + files.stageModPath(nf.stage.name);
        return false;
    }
    return true;
}
