/* The Properties panel: what is selected, as a form whose every change is
   an undoable edit of the stage's tables */
#include "mainwindow.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

QString slotType(int t);
bool leaves(int t);
extern const char *DIRS[8];

/* (code, value) pairs as the user writes them: "1A1F=1, 7093=0", codes in hex */
static QString codesText(const std::vector<std::pair<int, int>> &c) {
    QStringList parts;
    for (auto &[code, value] : c) parts << QString("%1=%2").arg(code, 4, 16, QChar('0')).toUpper().replace("X", "x").arg(value);
    return parts.join(", ");
}

static bool parseCodes(const QString &text, std::vector<std::pair<int, int>> &out) {
    out.clear();
    for (QString part : text.split(QRegularExpression("[,;]"), Qt::SkipEmptyParts)) {
        part = part.trimmed();
        int eq = part.indexOf('=');
        if (eq < 0) return false;
        bool ok1, ok2;
        int code = part.left(eq).trimmed().toInt(&ok1, 16), value = part.mid(eq + 1).trimmed().toInt(&ok2, 0);
        if (!ok1 || !ok2 || code < 0 || code > 0xFFFE) return false;
        out.push_back({code, value});
    }
    return true;
}

static QSpinBox *spin(int lo, int hi, int value, int base = 10) {
    auto *s = new QSpinBox;
    s->setRange(lo, hi);
    s->setDisplayIntegerBase(base);
    if (base == 16) s->setPrefix("0x");
    s->setValue(value);
    s->setKeyboardTracking(false); /* a value on Enter, an arrow or leaving, not on every digit */
    return s;
}

static QComboBox *dirBox(int dir) {
    auto *c = new QComboBox;
    for (int d = 0; d < 8; d++) c->addItem(DIRS[d], d);
    c->setCurrentIndex(std::clamp(dir, 0, 7));
    return c;
}

QLabel *MainWindow::richLabel(const QString &html) {
    auto *l = new QLabel(html);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    connect(l, &QLabel::linkActivated, this, &MainWindow::linkClicked);
    return l;
}

void MainWindow::linkClicked(const QString &link) {
    QStringList p = link.split(':');
    if (p.size() < 2) return;
    int n = p[1].toInt();
    if (p[0] == "mode" && modeStage_.contains(n)) openStage(QString::fromStdString(stages_[modeStage_[n]].name));
    else if (p[0] == "actor") select(KIND_ACTOR, n, true);
    else if (p[0] == "paint") {
        setTool(TOOL_TRIGGERS);
        valueBox_->setCurrentIndex(std::max(0, valueBox_->findData(n)));
    }
}

static QString esc(const std::string &s) { return QString::fromStdString(s).toHtmlEscaped().replace("\n", "<br>"); }

void MainWindow::showProperties() {
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const Stage &s = field_.stage;
    if (selKind_ == KIND_ACTOR && selIndex_ >= 0 && selIndex_ < (int)s.actors.size()) actorForm(f, selIndex_);
    else if (selKind_ == KIND_SLOT && selIndex_ >= 0 && selIndex_ < (int)s.slots.size()) slotForm(f, selIndex_);
    else if (selKind_ == KIND_OBJECT && selIndex_ >= 0 && selIndex_ < (int)s.objects.size()) objectForm(f, selIndex_);
    else if (selKind_ == KIND_START) startForm(f);
    else if (selKind_ == KIND_BATTLES) battlesForm(f);
    else stageForm(f);
    /* the old form may be the sender of the signal that brought us here */
    if (QWidget *old = props_->takeWidget()) old->deleteLater();
    props_->setWidget(w);
}

