/* Fields shared between editors: a .dw3field file holds some fields as
   saved (their tables, files, characters of your own, the texts and names
   they use, their flags' names). What is the game's stays out: a file is a
   delta from the game's (delta.h), whoever imports it has those from their
   disc, and a text only goes when it is new or changed. An import numbers
   what would clash with the importer's own: new fields' modes and files,
   characters, the texts added, the flags the editor gave out. */
#include "mainwindow.h"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>
#include <set>
#include <tuple>
#include "delta.h"

static const QByteArray MAGIC = "DW3FIELD\n";
static const int FORMAT = 1;

static QString b64(const std::vector<uint8_t> &v) {
    return QString::fromLatin1(QByteArray((const char *)v.data(), (int)v.size()).toBase64());
}
static std::vector<uint8_t> unb64(const QJsonValue &v) {
    QByteArray a = QByteArray::fromBase64(v.toString().toLatin1());
    return std::vector<uint8_t>(a.begin(), a.end());
}

/* a text file's strings: the whole file (entry < 0) or an archive's entry */
static std::vector<std::vector<uint8_t>> strings_of(const std::vector<uint8_t> &f, int entry) {
    if (entry < 0) return read_raw_strings(f);
    size_t n;
    const uint8_t *e = archive_entry(f, entry, &n);
    return e ? read_raw_strings(std::vector<uint8_t>(e, e + n)) : std::vector<std::vector<uint8_t>>();
}

/* the files a stage's tables name */
static std::set<int> stage_fids(const Stage &s) {
    std::set<int> out = {s.mapFile, s.imageFile, s.sheetEntry >> 16, s.imageEntry >> 16};
    for (int m : s.mapFiles) out.insert(m >> 16);
    out.erase(0);
    return out;
}

/* every condition and action code of a stage's tables */
static void each_code(Stage &s, const std::function<void(int &)> &f) {
    for (Actor &a : s.actors) {
        for (auto &c : a.conditions) f(c.first);
        for (Talk &t : a.talks) {
            for (auto &c : t.conditions) f(c.first);
            for (auto &c : t.actions) f(c.first);
        }
    }
    for (Slot &t : s.slots)
        for (auto &c : t.cond)
            if (c[0] != 0xFFFF) f(c[0]);
}

/* the Say commands' text indices of an event's script */
static std::vector<int> said_texts(const Event &e) {
    std::vector<int> out;
    for (const Command &c : parse_script(e.script))
        if ((c.op >> 8) == 2 && c.args.size() > 1) out.push_back(c.args[1]);
    return out;
}

/* an event's script with its characters, texts and modes renumbered; as it
   was when the editor can't read all of it */
static void remap_script(Event &e, const std::function<int(int)> &chr, const std::function<int(int)> &text,
                         const std::function<int(int)> &mode) {
    std::vector<Command> cmds = parse_script(e.script);
    if (build_script(cmds) != e.script) return;
    for (Command &c : cmds) {
        auto arg = [&](size_t i) { return i < c.args.size() ? &c.args[i] : nullptr; };
        int *who = nullptr;
        if (c.op == 0x100 || c.op == 0x102 || c.op == 0x302 || c.op == 0x303) who = arg(0);
        else if (c.op == 0x101 && !c.args.empty() && c.args[0] < SCRIPT_COMMANDS) who = arg(0);
        else if ((c.op >> 8) == 2) {
            who = arg(2);
            if (int *t = arg(1)) *t = text(*t);
        } else if (c.op == 0x600) who = arg(1);
        else if (c.op == 0x304 && arg(0)) *arg(0) = mode(*arg(0));
        if (who) *who = chr(*who);
    }
    e.script = build_script(cmds);
}

/* a flag the editor gives out (newFlag's groups), not a field's visited one */
static bool editor_flag(int code) {
    int group = code >> 8;
    return is_flag(code) && visited_mode(code) < 0 && (group == 0x40 || group == 0x1C || group == 0x1A);
}

