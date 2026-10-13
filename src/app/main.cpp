/* DW3 Field Editor: Digimon World 2003's fields (the maps you walk), with
   their logic maps, objects, exits and characters, read from the disc, for
   the native port.

   dw3fe [stage]                    opens the editor (and a stage, e.g. wstag330)
   dw3fe --shot out.png stage       draws a stage's map to a picture and exits
   dw3fe --check                    reads every stage and prints what it found
   dw3fe --selftest                 checks what saving rests on over every stage
   dw3fe --encounters               prints the game's encounters: their enemies and the stages with them
   dw3fe stage --select actor:N     opens a stage with something selected
                                    (actor:N, slot:N, object:N, event:N, start)
         [--window-shot out.png]    and saves the window and its properties as drawn, then quits
   dw3fe --data DIR                 the port's data folder (files/, mods/, stages/), or the editor's own
   dw3fe --disc IMAGE               the game's disc (.cue, .bin or .iso): the fields' tables and files
                                    from it, when the data folder has none
   dw3fe --dump-disc IMAGE DIR      the fields' tables from the disc into DIR, as the port's dump
   dw3fe --new-field stage [--size WxH] [--background picture.png]
                                    a new field from a stage: a copy, or an empty map
         [--toyagumon DIR [--cell N]]
                                    its map a Toyagumon3D game export (unzipped), a cell N pixels wide
   dw3fe stage --add-object picture.png X,Y [--frames N]
                                    an object from a picture at a point, saved
   dw3fe --export-template HEX|standard out.png
                                    a character's template, its layout inside
   dw3fe --import-character sheet.png
                                    a filled template as a character of your own
   dw3fe stage --add-character HEX X,Y
                                    a character with a slot of its own at a point, saved
   dw3fe --export-fields out.dw3field stage[,stage...]
                                    saved fields as a file to share
   dw3fe --import-fields in.dw3field
                                    someone's shared fields into yours

   The stages' tables come from the disc (DATA/stages, made once), or from the
   port: dw2003 --dump-stages DATA/stages. */
#include <QApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QPushButton>
#include <QDir>
#include <QFileDialog>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStyleFactory>
#include <QScrollArea>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <cstdio>
#include "mainwindow.h"
#include "characters.h"
#include "disc.h"
#include "codes.h"
#include "delta.h"
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

/* the disc's executable, Digimon World 2003 (Europe, SLES-03936) */
static const char EXE_SHA1[] = "d1b7e4d646e3a9c2b88fdb25d20b5f7116bbb06d";