void MainWindow::actorForm(QFormLayout *f, int index) {
    const Actor &a = field_.stage.actors[index];
    int group = actorGroup_[index];
    f->addRow(richLabel(QString("<h3>%1</h3>").arg(actorName(index).toHtmlEscaped())));
    QString notes = QString("Entry #%1").arg(index);
    if (invisibleActor(index)) notes += "<br>Not drawn: a spot the player talks to (this character has no sprite)";
    if (group < 0) notes += "<br>Placed by the field's events (0, 0 in the table)";
    else if (groups_[group].actors.size() > 1) {
        const auto &v = groups_[group].actors;
        int k = (int)(std::find(v.begin(), v.end(), index) - v.begin());
        QString nav = QString("Version %1 of %2").arg(k + 1).arg(v.size());
        if (k > 0) nav = QString("<a href='actor:%1'>&larr;</a> ").arg(v[k - 1]) + nav;
        if (k + 1 < (int)v.size()) nav += QString(" <a href='actor:%1'>&rarr;</a>").arg(v[k + 1]);
        notes += "<br>" + nav + "<br><span style='color:#999'>the game shows the first whose conditions hold</span>";
    }
    f->addRow(richLabel(notes));

    auto edit = [this, index](auto change, const QString &what) {
        Stage s = field_.stage;
        change(s.actors[index]);
        changeTables(s, what);
    };
    auto *id = spin(0, (int)animFiles_.size() - 1, a.id, 16);
    id->setToolTip("The character: its sprite and size (FIELDSTG_fileEntries)");
    connect(id, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Actor &x) { x.id = v; }, "Change the character"); });
    f->addRow("Character", id);
    auto *image = spin(0, std::max(0, (int)field_.stage.images.size() - 3), a.image);
    image->setToolTip("Where in VRAM its frames go: one of the field's character image slots");
    connect(image, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Actor &x) { x.image = v; }, "Change the image slot"); });
    f->addRow("Image slot", image);
    auto *x = spin(0, field_.width(), a.x), *y = spin(0, field_.height(), a.y);
    connect(x, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Actor &c) { c.x = v; }, "Move a character"); });
    connect(y, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Actor &c) { c.y = v; }, "Move a character"); });
    auto *pos = new QHBoxLayout;
    pos->addWidget(x);
    pos->addWidget(y);
    f->addRow("Position", pos);
    auto *dir = dirBox(a.dir);
    connect(dir, &QComboBox::currentIndexChanged, this, [=](int v) { edit([v](Actor &c) { c.dir = v; }, "Turn a character"); });
    f->addRow("Facing", dir);
    auto *cond = new QLineEdit(codesText(a.conditions));
    cond->setPlaceholderText("always");
    cond->setToolTip("code=value pairs, codes in hex: 1A1F=1 (flag 1A:1F set), 602A=1 (story point 0x2A)...");
    connect(cond, &QLineEdit::editingFinished, this, [=] {
        std::vector<std::pair<int, int>> c;
        if (!parseCodes(cond->text(), c)) return cond->setStyleSheet("color:#f66");
        if (c != field_.stage.actors[index].conditions) edit([c](Actor &x) { x.conditions = c; }, "Change when a character appears");
    });
    f->addRow("Appears if", cond);
    f->addRow("", richLabel("<span style='color:#999'>" + esc(describe_codes(a.conditions, text_, false)) + "</span>"));

    /* what it says: each line with its conditions and actions */
    f->addRow(richLabel("<h4>What it says</h4><span style='color:#999'>The first line whose conditions hold; the "
                        "last one has none.</span>"));
    for (int k = 0; k < (int)a.talks.size(); k++) {
        const Talk &t = a.talks[k];
        auto *box = new QGroupBox(QString("Line %1").arg(k + 1));
        auto *bf = new QFormLayout(box);
        auto *when = new QLineEdit(codesText(t.conditions));
        when->setPlaceholderText(k + 1 == (int)a.talks.size() ? "always (the last line)" : "always");
        connect(when, &QLineEdit::editingFinished, this, [=] {
            std::vector<std::pair<int, int>> c;
            if (!parseCodes(when->text(), c)) return when->setStyleSheet("color:#f66");
            if (c != field_.stage.actors[index].talks[k].conditions) edit([c, k](Actor &x) { x.talks[k].conditions = c; }, "Change a line's conditions");
        });
        bf->addRow("When", when);
        auto *textBox = new QComboBox;
        for (int e = 0; e < (int)talks_.size(); e++) {
            QString line = QString::fromStdString(talks_[e]).replace('\n', ' ');
            textBox->addItem(QString("%1: %2").arg(e).arg(line.left(70)), e);
        }
        if (t.entry >= (int)talks_.size()) textBox->addItem(QString("%1: (no such text)").arg(t.entry), t.entry);
        textBox->setCurrentIndex(std::max(0, textBox->findData(t.entry)));
        textBox->setToolTip("A text of the field's text file");
        connect(textBox, &QComboBox::currentIndexChanged, this, [=](int) {
            int e = textBox->currentData().toInt();
            edit([e, k](Actor &x) { x.talks[k].entry = e; }, "Change a line's text");
        });
        bf->addRow("Says", textBox);
        bf->addRow("", richLabel("<i>" + esc(talkText(t.entry)) + "</i>"));
        auto *then = new QLineEdit(codesText(t.actions));
        then->setPlaceholderText("nothing");
        then->setToolTip("code=value pairs it applies when the text ends: 1A1F=1 sets flag 1A:1F...");
        connect(then, &QLineEdit::editingFinished, this, [=] {
            std::vector<std::pair<int, int>> c;
            if (!parseCodes(then->text(), c)) return then->setStyleSheet("color:#f66");
            if (c != field_.stage.actors[index].talks[k].actions) edit([c, k](Actor &x) { x.talks[k].actions = c; }, "Change a line's actions");
        });
        bf->addRow("Then", then);
        if (!t.actions.empty()) bf->addRow("", richLabel("<span style='color:#999'>" + esc(describe_codes(t.actions, text_, true)) + "</span>"));
        auto *buttons = new QHBoxLayout;
        auto *up = new QToolButton, *down = new QToolButton, *del = new QToolButton;
        up->setText("Up"), down->setText("Down"), del->setText("Remove");
        up->setEnabled(k > 0), down->setEnabled(k + 1 < (int)a.talks.size()), del->setEnabled(a.talks.size() > 1);
        connect(up, &QToolButton::clicked, this, [=] { edit([k](Actor &x) { std::swap(x.talks[k], x.talks[k - 1]); }, "Move a line up"); });
        connect(down, &QToolButton::clicked, this, [=] { edit([k](Actor &x) { std::swap(x.talks[k], x.talks[k + 1]); }, "Move a line down"); });
        connect(del, &QToolButton::clicked, this, [=] { edit([k](Actor &x) { x.talks.erase(x.talks.begin() + k); }, "Remove a line"); });
        buttons->addWidget(up);
        buttons->addWidget(down);
        buttons->addStretch();
        buttons->addWidget(del);
        bf->addRow(buttons);
        f->addRow(box);
    }
    auto *add = new QPushButton("Add a line");
    connect(add, &QPushButton::clicked, this, [=] {
        edit([](Actor &x) { x.talks.insert(x.talks.begin(), Talk{x.talks.empty() ? 0 : x.talks[0].entry, {}, {}}); }, "Add a line");
    });
    add->setToolTip("A first line; give it conditions, so the others still come after it");
    f->addRow(add);
}

