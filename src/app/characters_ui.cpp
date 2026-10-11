/* The characters in the editor: picking one by its look and name, the
   template sheets, your own characters (importing, placing, managing) and
   the image slots the field's characters take */
#include "mainwindow.h"
#include "mapview.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QRegularExpression>
#include <QStatusBar>
#include <QVBoxLayout>
#include <map>
#include <set>

/* --- the template sheets --- */

/* the layout's key in the PNG's text */
static const char *TEMPLATE_KEY = "dw3fe-template";

bool writeTemplateSheet(const QString &path, const Image &sheet, const CharTemplate &t, QString *err) {
    QImage q = toQImage(sheet);
    q.setText(TEMPLATE_KEY, QString::fromStdString(t.json()));
    if (!q.save(path, "PNG")) return *err = "Can't write " + path, false;
    return true;
}

int readTemplateSheet(const QString &path, Image *sheet, CharTemplate *t, QString *err) {
    QImage q(path);
    if (q.isNull()) return *err = "Can't read " + path, TEMPLATE_NONE;
    *sheet = fromQImage(q, q.width(), q.height());
    /* the layout in the picture, or (the editor's first sheets) a .json beside it */
    QString layout = q.text(TEMPLATE_KEY);
    QFile jf(path.left(path.size() - 4) + ".json");
    if (layout.isEmpty() && jf.open(QIODevice::ReadOnly)) layout = QString::fromUtf8(jf.readAll());
    Json j;
    if (!layout.isEmpty() && Json::parse(layout.toStdString(), j) && t->parse(j)) return TEMPLATE_READ;
    /* some programs drop a PNG's text when they save: the standard's size is the standard */
    CharTemplate s = standard_template();
    if (q.width() == s.cols() * s.cellW && q.height() == s.rows() * s.cellH) return *t = s, TEMPLATE_READ;
    *err = "The sheet's layout isn't in it: the program it was drawn in may have dropped it when saving.";
    return TEMPLATE_NONE;
}

/* --- the characters by look and name --- */

QString speakerOf(const std::string &line) {
    size_t colon = line.find(": ");
    if (colon != std::string::npos && colon > 0 && colon < 24 && line.find('\n') > colon) return QString::fromStdString(line.substr(0, colon));
    return QString();
}

const std::vector<MainWindow::CharacterInfo> &MainWindow::characters() const {
    if (!characters_.empty()) return characters_;
    /* who says the lines of each character, and where it stands, over the game's fields */
    std::map<int, std::map<QString, int>> said;
    std::map<int, QStringList> where;
    std::map<int, std::vector<std::string>> texts; /* story copies share their text files */
    for (const Stage &s : stages_) {
        if (s.textFile && !texts.count(s.textFile)) texts[s.textFile] = text_.stageText(files_, s.textFile);
        const std::vector<std::string> &lines = texts[s.textFile];
        QString title = stageTitle(s);
        for (const Actor &a : s.actors) {
            QStringList &w = where[a.id];
            if (!w.contains(title)) w << title;
            for (const Talk &t : a.talks)
                if (t.entry >= 0 && t.entry < (int)lines.size()) {
                    QString who = speakerOf(lines[t.entry]);
                    if (!who.isEmpty()) said[a.id][who]++;
                }
        }
    }
    std::map<int, QString> own;
    for (auto &c : read_own_characters(files_)) own[c.id] = QString::fromStdString(c.name);
    for (int id = 0; id < (int)animFiles_.size(); id++) {
        if (!animFiles_[id] && !where.count(id)) continue;
        CharacterInfo c{id, QString(), where[id], QPixmap(), id >= GAME_CHARACTERS};
        if (own.count(id)) c.name = own[id];
        int best = 0;
        for (auto &[who, n] : said[id])
            if (n > best) best = n, c.name = c.mine ? c.name : who;
        /* its front, standing, twice its size */
        QPixmap pm(64, 80);
        pm.fill(Qt::transparent);
        Sprite sp = actor_sprite(files_, animFiles_[id], 1);
        if (sp.img.w) {
            QImage q = toQImage(sp.img);
            q = q.scaled(q.width() * 2, q.height() * 2);
            if (q.width() > 64 || q.height() > 80) q = q.scaled(64, 80, Qt::KeepAspectRatio);
            QPainter p(&pm);
            p.drawImage((64 - q.width()) / 2, 80 - q.height(), q);
        }
        c.thumb = pm;
        characters_.push_back(c);
    }
    return characters_;
}

