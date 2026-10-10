/* DW3 Field Editor: Digimon World 2003's fields (the maps you walk), with
   their logic maps, objects, exits and characters, for the native port.

   dw3fe [stage]                    opens the editor (and a stage, e.g. wstag330)
   dw3fe --shot out.png stage       draws a stage's map to a picture and exits
   dw3fe --check                    reads every stage and prints what it found
   dw3fe --selftest                 checks what saving rests on over every stage
   dw3fe stage --select actor:N     opens a stage with something selected
                                    (actor:N, slot:N, object:N, start)
   dw3fe --data DIR                 the port's data folder (files/, mods/, stages/)

   The stages' tables come from the port: dw2003 --dump-stages DATA/stages. */
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QImage>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QStyleFactory>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <map>
#include <cstdio>
#include "mainwindow.h"
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

static void attach_console() {
#ifdef _WIN32
    if (_isatty(1) || GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
}

/* The stages of DIR/stages: a .json file each */
static std::vector<Stage> load_stages(const QString &dir) {
    std::vector<Stage> out;
    for (const QFileInfo &fi : QDir(dir + "/stages").entryInfoList({"*.json"}, QDir::Files)) {
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

/* What saving rests on, over every stage: each logic map encoded and read
   back the same, inside its rebuilt file; a stage's mod empty when nothing
   changed, and carrying a change back when one did */
static int run_selftest(const Files &files, const std::vector<Stage> &stages) {
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
    printf("%zu stages, %d logic maps (%ld bytes as the game has them, %ld encoded): %d problems\n", stages.size(), maps,
           before, after, bad);
    return bad != 0;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("DW3 Field Editor");
    app.setOrganizationName("dw3fe");
    app.setStyle(QStyleFactory::create("Fusion"));

    QString data, shot, open, selectWhat;
    bool check = false, selftest = false;
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); i++) {
        if (args[i] == "--data" && i + 1 < args.size()) data = args[++i];
        else if (args[i] == "--shot" && i + 1 < args.size()) shot = args[++i];
        else if (args[i] == "--check") check = true;
        else if (args[i] == "--selftest") selftest = true;
        else if (args[i] == "--select" && i + 1 < args.size()) selectWhat = args[++i];
        else open = args[i];
    }
    if (check || selftest || !shot.isEmpty()) attach_console();

    /* the data folder: --data, the last one used, or data/ next to the exe */
    QSettings settings;
    QStringList tries;
    if (!data.isEmpty()) tries << data;
    tries << settings.value("data").toString() << app.applicationDirPath() + "/data" << app.applicationDirPath() + "/../data";
    std::vector<Stage> stages;
    for (const QString &d : tries)
        if (!d.isEmpty() && !(stages = load_stages(d)).empty()) {
            data = QDir(d).absolutePath();
            break;
        }
    if (stages.empty() && !check && !selftest && shot.isEmpty()) {
        QMessageBox::information(nullptr, "DW3 Field Editor",
                                 "Pick the native port's data folder: the one with files/ and stages/.\n\n"
                                 "stages/ is made by the port: dw2003 --dump-stages data/stages");
        data = QFileDialog::getExistingDirectory(nullptr, "The port's data folder");
        if (!data.isEmpty()) stages = load_stages(data);
    }
    if (stages.empty()) {
        fprintf(stderr, "no stages: pass --data DIR, the port's data folder with stages/ (dw2003 --dump-stages DIR/stages)\n");
        if (!check && shot.isEmpty())
            QMessageBox::critical(nullptr, "DW3 Field Editor", "No stages there. Make them with: dw2003 --dump-stages data/stages");
        return 1;
    }
    settings.setValue("data", data);
    Files files{data.toStdString()};
    GameText text;
    text.load(files);
    /* each character's animation file, for the characters' sprites */
    std::vector<int> animFiles;
    QFile cf(data + "/stages/characters.json");
    Json cj;
    if (cf.open(QIODevice::ReadOnly) && Json::parse(cf.readAll().toStdString(), cj))
        for (auto &v : cj["animFiles"].items) animFiles.push_back(v.i());

    if (selftest) return run_selftest(files, stages);
    if (check) {
        int bad = 0;
        for (auto &s : stages) {
            Field f;
            if (!f.load(files, s, &animFiles)) printf("%s: %s\n", s.name.c_str(), f.error.c_str()), bad++;
        }
        printf("%d stages, %d unread; %d area and %d place names\n", (int)stages.size(), bad, (int)text.areas.size(), (int)text.places.size());
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
    w.showMaximized();
    w.openStage(open.isEmpty() ? settings.value("stage", "wstag330").toString() : open);
    if (!selectWhat.isEmpty()) QTimer::singleShot(100, &w, [&] { w.selectByName(selectWhat); });
    int r = app.exec();
    return r;
}