void MainWindow::slotForm(QFormLayout *f, int index) {
    const Slot &t = field_.stage.slots[index];
    f->addRow(richLabel(QString("<h3>%1 (slot %2)</h3>").arg(slotType(t.type)).arg(index)));
    auto edit = [this, index](auto change, const QString &what) {
        Stage s = field_.stage;
        change(s.slots[index]);
        changeTables(s, what);
    };
    auto *type = new QComboBox;
    static const char *TIPS[15] = {"", "leaves for another field", "climbs up a ladder", "climbs down a ladder", "drops down",
                                   "changes the player's depth", "changes the floor map the player walks on", "the gauge game",
                                   "starts an event", "a warp, with effect 1", "a warp, with effect 0", "slides", "stops a slide",
                                   "sends the player flying", "sends the player flying to another field"};
    for (int k = 1; k < 15; k++) type->addItem(QString("%1: %2").arg(slotType(k), TIPS[k]), k);
    type->setCurrentIndex(std::max(0, type->findData(t.type)));
    connect(type, &QComboBox::currentIndexChanged, this, [=](int) {
        int v = type->currentData().toInt();
        edit([v](Slot &s) { s.type = v; }, "Change a slot's type");
    });
    f->addRow("Type", type);
    if (leaves(t.type)) {
        auto *dest = new QComboBox;
        std::vector<std::pair<QString, int>> modes;
        for (auto &st : stages_)
            for (auto &m : st.modes) modes.push_back({modeName(m.mode) + QString("  (%1%2)").arg(m.mode).arg(m.extra ? ", extra" : ""), m.mode});
        std::sort(modes.begin(), modes.end());
        for (auto &[n, m] : modes) dest->addItem(n, m);
        int at = dest->findData((int16_t)t.arg);
        if (at < 0) dest->addItem(QString("mode %1").arg((int16_t)t.arg), (int16_t)t.arg), at = dest->count() - 1;
        dest->setCurrentIndex(at);
        connect(dest, &QComboBox::currentIndexChanged, this, [=](int) {
            int v = dest->currentData().toInt();
            edit([v](Slot &s) { s.arg = v & 0xFFFF; }, "Change where an exit leads");
        });
        f->addRow("Leads to", dest);
        f->addRow("", richLabel(QString("<a href='mode:%1'>Open it</a>").arg((int16_t)t.arg)));
        auto *x = spin(-32768, 32767, (int16_t)t.x), *y = spin(-32768, 32767, (int16_t)t.y);
        connect(x, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.x = v & 0xFFFF; }, "Change where an exit arrives"); });
        connect(y, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.y = v & 0xFFFF; }, "Change where an exit arrives"); });
        auto *pos = new QHBoxLayout;
        pos->addWidget(x);
        pos->addWidget(y);
        f->addRow("Arrives at", pos);
        auto *dir = dirBox(t.dir);
        connect(dir, &QComboBox::currentIndexChanged, this, [=](int v) { edit([v](Slot &s) { s.dir = v; }, "Change an exit's facing"); });
        f->addRow("Facing", dir);
        auto *place = spin(0, 65535, t.place), *placeArg = spin(0, 65535, t.placeArg);
        place->setToolTip("GAME.place on arrival: which place's battles the next field uses");
        connect(place, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.place = v; }, "Change an exit's place"); });
        connect(placeArg, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.placeArg = v; }, "Change an exit's place"); });
        auto *pl = new QHBoxLayout;
        pl->addWidget(place);
        pl->addWidget(placeArg);
        f->addRow("Place, argument", pl);
    } else {
        auto *arg = spin(-32768, 32767, (int16_t)t.arg);
        arg->setToolTip("By type: a depth, a map, an event, or a height in 16 pixels (plus 1 for a climb)");
        connect(arg, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.arg = v & 0xFFFF; }, "Change a slot's argument"); });
        f->addRow("Argument", arg);
        auto *x = spin(-32768, 32767, (int16_t)t.x), *y = spin(-32768, 32767, (int16_t)t.y), *d = spin(0, 65535, t.dir);
        connect(x, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.x = v & 0xFFFF; }, "Change a slot"); });
        connect(y, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.y = v & 0xFFFF; }, "Change a slot"); });
        connect(d, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.dir = v; }, "Change a slot"); });
        auto *vals = new QHBoxLayout;
        vals->addWidget(x);
        vals->addWidget(y);
        vals->addWidget(d);
        f->addRow("Values", vals);
    }
    auto *hide = spin(0, 255, t.hideAnim);
    hide->setToolTip("The animation of the map objects hidden on arrival, or 0");
    connect(hide, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Slot &s) { s.hideAnim = v; }, "Change a slot"); });
    f->addRow("Hides objects", hide);
    std::vector<std::pair<int, int>> c;
    for (auto &k : t.cond)
        if (k[0] != 0xFFFF) c.push_back({k[0], k[1]});
    auto *cond = new QLineEdit(codesText(c));
    cond->setPlaceholderText("always");
    cond->setToolTip("At most two code=value pairs, codes in hex");
    connect(cond, &QLineEdit::editingFinished, this, [=] {
        std::vector<std::pair<int, int>> p;
        if (!parseCodes(cond->text(), p) || p.size() > 2) return cond->setStyleSheet("color:#f66");
        edit([p](Slot &s) {
            for (int k = 0; k < 2; k++) s.cond[k][0] = k < (int)p.size() ? p[k].first : 0xFFFF, s.cond[k][1] = k < (int)p.size() ? p[k].second : 0;
        }, "Change when a slot works");
    });
    f->addRow("Works if", cond);
    f->addRow("", richLabel("<span style='color:#999'>" + esc(describe_codes(c, text_, false)) + "</span>"));
    QString cells = index < 32 && !slotCells_[index].isNull()
                        ? QString("Cells %1, %2 to %3, %4").arg(slotCells_[index].left()).arg(slotCells_[index].top()).arg(slotCells_[index].right()).arg(slotCells_[index].bottom())
                        : QString("No cells set it off yet");
    f->addRow(richLabel(cells + QString(" · <a href='paint:%1'>Paint its cells</a>").arg(index)));
}