QString MainWindow::characterLabel(int id) const {
    for (auto &c : characters())
        if (c.id == id && !c.name.isEmpty()) return QString("%1 (0x%2)").arg(c.name).arg(id, 0, 16);
    return QString("Character 0x%1").arg(id, 0, 16);
}

QPixmap MainWindow::characterThumb(int id) const {
    for (auto &c : characters())
        if (c.id == id) return c.thumb;
    return QPixmap();
}

int MainWindow::pickCharacter(const QString &title, int current, bool onlyMine) {
    const auto &all = characters();
    std::set<int> here;
    for (const Actor &a : field_.stage.actors) here.insert(a.id);
    QDialog d(this);
    d.setWindowTitle(title);
    d.resize(760, 560);
    auto *v = new QVBoxLayout(&d);
    auto *top = new QHBoxLayout;
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search by name, field or number");
    search->setClearButtonEnabled(true);
    auto *scope = new QComboBox;
    scope->addItems({"All characters", "On this field", "Yours"});
    if (onlyMine) scope->setCurrentIndex(2);
    top->addWidget(search, 1);
    top->addWidget(scope);
    v->addLayout(top);
    auto *list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(64, 80));
    list->setGridSize(QSize(124, 132));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWordWrap(true);
    list->setUniformItemSizes(true);
    v->addWidget(list, 1);
    auto *where = new QLabel;
    where->setWordWrap(true);
    where->setStyleSheet("color:#999");
    v->addWidget(where);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(buttons);
    /* this field's first, then the rest by number */
    std::vector<const CharacterInfo *> order;
    for (auto &c : all) order.push_back(&c);
    std::stable_sort(order.begin(), order.end(), [&](auto *a, auto *b) { return here.count(a->id) > here.count(b->id); });
    for (const CharacterInfo *c : order) {
        auto *it = new QListWidgetItem(QIcon(c->thumb), (c->name.isEmpty() ? QString("Character") : c->name) + QString("\n0x%1").arg(c->id, 0, 16));
        it->setData(Qt::UserRole, c->id);
        it->setToolTip(characterLabel(c->id));
        list->addItem(it);
        if (c->id == current) list->setCurrentItem(it);
    }
    auto info = [&](int id) -> const CharacterInfo * {
        for (auto &c : all)
            if (c.id == id) return &c;
        return nullptr;
    };
    auto filter = [=] {
        QString q = search->text().trimmed();
        for (int i = 0; i < list->count(); i++) {
            const CharacterInfo *c = info(list->item(i)->data(Qt::UserRole).toInt());
            bool show = scope->currentIndex() == 0 || (scope->currentIndex() == 1 && here.count(c->id)) || (scope->currentIndex() == 2 && c->mine);
            if (show && !q.isEmpty())
                show = c->name.contains(q, Qt::CaseInsensitive) || c->fields.join("\n").contains(q, Qt::CaseInsensitive) ||
                       QString::number(c->id, 16).compare(q.startsWith("0x") ? q.mid(2) : q, Qt::CaseInsensitive) == 0 ||
                       QString::number(c->id) == q;
            list->item(i)->setHidden(!show);
        }
    };
    connect(search, &QLineEdit::textChanged, &d, filter);
    connect(scope, &QComboBox::currentIndexChanged, &d, filter);
    connect(list, &QListWidget::currentItemChanged, &d, [=](QListWidgetItem *it) {
        const CharacterInfo *c = it ? info(it->data(Qt::UserRole).toInt()) : nullptr;
        if (!c) return where->clear();
        QStringList f = c->fields.mid(0, 12);
        if (c->fields.size() > 12) f << QString("%1 more").arg(c->fields.size() - 12);
        where->setText(characterLabel(c->id) + (f.isEmpty() ? QString(c->mine ? ": yours" : "") : ": in " + f.join(", ")));
    });
    connect(list, &QListWidget::itemDoubleClicked, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    filter();
    if (list->currentItem()) {
        list->scrollToItem(list->currentItem(), QAbstractItemView::PositionAtCenter);
        Q_EMIT list->currentItemChanged(list->currentItem(), nullptr);
    }
    search->setFocus();
    if (d.exec() != QDialog::Accepted || !list->currentItem() || list->currentItem()->isHidden()) return -1;
    return list->currentItem()->data(Qt::UserRole).toInt();
}

/* --- the image slots --- */

