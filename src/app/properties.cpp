/* The Properties panel: what is selected, as a form whose every change is
   an undoable edit of the stage's tables */
#include "mainwindow.h"
#include "characters.h"
#include <QMessageBox>
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
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

QString slotType(int t);
bool leaves(int t);
extern const char *DIRS[8];

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
    else if (p[0] == "slot") select(KIND_SLOT, n, true);
    else if (p[0] == "event") select(KIND_EVENT, n, true);
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
    else if (selKind_ == KIND_EVENT && selIndex_ >= 0 && selIndex_ < (int)s.events.size()) eventForm(f, selIndex_);
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
    /* the character, by its look; it takes an image slot that suits it */
    auto *who = new QToolButton;
    who->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    who->setIcon(QIcon(characterThumb(a.id)));
    who->setIconSize(QSize(32, 40));
    who->setText(characterLabel(a.id) + "  ▾");
    who->setToolTip("The character: its sprite and size. Click to pick another by its look");
    connect(who, &QToolButton::clicked, this, [=] {
        int v = pickCharacter("The character", field_.stage.actors[index].id);
        if (v < 0 || v == field_.stage.actors[index].id) return;
        Stage s = field_.stage;
        s.actors[index].id = v;
        QString err;
        if (!giveSlot(s, index, &err)) statusBar()->showMessage(err + " It keeps its slot.", 6000);
        changeTables(s, "Change the character");
    });
    f->addRow("Character", who);
    /* its image slot: shared only by characters of its look, as the game does */
    auto *image = spin(0, std::max(0, (int)field_.stage.images.size() - 3), a.image);
    image->setToolTip("Where in VRAM its frames go: one of the field's character image slots. The editor picks it; "
                      "characters of the same look share one");
    connect(image, &QSpinBox::valueChanged, this, [=](int v) { edit([v](Actor &x) { x.image = v; }, "Change the image slot"); });
    auto *slotRow = new QHBoxLayout;
    slotRow->addWidget(image);
    QStringList others = slotSharers(index);
    if (!others.isEmpty()) {
        auto *own = new QToolButton;
        own->setText("Give it its own");
        own->setToolTip("Makes it an image slot where the field's sprite memory has room, as big as its frames");
        connect(own, &QToolButton::clicked, this, [=] {
            Stage s = field_.stage;
            QString err;
            if (!giveSlot(s, index, &err, true)) return (void)QMessageBox::information(this, "Image slot", err);
            changeTables(s, "Give a character its own image slot");
        });
        slotRow->addWidget(own);
    }
    slotRow->addStretch();
    f->addRow("Image slot", slotRow);
    if (!others.isEmpty())
        f->addRow("", richLabel("<span style='color:#e66'>Shared with " + others.join(", ").toHtmlEscaped() +
                                ": when both are on screen, one is drawn with the other's frames.</span>"));
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
    if (a.conditions.empty()) f->addRow("Appears", richLabel("always"));
    else f->addRow(richLabel("Appears if all of these hold:"));
    f->addRow(codesEditor(a.conditions, false, 0, [=](const Codes &c) { edit([c](Actor &x) { x.conditions = c; }, "Change when a character appears"); }));

    /* what it says: each line with its conditions and actions */
    f->addRow(richLabel("<h4>What it says</h4><span style='color:#999'>The first line whose conditions hold; the "
                        "last one has none.</span>"));
    for (int k = 0; k < (int)a.talks.size(); k++) {
        const Talk &t = a.talks[k];
        auto *box = new QGroupBox(QString("Line %1").arg(k + 1));
        auto *bf = new QFormLayout(box);
        bf->addRow(richLabel(t.conditions.empty() ? (k == 0 ? (a.talks.size() == 1 ? "Always said" : "Always said (the lines below never are)") : k + 1 == (int)a.talks.size() ? "Said when the lines above aren't" : "Always said (the lines below never are)")
                                                  : "Said when all of these hold:"));
        bf->addRow(codesEditor(t.conditions, false, 0, [=](const Codes &c) { edit([c, k](Actor &x) { x.talks[k].conditions = c; }, "Change a line's conditions"); }));
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
        if (untranslated([this](int l) { return talkKey(l); }, t.entry)) bf->addRow("", richLabel("<span style='color:#e0b040'>The same in every language: switch View &gt; Text language to translate it.</span>"));
        /* the game's empty texts only end its lists, after lines that cover every case */
        if (k == 0 && talkText(t.entry).empty() && !invisibleActor(index))
            bf->addRow("", richLabel("<span style='color:#e66'>An empty text: talking to it shows an empty box and the game "
                                     "crashes. Give it a new text.</span>"));
        auto *textButtons = new QHBoxLayout;
        auto *editText = new QToolButton, *newText = new QToolButton;
        editText->setText("Edit the text...");
        editText->setToolTip(QString("Change this text, in %1").arg(LANGUAGE_NAMES[text_.lang]));
        editText->setEnabled(t.entry >= 0 && t.entry < (int)talks_.size());
        newText->setText("New text...");
        newText->setToolTip("Write a text of its own for this line");
        connect(editText, &QToolButton::clicked, this, [=] { editTalkText(field_.stage.actors[index].talks[k].entry); });
        connect(newText, &QToolButton::clicked, this, [=] { newTalkText(index, k); });
        textButtons->addWidget(editText);
        textButtons->addWidget(newText);
        textButtons->addStretch();
        bf->addRow("", textButtons);
        bf->addRow(richLabel(t.actions.empty() ? "Then nothing happens" : "Then, as the text ends:"));
        bf->addRow(codesEditor(t.actions, true, 0, [=](const Codes &c) { edit([c, k](Actor &x) { x.talks[k].actions = c; }, "Change a line's actions"); }));
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

/* The commands an event script has (FIELDSTG_runEvent), as the form shows them */
struct CommandInfo {
    int op;
    const char *name, *args[4], *tip;
};
static const CommandInfo COMMANDS[] = {
    {0x100, "Place", {"character", "x", "y"}, "puts a character at a point of the map"},
    {0x102, "Walk to", {"character", "x", "y", "facing"}, "walks a character to a point, facing a way there"},
    {0x101, "Pose", {"character", "animation", "facing"},
     "plays a character's animation set (1 stand, 4 walk, 5 run, 8 search...) facing a way; a character of 0x320 on "
     "is a script command, 0x32D the field's (its animation the command)"},
    {0x200, "Say", {"box", "text", "character", "box type"},
     "opens talk box 0 or 1 with a text of the event's: box types 0-3 by a character, 4 a message"},
    {0x300, "Wait", {"frames"}, "waits some frames (50 or 60 a second)"},
    {0x301, "Wait for the text", {}, "waits until talk box 0 is closed"},
    {0x302, "Wait for the walk", {"character"}, "waits until a character gets where it walks"},
    {0x303, "Wait for the animation", {"character"}, "waits until a character's animation ends"},
    {0x304, "Leave for", {"mode", "x", "y", "facing"}, "leaves for a field (its mode), arriving at a point: the event ends"},
    {0x600, "Camera follows", {"snap", "character"}, "the camera follows a character (snap 1: at once)"},
    {0x601, "Camera to", {"snap", "x", "y"}, "the camera goes to a point (snap 1: at once)"},
};
static const CommandInfo *commandInfo(int op) {
    for (const CommandInfo &c : COMMANDS)
        if (c.op == op || ((op >> 8) == 2 && c.op == 0x200)) return &c;
    return nullptr;
}
QString commandName(int op) {
    const CommandInfo *c = commandInfo(op);
    return c ? c->name : QString("command %1").arg(op, 0, 16);
}

/* The field's commands (FIELD_TASK_COMMANDS' arguments, field_map.h) */
static const std::pair<int, const char *> FIELD_COMMANDS[] = {
    {0x337, "halt partners"}, {0x338, "player icon 1, item got"}, {0x339, "player icon 2"}, {0x34A, "player icon 3, item got"},
    {0x34D, "hide objects 0"}, {0x34E, "hide objects 1"}, {0x34F, "hide objects 2"}, {0x350, "hide objects 3"},
    {0x351, "hide objects 4"}, {0x352, "hide objects 5"}, {0x353, "show objects 0"}, {0x354, "show objects 1"},
    {0x355, "show objects 2"}, {0x356, "show objects 3"}, {0x357, "show objects 4"}, {0x358, "show objects 5"},
    {0x365, "play INFO_SIG"}, {0x366, "play GAYALOOP"}, {0x367, "stop GAYALOOP"}, {0x368, "play WEAR_OFF"},
    {0x369, "play DEMO_BGM"}, {0x36A, "play SE000002"}, {0x36B, "play BEAM_SHT"}, {0x36C, "play SWITCH02"},
    {0x36D, "play MASK_SET"}, {0x36E, "play BM_ERASE"}, {0x36F, "play LD_ERASE"}, {0x370, "play PLAYER11"},
    {0x371, "stop PLAYER11"}, {0x372, "shake the camera"}, {0x373, "stop the camera's shake"}, {0x374, "play TRAP_OFF"},
    {0x375, "play SAVEDEMO"}, {0x376, "search the event spot"}, {0x377, "play SWITCH03"}, {0x378, "play SN_ENTRY"},
    {0x379, "play SN_ERASE"}, {0x37A, "play TELEPORT"}, {0x37C, "play BULB_003"}, {0x37D, "play GONDRA_S"},
    {0x37E, "play PIYOPIYO"}, {0x37F, "play COMCD103"}, {0x380, "play COMCD201"}, {0x381, "play COMCD111"},
    {0x382, "play BEAM_HIT"}, {0x383, "play SWITCH01"}, {0x384, "play COMCD115"}, {0x385, "stop COMCD115"},
    {0x386, "stop BEAM_HIT"},
};

QString MainWindow::characterName(int id) const {
    if (id == CHARACTER_PLAYER) return "the player";
    if (id >= CHARACTER_PARTNERS && id < CHARACTER_PARTNERS + 3) return QString("partner %1").arg(id - CHARACTER_PARTNERS + 1);
    for (int i = 0; i < (int)field_.stage.actors.size(); i++)
        if (field_.stage.actors[i].id == id) return actorName(i);
    return QString("character 0x%1").arg(id, 0, 16);
}

void MainWindow::eventForm(QFormLayout *f, int index) {
    const Event &e = field_.stage.events[index];
    const Stage &st = field_.stage;
    f->addRow(richLabel(QString("<h3>Event %1</h3>").arg(e.id)));
    /* what starts it in this field */
    QStringList by;
    for (int i = 0; i < (int)st.slots.size(); i++)
        if (st.slots[i].type == 8 && (int16_t)st.slots[i].arg == e.id) by << QString("<a href='slot:%1'>slot %1</a>").arg(i);
    for (int i = 0; i < (int)st.actors.size(); i++)
        for (const Talk &t : st.actors[i].talks)
            for (auto &[code, value] : t.actions)
                if (e.id >= EDITOR_EVENTS && code == (0x9100 | (e.id - EDITOR_EVENTS)))
                    by << QString("<a href='actor:%1'>%2</a>'s line").arg(i).arg(actorName(i).toHtmlEscaped());
    QString notes = by.isEmpty() ? "Nothing in this field starts it: a slot of type Event does, or the story (the game's own lists)."
                                 : "Started by " + by.join(", ") + ".";
    if (e.id >= EDITOR_EVENTS && e.id < EDITOR_EVENTS + 0x100)
        notes += QString("<br>A character's line starts it with the action <b>Start an event: Event %1</b>.").arg(e.id);
    if (e.start) notes += "<br>Code starts it: it has no script to edit.";
    if (e.end) notes += "<br>Code of its own runs when it ends.";
    f->addRow(richLabel(notes));
    if (!e.scripted) return;

    auto commands = parse_script(e.script);
    auto edit = [this, index](std::vector<Command> c, const QString &what) {
        Stage s = field_.stage;
        s.events[index].script = build_script(c);
        changeTables(s, what);
    };
    int key = eventKey(e.text, text_.lang);
    auto texts = e.text ? textStrings(key) : std::vector<std::vector<uint8_t>>();
    f->addRow(richLabel(QString("<h4>Script</h4><span style='color:#999'>%1 commands, run in order; the waits hold the "
                                "script until they are done. Texts: file %2, entry %3.</span>")
                            .arg(commands.size()).arg(e.text >> 16).arg(e.text & 0xFFFF)));
    for (int k = 0; k < (int)commands.size(); k++) {
        const Command &c = commands[k];
        const CommandInfo *info = commandInfo(c.op);
        auto *box = new QGroupBox(QString("%1. %2").arg(k + 1).arg(info ? info->name : "?"));
        auto *bf = new QFormLayout(box);
        auto *kind = new QComboBox;
        for (const CommandInfo &ci : COMMANDS) kind->addItem(ci.name, ci.op);
        kind->setCurrentIndex(std::max(0, kind->findData(info ? info->op : -1)));
        if (info) kind->setToolTip(info->tip);
        connect(kind, &QComboBox::currentIndexChanged, this, [=](int) {
            auto cs = commands;
            cs[k].op = kind->currentData().toInt();
            cs[k].args.resize(command_args(cs[k].op), 0);
            edit(cs, "Change a command");
        });
        bf->addRow("Command", kind);
        for (int a = 0; a < (int)c.args.size(); a++) {
            QString label = info && info->args[a] ? info->args[a] : QString("argument %1").arg(a + 1);
            bool character = label == "character", hex = character || label == "mode";
            auto *v = spin(-32768, 32767, c.args[a], hex ? 16 : 10);
            connect(v, &QSpinBox::valueChanged, this, [=](int val) {
                auto cs = commands;
                cs[k].args[a] = val;
                edit(cs, "Change a command");
            });
            bf->addRow(label.left(1).toUpper() + label.mid(1), v);
            if (character && c.args[a] >= SCRIPT_COMMANDS) {
                QString what = QString("script command %1").arg(c.args[a]);
                if (c.args[a] == FIELD_COMMANDS_TASK) what = "the field's command";
                bf->addRow("", richLabel("<span style='color:#999'>" + what + "</span>"));
            } else if (character) bf->addRow("", richLabel("<span style='color:#999'>" + characterName(c.args[a]).toHtmlEscaped() + "</span>"));
            if (label == "facing" && c.args[a] >= 0 && c.args[a] < 8) v->setSuffix(QString("  (%1)").arg(DIRS[c.args[a]]));
            if (label == "mode") bf->addRow("", richLabel("<span style='color:#999'>" + modeName(c.args[a]).toHtmlEscaped() + "</span>"));
        }
        /* a pose of the field's commands: its name */
        if (c.op == 0x101 && c.args.size() > 1 && c.args[0] == FIELD_COMMANDS_TASK)
            for (auto &[id, name] : FIELD_COMMANDS)
                if (id == c.args[1]) bf->addRow("", richLabel(QString("<span style='color:#999'>%1</span>").arg(name)));
        /* a speech: its text, written here */
        if ((c.op >> 8) == 2 && c.args.size() > 1) {
            int t = c.args[1];
            if (!e.text) bf->addRow("", richLabel("<span style='color:#f66'>The event has no texts.</span>"));
            else {
                std::string line = t >= 0 && t < (int)texts.size() ? decode_text(texts[t].data(), texts[t].size()) : "(no such text)";
                bf->addRow("", richLabel("<i>" + esc(line) + "</i>"));
                int evText = e.text;
                if (untranslated([this, evText](int l) { return eventKey(evText, l); }, t)) bf->addRow("", richLabel("<span style='color:#e0b040'>The same in every language: switch View &gt; Text language to translate it.</span>"));
                auto *tb = new QHBoxLayout;
                auto *editText = new QToolButton, *newText = new QToolButton;
                editText->setText("Edit the text...");
                editText->setEnabled(t >= 0 && t < (int)texts.size());
                newText->setText("New text...");
                connect(editText, &QToolButton::clicked, this, [=] {
                    editString(key, t, QString("Text %1 of event %2's texts, in %3.").arg(t).arg(e.id).arg(LANGUAGE_NAMES[text_.lang]));
                });
                int text = e.text;
                connect(newText, &QToolButton::clicked, this, [=] {
                    GameString g;
                    if (t >= 0 && t < (int)texts.size()) g.speaker = bytes_to_text(texts[t]).speaker;
                    appendString([this, text](int l) { return eventKey(text, l); }, g,
                                 "A new text at the end of the event's texts, in every language as you write it here.",
                                 [=](int entry) {
                                     auto cs = commands;
                                     cs[k].args[1] = entry;
                                     edit(cs, "Change a command");
                                 });
                });
                tb->addWidget(editText);
                tb->addWidget(newText);
                tb->addStretch();
                bf->addRow("", tb);
            }
        }
        auto *buttons = new QHBoxLayout;
        auto *up = new QToolButton, *down = new QToolButton, *del = new QToolButton;
        up->setText("Up"), down->setText("Down"), del->setText("Remove");
        up->setEnabled(k > 0), down->setEnabled(k + 1 < (int)commands.size());
        connect(up, &QToolButton::clicked, this, [=] {
            auto cs = commands;
            std::swap(cs[k], cs[k - 1]);
            edit(cs, "Move a command up");
        });
        connect(down, &QToolButton::clicked, this, [=] {
            auto cs = commands;
            std::swap(cs[k], cs[k + 1]);
            edit(cs, "Move a command down");
        });
        connect(del, &QToolButton::clicked, this, [=] {
            auto cs = commands;
            cs.erase(cs.begin() + k);
            edit(cs, "Remove a command");
        });
        buttons->addWidget(up);
        buttons->addWidget(down);
        buttons->addStretch();
        buttons->addWidget(del);
        bf->addRow(buttons);
        f->addRow(box);
    }
    auto *add = new QPushButton("Add a command");
    add->setToolTip("A wait of 30 frames at the end; change it to what the event does next");
    connect(add, &QPushButton::clicked, this, [=] {
        auto cs = commands;
        cs.push_back({0x300, {30}});
        edit(cs, "Add a command");
    });
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
    } else if (t.type == 8) {
        /* an event of the field's */
        auto *ev = new QComboBox;
        for (const Event &e : field_.stage.events) ev->addItem(QString("Event %1").arg(e.id), e.id);
        int at = ev->findData((int16_t)t.arg);
        if (at < 0) ev->addItem(QString("Event %1 (the field has none)").arg((int16_t)t.arg), (int16_t)t.arg), at = ev->count() - 1;
        ev->setCurrentIndex(at);
        connect(ev, &QComboBox::currentIndexChanged, this, [=](int) {
            int v = ev->currentData().toInt();
            edit([v](Slot &s) { s.arg = v & 0xFFFF; }, "Change a slot's event");
        });
        f->addRow("Starts", ev);
        for (int k = 0; k < (int)field_.stage.events.size(); k++)
            if (field_.stage.events[k].id == (int16_t)t.arg) f->addRow("", richLabel(QString("<a href='event:%1'>Open it</a>").arg(k)));
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
    f->addRow(richLabel(c.empty() ? "Always works" : "Works if all of these hold (two at most):"));
    f->addRow(codesEditor(c, false, 2, [=](const Codes &p) {
        edit([p](Slot &s) {
            for (int k = 0; k < 2; k++) s.cond[k][0] = k < (int)p.size() ? p[k].first : 0xFFFF, s.cond[k][1] = k < (int)p.size() ? p[k].second : 0;
        }, "Change when a slot works");
    }));
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