void MainWindow::objectForm(QFormLayout *f, int index) {
    const Object &o = field_.stage.objects[index];
    f->addRow(richLabel(QString("<h3>Object #%1</h3>").arg(index)));
    auto edit = [this, index](auto change, const QString &what) {
        Stage s = field_.stage;
        change(s.objects[index]);
        changeTables(s, what);
    };
    auto field = [&](const QString &name, int lo, int hi, int value, auto set, const QString &tip = QString()) {
        auto *s = spin(lo, hi, value);
        s->setToolTip(tip);
        connect(s, &QSpinBox::valueChanged, this, [=](int v) { edit([=](Object &x) { set(x, v); }, "Change an object"); });
        f->addRow(name, s);
    };
    auto *visible = new QCheckBox("Shown from the start");
    visible->setChecked(o.visible);
    connect(visible, &QCheckBox::toggled, this, [=](bool v) { edit([v](Object &x) { x.visible = v; }, "Show or hide an object"); });
    f->addRow("", visible);
    field("X", -32768, 32767, o.x, [](Object &x, int v) { x.x = v; });
    field("Y", -32768, 32767, o.y, [](Object &x, int v) { x.y = v; });
    field("Frame", 0, 255, o.frame, [](Object &x, int v) { x.frame = v; }, "The frame of the field's sprite sheet");
    field("Depth", 0, 255, o.depth, [](Object &x, int v) { x.depth = v; }, "Its order table depth: larger is farther (the map's tiles are 3, 7 and 11)");
    field("Palette row", 0, 255, o.clutRow, [](Object &x, int v) { x.clutRow = v; });
    field("Sorted at y", -32768, 32767, o.sortY, [](Object &x, int v) { x.sortY = v; }, "Drawn among the characters sorted by this y, or 0");
    field("Animation group", 0, 255, o.anim, [](Object &x, int v) { x.anim = v; }, "Events find objects by it; 255: a hidden spot");
    auto *cycle = new QComboBox;
    cycle->addItems({"None", "Cycles its frames", "Cycles its palette rows", "Palette rows back and forth"});
    cycle->setCurrentIndex(std::clamp(o.cycle, 0, 3));
    connect(cycle, &QComboBox::currentIndexChanged, this, [=](int v) { edit([v](Object &x) { x.cycle = v; }, "Change an object's animation"); });
    f->addRow("Animation", cycle);
    field("From", 0, 255, o.cycleFirst, [](Object &x, int v) { x.cycleFirst = v; });
    field("To", 0, 255, o.cycleLast, [](Object &x, int v) { x.cycleLast = v; });
    field("Every (frames)", 0, 255, o.cycleDelay, [](Object &x, int v) { x.cycleDelay = v; });
    field("Margin", 1, 255, o.margin, [](Object &x, int v) { x.margin = v; }, "How far off the view it is still drawn");
}

