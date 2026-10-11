/* The conditions and actions as the user sees them: the story's points by
   who moves the story on and where, the flags by name and by what sets and
   checks them, the rest by the game's names; and the editor of a list of
   them, a row each, picked from lists */
#include "mainwindow.h"
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFrame>
#include <QDialog>
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFile>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <set>

/* mods/flags.json: the user's names for flags, {"1A1F": "Met Kail", ...} */
void MainWindow::loadFlagNames() {
    flagNames_.clear();
    QFile f(QString::fromStdString(files_.dir + "/mods/flags.json"));
    Json j;
    if (!f.open(QIODevice::ReadOnly) || !Json::parse(f.readAll().toStdString(), j)) return;
    for (auto &[key, v] : j.fields) {
        bool ok;
        int code = QString::fromStdString(key).toInt(&ok, 16);
        if (ok && is_flag(code) && !v.str.empty()) flagNames_[code] = QString::fromStdString(v.str);
    }
}

bool MainWindow::saveFlagNames() {
    std::string out = "{";
    for (auto &[code, name] : flagNames_) {
        std::string n;
        for (char c : name.toStdString()) n += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
        out += (out.size() > 1 ? ",\n" : "\n") + QString("\"%1\":").arg(code, 4, 16, QChar('0')).toUpper().toStdString() + "\"" + n + "\"";
    }
    out += "\n}\n";
    QDir().mkpath(QString::fromStdString(files_.dir + "/mods"));
    QFile f(QString::fromStdString(files_.dir + "/mods/flags.json"));
    if (!f.open(QIODevice::WriteOnly) || f.write(out.data(), (qint64)out.size()) != (qint64)out.size()) {
        QMessageBox::critical(this, "Flags", "Can't write mods/flags.json");
        return false;
    }
    invalidateCodes();
    return true;
}

void MainWindow::invalidateCodes() {
    wordsBuilt_ = false;
    flagUses_.clear();
    flagUsesBuilt_ = false;
}

/* "0x05 (after Seiryu Leader, East Sector)"; full: what it gives too */
QString MainWindow::storyName(int point, bool full) const {
    QString hex = QString("0x%1").arg(point, 2, 16, QChar('0')).toUpper().replace("0X", "0x");
    if (point == 0x2D) return hex + " (the extra chapter)";
    const StoryStep *s = story_step(point);
    if (!s) return hex;
    QString where;
    for (const Stage &st : stages_)
        if (st.name == s->stage) where = full && !st.modes.empty() ? modeName(st.modes[0].mode) : stageTitle(st);
    QString what = *s->who ? QString("after %1, %2").arg(s->who, where) : QString("after an event in %1").arg(where);
    if (point == 0x2B) what += "; each battle plays a movie first";
    if (full && *s->gift) what += QString(" (gives %1)").arg(s->gift);
    if (full) what += QString(" — event %1").arg(s->event);
    return hex + " (" + what + ")";
}

/* every flag something uses: the fields' tables as saved (the open one as
   edited aside) and the game's code */
const std::map<int, std::vector<MainWindow::FlagUse>> &MainWindow::flagUses() const {
    if (flagUsesBuilt_) return flagUses_;
    flagUsesBuilt_ = true;
    for (int i = 0; i < (int)stages_.size(); i++) {
        if (stages_[i].name == field_.stage.name) continue;
        Stage s = savedStage(i);
        for (auto &[code, use] : tableUses(s)) flagUses_[code].push_back({i, use});
    }
    for (int code : game_flags())
        for (const GameFlagUse &u : game_flag_uses(code)) {
            int stage = -1;
            for (int i = 0; i < (int)stages_.size(); i++)
                if (stages_[i].name == u.stage) stage = i;
            QString where = stage >= 0 ? stageTitle(stages_[stage]) : "every field";
            QString what = u.set ? (u.event >= 0 ? QString("event %1's end %2 it").arg(u.event).arg(u.value ? "sets" : "clears")
                                                 : QString("the game's code %1 it").arg(u.value ? "sets" : "clears"))
                                 : QString("the game's code checks it");
            flagUses_[code].push_back({stage, {what + " (" + where + ")", (bool)u.set}});
        }
    return flagUses_;
}