static void attach_console() {
#ifdef _WIN32
    if (_isatty(1) || GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
}

/* The stages of DIR/stages, a .json file each, and the new fields of DIR/mods/stages (new*.json) */
static std::vector<Stage> load_stages(const QString &dir) {
    std::vector<Stage> out;
    QFileInfoList list = QDir(dir + "/stages").entryInfoList({"*.json"}, QDir::Files);
    if (!list.isEmpty()) list += QDir(dir + "/mods/stages").entryInfoList({"new*.json"}, QDir::Files);
    for (const QFileInfo &fi : list) {
        QFile f(fi.filePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        Json j;
        Stage s;
        if (Json::parse(f.readAll().toStdString(), j) && s.load(j) && !s.name.empty()) out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.name < b.name; });
    return out;
}

/* The map composed, as --shot draws it */
static QImage compose(const Field &f) {
    QImage img(f.width(), f.height(), QImage::Format_RGBA8888);
    img.fill(Qt::black);
    QPainter p(&img);
    struct Item { int depth, kind, index; };
    std::vector<Item> items;
    for (int d = 0; d < 3; d++) items.push_back({TILE_DEPTHS[d], 0, d});
    for (int i = 0; i < (int)f.stage.objects.size(); i++)
        if (f.stage.objects[i].visible) items.push_back({f.stage.objects[i].depth, 1, i});
    for (int i = 0; i < (int)f.actors.size(); i++) items.push_back({4, 2, i});
    std::stable_sort(items.begin(), items.end(), [](auto &a, auto &b) { return a.depth > b.depth; });
    auto q = [](const Image &i) { return QImage((const uchar *)i.px.data(), i.w, i.h, i.w * 4, QImage::Format_RGBA8888); };
    for (auto &it : items) {
        if (it.kind == 0) {
            for (int t = 0; t < f.tilesW * f.tilesH; t++)
                if (f.tiles[it.index][t].w) p.drawImage(t % f.tilesW * 128, t / f.tilesW * 128, q(f.tiles[it.index][t]));
        } else if (it.kind == 1) {
            const Sprite &s = f.objects[it.index];
            if (s.img.w) p.drawImage(f.stage.objects[it.index].x + s.ox, f.stage.objects[it.index].y + s.oy, q(s.img));
        } else {
            const Sprite &s = f.actors[it.index];
            if (s.img.w) p.drawImage(f.stage.actors[it.index].x + s.ox, f.stage.actors[it.index].y + s.oy, q(s.img));
        }
    }
    return img;
}

/* A Toyagumon3D "Export for game" folder (world.png, depth.png,
   world_grid.json and .bin) as an empty map: the picture scaled so a cell's
   top is cellPx wide, in the middle of whole tiles.
   The floor, from the grid: walkable over each cell's top, higher floors and
   nearer ones over the rest, and blocked along a cell's edges where the
   floor next to it isn't within a half block (lower, or none: water, the
   island's rim). A world of sculpted ground has none there: then from
   depth.png, each pixel's place in the world, walkable where it is flat and
   above the lowest surface (the water), blocked where the next pixel is a
   half block higher or lower, in 8-pixel cells.
   The game has no depth test, only a background drawn over the characters:
   a pixel goes there when it is nearer than a character standing on the
   floor below it (a billboard CHAR_PX tall, as deep as its feet less its
   height) would be. The start: the floor nearest the middle */
struct ToyaMap {
    int w = 0, h = 0, startX = 0, startY = 0;
    Image bg, front;
    std::vector<uint8_t> floor;
};
static bool load_toyagumon(const QString &dir, int cellPx, ToyaMap *t, QString *err) {
    QFile jf(dir + "/world_grid.json"), bf(dir + "/world_grid.bin");
    Json j;
    QImage pic(dir + "/world.png"), dimg(dir + "/depth.png");
    if (!jf.open(QIODevice::ReadOnly) || !Json::parse(jf.readAll().toStdString(), j) || !bf.open(QIODevice::ReadOnly) ||
        pic.isNull() || dimg.isNull())
        return *err = "no world.png, depth.png, world_grid.json and world_grid.bin in " + dir, false;
    QByteArray grid = bf.readAll();
    const Json &v = j["view"], &dj = v["depth"];
    double px[4], py[4], dc[4], bh = j["blockHeight"].num, nearD = dj["near"].num, farD = dj["far"].num;
    for (int k = 0; k < 4; k++) px[k] = v["px"][k].num, py[k] = v["py"][k].num, dc[k] = dj["coefficients"][k].num;
    int gw = j["width"].i(), gh = j["height"].i(), fpc = j["floorsPerCell"].i(), oi = j["origin"]["i"].i(),
        ok = j["origin"]["j"].i();
    if (gw < 1 || gh < 1 || fpc < 1 || grid.size() < (qsizetype)gw * gh * fpc || px[0] == 0)
        return *err = "world_grid.bin doesn't match world_grid.json", false;
    double s = cellPx / (2 * std::fabs(px[0]));
    int pw = qRound(pic.width() * s), ph = qRound(pic.height() * s);
    t->w = (pw + 127) / 128, t->h = (ph + 127) / 128;
    if (t->w * t->h > MAX_TILES)
        return *err = QString("%1 x %2 tiles, more than %3: give a smaller --cell").arg(t->w).arg(t->h).arg(MAX_TILES), false;
    int ox = (t->w * 128 - pw) / 2, oy = (t->h * 128 - ph) / 2, W = t->w * 128, H = t->h * 128;
    QImage canvas(W, H, QImage::Format_ARGB32);
    canvas.fill(Qt::black);
    QPainter p(&canvas);
    p.drawImage(ox, oy, pic.scaled(pw, ph, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    p.end();

    /* each pixel's depth (-1: nothing) and its height in the world: the
       picture's x, y and the depth undone (the view's three rows inverted) */
    dimg = dimg.convertToFormat(QImage::Format_RGB32);
    std::vector<double> depth((size_t)W * H, -1), height((size_t)W * H, 0);
    double m[3][3] = {{px[0], px[1], px[2]}, {py[0], py[1], py[2]}, {dc[0], dc[1], dc[2]}};
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                 m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    /* the middle row of the inverse: the height from the three */
    double hy[3] = {-(m[1][0] * m[2][2] - m[1][2] * m[2][0]) / det, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det,
                    -(m[0][0] * m[1][2] - m[0][2] * m[1][0]) / det};
    double lowest = 1e18;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sx = (int)((x - ox) / s), sy = (int)((y - oy) / s);
            if (x < ox || y < oy || sx >= dimg.width() || sy >= dimg.height()) continue;
            QRgb e = dimg.pixel(sx, sy);
            if (qBlue(e) != 255) continue;
            size_t at = (size_t)y * W + x;
            depth[at] = nearD + (qRed(e) << 8 | qGreen(e)) / 65535.0 * (farD - nearD);
            double X = (x - ox) / s - px[3], Y = (y - oy) / s - py[3];
            height[at] = hy[0] * X + hy[1] * Y + hy[2] * (depth[at] - dc[3]);
            lowest = std::min(lowest, height[at]);
        }

    auto floorAt = [&](int i, int k, int f) { return (uint8_t)grid[(k * gw + i) * fpc + f]; };
    auto pt = [&](double i, double y, double k) { /* (i, k) in the grid, from its origin */
        i += oi, k += ok;
        return QPointF(ox + s * (px[0] * i + px[1] * y + px[2] * k + px[3]), oy + s * (py[0] * i + py[1] * y + py[2] * k + py[3]));
    };
    struct Cell { int i, k, b; };
    std::vector<Cell> cells;
    for (int k = 0; k < gh; k++)
        for (int i = 0; i < gw; i++)
            for (int f = 0; f < fpc; f++)
                if (floorAt(i, k, f)) cells.push_back({i, k, floorAt(i, k, f)});
    t->floor.assign((size_t)W * H, 0);
    if (!cells.empty()) {
        /* low to high, then far to near (up the picture first) */
        std::stable_sort(cells.begin(), cells.end(), [&](const Cell &a, const Cell &b) {
            return a.b != b.b ? a.b < b.b : py[0] * a.i + py[2] * a.k < py[0] * b.i + py[2] * b.k;
        });
        QImage fl(W, H, QImage::Format_Grayscale8);
        fl.fill(0);
        QPainter q(&fl);
        q.setPen(Qt::NoPen), q.setBrush(Qt::white);
        for (auto &c : cells) {
            double y = (c.b - 1) / 2.0 * bh;
            QPointF d[4] = {pt(c.i, y, c.k), pt(c.i + 1, y, c.k), pt(c.i + 1, y, c.k + 1), pt(c.i, y, c.k + 1)};
            q.drawPolygon(d, 4);
        }
        /* the edges a step too high: on the higher cell's side only, as its top hides the lower one's */
        q.setPen(QPen(Qt::black, 6, Qt::SolidLine, Qt::FlatCap));
        static const int SIDES[4][6] = {{1, 0, 1, 0, 1, 1}, {-1, 0, 0, 0, 0, 1}, {0, 1, 0, 1, 1, 1}, {0, -1, 0, 0, 1, 0}};
        for (auto &c : cells)
            for (auto &e : SIDES) {
                int ni = c.i + e[0], nk = c.k + e[1], top = 0;
                bool joins = false;
                for (int f = 0; ni >= 0 && nk >= 0 && ni < gw && nk < gh && f < fpc; f++) {
                    int b = floorAt(ni, nk, f);
                    joins |= b && std::abs(b - c.b) <= 1, top = std::max(top, b);
                }
                if (joins || top > c.b) continue;
                double y = (c.b - 1) / 2.0 * bh;
                q.drawLine(pt(c.i + e[2], y, c.k + e[3]), pt(c.i + e[4], y, c.k + e[5]));
            }
        q.end();
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) t->floor[(size_t)y * W + x] = fl.constScanLine(y)[x] > 127;
    } else {
        /* flat: a pixel up the picture on a wall is this much higher */
        double wall = 1 / (s * std::fabs(py[1]));
        std::vector<uint8_t> walk((size_t)W * H, 0);
        for (int y = 1; y < H - 1; y++)
            for (int x = 1; x < W - 1; x++) {
                size_t at = (size_t)y * W + x;
                if (depth[at] < 0 || height[at] < lowest + bh / 4) continue;
                bool ok = true;
                for (int n : {-1, 1, -W, W}) {
                    double dh = depth[at + n] < 0 ? 1e9 : std::fabs(height[at + n] - height[at]);
                    ok &= dh < wall * 0.35;
                }
                walk[at] = ok;
            }
        /* in the logic map's smallest cells: walkable when most of it is */
        for (int y = 0; y < H; y += 8)
            for (int x = 0; x < W; x += 8) {
                int n = 0;
                for (int b = 0; b < 8; b++)
                    for (int a = 0; a < 8; a++) n += walk[(size_t)(y + b) * W + x + a];
                for (int b = 0; n > 40 && b < 8; b++)
                    for (int a = 0; a < 8; a++) t->floor[(size_t)(y + b) * W + x + a] = 1;
            }
    }

    /* the front: depth.png's depth against a character's on the floor below */
    QImage front(W, H, QImage::Format_ARGB32);
    front.fill(Qt::transparent);
    const int CHAR_PX = 40;
    const double upDepth = dc[1] / (s * std::fabs(py[1])); /* a sprite's depth, a pixel up */
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double dp = depth[(size_t)y * W + x];
            if (dp < 0) continue;
            bool over = false;
            for (int up = 2; up <= CHAR_PX && !over && y + up < H; up += 2)
                for (int dx : {-6, 0, 6}) {
                    size_t at = (size_t)(y + up) * W + std::clamp(x + dx, 0, W - 1);
                    if (t->floor[at] && depth[at] >= 0 && dp < depth[at] + upDepth * up - 0.25) {
                        over = true;
                        break;
                    }
                }
            if (over) front.setPixel(x, y, canvas.pixel(x, y));
        }
    t->bg = fromQImage(canvas, W, H);
    t->front = fromQImage(front, W, H);

    /* the start, on the floor nearest the middle */
    double best = 1e18;
    for (int y = 0; y < H; y += 4)
        for (int x = 0; x < W; x += 4)
            if (t->floor[(size_t)y * W + x] && std::hypot(x - W / 2.0, y - H / 2.0) < best)
                best = std::hypot(x - W / 2.0, y - H / 2.0), t->startX = x * 256, t->startY = y * 256;
    if (best == 1e18) return *err = "the world has no floor", false;
    return true;
}

static uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* What saving rests on, over every stage: each logic map encoded and read
   back the same, inside its rebuilt file; a stage's mod empty when nothing
   changed, and carrying a change back when one did */
static int run_selftest(const Files &files, const std::vector<Stage> &stages, const std::vector<int> &animFiles) {
    int maps = 0, bad = 0;
    long before = 0, after = 0;
    for (const Stage &s : stages) {
        std::map<int, std::map<int, std::vector<uint8_t>>> byFile;
        for (int i = 0; i < 8; i++) {
            if (!s.mapFiles[i]) continue;
            int fid = s.mapFiles[i] >> 16, entry = s.mapFiles[i] & 0xFFFF;
            std::vector<uint8_t> f = files.readOriginal(fid);
            size_t n;
            const uint8_t *e = archive_entry(f, entry, &n);
            LogicMap m, back;
            if (!e || !m.load(e, n)) continue;
            std::string err;
            std::vector<uint8_t> enc = m.encode(&err);
            maps++;
            before += (long)n, after += (long)enc.size();
            if (enc.empty() || !back.load(enc.data(), enc.size()) || back.px != m.px || back.w != m.w || back.h != m.h) {
                printf("%s: logic map %d doesn't come back (%s)\n", s.name.c_str(), i, err.c_str());
                bad++;
                continue;
            }
            byFile[fid][entry] = enc;
        }
        /* the files rebuilt with the encoded maps: every entry reads as before */
        for (auto &[fid, repl] : byFile) {
            std::vector<uint8_t> orig = files.readOriginal(fid), re = rebuild_archive(orig, repl);
            for (int k = 0; k < 64; k++) {
                size_t n1, n2;
                const uint8_t *a = archive_entry(orig, k, &n1), *b = archive_entry(re, k, &n2);
                if (!a && !b) continue;
                bool same;
                if (repl.count(k)) {
                    LogicMap m1, m2;
                    same = a && b && m1.load(a, n1) && m2.load(b, n2) && m1.px == m2.px;
                } else
                    same = a && b && n2 >= n1 - 3 && memcmp(a, b, std::min(n1, n2)) == 0;
                if (!same) printf("%s: file %d entry %d changed in the rebuild\n", s.name.c_str(), fid, k), bad++;
            }
        }
        /* the events' scripts read as commands and written back the same */
        for (const Event &e : s.events)
            if (e.scripted && build_script(parse_script(e.script)) != e.script)
                printf("%s: event %d's script doesn't come back\n", s.name.c_str(), e.id), bad++;
        /* the mod: nothing for an unchanged stage; a moved character carried back */
        if (!stage_mod_json(s, s).empty()) printf("%s: a mod with no change\n", s.name.c_str()), bad++;
        if (!s.actors.empty()) {
            Stage edited = s;
            edited.actors[0].x += 5;
            edited.startDir = (s.startDir + 1) & 7;
            Json j;
            Stage applied = s;
            if (!Json::parse(stage_mod_json(edited, s), j)) printf("%s: the mod doesn't parse\n", s.name.c_str()), bad++;
            applied.loadSections(j);
            if (!stage_mod_json(applied, edited).empty()) printf("%s: the mod doesn't carry the change\n", s.name.c_str()), bad++;
        }
        /* a shared field's file: its map file with some bytes changed, as a
           delta from the game's that carries little more than them */
        if (std::vector<uint8_t> orig = files.readOriginal(s.mapFile); orig.size() > 4096) {
            std::vector<uint8_t> changed = orig, back;
            for (size_t k = 2048; k < 3048; k++) changed[k] ^= 0x5A;
            std::vector<uint8_t> d = delta_encode(changed, {&orig});
            if (!delta_decode(d, {&orig}, &back) || back != changed || d.size() > 1200)
                printf("%s: the map file's delta doesn't come back (%zu bytes)\n", s.name.c_str(), d.size()), bad++;
        }
    }
    /* the background: every tile encoded and read back, the same when it has
       few enough colors; the map file rebuilt as it was */
    std::set<int> seen;
    int tiles = 0, exact = 0, drawn = 0, sprites = 0;
    for (const Stage &s : stages) {
        if (!s.mapFile || s.isNew() || !seen.insert(s.mapFile).second) continue;
        Field f;
        if (!f.load(files, s)) continue;
        for (int t = 0; t < f.tilesW * f.tilesH; t++) {
            if (f.tileData[t].empty()) continue;
            Image layers[3], back[3];
            for (int d = 0; d < 3; d++) layers[d] = f.tiles[d][t];
            std::string err;
            std::vector<uint8_t> chunk = encode_tile(layers, s.color, &err);
            tiles++;
            if (!err.empty()) {
                printf("%s: tile %d can't be encoded: %s\n", s.name.c_str(), t, err.c_str()), bad++;
                continue;
            }
            decode_tile(chunk, s.color, back);
            /* what shows, nearest depth first, the same */
            bool same = true;
            for (int i = 0; i < 128 * 128 && same; i++) {
                uint32_t a = 0, b = 0;
                for (int d : {1, 0, 2}) {
                    if (!a && layers[d].w) a = layers[d].px[i];
                    if (!b && back[d].w) b = back[d].px[i];
                }
                same = a == b;
            }
            exact += same;
        }
        /* tiles added on every side and taken away again: the map as it was */
        Field g = f;
        g.resize(1, 2, 1, 2);
        g.resize(-1, -2, -1, -2);
        bool back = g.tilesW == f.tilesW && g.tileData == f.tileData && stage_mod_json(g.stage, f.stage).empty();
        for (int i = 0; i < 8; i++) back &= g.maps[i].px == f.maps[i].px;
        if (!back) printf("%s: resizing back and forth changed the map\n", s.name.c_str()), bad++;
        /* a sprite imported where there is room: the sheet's frames drawn as before, the new ones as the picture */
        if (s.sheetEntry) {
            Field h = f;
            Image pic;
            pic.resize(24, 32);
            for (int y = 4; y < 32; y++)
                for (int x = 2; x < 22; x++) pic.px[(size_t)y * 24 + x] = 0xFF000000u | (uint32_t)(x * 10) << 8 | (uint32_t)(y * 6) << 16 | 0x40;
            std::string err;
            int id = h.importSprite({pic, pic}, &err);
            sprites += id >= 0;
            if (id >= 0) {
                size_t n0, n1;
                const uint8_t *a = archive_entry(f.sheetFile_, s.sheetEntry & 0xFFFF, &n0), *b = archive_entry(h.sheetFile_, s.sheetEntry & 0xFFFF, &n1);
                h.vram = Vram();
                h.reloadImages(files, nullptr);
                int neutral[3] = {128, 128, 128};
                /* the frames the sheet has (the drawer reads past its ids for one it hasn't) */
                for (uint32_t k = rd32le(a + 4); k < n0 && a[k] != 0xFF; k++) {
                    int fr = a[k];
                    Sprite x = draw_sheet_frame(a, n0, fr, f.vram, 0x140, 0x100, 0, 0x1F0, 0, neutral);
                    Sprite y = draw_sheet_frame(b, n1, fr, h.vram, 0x140, 0x100, 0, 0x1F0, 0, neutral);
                    if (x.img.px != y.img.px || x.ox != y.ox) printf("%s: frame %d changed with a sprite imported\n", s.name.c_str(), fr), bad++;
                }
                Sprite y = draw_sheet_frame(b, n1, id + 1, h.vram, 0x140, 0x100, 0, 0x1F0, 0, neutral);
                if (y.img.w != 20 || y.img.h != 28 || y.ox != -10 || y.oy != -28) printf("%s: the imported sprite draws wrong\n", s.name.c_str()), bad++;
            }
        }
        std::vector<uint8_t> data = f.mapFileData(), orig = files.read(s.mapFile);
        int room;
        memcpy(&room, &data[12], 4);
        if (memcmp(data.data(), orig.data(), 12) != 0 || memcmp(&data[16], &orig[16], f.tilesW * f.tilesH * 2) != 0)
            printf("%s: the map file's header changed\n", s.name.c_str()), bad++;
        for (int t = 0; t < f.tilesW * f.tilesH; t++)
            if (!f.tileData[t].empty() && memcmp(&data[2048 + (size_t)t * room], f.tileData[t].data(), f.tileData[t].size()) != 0)
                printf("%s: tile %d moved in the map file\n", s.name.c_str(), t), bad++;
        drawn++;
    }
    /* the characters: each one's template exported, imported as a file and
       exported again the same (those within 15 colors) */
    int exported = 0, back = 0;
    for (int c = 0; c < (int)animFiles.size(); c++) {
        CharTemplate t, t2;
        Image sheet, sheet2;
        std::string err;
        if (!animFiles[c] || !export_character(files, animFiles[c], 32, &t, &sheet, &err)) continue;
        exported++;
        std::vector<std::string> problems;
        std::vector<uint8_t> f = import_character(sheet, t, false, &problems);
        if (f.empty()) continue; /* more than 15 colors, or a drawing the game leaves empty */
        if (!export_character(f, 0, 32, &t2, &sheet2, &err) || t2.json() != t.json() || sheet2.px != sheet.px)
            printf("character %d doesn't come back as a template\n", c), bad++;
        else back++;
    }
    /* the texts: every string of the fields' text files and the banner's names, in every
       language, as the editor writes it and back the same; each file rebuilt the same */
    std::set<int> textFiles = {AREA_NAMES_FILE, PLACE_NAMES_FILE};
    for (const Stage &s : stages) textFiles.insert(s.textFile);
    int strings = 0, readable = 0;
    for (int english : textFiles)
        for (int lang = 0; lang < LANG_COUNT; lang++) {
            std::vector<uint8_t> file = files.readOriginal(english + lang);
            auto raw = read_raw_strings(file);
            for (auto &str : raw) {
                GameString g = bytes_to_text(str);
                strings++;
                readable += g.text.compare(0, 1, "{") != 0 || str.empty();
                if (text_to_bytes(g) != str) printf("file %d: a string doesn't come back\n", english + lang), bad++;
            }
            std::vector<uint8_t> again = build_text_file(raw);
            if (again.size() > file.size() || memcmp(again.data(), file.data(), again.size()) != 0)
                printf("file %d: rebuilt differently\n", english + lang), bad++;
        }
    printf("%zu stages, %d logic maps (%ld bytes as the game has them, %ld encoded), %d backgrounds' %d tiles (%d drawn the "
           "same once encoded; a sprite imported in %d), %zu text files' %d strings in %d languages (%d readable as "
           "text): %d problems\n",
           stages.size(), maps, before, after, drawn, tiles, exact, sprites, textFiles.size(), strings, LANG_COUNT, readable, bad);
    printf("%d characters exported as templates, %d of them imported and exported again the same\n", exported, back);
    return bad != 0;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("DW3 Field Editor");
    app.setOrganizationName("dw3fe");
    app.setStyle(QStyleFactory::create("Fusion"));

    QString data, discPath, shot, open, selectWhat, newField, size, background, toyagumon, addObject, addAt, exportId, exportTo, importFrom, addChar, windowShot, exportFields, fieldNames, importFields;
    int frames = 1, cellPx = 64;
    bool check = false, selftest = false, encounters = false;
    QStringList args = app.arguments();
    if (args.size() == 4 && args[1] == "--dump-disc") {
        /* the fields' tables from the disc, as the port's --dump-stages writes them */
        attach_console();
        Disc disc;
        int n = 0;
        std::string err = disc.open(args[2].toStdString());
        if (err.empty()) QDir().mkpath(args[3]), err = dump_stages(disc, args[3].toStdString(), &n);
        if (!err.empty()) return fprintf(stderr, "--dump-disc: %s\n", err.c_str()), 1;
        printf("%d stages dumped to %s\n", n, qPrintable(args[3]));
        return 0;
    }
    for (int i = 1; i < args.size(); i++) {
        if (args[i] == "--data" && i + 1 < args.size()) data = args[++i];
        else if (args[i] == "--disc" && i + 1 < args.size()) discPath = args[++i];
        else if (args[i] == "--shot" && i + 1 < args.size()) shot = args[++i];
        else if (args[i] == "--new-field" && i + 1 < args.size()) newField = args[++i];
        else if (args[i] == "--size" && i + 1 < args.size()) size = args[++i];
        else if (args[i] == "--background" && i + 1 < args.size()) background = args[++i];
        else if (args[i] == "--toyagumon" && i + 1 < args.size()) toyagumon = args[++i];
        else if (args[i] == "--cell" && i + 1 < args.size()) cellPx = args[++i].toInt();
        else if (args[i] == "--add-object" && i + 2 < args.size()) addObject = args[++i], addAt = args[++i];
        else if (args[i] == "--frames" && i + 1 < args.size()) frames = args[++i].toInt();
        else if (args[i] == "--export-template" && i + 2 < args.size()) exportId = args[++i], exportTo = args[++i];
        else if (args[i] == "--import-character" && i + 1 < args.size()) importFrom = args[++i];
        else if (args[i] == "--add-character" && i + 2 < args.size()) addChar = args[++i], addAt = args[++i];
        else if (args[i] == "--export-fields" && i + 2 < args.size()) exportFields = args[++i], fieldNames = args[++i];
        else if (args[i] == "--import-fields" && i + 1 < args.size()) importFields = args[++i];
        else if (args[i] == "--check") check = true;
        else if (args[i] == "--selftest") selftest = true;
        else if (args[i] == "--encounters") encounters = true;
        else if (args[i] == "--select" && i + 1 < args.size()) selectWhat = args[++i];
        else if (args[i] == "--window-shot" && i + 1 < args.size()) windowShot = args[++i];
        else open = args[i];
    }
    bool console = check || selftest || !shot.isEmpty() || !newField.isEmpty() || !addObject.isEmpty() ||
                   !exportId.isEmpty() || !importFrom.isEmpty() || !addChar.isEmpty() ||
                   !exportFields.isEmpty() || !importFields.isEmpty();
    if (console) attach_console();

    /* the data folder, the port's or the editor's own made from the disc:
       --data, the last one used, or data/ next to the exe */
    QSettings settings;
    std::shared_ptr<Disc> disc;
    auto openDisc = [&](const QString &path) {
        auto d = std::make_shared<Disc>();
        std::string err = d->open(path.toStdString());
        if (err.empty() &&
            QCryptographicHash::hash(QByteArray((const char *)d->exe.data(), (int)d->exe.size()), QCryptographicHash::Sha1).toHex() != EXE_SHA1)
            err = "its SLES_039.36 isn't Digimon World 2003 (Europe, SLES-03936)'s";
        if (!err.empty()) {
            fprintf(stderr, "%s: %s\n", qPrintable(path), err.c_str());
            if (!console) QMessageBox::critical(nullptr, "DW3 Field Editor", path + ":\n" + QString::fromStdString(err));
            return false;
        }
        disc = d;
        if (!console) settings.setValue("disc", QFileInfo(path).absoluteFilePath());
        return true;
    };
    auto askDisc = [&]() {
        QString path = QFileDialog::getOpenFileName(nullptr, "Your Digimon World 2003 (Europe) disc", settings.value("disc").toString(),
                                                    "Disc images (*.cue *.bin *.iso);;All files (*)");
        return !path.isEmpty() && openDisc(path);
    };
    /* the fields' tables made from the disc into DIR/stages */
    auto fromDisc = [&](const QString &dir) {
        int n = 0;
        QDir().mkpath(dir + "/stages");
        std::string err = dump_stages(*disc, (dir + "/stages").toStdString(), &n);
        if (!err.empty()) {
            fprintf(stderr, "the disc's fields: %s\n", err.c_str());
            if (!console) QMessageBox::critical(nullptr, "DW3 Field Editor", "Reading the disc's fields: " + QString::fromStdString(err));
        }
        return load_stages(dir);
    };

    QStringList tries;
    if (!data.isEmpty()) tries << data;
    else tries << settings.value("data").toString() << app.applicationDirPath() + "/data" << app.applicationDirPath() + "/../data";
    std::vector<Stage> stages;
    for (const QString &d : tries)
        if (!d.isEmpty() && !(stages = load_stages(d)).empty()) {
            data = QDir(d).absolutePath();
            break;
        }
    if (stages.empty() && !discPath.isEmpty() && openDisc(discPath)) {
        if (data.isEmpty()) data = app.applicationDirPath() + "/data";
        stages = fromDisc(data);
    } else if (stages.empty() && !console) {
        QMessageBox box(QMessageBox::Question, "DW3 Field Editor",
                        "Where are the game's fields?\n\n"
                        "Your disc image: the editor reads the fields from it, and keeps its changes in a folder of its own.\n"
                        "The native port's data folder: the one with files/ and stages/ (dw2003 --dump-stages data/stages).");
        QPushButton *fromImage = box.addButton("Your disc image...", QMessageBox::AcceptRole);
        QPushButton *fromPort = box.addButton("The port's data folder...", QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == fromImage && askDisc()) {
            data = QFileDialog::getExistingDirectory(nullptr, "A folder for the editor's work (its changes go in mods/)",
                                                     app.applicationDirPath());
            if (!data.isEmpty()) stages = fromDisc(data);
        } else if (box.clickedButton() == fromPort) {
            data = QFileDialog::getExistingDirectory(nullptr, "The port's data folder");
            if (!data.isEmpty()) stages = load_stages(data);
        }
    }
    if (stages.empty()) {
        fprintf(stderr, "no stages: pass --data DIR, the port's data folder with stages/ (dw2003 --dump-stages DIR/stages), "
                        "or --disc IMAGE\n");
        if (!console) QMessageBox::critical(nullptr, "DW3 Field Editor", "No fields there.");
        return 1;
    }
    data = QDir(data).absolutePath();
    /* remembered for the next start: not from a command-line run (--check, --shot...) */
    if (!console) settings.setValue("data", data);
    /* the game's files: the folder's files/, or the disc */
    if (!QDir(data + "/files").exists() && disc == nullptr) {
        QString last = settings.value("disc").toString();
        if (!(QFileInfo::exists(last) && openDisc(last)) && (console || !askDisc())) {
            fprintf(stderr, "no game files: %s has no files/; pass --disc IMAGE\n", qPrintable(data));
            return 1;
        }
    }
    Files files{data.toStdString(), disc};
    GameText text;
    text.load(files, std::clamp(settings.value("language", 0).toInt(), 0, LANG_COUNT - 1));
    /* each character's animation file, for the characters' sprites */
    std::vector<int> animFiles;
    QFile cf(data + "/stages/characters.json");
    Json cj;
    if (cf.open(QIODevice::ReadOnly) && Json::parse(cf.readAll().toStdString(), cj))
        for (auto &v : cj["animFiles"].items) animFiles.push_back(v.i());
    /* and the editor's own, past them (mods/characters.json) */
    for (auto &c : read_own_characters(files))
        if (c.id >= (int)animFiles.size()) animFiles.resize(c.id + 1, 0), animFiles[c.id] = c.animFile;

    if (selftest) return run_selftest(files, stages, animFiles);
    if (encounters) {
        /* each encounter, and where the stages' random battles start it (stage:place.area xN of 8) */
        std::vector<std::vector<std::string>> used(text.encounters.size());
        for (auto &s : stages)
            for (auto &pl : s.places)
                for (int a = 0; a < 4; a++) {
                    std::map<int, int> n;
                    for (auto &b : pl.areas[a].battles) n[b.encounter]++;
                    for (auto &[e, c] : n)
                        if (e > 0 && e < (int)used.size())
                            used[e].push_back(s.name + ":" + std::to_string(pl.id) + "." + std::to_string(a + 1) + "x" + std::to_string(c));
                }
        for (int k = 0; k < (int)text.encounters.size(); k++) {
            const GameEncounter &e = text.encounters[k];
            printf("%d (kind %d, ambush %d):", k, e.kind, e.ambushChance);
            for (auto &x : e.enemies) printf(" %s Lv %d HP %d MP %d x%d/16;", x.name.c_str(), x.level, x.hp, x.mp, x.strength);
            for (auto &u : used[k]) printf(" %s", u.c_str());
            printf("\n");
        }
        return text.encounters.empty();
    }
    if (!exportId.isEmpty()) {
        /* a character's template (hex number), or "standard" for the blank one */
        CharTemplate t = standard_template();
        Image sheet = blank_sheet(t);
        std::string err;
        int c = exportId.toInt(nullptr, 16);
        if (exportId != "standard" && (c < 0 || c >= (int)animFiles.size() || !export_character(files, animFiles[c], 32, &t, &sheet, &err)))
            return fprintf(stderr, "--export-template: %s\n", err.empty() ? "no such character" : err.c_str()), 1;
        QString why;
        if (!writeTemplateSheet(exportTo, sheet, t, &why)) return fprintf(stderr, "%s\n", qPrintable(why)), 1;
        printf("%s: %d rows of %d cells of %d x %d\n", qPrintable(exportTo), t.rows(), t.cols(), t.cellW, t.cellH);
        return 0;
    }
    if (!importFrom.isEmpty()) {
        CharTemplate t;
        Image sheet;
        QString why;
        if (readTemplateSheet(importFrom, &sheet, &t, &why) == TEMPLATE_NONE)
            return fprintf(stderr, "--import-character: %s\n", qPrintable(why)), 1;
        std::vector<std::string> problems;
        std::vector<uint8_t> f = import_character(sheet, t, false, &problems);
        for (auto &p : problems) fprintf(stderr, "%s\n", p.c_str());
        std::string err;
        int id = f.empty() ? -1 : register_character(files, f, t.width, (int)animFiles.size(), QFileInfo(importFrom).completeBaseName().toStdString(), &err);
        if (id < 0) return fprintf(stderr, "%s\n", err.c_str()), 1;
        printf("character %d (0x%X)\n", id, id);
        return 0;
    }
    if (!newField.isEmpty()) {
        /* a new field from a stage as saved: a copy, or an empty map (--size WxH), its background a picture */
        const Stage *from = nullptr;
        for (auto &s : stages)
            if (s.name == newField.toStdString()) from = &s;
        if (!from) return fprintf(stderr, "--new-field: no stage named %s\n", qPrintable(newField)), 1;
        Stage st = *from;
        QFile mf(QString::fromStdString(files.stageModPath(st.name)));
        Json mj;
        if (!st.isNew() && mf.open(QIODevice::ReadOnly) && Json::parse(mf.readAll().toStdString(), mj)) st.loadSections(mj);
        Field f;
        if (!f.load(files, st, &animFiles)) return fprintf(stderr, "%s\n", f.error.c_str()), 1;
        int w = 0, h = 0, mode = free_field_mode(stages);
        if (!size.isEmpty() && (sscanf(qPrintable(size), "%dx%d", &w, &h) != 2 || w < 1 || h < 1 || w * h > MAX_TILES))
            return fprintf(stderr, "--size: WxH tiles, at most %d of them\n", MAX_TILES), 1;
        NewField nf;
        std::string err;
        ToyaMap toya;
        QString toyaErr;
        if (!toyagumon.isEmpty() && !load_toyagumon(toyagumon, cellPx, &toya, &toyaErr))
            return fprintf(stderr, "--toyagumon: %s\n", qPrintable(toyaErr)), 1;
        if (!toyagumon.isEmpty()) w = toya.w, h = toya.h;
        if (!mode) return fprintf(stderr, "no mode is left for a new field\n"), 1;
        if (!make_new_field(files, f, mode, st.modes[0].area, st.modes[0].place, w, h, &nf, &err,
                            toyagumon.isEmpty() ? nullptr : &toya.floor))
            return fprintf(stderr, "%s\n", err.c_str()), 1;
        if (!toyagumon.isEmpty()) nf.stage.startX = toya.startX, nf.stage.startY = toya.startY;
        if (!write_new_field(files, nf, &err)) return fprintf(stderr, "%s\n", err.c_str()), 1;
        if (!background.isEmpty() || !toyagumon.isEmpty()) {
            Field n;
            QImage pic(background);
            if ((toyagumon.isEmpty() && pic.isNull()) || !n.load(files, nf.stage))
                return fprintf(stderr, "can't read %s\n", qPrintable(background)), 1;
            Image mid = toyagumon.isEmpty() ? fromQImage(pic, n.width(), n.height()) : toya.bg, clear;
            clear.resize(n.width(), n.height());
            const Image *layers[3] = {&mid, &clear, toyagumon.isEmpty() ? &clear : &toya.front};
            int changed = 0;
            if (!n.importLayers(layers, &err, &changed) || !files.write(nf.stage.mapFile, n.mapFileData()))
                return fprintf(stderr, "the background: %s\n", err.c_str()), 1;
            printf("%d tiles from %s\n", changed, qPrintable(toyagumon.isEmpty() ? background : toyagumon));
        }
        printf("%s: mode %d (0x%X), on %s's code\n", nf.stage.name.c_str(), mode, mode, nf.stage.templ.c_str());
        return 0;
    }
    if (check) {
        int bad = 0, full = 0, noPage = 0;
        for (auto &s : stages) {
            Field f;
            if (!f.load(files, s, &animFiles)) printf("%s: %s\n", s.name.c_str(), f.error.c_str()), bad++;
            /* the VRAM it leaves its characters and objects */
            int chars, pages, percent;
            vram_room(files, f.stage, f.imagesData, animFiles, 8, 40, &chars, &pages, &percent);
            full += chars == 0, noPage += pages == 0;
        }
        printf("%d stages, %d unread; %d area and %d place names\n", (int)stages.size(), bad, (int)text.areas.size(), (int)text.places.size());
        printf("sprite memory: %d fields without room for another character, %d for another page of objects\n", full, noPage);
        /* the conditions and actions in words: the ones left as numbers */
        CodeWords cw;
        cw.items = text.items, cw.cards = text.cards;
        for (int i = 0; i < 8; i++) cw.partners.push_back(i + 1 < (int)text.digimon.size() ? text.digimon[i + 1] : "?");
        std::map<int, int> raw;
        int codes = 0;
        auto words = [&](const Codes &c, bool actions) {
            for (auto &[code, value] : c) {
                codes++;
                std::string w = describe_code(code, value, cw, actions);
                if (w.rfind("code ", 0) == 0 || w.find("special ") != std::string::npos || w.find("[not a flag") != std::string::npos) raw[code]++;
            }
        };
        for (auto &s : stages) {
            for (auto &a : s.actors) {
                words(a.conditions, false);
                for (auto &t : a.talks) words(t.conditions, false), words(t.actions, true);
            }
            for (auto &t : s.slots)
                for (auto &c : t.cond)
                    if (c[0] != 0xFFFF) words({{c[0], c[1]}}, false);
        }
        printf("codes: %d, %d left as numbers; partners:", codes, (int)raw.size());
        for (auto &p : cw.partners) printf(" %s", p.c_str());
        for (auto &[code, n] : raw) printf("\n  %04X x%d: %s", code, n, describe_code(code, 1, cw, false).c_str());
        printf("\n");
        return bad != 0;
    }
    if (!shot.isEmpty()) {
        for (auto &s : stages)
            if (s.name == open.toStdString()) {
                Field f;
                if (!f.load(files, s, &animFiles)) return fprintf(stderr, "%s\n", f.error.c_str()), 1;
                if (!compose(f).save(shot)) return fprintf(stderr, "can't write %s\n", qPrintable(shot)), 1;
                printf("%s: %dx%d\n", qPrintable(shot), f.width(), f.height());
                return 0;
            }
        fprintf(stderr, "--shot: no stage named %s\n", qPrintable(open));
        return 1;
    }

    MainWindow w(files, std::move(stages), text, animFiles);
    if (!exportFields.isEmpty() || !importFields.isEmpty()) {
        QString msg;
        bool ok = exportFields.isEmpty() ? w.importFields(importFields, false, &msg)
                                         : w.exportFields(exportFields, fieldNames.split(','), &msg);
        if (!ok) return fprintf(stderr, "%s\n", qPrintable(msg)), 1;
        printf("%s\n", qPrintable(exportFields.isEmpty() ? msg : "exported to " + exportFields));
        return 0;
    }
    if (!addChar.isEmpty()) {
        /* a character (hex number) with a slot of its own at a point of a stage, saved */
        QStringList xy = addAt.split(',');
        QString err;
        w.openStage(open);
        if (xy.size() != 2 || !w.addCharacter(addChar.toInt(nullptr, 16), QPointF(xy[0].toInt(), xy[1].toInt()), &err, false) || !w.save())
            return fprintf(stderr, "--add-character: %s\n", qPrintable(err.isEmpty() ? "X,Y and a stage" : err)), 1;
        printf("%s: character 0x%s at %s\n", qPrintable(open), qPrintable(addChar), qPrintable(addAt));
        return 0;
    }
    if (!addObject.isEmpty()) {
        /* a picture as an object of a stage, saved: dw3fe stage --add-object picture.png X,Y [--frames N] */
        QStringList xy = addAt.split(',');
        QString err;
        w.openStage(open);
        if (xy.size() != 2 || !w.addObjectFromFile(addObject, frames, QPointF(xy[0].toInt(), xy[1].toInt()), &err) || !w.save())
            return fprintf(stderr, "--add-object: %s\n", qPrintable(err.isEmpty() ? "X,Y and a stage to add it to" : err)), 1;
        printf("%s: an object from %s at %s\n", qPrintable(open), qPrintable(addObject), qPrintable(addAt));
        return 0;
    }
    w.showMaximized();
    w.openStage(open.isEmpty() ? settings.value("stage", "wstag330").toString() : open);
    if (!selectWhat.isEmpty()) QTimer::singleShot(100, &w, [&] { w.selectByName(selectWhat); });
    /* --window-shot: the window as drawn, once it is up, then quit */
    if (!windowShot.isEmpty())
        QTimer::singleShot(1500, &w, [&] {
            w.grab().save(windowShot);
            /* and the whole of the properties, beside it: name-props.png */
            for (QScrollArea *a : w.findChildren<QScrollArea *>())
                if (a->widget() && a->parentWidget() && a->parentWidget()->objectName() == "properties")
                    a->widget()->grab().save(QString(windowShot).replace(".png", "-props.png"));
            app.quit();
        });
    int r = app.exec();
    return r;
}