static bool write_bytes(const QString &path, const QByteArray &data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

/* whether a field (as saved) has changes: its tables, or a file it names in mods/ */
static bool has_changes(const Files &files, const Stage &s) {
    if (s.isNew() || QFile::exists(QString::fromStdString(files.stageModPath(s.name)))) return true;
    for (int fid : stage_fids(s))
        if (QFile::exists(QString::asprintf("%s/mods/%04d.bin", files.dir.c_str(), fid))) return true;
    return false;
}

bool MainWindow::exportFields(const QString &path, const QStringList &names, QString *err) {
    QJsonArray fields, filesOut, chars, texts;
    QJsonObject disc, flags;
    std::set<int> charIds, doneFids, codes;
    std::set<std::tuple<int, int, int>> doneTexts;
    auto modPath = [&](int fid) { return QString::asprintf("%s/mods/%04d.bin", files_.dir.c_str(), fid); };
    /* string i of a text file (E its English copy) in every language, when it isn't the game's */
    auto addText = [&](int E, int entry, int i) {
        if (!E || i < 0 || !doneTexts.insert({E, entry, i}).second) return;
        size_t game = strings_of(files_.readOriginal(E), entry).size();
        bool changed = i >= (int)game;
        QJsonArray langs;
        for (int l = 0; l < LANG_COUNT; l++) {
            auto now = strings_of(files_.read(E + l), entry), orig = strings_of(files_.readOriginal(E + l), entry);
            if (i >= (int)now.size()) return;
            changed |= i >= (int)orig.size() || now[i] != orig[i];
            langs.append(b64(now[i]));
        }
        if (changed) texts.append(QJsonObject{{"file", E}, {"entry", entry}, {"index", i}, {"added", i >= (int)game}, {"strings", langs}});
    };
    for (const QString &name : names) {
        int k = -1;
        for (int i = 0; i < (int)stages_.size(); i++)
            if (stages_[i].name == name.toStdString()) k = i;
        if (k < 0) return *err = "There's no field " + name + ".", false;
        Stage s = savedStage(k);
        QFile mf(QString::fromStdString(files_.stageModPath(s.name)));
        QByteArray tables = mf.open(QIODevice::ReadOnly) ? mf.readAll() : QByteArray();
        if (!has_changes(files_, s)) return *err = stageTitle(s) + " has no saved changes.", false;
        fields.append(QJsonObject{{"name", name}, {"stage", QString::fromUtf8(tables)}});
        /* the files: deltas from the game's the field started from */
        const Stage *from = &stages_[k];
        for (const Stage &t : stages_)
            if (s.isNew() && t.name == s.templ) from = &t;
        std::set<int> bases = stage_fids(*from);
        for (int fid : stage_fids(s)) {
            if (!doneFids.insert(fid).second || !QFile::exists(modPath(fid))) continue;
            std::vector<std::vector<uint8_t>> base;
            QJsonArray baseFids;
            std::set<int> try_ = bases;
            try_.insert(fid);
            for (int b : try_) {
                std::vector<uint8_t> o = files_.readOriginal(b);
                if (o.empty()) continue;
                base.push_back(std::move(o));
                baseFids.append(b);
                disc[QString::number(b)] = (qint64)crc32(base.back());
            }
            std::vector<const std::vector<uint8_t> *> ptrs;
            for (auto &b : base) ptrs.push_back(&b);
            filesOut.append(QJsonObject{{"fid", fid}, {"bases", baseFids}, {"data", b64(delta_encode(files_.read(fid), ptrs))}});
        }
        for (const Actor &a : s.actors) {
            if (a.id >= GAME_CHARACTERS) charIds.insert(a.id);
            for (const Talk &t : a.talks) addText(s.textFile, -1, t.entry);
        }
        for (const Event &e : s.events)
            if (e.text && e.scripted)
                for (int i : said_texts(e)) addText((e.text >> 16) + 2, e.text & 0xFFFF, i);
        if (s.isNew() && !s.modes.empty()) addText(AREA_NAMES_FILE, -1, s.modes[0].area), addText(PLACE_NAMES_FILE, -1, s.modes[0].place);
        each_code(s, [&](int &c) { codes.insert(c); });
    }
    for (const OwnCharacter &c : read_own_characters(files_))
        if (charIds.count(c.id))
            chars.append(QJsonObject{{"id", c.id}, {"name", QString::fromStdString(c.name)}, {"width", c.width},
                                     {"data", b64(files_.read(c.animFile >> 16))}});
    for (int c : codes)
        if (flagNames_.count(c)) flags[QString("%1").arg(c, 4, 16, QChar('0')).toUpper()] = flagNames_.at(c);
    QJsonObject doc{{"dw3field", FORMAT}, {"editor", DW3FE_VERSION}, {"fields", fields}, {"files", filesOut}, {"characters", chars},
                    {"texts", texts}, {"flags", flags}, {"disc", disc}};
    if (!write_bytes(path, MAGIC + qCompress(QJsonDocument(doc).toJson(QJsonDocument::Compact), 9)))
        return *err = "Can't write " + path, false;
    return true;
}

bool MainWindow::importFields(const QString &path, bool ask, QString *report) {
    auto fail = [&](const QString &e) {
        *report = e;
        return false;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail("Can't read " + path);
    QByteArray raw = f.readAll();
    if (!raw.startsWith(MAGIC)) return fail(QFileInfo(path).fileName() + " isn't a file of shared fields.");
    QJsonObject doc = QJsonDocument::fromJson(qUncompress(raw.mid(MAGIC.size()))).object();
    if (doc["dw3field"].toInt() != FORMAT)
        return fail("The file is from another version of the editor (" + doc["editor"].toString("?") + "): update this one.");
    /* the same game: the files the deltas copy from */
    QJsonObject disc = doc["disc"].toObject();
    for (auto it = disc.begin(); it != disc.end(); ++it)
        if ((qint64)crc32(files_.readOriginal(it.key().toInt())) != it.value().toInteger())
            return fail(QString("The fields were made from another copy of the game: its file %1 isn't yours.").arg(it.key()));

    /* the fields: a game field's saved changes, or a new field's whole */
    struct In {
        int k = -1; /* the game field's index in stages_ */
        Stage s;
    };
    std::vector<In> in;
    QStringList replaced;
    for (const QJsonValue &v : doc["fields"].toArray()) {
        QJsonObject o = v.toObject();
        std::string name = o["name"].toString().toStdString(), tables = o["stage"].toString().toStdString();
        Json j;
        if (!tables.empty() && !Json::parse(tables, j)) return fail(QString("The tables of %1 can't be read.").arg(name.c_str()));
        In x;
        if (!j["template"].str.empty()) {
            if (!x.s.load(j)) return fail(QString("The new field %1 can't be read.").arg(name.c_str()));
            bool known = false;
            for (const Stage &t : stages_) known |= t.name == x.s.templ;
            if (!known || x.s.modes.empty()) return fail(QString("The new field %1 is made from a field this game hasn't.").arg(name.c_str()));
        } else {
            for (int i = 0; i < (int)stages_.size(); i++)
                if (stages_[i].name == name && !stages_[i].isNew()) x.k = i;
            if (x.k < 0) return fail(QString("This game has no field %1.").arg(name.c_str()));
            x.s = stages_[x.k];
            if (!tables.empty()) x.s.loadSections(j);
            if (has_changes(files_, savedStage(x.k))) replaced << stageTitle(stages_[x.k]);
        }
        in.push_back(std::move(x));
    }
    if (in.empty()) return fail("The file has no fields.");

    /* the new fields' modes: theirs where free, else the next free one (free_field_mode's) */
    std::map<int, int> modeMap, fidMap;
    std::set<int> modes = {0x300};
    for (const Stage &t : stages_)
        for (auto &m : t.modes) modes.insert(m.mode);
    std::vector<In *> clash;
    for (In &x : in)
        if (x.k < 0 && !modes.insert(x.s.modes[0].mode).second) clash.push_back(&x);
    for (In *x : clash) {
        int old = x->s.modes[0].mode, mode = 0x2F0;
        while (mode < 0x400 && modes.count(mode)) mode++;
        if (mode >= 0x400) return fail("There are no field numbers left for the new fields.");
        modes.insert(mode);
        Stage moved = x->s;
        moved.modes[0].mode = mode;
        modeMap[old] = mode;
        fidMap[own_tiles_fid(x->s)] = own_tiles_fid(moved), fidMap[own_tiles_fid(x->s) + 1] = own_tiles_fid(moved) + 1;
        fidMap[own_images_fid(x->s)] = own_images_fid(moved);
    }

    /* the files, made again from this game's */
    std::map<int, std::vector<uint8_t>> filesIn;
    for (const QJsonValue &v : doc["files"].toArray()) {
        QJsonObject o = v.toObject();
        std::vector<std::vector<uint8_t>> base;
        for (const QJsonValue &b : o["bases"].toArray()) base.push_back(files_.readOriginal(b.toInt()));
        std::vector<const std::vector<uint8_t> *> ptrs;
        for (auto &b : base) ptrs.push_back(&b);
        int fid = o["fid"].toInt();
        if (fidMap.count(fid)) fid = fidMap[fid];
        if (!delta_decode(unb64(o["data"]), ptrs, &filesIn[fid])) return fail(QString("The file %1 in it is broken.").arg(o["fid"].toInt()));
    }

    /* the characters: one of yours with the same drawings, or a new one */
    struct CharIn {
        int id, width, mine;
        std::string name;
        std::vector<uint8_t> data;
    };
    std::vector<CharIn> charsIn;
    std::vector<OwnCharacter> own = read_own_characters(files_);
    for (const QJsonValue &v : doc["characters"].toArray()) {
        QJsonObject o = v.toObject();
        CharIn c{o["id"].toInt(), o["width"].toInt(), -1, o["name"].toString().toStdString(), unb64(o["data"])};
        for (const OwnCharacter &m : own)
            if (m.width == c.width && files_.read(m.animFile >> 16) == c.data) c.mine = m.id;
        charsIn.push_back(std::move(c));
    }

    /* the texts: a game's string changed in place, an added one at the end
       (or where the same one is already); each file's languages at once */
    std::map<std::pair<int, int>, std::vector<std::vector<std::vector<uint8_t>>>> textFiles; /* (E, entry): by language */
    std::map<std::tuple<int, int, int>, int> textMap;
    int changedTexts = 0, addedTexts = 0;
    for (const QJsonValue &v : doc["texts"].toArray()) {
        QJsonObject o = v.toObject();
        int E = o["file"].toInt(), entry = o["entry"].toInt(), i = o["index"].toInt();
        QJsonArray langs = o["strings"].toArray();
        if (!E || langs.size() != LANG_COUNT) return fail("A text in it is broken.");
        auto &tf = textFiles[{E, entry}];
        if (tf.empty())
            for (int l = 0; l < LANG_COUNT; l++) tf.push_back(strings_of(files_.read(E + l), entry));
        std::vector<std::vector<uint8_t>> s;
        for (int l = 0; l < LANG_COUNT; l++) s.push_back(unb64(langs[l]));
        if (!o["added"].toBool()) {
            for (int l = 0; l < LANG_COUNT; l++)
                if (i < (int)tf[l].size()) tf[l][i] = s[l];
            textMap[{E, entry, i}] = i, changedTexts++;
            continue;
        }
        int game = (int)strings_of(files_.readOriginal(E), entry).size(), at = -1;
        for (int j = game; j < (int)tf[0].size() && at < 0; j++) {
            bool same = true;
            for (int l = 0; l < LANG_COUNT && same; l++) same = j < (int)tf[l].size() && tf[l][j] == s[l];
            if (same) at = j;
        }
        if (at < 0) {
            at = (int)tf[0].size();
            for (int l = 0; l < LANG_COUNT; l++)
                if ((int)tf[l].size() != at) return fail(QString("Your text file %1's languages differ in length.").arg(E));
            for (int l = 0; l < LANG_COUNT; l++) tf[l].push_back(s[l]);
            addedTexts++;
        }
        textMap[{E, entry, i}] = at;
    }

    /* the flags the editor gave out that your fields use too: others */
    std::set<int> gameFlags, yours, theirs;
    for (int c : game_flags()) gameFlags.insert(c);
    for (int k = 0; k < (int)stages_.size(); k++) {
        Stage s = stages_[k];
        if (!s.isNew()) each_code(s, [&](int &c) { gameFlags.insert(c); });
        bool goes = false;
        for (In &x : in) goes |= x.k == k;
        if (goes) continue; /* replaced */
        Stage saved = savedStage(k);
        each_code(saved, [&](int &c) { yours.insert(c); });
    }
    for (auto &[c, n] : flagNames_) yours.insert(c);
    for (In &x : in) each_code(x.s, [&](int &c) { theirs.insert(c); });
    std::map<int, int> flagMap;
    std::set<int> used = yours;
    used.insert(theirs.begin(), theirs.end());
    for (int c : theirs) {
        if (!editor_flag(c) || gameFlags.count(c) || !yours.count(c)) continue;
        int n = newFlag(used);
        if (n < 0) return fail("There are no free flags left for the fields' own.");
        flagMap[c] = n, used.insert(n);
    }

    if (ask) {
        QStringList what;
        for (In &x : in) what << (x.k >= 0 ? stageTitle(x.s) : QString::fromStdString(x.s.name) + " (new)");
        QString q = QString("Import %1 field(s) made with editor %2:\n%3").arg(in.size()).arg(doc["editor"].toString("?"), what.join(", "));
        if (!replaced.isEmpty()) q += "\n\nYour saved changes to " + replaced.join(", ") + " are replaced.";
        if (QMessageBox::question(this, "Import fields", q) != QMessageBox::Yes) return fail("");
    }

    /* --- from here on, written --- */
    for (In &x : in) {
        if (x.k < 0) continue;
        const Stage &g = stages_[x.k];
        Stage saved = savedStage(x.k);
        QFile::remove(QString::fromStdString(files_.stageModPath(g.name)));
        for (int i = 0; i < 8; i++)
            if (saved.mapFiles[i]) files_.remove(saved.mapFiles[i] >> 16);
        files_.remove(own_tiles_fid(g));
        files_.remove(own_images_fid(g));
    }
    QDir().mkpath(QString::fromStdString(files_.dir + "/mods/stages"));
    for (auto &[fid, data] : filesIn)
        if (!files_.write(fid, data)) return fail(QString("Can't write mods/%1.bin").arg(fid, 4, 10, QChar('0')));
    std::map<int, int> charMap;
    int newChars = 0;
    for (CharIn &c : charsIn) {
        if (c.mine >= 0) {
            charMap[c.id] = c.mine;
            continue;
        }
        std::string e;
        int id = register_character(files_, c.data, c.width, (int)animFiles_.size(), c.name, &e);
        if (id < 0) return fail(QString::fromStdString(e));
        if ((int)animFiles_.size() <= id) animFiles_.resize(id + 1, 0);
        animFiles_[id] = (CHARACTER_FILES + id - GAME_CHARACTERS) << 16;
        charMap[c.id] = id, newChars++;
    }
    for (auto &[key, langs] : textFiles)
        for (int l = 0; l < LANG_COUNT; l++) {
            int fid = key.first + l;
            std::vector<uint8_t> data = key.second < 0 ? build_text_file(langs[l])
                                                       : rebuild_archive(files_.read(fid), {{key.second, build_text_file(langs[l], false)}});
            if (data == files_.readOriginal(fid)) files_.remove(fid);
            else if (!files_.write(fid, data)) return fail(QString("Can't write mods/%1.bin").arg(fid, 4, 10, QChar('0')));
        }
    QJsonObject names = doc["flags"].toObject();
    bool named = false;
    for (auto it = names.begin(); it != names.end(); ++it) {
        int c = it.key().toInt(nullptr, 16);
        if (flagMap.count(c)) c = flagMap[c];
        if (!flagNames_.count(c)) flagNames_[c] = it.value().toString(), named = true;
    }
    if (named) saveFlagNames();

    /* the tables, renumbered */
    auto mapped = [](const std::map<int, int> &m, int v) {
        auto it = m.find(v);
        return it != m.end() ? it->second : v;
    };
    auto fileOf = [&](int ref) { return ref ? mapped(fidMap, ref >> 16) << 16 | (ref & 0xFFFF) : 0; };
    auto text = [&](int E, int entry, int i) {
        auto it = textMap.find({E, entry, i});
        return it != textMap.end() ? it->second : i;
    };
    QStringList done, lost;
    for (In &x : in) {
        Stage &s = x.s;
        if (x.k < 0) {
            Stage::Mode &m = s.modes[0];
            m.mode = mapped(modeMap, m.mode);
            m.area = text(AREA_NAMES_FILE, -1, m.area), m.place = text(PLACE_NAMES_FILE, -1, m.place);
            char name[16];
            snprintf(name, sizeof name, "new%03x", m.mode);
            s.name = name;
        }
        s.mapFile = mapped(fidMap, s.mapFile), s.imageFile = mapped(fidMap, s.imageFile);
        s.sheetEntry = fileOf(s.sheetEntry), s.imageEntry = fileOf(s.imageEntry);
        for (int &r : s.mapFiles) r = fileOf(r);
        each_code(s, [&](int &c) { c = mapped(flagMap, c); });
        for (Slot &t : s.slots)
            if (slot_leaves(t.type) && modeMap.count((int16_t)t.arg)) t.arg = modeMap[(int16_t)t.arg] & 0xFFFF;
        for (Actor &a : s.actors) {
            a.id = mapped(charMap, a.id);
            for (Talk &t : a.talks) t.entry = text(s.textFile, -1, t.entry);
        }
        for (Event &e : s.events) {
            int E = (e.text >> 16) + 2, entry = e.text & 0xFFFF;
            remap_script(e, [&](int c) { return mapped(charMap, c); }, [&](int i) { return e.text ? text(E, entry, i) : i; },
                         [&](int m) { return mapped(modeMap, m); });
        }
        std::string json = x.k < 0 ? new_field_json(s) : stage_mod_json(s, stages_[x.k]);
        QString p = QString::fromStdString(files_.stageModPath(s.name));
        if (!json.empty() && !write_bytes(p, QByteArray::fromStdString(json))) return fail("Can't write " + p);
        if (x.k < 0) stages_.push_back(s);
        done << stageTitle(s);
    }
    rebuildModes();
    /* exits that lead nowhere this game has: they hang it */
    for (In &x : in)
        for (int i = 0; i < (int)x.s.slots.size(); i++) {
            const Slot &t = x.s.slots[i];
            if (slot_leaves(t.type) && (int16_t)t.arg >= 0x2F0 && !modeStage_.contains((int16_t)t.arg))
                lost << QString("%1's slot %2").arg(stageTitle(x.s)).arg(i);
        }

    characters_.clear();
    invalidateCodes();
    text_.load(files_, text_.lang);
    stageTree_->clear();
    buildStageTree();
    if (!field_.stage.name.empty()) {
        /* the open field again: its texts or its tables may be the file's now */
        QString name = QString::fromStdString(field_.stage.name);
        field_.stage.name.clear();
        openStage(name);
    }
    *report = "Imported: " + done.join(", ") + ".";
    if (!modeMap.empty()) *report += QString("\n%1 new field(s) had a number of yours: they got others.").arg(modeMap.size());
    if (newChars || charsIn.size()) *report += QString("\nCharacters: %1, %2 of them new to you.").arg(charsIn.size()).arg(newChars);
    if (changedTexts || addedTexts) *report += QString("\nTexts: %1 changed, %2 added.").arg(changedTexts).arg(addedTexts);
    if (!flagMap.empty()) *report += QString("\n%1 flag(s) your fields use already: the imported ones use others.").arg(flagMap.size());
    if (!lost.isEmpty()) *report += "\n\nThese exits lead to a field that wasn't in the file: " + lost.join(", ") + ". Change them, or the game hangs there.";
    return true;
}

/* the saved fields that go with s: the new fields its exits lead to, and the fields whose exits lead to it */
static std::set<std::string> linked(const std::vector<Stage> &saved, const Stage &s) {
    std::set<std::string> out;
    auto leadsTo = [](const Stage &a, const Stage &b) {
        for (const Slot &t : a.slots)
            for (auto &m : b.modes)
                if (slot_leaves(t.type) && (int16_t)t.arg == m.mode) return true;
        return false;
    };
    for (const Stage &t : saved)
        if ((t.isNew() && leadsTo(s, t)) || (s.isNew() && leadsTo(t, s))) out.insert(t.name);
    return out;
}

void MainWindow::exportFieldsDialog() {
    if (!maybeSave()) return;
    QDialog d(this);
    d.setWindowTitle("Export fields");
    auto *v = new QVBoxLayout(&d);
    v->addWidget(richLabel("The fields to share, as you saved them: one file with their changes, your characters and "
                           "texts they use. Nothing of the game's goes in it: whoever imports it needs their own disc.<br>"
                           "<span style='color:#999'>A new field goes with the field whose exit leads to it.</span>"));
    auto *list = new QListWidget(&d);
    v->addWidget(list, 1);
    std::vector<Stage> saved;
    for (int k = 0; k < (int)stages_.size(); k++) saved.push_back(savedStage(k));
    std::set<std::string> pick;
    if (!field_.stage.name.empty()) {
        pick = linked(saved, field_.stage);
        pick.insert(field_.stage.name);
    }
    for (int k = 0; k < (int)stages_.size(); k++) {
        if (!has_changes(files_, saved[k])) continue;
        auto *it = new QListWidgetItem(stageTitle(stages_[k]) + "  [" + QString::fromStdString(stages_[k].name) + "]", list);
        it->setData(Qt::UserRole, QString::fromStdString(stages_[k].name));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(pick.count(stages_[k].name) ? Qt::Checked : Qt::Unchecked);
    }
    if (!list->count()) {
        QMessageBox::information(this, "Export fields", "No field has saved changes yet.");
        return;
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.resize(480, 420);
    if (d.exec() != QDialog::Accepted) return;
    QStringList names;
    for (int i = 0; i < list->count(); i++)
        if (list->item(i)->checkState() == Qt::Checked) names << list->item(i)->data(Qt::UserRole).toString();
    if (names.isEmpty()) return;
    QSettings settings;
    QString start = settings.value("shared", QDir::homePath()).toString() + "/" + names[0] + ".dw3field";
    QString path = QFileDialog::getSaveFileName(this, "Export fields", start, "Shared fields (*.dw3field)");
    if (path.isEmpty()) return;
    settings.setValue("shared", QFileInfo(path).absolutePath());
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = exportFields(path, names, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) QMessageBox::critical(this, "Export fields", err);
    else statusBar()->showMessage(QString("%1 field(s) exported to %2").arg(names.size()).arg(QFileInfo(path).fileName()), 8000);
}

void MainWindow::importFieldsDialog() {
    if (!maybeSave()) return;
    QSettings settings;
    QString path = QFileDialog::getOpenFileName(this, "Import fields", settings.value("shared", QDir::homePath()).toString(),
                                                "Shared fields (*.dw3field)");
    if (path.isEmpty()) return;
    settings.setValue("shared", QFileInfo(path).absolutePath());
    QString report;
    bool ok = importFields(path, true, &report);
    if (ok) QMessageBox::information(this, "Import fields", report);
    else if (!report.isEmpty()) QMessageBox::critical(this, "Import fields", report);
}