bool MainWindow::giveSlot(Stage &s, int k, QString *err, bool own) {
    Actor &a = s.actors[k];
    if (a.id < 0 || a.id >= (int)animFiles_.size() || !animFiles_[a.id]) return true; /* not drawn: any slot */
    /* the game never shares a slot between two looks: a character of the same look lends its own */
    for (int i = 0; i < (int)s.actors.size(); i++)
        if (!own && i != k && s.actors[i].id == a.id) return a.image = s.actors[i].image, true;
    int old = a.image, w, h;
    a.image = -1000; /* its old slot is free while the room is measured */
    character_size(files_, animFiles_[a.id], &w, &h);
    int slot = add_character_slot(s, field_vram(files_, s, field_.imagesData, animFiles_, true), w, h);
    if (slot < 0) {
        a.image = old;
        if (err) *err = "The field's sprite memory has no room left for this character's frames.";
        return false;
    }
    a.image = slot;
    return true;
}

QStringList MainWindow::slotSharers(int actor) const {
    const Stage &s = field_.stage;
    QStringList out;
    std::set<int> ids;
    for (int i = 0; i < (int)s.actors.size(); i++)
        if (i != actor && s.actors[i].image == s.actors[actor].image && s.actors[i].id != s.actors[actor].id &&
            s.actors[i].id < (int)animFiles_.size() && animFiles_[s.actors[i].id] && ids.insert(s.actors[i].id).second)
            out << characterLabel(s.actors[i].id);
    return out;
}

bool MainWindow::addCharacter(int id, QPointF at, QString *err, bool ask) {
    if (id < 0 || id >= (int)animFiles_.size() || (!animFiles_[id] && id < GAME_CHARACTERS))
        return *err = "no character has that number", false;
    Stage s = field_.stage;
    s.actors.push_back(Actor{id, 0, qRound(at.x()), qRound(at.y()), 1, {}, {Talk{0, {}, {}}}});
    int k = (int)s.actors.size() - 1;
    if (!giveSlot(s, k, err)) return false;
    /* a line of its own: the text file's first, empty one ends the game's lists of lines and is
       never said (an empty talk box, then a crash) */
    GameString first;
    for (auto &c : characters())
        if (c.id == id) first.speaker = c.name.toStdString();
    first.text = "...";
    int entry = appendString([this](int l) { return talkKey(l); }, first,
                             "What it says when the player talks to it: a new text at the end of the field's text file, "
                             "in every language as you write it here.",
                             [this, s, k](int entry) mutable {
                                 s.actors[k].talks[0].entry = entry;
                                 changeTables(s, "Add a character");
                             },
                             ask);
    if (entry < 0) return *err = QString(), false;
    select(KIND_ACTOR, k, false);
    return true;
}

void MainWindow::addCharacterAt(QPointF at) {
    int id = pickCharacter("Add a character", field_.stage.actors.empty() ? 0x1B : field_.stage.actors.back().id);
    QString err;
    if (id >= 0 && !addCharacter(id, at, &err) && !err.isEmpty()) QMessageBox::information(this, "Add a character", err);
}

/* --- templates and your own characters --- */

static QString fileSafe(QString s) {
    s = s.toLower().replace(QRegularExpression("[^a-z0-9]+"), "-");
    while (s.startsWith('-')) s.remove(0, 1);
    while (s.endsWith('-')) s.chop(1);
    return s;
}

void MainWindow::exportTemplate(int id) {
    CharTemplate t = standard_template();
    Image sheet;
    QString base = "standard";
    if (id >= 0) {
        std::string err;
        if (id >= (int)animFiles_.size() || !export_character(files_, animFiles_[id], 32, &t, &sheet, &err)) {
            QMessageBox::warning(this, "Export a template", err.empty() ? "That character has no sprites." : QString::fromStdString(err));
            return;
        }
        QString name;
        for (auto &c : characters())
            if (c.id == id) name = c.name;
        base = (name.isEmpty() ? "character" : fileSafe(name)) + QString("-%1").arg(id, 0, 16);
    } else sheet = blank_sheet(t);
    QSettings settings;
    QString path = QFileDialog::getSaveFileName(this, "Save the template", QDir(settings.value("pictures", QDir::homePath()).toString()).filePath(base + ".png"),
                                                "PNG pictures (*.png)");
    if (path.isEmpty()) return;
    settings.setValue("pictures", QFileInfo(path).absolutePath());
    QString err;
    if (!writeTemplateSheet(path, sheet, t, &err)) {
        QMessageBox::critical(this, "Export a template", err);
        return;
    }
    QStringList rows;
    for (int r = 0; r < t.rows(); r++) rows << QString("%1. %2 (%3 drawings)").arg(r + 1).arg(QString::fromStdString(row_name(t, r))).arg(t.drawings(r / t.dirs()));
    QMessageBox::information(this, "Export a template",
                             QString("Written: %1, with its layout inside.\n\nCells of %2 x %3, the feet at the cross (%4, %5). "
                                     "A row for each animation and direction:\n%6\n\nDraw each cell with 15 colors at most, "
                                     "without a shadow (the game draws it), and import it with Characters > Import a character.")
                                 .arg(QFileInfo(path).fileName())
                                 .arg(t.cellW).arg(t.cellH).arg(t.anchorX).arg(t.anchorY).arg(rows.join("\n")));
}