void MainWindow::startForm(QFormLayout *f) {
    const Stage &s = field_.stage;
    f->addRow(richLabel("<h3>Start point</h3><span style='color:#999'>Where the player stands when the field is "
                        "entered without an exit's position</span>"));
    auto *x = spin(0, field_.width(), s.startX >> 8), *y = spin(0, field_.height(), s.startY >> 8);
    connect(x, &QSpinBox::valueChanged, this, [=](int v) {
        Stage t = field_.stage;
        t.startX = v << 8;
        changeTables(t, "Move the start");
    });
    connect(y, &QSpinBox::valueChanged, this, [=](int v) {
        Stage t = field_.stage;
        t.startY = v << 8;
        changeTables(t, "Move the start");
    });
    auto *pos = new QHBoxLayout;
    pos->addWidget(x);
    pos->addWidget(y);
    f->addRow("Position", pos);
    auto *dir = dirBox(s.startDir);
    connect(dir, &QComboBox::currentIndexChanged, this, [=](int v) {
        Stage t = field_.stage;
        t.startDir = v;
        changeTables(t, "Turn the start");
    });
    f->addRow("Facing", dir);
}

void MainWindow::stageForm(QFormLayout *f) {
    const Stage &s = field_.stage;
    QString h = "<h3>" + modeName(s.modes.empty() ? -1 : s.modes[0].mode).toHtmlEscaped() + "</h3><table>";
    auto row = [&](const QString &k, const QString &v) { h += "<tr><td style='color:#999;padding-right:8px'>" + k + "</td><td>" + v + "</td></tr>"; };
    row("Stage", QString::fromStdString(s.name).toUpper());
    QStringList modes;
    for (auto &m : s.modes) modes << QString::number(m.mode) + (m.extra ? " (extra chapter)" : "");
    row("Modes", modes.join(", "));
    row("Size", QString("%1 × %2 tiles (%3 × %4 pixels)").arg(field_.tilesW).arg(field_.tilesH).arg(field_.width()).arg(field_.height()));
    row("Characters", QString::number(s.actors.size()));
    row("Exits and triggers", QString::number(s.slots.size()));
    row("Objects", QString::number(s.objects.size()));
    row("Battle places", QString::number(s.places.size()));
    row("Events", QString::number(s.events.size()));
    h += "</table><p style='color:#999'>Click something on the map or in Contents to edit it here. Right click the "
         "map to add things; the tools above paint the floor, the triggers and the battle areas.</p>";
    f->addRow(richLabel(h));
}

