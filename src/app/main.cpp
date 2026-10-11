/* DW3 Field Editor: Digimon World 2003's fields (the maps you walk), with
   their logic maps, objects, exits and characters, for the native port.

   dw3fe [stage]                    opens the editor (and a stage, e.g. wstag330)
   dw3fe --shot out.png stage       draws a stage's map to a picture and exits
   dw3fe --check                    reads every stage and prints what it found
   dw3fe --selftest                 checks what saving rests on over every stage
   dw3fe stage --select actor:N     opens a stage with something selected
                                    (actor:N, slot:N, object:N, event:N, start)
         [--window-shot out.png]    and saves the window and its properties as drawn, then quits
   dw3fe --data DIR                 the port's data folder (files/, mods/, stages/), or the editor's own
   dw3fe --disc IMAGE               the game's disc (.cue, .bin or .iso): the fields' tables and files
                                    from it, when the data folder has none
   dw3fe --dump-disc IMAGE DIR      the fields' tables from the disc into DIR, as the port's dump
   dw3fe --new-field stage [--size WxH] [--background picture.png]
                                    a new field from a stage: a copy, or an empty map
   dw3fe stage --add-object picture.png X,Y [--frames N]
                                    an object from a picture at a point, saved
   dw3fe --export-template HEX|standard out.png
                                    a character's template, its layout inside
   dw3fe --import-character sheet.png
                                    a filled template as a character of your own
   dw3fe stage --add-character HEX X,Y
                                    a character with a slot of its own at a point, saved

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
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <cstdio>
#include "mainwindow.h"
#include "characters.h"
#include "disc.h"
#include "codes.h"
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

    QString data, discPath, shot, open, selectWhat, newField, size, background, addObject, addAt, exportId, exportTo, importFrom, addChar, windowShot;
    int frames = 1;
    bool check = false, selftest = false;
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
        else if (args[i] == "--add-object" && i + 2 < args.size()) addObject = args[++i], addAt = args[++i];
        else if (args[i] == "--frames" && i + 1 < args.size()) frames = args[++i].toInt();
        else if (args[i] == "--export-template" && i + 2 < args.size()) exportId = args[++i], exportTo = args[++i];
        else if (args[i] == "--import-character" && i + 1 < args.size()) importFrom = args[++i];
        else if (args[i] == "--add-character" && i + 2 < args.size()) addChar = args[++i], addAt = args[++i];
        else if (args[i] == "--check") check = true;
        else if (args[i] == "--selftest") selftest = true;
        else if (args[i] == "--select" && i + 1 < args.size()) selectWhat = args[++i];
        else if (args[i] == "--window-shot" && i + 1 < args.size()) windowShot = args[++i];
        else open = args[i];
    }
    bool console = check || selftest || !shot.isEmpty() || !newField.isEmpty() || !addObject.isEmpty() ||
                   !exportId.isEmpty() || !importFrom.isEmpty() || !addChar.isEmpty();
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
        settings.setValue("disc", QFileInfo(path).absoluteFilePath());
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
    settings.setValue("data", data);
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
        if (!mode) return fprintf(stderr, "no mode is left for a new field\n"), 1;
        if (!make_new_field(files, f, mode, st.modes[0].area, st.modes[0].place, w, h, &nf, &err) || !write_new_field(files, nf, &err))
            return fprintf(stderr, "%s\n", err.c_str()), 1;
        if (!background.isEmpty()) {
            Field n;
            QImage pic(background);
            if (pic.isNull() || !n.load(files, nf.stage)) return fprintf(stderr, "can't read %s\n", qPrintable(background)), 1;
            Image mid = fromQImage(pic, n.width(), n.height()), clear;
            clear.resize(n.width(), n.height());
            const Image *layers[3] = {&mid, &clear, &clear};
            int changed = 0;
            if (!n.importLayers(layers, &err, &changed) || !files.write(nf.stage.mapFile, n.mapFileData()))
                return fprintf(stderr, "the background: %s\n", err.c_str()), 1;
            printf("%d tiles from %s\n", changed, qPrintable(background));
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