void MainWindow::importCharacter(int replace) {
    QSettings settings;
    QString path = QFileDialog::getOpenFileName(this, replace < 0 ? "Import a character: the filled template" : "Import its new drawings: the filled template",
                                                settings.value("pictures", QDir::homePath()).toString(), "PNG pictures (*.png)");
    if (path.isEmpty()) return;
    settings.setValue("pictures", QFileInfo(path).absolutePath());
    Image sheet;
    CharTemplate t;
    QString why;
    if (readTemplateSheet(path, &sheet, &t, &why) == TEMPLATE_NONE) {
        /* the layout is lost: the character the sheet came from has it */
        if (QMessageBox::question(this, "Import a character", why + "\n\nPick the character it was exported from, and its layout is taken from it?") !=
            QMessageBox::Yes)
            return;
        int from = pickCharacter("The character the sheet was exported from", replace);
        Image unused;
        std::string err;
        if (from < 0 || !export_character(files_, animFiles_[from], 32, &t, &unused, &err)) return;
    }
    std::vector<std::string> problems;
    std::vector<uint8_t> file = import_character(sheet, t, false, &problems);
    if (file.empty()) {
        QString list;
        for (auto &p : problems) list += "\n- " + QString::fromStdString(p);
        QMessageBox box(QMessageBox::Warning, "Import a character", "The sheet isn't to the template yet:" + list, QMessageBox::Cancel, this);
        QPushButton *reuse = nullptr;
        bool onlyEmpty = true;
        for (auto &p : problems) onlyEmpty &= p.find("is empty") != std::string::npos;
        if (onlyEmpty) reuse = box.addButton("Fill the empty ones with the drawing before (an exception)", QMessageBox::AcceptRole);
        box.exec();
        if (!reuse || box.clickedButton() != reuse) return;
        problems.clear();
        file = import_character(sheet, t, true, &problems);
        if (file.empty()) return;
    }
    std::string err;
    if (replace >= 0) {
        /* the same number: every field that has it shows the new drawings */
        std::vector<OwnCharacter> own = read_own_characters(files_);
        for (auto &c : own)
            if (c.id == replace) {
                c.width = t.width;
                if (!files_.write(c.animFile >> 16, file) || !write_own_characters(files_, own, &err)) {
                    QMessageBox::critical(this, "Import a character", err.empty() ? "Can't write its file" : QString::fromStdString(err));
                    return;
                }
            }
        characters_.clear();
        field_.refreshSprites(files_, &animFiles_);
        refreshItems();
        statusBar()->showMessage(characterLabel(replace) + " has its new drawings", 4000);
        return;
    }
    bool ok;
    QString name = QInputDialog::getText(this, "Import a character", "Its name, as the editor shows it:", QLineEdit::Normal,
                                         QFileInfo(path).completeBaseName(), &ok);
    if (!ok) return;
    int id = register_character(files_, file, t.width, (int)animFiles_.size(), name.trimmed().toStdString(), &err);
    if (id < 0) {
        QMessageBox::critical(this, "Import a character", QString::fromStdString(err));
        return;
    }
    if ((int)animFiles_.size() <= id) animFiles_.resize(id + 1, 0);
    animFiles_[id] = (CHARACTER_FILES + id - GAME_CHARACTERS) << 16;
    characters_.clear();
    if (field_.stage.name.empty()) return;
    if (QMessageBox::question(this, "Import a character", characterLabel(id) + " is yours.\n\nPlace it on this field?") == QMessageBox::Yes)
        placeCharacter(id);
}

void MainWindow::placeCharacter(int id) {
    /* in the middle of what the map shows; drag it from there */
    QPointF c = view_->mapToScene(view_->viewport()->rect().center());
    c.setX(std::clamp(c.x(), 0.0, (double)field_.width())), c.setY(std::clamp(c.y(), 0.0, (double)field_.height()));
    QString err;
    if (!addCharacter(id, c, &err) && !err.isEmpty()) QMessageBox::information(this, "Place a character", err);
}