void MainWindow::battlesForm(QFormLayout *f) {
    const Stage &s = field_.stage;
    f->addRow(richLabel("<h3>Battles</h3><span style='color:#999'>Each place's four battle areas (the areas painted on "
                        "the map): how often battles come, and the eight the game picks one of at random. A stage with "
                        "several places uses the one GAME.place names.</span>"));
    if (s.places.empty()) {
        f->addRow(richLabel("This field has no battles."));
        return;
    }
    for (int p = 0; p < (int)s.places.size(); p++) {
        for (int a = 0; a < 4; a++) {
            const Area &ar = s.places[p].areas[a];
            auto *box = new QGroupBox(QString("Place %1 · area %2").arg(s.places[p].id).arg(a + 1));
            auto *bf = new QFormLayout(box);
            auto edit = [this, p, a](auto change, const QString &what) {
                Stage t = field_.stage;
                change(t.places[p].areas[a]);
                changeTables(t, what);
            };
            auto *rate = spin(0, 255, ar.rate);
            rate->setToolTip("How often battles come: an index of FIELDSTG_battleRates (0: none)");
            connect(rate, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Area &x) { x.rate = v; }, "Change a battle rate"); });
            bf->addRow("Rate", rate);
            for (int k = 0; k < 8; k++) {
                auto *enc = spin(0, 65535, ar.battles[k].encounter), *stage = spin(0, 255, ar.battles[k].stage);
                enc->setToolTip("The encounter (FIELDSTG_encounters)");
                stage->setToolTip("The fight stage it is fought on");
                connect(enc, &QSpinBox::valueChanged, this, [=](int v) { edit([v, k](Area &x) { x.battles[k].encounter = v; }, "Change a battle"); });
                connect(stage, &QSpinBox::valueChanged, this, [=](int v) { edit([v, k](Area &x) { x.battles[k].stage = v; }, "Change a battle"); });
                auto *row = new QHBoxLayout;
                row->addWidget(new QLabel("encounter"));
                row->addWidget(enc);
                row->addWidget(new QLabel("stage"));
                row->addWidget(stage);
                bf->addRow(QString("%1").arg(k + 1), row);
            }
            f->addRow(box);
        }
    }
}