/* the flags a stage's tables use, each with what uses it */
std::vector<std::pair<int, MainWindow::FlagUseText>> MainWindow::tableUses(const Stage &s) const {
    std::vector<std::pair<int, FlagUseText>> out;
    QString where = stageTitle(s);
    auto add = [&](const Codes &codes, const QString &who, bool actions) {
        for (auto &[code, value] : codes)
            if (is_flag(code))
                out.push_back({code, {QString("%1 %2 (%3)").arg(who, actions ? (value ? "sets it" : "clears it") : (value ? "if it is set" : "if it isn't set"), where), actions}});
    };
    for (int k = 0; k < (int)s.actors.size(); k++) {
        const Actor &a = s.actors[k];
        QString name = characterLabel(a.id);
        add(a.conditions, name + " appears", false);
        for (int t = 0; t < (int)a.talks.size(); t++) {
            add(a.talks[t].conditions, QString("%1's line %2 is said").arg(name).arg(t + 1), false);
            add(a.talks[t].actions, QString("%1's line %2").arg(name).arg(t + 1), true);
        }
    }
    for (int k = 0; k < (int)s.slots.size(); k++) {
        Codes c;
        for (auto &p : s.slots[k].cond)
            if (p[0] != 0xFFFF) c.push_back({p[0], p[1]});
        add(c, QString("slot %1 works").arg(k), false);
    }
    return out;
}

std::vector<MainWindow::FlagUse> MainWindow::usesOf(int code) const {
    std::vector<FlagUse> out;
    int open = -1;
    for (int i = 0; i < (int)stages_.size(); i++)
        if (stages_[i].name == field_.stage.name) open = i;
    for (auto &[c, use] : tableUses(field_.stage))
        if (c == code) out.push_back({open, use});
    auto it = flagUses().find(code);
    if (it != flagUses().end()) out.insert(out.end(), it->second.begin(), it->second.end());
    return out;
}

/* "1A:1F, Met Kail"; one without a name: what sets it, or checks it */
QString MainWindow::flagLabel(int code) const {
    QString ref = QString::fromStdString(flag_ref(code));
    auto n = flagNames_.find(code);
    if (n != flagNames_.end()) return ref + ", " + n->second;
    int mode = visited_mode(code);
    if (mode >= 0) return ref + ", visited " + modeName(mode);
    if (code == 0x10) return ref + ", won the last card battle";
    if (code == 0x11) return ref + ", back from a card battle";
    if (code == 0x12) return ref + ", the player talks to what it faces";
    auto uses = usesOf(code);
    for (auto &u : uses)
        if (u.use.set) return ref + ", " + u.use.text;
    if (!uses.empty()) return ref + ", " + uses[0].use.text;
    return ref + ", unused";
}

const CodeWords &MainWindow::words() const {
    if (wordsBuilt_) return words_;
    wordsBuilt_ = true;
    words_ = CodeWords();
    words_.items = text_.items, words_.cards = text_.cards;
    for (int i = 0; i < 8; i++) words_.partners.push_back(i + 1 < (int)text_.digimon.size() ? text_.digimon[i + 1] : "");
    for (int p = 0; p < STORY_POINTS; p++) words_.story.push_back(storyName(p, false).toStdString());
    for (auto it = modeStage_.begin(); it != modeStage_.end(); ++it) words_.fields[it.key()] = modeName(it.key()).toStdString();
    for (auto &[code, name] : flagNames_) words_.flags[code] = name.toStdString();
    return words_;
}