void MainWindow::manageCharacters() {
    QDialog d(this);
    d.setWindowTitle("Your characters");
    d.resize(640, 420);
    auto *h = new QHBoxLayout(&d);
    auto *list = new QListWidget;
    list->setIconSize(QSize(64, 80));
    h->addWidget(list, 1);
    auto *side = new QVBoxLayout;
    h->addLayout(side);
    auto button = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text);
        b->setToolTip(tip);
        side->addWidget(b);
        return b;
    };
    auto *add = button("Import a new one...", "A filled template as a new character");
    auto *place = button("Place on this field", "Puts it in the middle of the map, with an image slot");
    auto *redraw = button("Import new drawings...", "Its frames from a filled template: every field that has it shows them");
    auto *exp = button("Export its template...", "Its frames as a template sheet to draw over");
    auto *rename = button("Rename...", "The name the editor shows");
    auto *del = button("Delete...", "Out of the list, and its file");
    side->addStretch();
    auto *close = button("Close", "");
    connect(close, &QPushButton::clicked, &d, &QDialog::accept);
    auto fill = [&] {
        list->clear();
        for (auto &c : read_own_characters(files_)) {
            auto *it = new QListWidgetItem(QIcon(characterThumb(c.id)), characterLabel(c.id));
            it->setData(Qt::UserRole, c.id);
            list->addItem(it);
        }
        if (!list->count()) list->addItem("None yet: Import a new one, from a template (Characters > Export a template)");
        list->setCurrentRow(0);
    };
    auto current = [&] { return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toInt() : 0; };
    auto enable = [&] {
        bool any = current() >= GAME_CHARACTERS;
        for (QPushButton *b : {place, redraw, exp, rename, del}) b->setEnabled(any);
        place->setEnabled(any && !field_.stage.name.empty());
    };
    connect(list, &QListWidget::currentRowChanged, &d, enable);
    connect(add, &QPushButton::clicked, &d, [&] {
        importCharacter(-1);
        fill();
    });
    connect(place, &QPushButton::clicked, &d, [&] {
        placeCharacter(current());
        d.accept();
    });
    connect(redraw, &QPushButton::clicked, &d, [&] {
        importCharacter(current());
        fill();
    });
    connect(exp, &QPushButton::clicked, &d, [&] { exportTemplate(current()); });
    connect(rename, &QPushButton::clicked, &d, [&] {
        std::vector<OwnCharacter> own = read_own_characters(files_);
        for (auto &c : own)
            if (c.id == current()) {
                bool ok;
                QString n = QInputDialog::getText(&d, "Rename", "Its name:", QLineEdit::Normal, QString::fromStdString(c.name), &ok);
                if (!ok) return;
                c.name = n.trimmed().toStdString();
                std::string err;
                if (!write_own_characters(files_, own, &err)) QMessageBox::critical(&d, "Rename", QString::fromStdString(err));
            }
        characters_.clear();
        fill();
        fillEntities();
    });
    connect(del, &QPushButton::clicked, &d, [&] {
        int id = current();
        /* the fields saved with it, and this one */
        QStringList used;
        QString needle = QString("\"id\":%1,").arg(id);
        for (const QFileInfo &fi : QDir(QString::fromStdString(files_.dir + "/mods/stages")).entryInfoList({"*.json"})) {
            QFile f(fi.filePath());
            if (f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).remove(' ').contains(needle)) used << fi.completeBaseName();
        }
        for (const Actor &a : field_.stage.actors)
            if (a.id == id && !used.contains(QString::fromStdString(field_.stage.name))) used << QString::fromStdString(field_.stage.name);
        QString warn = used.isEmpty() ? QString() : "\n\nThese fields have it, and would show nobody there: " + used.join(", ");
        if (QMessageBox::question(&d, "Delete a character", "Delete " + characterLabel(id) + "? Its file goes too." + warn) != QMessageBox::Yes)
            return;
        std::string err;
        if (!delete_character(files_, id, &err)) return (void)QMessageBox::critical(&d, "Delete a character", QString::fromStdString(err));
        if (id < (int)animFiles_.size()) animFiles_[id] = 0;
        characters_.clear();
        field_.refreshSprites(files_, &animFiles_);
        refreshItems();
        fill();
    });
    fill();
    enable();
    d.exec();
}