/* A flag picked from the ones something uses, a new one, or typed: its code, or -1 */
int MainWindow::pickFlag(int current) {
    QDialog d(this);
    d.setWindowTitle(current == -2 ? "Flags" : "Pick a flag");
    d.resize(820, 560);
    auto *v = new QVBoxLayout(&d);
    v->addWidget(new QLabel("The game remembers what happened with flags. Each one is named by its group and number (1A:1F); give the "
                            "ones you use a name of your own (double-click it).\nNew flag: one nothing uses yet, for your own story."));
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search a flag, a name, a character or a field");
    search->setClearButtonEnabled(true);
    v->addWidget(search);
    auto *list = new QTreeWidget;
    list->setColumnCount(3);
    list->setHeaderLabels({"Flag", "Name", "Used by"});
    list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    v->addWidget(list, 1);
    std::set<int> codes;
    for (auto &[code, uses] : flagUses()) codes.insert(code);
    for (auto &[code, use] : tableUses(field_.stage)) codes.insert(code);
    for (auto &[code, name] : flagNames_) codes.insert(code);
    if (current >= 0) codes.insert(current);
    auto fill = [&, this](int select) {
        list->clear();
        for (int code : codes) {
            auto uses = usesOf(code);
            QStringList u;
            for (auto &x : uses) u << x.use.text;
            auto n = flagNames_.find(code);
            QString name = n != flagNames_.end() ? n->second : "";
            int mode = visited_mode(code);
            if (name.isEmpty() && mode >= 0) name = "(visited " + modeName(mode) + ")";
            auto *it = new QTreeWidgetItem(list, {QString::fromStdString(flag_ref(code)), name, u.join("; ")});
            it->setToolTip(2, u.join("\n"));
            it->setData(0, Qt::UserRole, code);
            it->setFlags(it->flags() | Qt::ItemIsEditable);
            if (code == select) list->setCurrentItem(it);
        }
        list->resizeColumnToContents(0);
        list->setColumnWidth(1, 200);
        if (list->currentItem()) list->scrollToItem(list->currentItem());
    };
    fill(current);
    connect(search, &QLineEdit::textChanged, &d, [list](const QString &t) {
        for (int i = 0; i < list->topLevelItemCount(); i++) {
            QTreeWidgetItem *it = list->topLevelItem(i);
            it->setHidden(!t.isEmpty() && !(it->text(0) + " " + it->text(1) + " " + it->text(2)).contains(t, Qt::CaseInsensitive));
        }
    });
    /* only the name column is edited: the others go back */
    connect(list, &QTreeWidget::itemDoubleClicked, &d, [list](QTreeWidgetItem *it, int col) {
        if (col == 1) list->editItem(it, 1);
    });
    list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(list, &QTreeWidget::itemChanged, &d, [this](QTreeWidgetItem *it, int col) {
        if (col != 1) return;
        int code = it->data(0, Qt::UserRole).toInt();
        QString name = it->text(1).trimmed();
        if (name.startsWith("(visited ")) return;
        if (name.isEmpty()) flagNames_.erase(code);
        else flagNames_[code] = name;
        saveFlagNames();
    });
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton("New flag...");
    add->setToolTip("A flag nothing in the game uses, named as you say");
    row->addWidget(add);
    row->addStretch();
    auto *buttons = new QDialogButtonBox(current == -2 ? QDialogButtonBox::Close : QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    row->addWidget(buttons);
    v->addLayout(row);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (current != -2) connect(list, &QTreeWidget::itemActivated, &d, [&d](QTreeWidgetItem *, int col) { if (col != 1) d.accept(); });
    connect(add, &QPushButton::clicked, &d, [&, this] {
        int code = newFlag(codes);
        if (code < 0) return (void)QMessageBox::information(&d, "New flag", "No flag is left unused.");
        bool ok;
        QString name = QInputDialog::getText(&d, "New flag", QString("Flag %1 is unused. Its name (what it remembers):")
                                                                 .arg(QString::fromStdString(flag_ref(code))),
                                             QLineEdit::Normal, "", &ok).trimmed();
        if (!ok) return;
        if (!name.isEmpty()) flagNames_[code] = name, saveFlagNames();
        codes.insert(code);
        search->clear();
        fill(code);
    });
    if (d.exec() != QDialog::Accepted || !list->currentItem()) return -1;
    return list->currentItem()->data(0, Qt::UserRole).toInt();
}

/* A flag nothing uses: the fields' tables, the game's code, the names; group 0x40 from its last bit down (the
   game's own start low), then 0x1C's and 0x1A's */
int MainWindow::newFlag(const std::set<int> &used) const {
    for (int group : {0x40, 0x1C, 0x1A})
        for (int id = flag_group_bits(group) - 1; id >= 0; id--) {
            int code = group << 8 | id;
            if (!used.count(code) && usesOf(code).empty() && !flagNames_.count(code)) return code;
        }
    return -1;
}

/* The kinds of row, as the user picks them */
enum RowKind { K_STORY, K_RANGE, K_FLAG, K_VISITED, K_ITEM, K_CARD, K_SPECIAL, K_MONEY, K_CHARISMA, K_ARRIVAL, K_EVENT, K_BATTLE, K_CODE, K_COUNT };
struct RowKindInfo {
    const char *name;
    bool cond, action;
    const char *yes, *no, *doYes, *doNo; /* the value's words as a condition, as an action (none: always 1) */
    const char *tip;
};
static const RowKindInfo ROW_KINDS[K_COUNT] = {
    {"Story point", true, false, "is now", "isn't now", nullptr, nullptr,
     "The story's point (GAME.progress): the game moves it on at the end of its main events"},
    {"Story span", true, false, "holds", "doesn't hold", nullptr, nullptr, "The story is within a span of points (the game's special conditions)"},
    {"Flag", true, true, "is set", "isn't set", "set", "clear", "Something the game remembers: one of its flags"},
    {"Visited a field", true, true, "visited", "not visited", "mark visited", "mark not visited",
     "Each field's flag, set as the player first enters it"},
    {"Item", true, true, "has it", "hasn't it", "give", "take", "An item in the bag or worn"},
    {"Card", true, true, "has it", "hasn't it", "give", "take", "A card of the card battles"},
    {"Partners and more", true, true, "holds", "doesn't hold", nullptr, nullptr, "The game's special conditions: partners, item sets, flag counts"},
    {"Money", true, true, "holds", "doesn't hold", nullptr, nullptr, "The player's bits: has some, gets or pays some"},
    {"Party charisma", true, false, "reached", "not reached", nullptr, nullptr, "The party's charisma added up"},
    {"Came in through", true, false, "yes", "no", nullptr, nullptr, "The place a warp or an exit left as the player arrived (a slot's Place)"},
    {"Start an event", false, true, nullptr, nullptr, nullptr, nullptr, "One of this field's events of your own, once the text ends"},
    {"Event battle", false, true, nullptr, nullptr, nullptr, nullptr, "One of the stage's event battles"},
    {"Code (advanced)", true, true, nullptr, nullptr, nullptr, nullptr, "The code and value as the game's tables have them, in hex"},
};

static int rowKind(int code) {
    if (is_flag(code)) return visited_mode(code) >= 0 ? K_VISITED : K_FLAG;
    int kind = code_kind(code), id = code_id(code);
    if (kind == CODE_STORY) return id < STORY_POINTS ? K_STORY : K_CODE;
    if (kind == CODE_SPECIAL) {
        int t = special_test(id);
        if (t < 0) return K_CODE;
        if ((t & 0xF0) == 0x30) return K_RANGE;
        if ((t & 0xF0) == 0x20) return K_MONEY;
        return K_SPECIAL;
    }
    if (kind == CODE_PARTY_STAT) return id < PARTY_STAT_COUNT ? K_CHARISMA : K_CODE;
    if (kind == CODE_WARP_ARG) return K_ARRIVAL;
    if (kind >= CODE_ITEM && kind < 0x8F) return K_ITEM;
    if (kind == CODE_CARD) return K_CARD;
    if (kind == CODE_START_EVENT && id >= 0x100) return K_EVENT;
    if (kind == CODE_EVENT_BATTLE) return K_BATTLE;
    return K_CODE;
}

/* the special conditions a row of the kind offers */
static std::vector<int> specialsOf(int kind, bool actions) {
    std::vector<int> out;
    for (int id : special_ids()) {
        int t = special_test(id) & 0xF0;
        bool fits = kind == K_RANGE ? t == 0x30 : kind == K_MONEY ? t == 0x20 : t != 0x30 && t != 0x20;
        if (fits && special_is_action(id) == actions) out.push_back(id);
    }
    return out;
}

QWidget *MainWindow::codesEditor(const Codes &codes, bool actions, int max, const std::function<void(const Codes &)> &commit) {
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    const CodeWords &cw = words();
    auto set = [codes, commit](int k, int code, int value) {
        Codes c = codes;
        c[k] = {code, value};
        if (c != codes) commit(c);
    };
    for (int k = 0; k < (int)codes.size(); k++) {
        auto [code, value] = codes[k];
        int kind = rowKind(code), id = code_id(code);
        const RowKindInfo &info = ROW_KINDS[kind];
        auto *row = new QFrame;
        row->setFrameShape(QFrame::StyledPanel);
        auto *g = new QGridLayout(row);
        g->setContentsMargins(4, 4, 4, 4);
        row->setToolTip(QString("%1\n\nIn the game's tables: %2=%3")
                            .arg(QString::fromStdString(describe_code(code, value, cw, actions)))
                            .arg(code, 4, 16, QChar('0')).arg(value));

        auto *kindBox = new QComboBox;
        for (int i = 0; i < K_COUNT; i++)
            if (actions ? ROW_KINDS[i].action : ROW_KINDS[i].cond) kindBox->addItem(ROW_KINDS[i].name, i);
        kindBox->setCurrentIndex(std::max(0, kindBox->findData(kind)));
        kindBox->setToolTip(info.tip);
        g->addWidget(kindBox, 0, 0);
        connect(kindBox, &QComboBox::currentIndexChanged, this, [=, this](int) {
            int to = kindBox->currentData().toInt(), c = -1;
            int point = storyBox_->currentData().toInt();
            switch (to) {
            case K_STORY: c = CODE_STORY << 8 | (point >= 0 ? point : 4); break;
            case K_FLAG: c = pickFlag(-1); break;
            case K_VISITED: c = visited_flag(field_.stage.modes.empty() ? 0x200 : field_.stage.modes[0].mode); break;
            case K_ITEM: c = CODE_ITEM << 8 | 1; break;
            case K_CARD: c = CODE_CARD << 8 | 1; break;
            case K_CHARISMA: c = CODE_PARTY_STAT << 8; break;
            case K_ARRIVAL: c = CODE_WARP_ARG << 8; break;
            case K_BATTLE: c = CODE_EVENT_BATTLE << 8; break;
            case K_CODE: c = code; break;
            case K_EVENT:
                c = CODE_START_EVENT << 8 | 0x100;
                for (const Event &e : field_.stage.events)
                    if (e.id >= EDITOR_EVENTS) c = CODE_START_EVENT << 8 | (0x100 + e.id - EDITOR_EVENTS);
                break;
            default: {
                auto ids = specialsOf(to, actions);
                if (!ids.empty()) c = CODE_SPECIAL << 8 | ids[0];
            }
            }
            if (c < 0) return showProperties(); /* cancelled: as it was */
            set(k, c, 1);
        });

        /* its value: is or isn't, give or take */
        const char *yes = actions ? info.doYes : info.yes, *no = actions ? info.doNo : info.no;
        if (kind == K_CODE) {
            auto *val = new QSpinBox;
            val->setRange(0, 0xFFFF);
            val->setValue(value);
            val->setKeyboardTracking(false);
            val->setPrefix("value ");
            connect(val, &QSpinBox::valueChanged, this, [=](int x) { set(k, code, x); });
            g->addWidget(val, 0, 1);
        } else if (yes) {
            auto *val = new QComboBox;
            val->addItem(yes, 1);
            val->addItem(no, 0);
            val->setCurrentIndex(value ? 0 : 1);
            connect(val, &QComboBox::currentIndexChanged, this, [=](int i) { set(k, code, i == 0 ? (value ? value : 1) : 0); });
            g->addWidget(val, 0, 1);
        }
        auto *del = new QToolButton;
        del->setText("✕");
        del->setToolTip(actions ? "Remove this action" : "Remove this condition");
        connect(del, &QToolButton::clicked, this, [=] {
            Codes c = codes;
            c.erase(c.begin() + k);
            commit(c);
        });
        g->addWidget(del, 0, 2);
        g->setColumnStretch(0, 1);
        g->setColumnStretch(1, 1);

        /* what: a point, a flag, an item... */
        QWidget *what = nullptr;
        auto combo = [&](const std::vector<std::pair<int, QString>> &items, int current, const std::function<int(int)> &codeOf) {
            auto *c = new QComboBox;
            c->setMinimumContentsLength(16);
            c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            for (auto &[d, text] : items) c->addItem(text, d);
            if (c->findData(current) < 0) c->addItem(QString("%1 (not one of these)").arg(current), current);
            c->setCurrentIndex(c->findData(current));
            if (items.size() > 30) {
                c->setEditable(true);
                c->setInsertPolicy(QComboBox::NoInsert);
                c->completer()->setFilterMode(Qt::MatchContains);
                c->completer()->setCompletionMode(QCompleter::PopupCompletion);
                c->lineEdit()->setCursorPosition(0); /* its start, not its end */
            }
            c->setToolTip(c->currentText());
            c->view()->setMinimumWidth(420);
            connect(c, &QComboBox::activated, this, [=](int) { set(k, codeOf(c->currentData().toInt()), value); });
            return c;
        };
        std::vector<std::pair<int, QString>> items;
        switch (kind) {
        case K_STORY:
            for (int p = 0; p < STORY_POINTS; p++) items.push_back({p, storyName(p, false)});
            what = combo(items, id, [](int p) { return CODE_STORY << 8 | p; });
            break;
        case K_RANGE:
        case K_SPECIAL:
        case K_MONEY:
            for (int s : specialsOf(kind, actions)) items.push_back({s, QString::fromStdString(special_words(s, cw))});
            what = combo(items, id, [](int s) { return CODE_SPECIAL << 8 | s; });
            break;
        case K_FLAG: {
            auto *b = new QToolButton;
            b->setText(flagLabel(code));
            b->setToolButtonStyle(Qt::ToolButtonTextOnly);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            QStringList uses;
            for (auto &u : usesOf(code)) uses << u.use.text;
            b->setToolTip("Pick another flag, or name this one\n\nUsed by:\n" + (uses.isEmpty() ? QString("nothing else") : uses.join("\n")));
            connect(b, &QToolButton::clicked, this, [=, this] {
                int c = pickFlag(code);
                if (c >= 0) set(k, c, value);
                else showProperties(); /* a name may have changed */
            });
            what = b;
            break;
        }
        case K_VISITED: {
            for (auto it = modeStage_.begin(); it != modeStage_.end(); ++it)
                if (visited_flag(it.key()) >= 0) items.push_back({it.key(), modeName(it.key())});
            std::sort(items.begin(), items.end(), [](auto &a, auto &b) { return a.second < b.second; });
            what = combo(items, visited_mode(code), [](int m) { return visited_flag(m); });
            break;
        }
        case K_ITEM:
            for (int i = 0; i < (int)text_.items.size() && i < 0x200; i++)
                if (!text_.items[i].empty()) items.push_back({i, QString::fromStdString(text_.items[i])});
            what = combo(items, id, [kind = code_kind(code)](int i) { return kind << 8 | i; });
            break;
        case K_CARD:
            for (int i = 0; i < (int)text_.cards.size() && i < 0x200; i++)
                if (!text_.cards[i].empty()) items.push_back({i, QString::fromStdString(text_.cards[i])});
            what = combo(items, id, [](int i) { return CODE_CARD << 8 | i; });
            break;
        case K_CHARISMA:
            for (int i = 0; i < PARTY_STAT_COUNT; i++) items.push_back({i, QString("%1 charisma").arg(party_stat_threshold(i))});
            what = combo(items, id, [](int i) { return CODE_PARTY_STAT << 8 | i; });
            break;
        case K_EVENT:
            for (const Event &e : field_.stage.events)
                if (e.id >= EDITOR_EVENTS) items.push_back({0x100 + e.id - EDITOR_EVENTS, QString("Event %1").arg(e.id)});
            what = combo(items, id, [](int i) { return CODE_START_EVENT << 8 | i; });
            break;
        case K_ARRIVAL:
        case K_BATTLE: {
            auto *s = new QSpinBox;
            s->setRange(0, 0x1FF);
            s->setValue(id);
            s->setKeyboardTracking(false);
            if (kind == K_BATTLE) s->setPrefix("event battle ");
            else s->setToolTip("Under 30: the player came through place N + 1 (a slot's Place); 30 on: the place's argument is N - 29");
            connect(s, &QSpinBox::valueChanged, this, [=](int x) { set(k, code_kind(code) << 8 | x, value); });
            what = s;
            break;
        }
        default: {
            auto *s = new QSpinBox;
            s->setRange(0, 0xFFFE);
            s->setDisplayIntegerBase(16);
            s->setPrefix("code 0x");
            s->setValue(code);
            s->setKeyboardTracking(false);
            connect(s, &QSpinBox::valueChanged, this, [=](int x) { set(k, x, value); });
            what = s;
        }
        }
        /* as wide as the panel, however long its words */
        what->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        what->setMinimumWidth(120);
        g->addWidget(what, 1, 0, 1, 3);
        /* what it reads as, where the row's own words don't say it all */
        QString words = QString::fromStdString(describe_code(code, value, cw, actions));
        if (kind == K_CODE || kind == K_ARRIVAL || ((kind == K_RANGE || kind == K_SPECIAL || kind == K_MONEY) && words.size() > 45)) {
            auto *l = new QLabel(words);
            l->setWordWrap(true);
            l->setStyleSheet("color:#999");
            g->addWidget(l, 2, 0, 1, 3);
        }
        v->addWidget(row);
    }
    if (max <= 0 || (int)codes.size() < max) {
        auto *add = new QToolButton;
        add->setText(actions ? "Add an action" : "Add a condition");
        add->setToolTip(actions ? "Something the game does when the text ends: a flag, an item, an event..." : "Something that has to hold: the story's point, a flag, an item...");
        connect(add, &QToolButton::clicked, this, [=, this] {
            Codes c = codes;
            int point = storyBox_->currentData().toInt();
            /* a condition starts as the story's point shown; an action as a new flag picked */
            if (actions) {
                int f = pickFlag(-1);
                if (f < 0) return;
                c.push_back({f, 1});
            } else c.push_back({CODE_STORY << 8 | (point >= 0 ? point : 4), 1});
            commit(c);
        });
        auto *h = new QHBoxLayout;
        h->addWidget(add);
        h->addStretch();
        v->addLayout(h);
    }
    return w;
}

/* Story > Flags: every flag something uses, to name them */
void MainWindow::manageFlags() {
    pickFlag(-2);
    showProperties();
    fillEntities();
}
