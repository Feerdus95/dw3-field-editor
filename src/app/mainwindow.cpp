#include "mainwindow.h"
#include "mapview.h"
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

/* --- what the editor shows and edits, shared with properties.cpp --- */

const char *SLOT_TYPES[15] = {"none",  "Exit", "Climb up", "Climb down", "Drop",       "Depth",  "Map change", "Gauge",
                              "Event", "Warp", "Warp",     "Slide",      "Stop slide", "Launch", "Launch out"};
QString slotType(int t) { return t >= 0 && t < 15 ? SLOT_TYPES[t] : "?"; }
/* the slot types whose arg is a mode to go to */
bool leaves(int t) { return t == 1 || t == 9 || t == 10 || t == 13 || t == 14; }
const char *DIRS[8] = {"S", "SW", "W", "NW", "N", "NE", "E", "SE"};

QImage toQImage(const Image &img) {
    if (!img.w) return QImage();
    return QImage((const uchar *)img.px.data(), img.w, img.h, img.w * 4, QImage::Format_RGBA8888).copy();
}

static QColor slotColor(int i) {
    static const QRgb P[12] = {0xE6194B, 0x3CB44B, 0xFFE119, 0x4363D8, 0xF58231, 0x911EB4,
                               0x46F0F0, 0xF032E6, 0xBCF60C, 0xFABEBE, 0x008080, 0xE6BEFF};
    return QColor(P[i % 12]);
}

/* A logic map's value as an overlay color: the floor's walkable cells green
   (slopes lighter), the triggers by slot, the battle areas by area; 0 clear */
static QRgb overlayColor(int layer, int v, int highlight) {
    if (!v) return 0;
    if (layer == L_FLOOR) return ((v & 0xF) == 1 && !(v & 0x10) ? QColor(40, 200, 60) : QColor(150, 240, 170)).rgb();
    if (layer == L_TRIGGERS) {
        int slot = v & 0x1F;
        return (highlight < 0 ? slotColor(slot) : slot == highlight ? QColor(Qt::white) : QColor(60, 60, 60)).rgb();
    }
    return slotColor(v + 5).rgb();
}

static const int OVERLAY_LAYERS[3] = {L_FLOOR, L_TRIGGERS, L_AREAS};

/* --- undo --- */

/* A change to the stage's tables: the whole tables before and after */
class TablesCommand : public QUndoCommand {
  public:
    TablesCommand(MainWindow *w, const Stage &before, const Stage &after, const QString &what)
        : QUndoCommand(what), w_(w), before_(before), after_(after) {}
    void redo() override { w_->setTables(after_); }
    void undo() override { w_->setTables(before_); }

  private:
    MainWindow *w_;
    Stage before_, after_;
};

/* A stroke on a logic map: the pixels of the rectangle it touched, before
   and after; the stroke has painted them already when it is pushed */
class PaintCommand : public QUndoCommand {
  public:
    PaintCommand(MainWindow *w, int map, const QRect &r, std::vector<uint8_t> before, std::vector<uint8_t> after, const QString &what)
        : QUndoCommand(what), w_(w), map_(map), r_(r), before_(std::move(before)), after_(std::move(after)) {}
    void redo() override {
        if (first_) first_ = false;
        else w_->setMapRegion(map_, r_, after_);
    }
    void undo() override { w_->setMapRegion(map_, r_, before_); }

  private:
    MainWindow *w_;
    int map_;
    QRect r_;
    std::vector<uint8_t> before_, after_;
    bool first_ = true;
};

/* --- the window --- */

MainWindow::MainWindow(const Files &files, std::vector<Stage> stages, const GameText &text, std::vector<int> animFiles)
    : files_(files), stages_(std::move(stages)), text_(text), animFiles_(std::move(animFiles)) {
    setWindowTitle("DW3 Field Editor");
    for (int i = 0; i < (int)stages_.size(); i++)
        for (auto &m : stages_[i].modes) modeStage_.insert(m.mode, i);
    undo_ = new QUndoStack(this);
    connect(undo_, &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);

    view_ = new MapView(this);
    scene_ = new QGraphicsScene(this);
    view_->setScene(scene_);
    setCentralWidget(view_);

    /* the fields, by area */
    auto *left = new QDockWidget("Fields", this);
    left->setObjectName("fields");
    auto *lw = new QWidget(left);
    auto *ll = new QVBoxLayout(lw);
    ll->setContentsMargins(4, 4, 4, 4);
    filter_ = new QLineEdit(lw);
    filter_->setPlaceholderText("Search a field, an area or a number");
    filter_->setClearButtonEnabled(true);
    stageTree_ = new QTreeWidget(lw);
    stageTree_->setHeaderHidden(true);
    stageTree_->setColumnCount(1);
    ll->addWidget(filter_);
    ll->addWidget(stageTree_);
    left->setWidget(lw);
    addDockWidget(Qt::LeftDockWidgetArea, left);

    /* what the field holds, and what is selected */
    auto *ents = new QDockWidget("Contents", this);
    ents->setObjectName("contents");
    entities_ = new QTreeWidget(ents);
    entities_->setColumnCount(2);
    entities_->setHeaderLabels({"Item", "Where"});
    entities_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    entities_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    entities_->header()->setStretchLastSection(false);
    ents->setWidget(entities_);
    addDockWidget(Qt::RightDockWidgetArea, ents);
    auto *insp = new QDockWidget("Properties", this);
    insp->setObjectName("properties");
    props_ = new QScrollArea(insp);
    props_->setWidgetResizable(true);
    insp->setWidget(props_);
    addDockWidget(Qt::RightDockWidgetArea, insp);
    resizeDocks({left, ents}, {260, 380}, Qt::Horizontal);

    /* File and Edit */
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Save", QKeySequence::Save, this, &MainWindow::save);
    QAction *test = fileMenu->addAction("&Test in game", QKeySequence("F5"), this, &MainWindow::testInGame);
    test->setToolTip("Saves the field and opens the port on it (F5)");
    fileMenu->addSeparator();
    fileMenu->addAction("&Revert to the game's...", this, &MainWindow::revertStage);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);
    QMenu *editMenu = menuBar()->addMenu("&Edit");
    QAction *undoAct = undo_->createUndoAction(this, "&Undo");
    undoAct->setShortcut(QKeySequence::Undo);
    QAction *redoAct = undo_->createRedoAction(this, "&Redo");
    redoAct->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);
    editMenu->addSeparator();
    editMenu->addAction("&Delete", QKeySequence::Delete, this, &MainWindow::deleteSelection);

    /* the tools */
    QToolBar *tools = addToolBar("Tools");
    tools->setObjectName("tools");
    tools->setMovable(false);
    auto *toolGroup = new QActionGroup(this);
    static const char *TOOL_NAMES[TOOL_COUNT] = {"Select", "Paint floor", "Paint triggers", "Paint areas"};
    static const char *TOOL_KEYS[TOOL_COUNT] = {"V", "B", "T", "A"};
    static const char *TOOL_TIPS[TOOL_COUNT] = {
        "Select, drag to move characters, objects and the start; right click for more (V)",
        "Paint where the player can walk (B). Shift: a rectangle. Alt: pick the value under the cursor",
        "Paint the cells that set off a slot: an exit, a climb, an event... (T). Shift: a rectangle. Alt: pick",
        "Paint where random battles start, and which area's (A). Shift: a rectangle. Alt: pick"};
    for (int t = 0; t < TOOL_COUNT; t++) {
        QAction *a = tools->addAction(TOOL_NAMES[t]);
        a->setCheckable(true);
        a->setShortcut(QKeySequence(TOOL_KEYS[t]));
        a->setToolTip(TOOL_TIPS[t]);
        toolGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] { setTool(t); });
        toolActions_[t] = a;
    }
    toolActions_[TOOL_SELECT]->setChecked(true);
    tools->addSeparator();
    tools->addAction(undoAct);
    tools->addAction(redoAct);
    tools->addSeparator();
    tools->addAction(test);

    /* the paint options: the value, a trigger's facing, the brush */
    paintBar_ = addToolBar("Paint");
    paintBar_->setObjectName("paint");
    paintBar_->setMovable(false);
    paintBar_->addWidget(new QLabel(" Paint ", paintBar_));
    valueBox_ = new QComboBox(paintBar_);
    valueBox_->setMinimumContentsLength(28);
    paintBar_->addWidget(valueBox_);
    facingBox_ = new QComboBox(paintBar_);
    for (int d = 0; d < 8; d++) facingBox_->addItem(QString("facing %1").arg(DIRS[d]), d);
    facingBox_->setToolTip("The way the player faces when an exit or a climb works (the trigger's 3 high bits)");
    facingAction_ = paintBar_->addWidget(facingBox_);
    paintBar_->addWidget(new QLabel("  Brush ", paintBar_));
    sizeBox_ = new QComboBox(paintBar_);
    for (int s : {1, 4, 8, 16, 32, 64}) sizeBox_->addItem(QString("%1 px").arg(s), s);
    sizeBox_->setCurrentIndex(3);
    paintBar_->addWidget(sizeBox_);
    paintBar_->setVisible(false);
    insertToolBarBreak(paintBar_);

    /* View: the layers, in the menu and their toolbar */
    QMenu *viewMenu = menuBar()->addMenu("&View");
    QToolBar *tb = addToolBar("View");
    tb->setObjectName("viewbar");
    tb->setMovable(false);
    QAction *fit = viewMenu->addAction("&Fit to window", QKeySequence("F"), view_, &MapView::fit);
    viewMenu->addAction("Zoom &in", QKeySequence::ZoomIn, [this] { view_->zoomBy(1.25); });
    viewMenu->addAction("Zoom &out", QKeySequence::ZoomOut, [this] { view_->zoomBy(0.8); });
    viewMenu->addAction("&Actual size", QKeySequence("1"), [this] { view_->zoomBy(1 / view_->zoom()); });
    viewMenu->addSeparator();
    tb->addAction(fit);
    tb->addSeparator();
    static const char *NAMES[LAYER_COUNT] = {"Background", "Objects", "Floor", "Triggers", "Battle areas", "Characters", "Exits", "Start"};
    static const char *TIPS[LAYER_COUNT] = {
        "The map's picture", "The animated objects drawn over it",
        "Where the player can walk: green, the slopes lighter",
        "The cells that set off an exit, a climb, an event...: a color per slot",
        "The cells where random battles start: a color per area", "The field's characters",
        "Where each exit, climb or warp is, and where it leads", "Where the player starts"};
    static const bool ON[LAYER_COUNT] = {true, true, false, false, false, true, true, true};
    for (int l = 0; l < LAYER_COUNT; l++) {
        QAction *a = viewMenu->addAction(NAMES[l]);
        a->setCheckable(true);
        a->setChecked(ON[l]);
        a->setToolTip(TIPS[l]);
        a->setShortcut(QKeySequence(QString::number(l + 2 > 9 ? 0 : l + 2)));
        connect(a, &QAction::toggled, this, [this, l](bool) {
            if (l >= L_FLOOR && l <= L_AREAS) rebuildOverlays();
            applyLayers();
        });
        tb->addAction(a);
        layerActions_[l] = a;
    }
    tb->addSeparator();
    tb->addWidget(new QLabel(" Overlay opacity ", tb));
    opacity_ = new QSlider(Qt::Horizontal, tb);
    opacity_->setRange(15, 100);
    opacity_->setValue(45);
    opacity_->setMaximumWidth(120);
    tb->addWidget(opacity_);
    connect(opacity_, &QSlider::valueChanged, this, &MainWindow::applyLayers);
    tb->addSeparator();
    tb->addWidget(new QLabel(" Story point ", tb));
    storyBox_ = new QComboBox(tb);
    storyBox_->addItem("any", -1);
    for (int p = 0; p <= 0x2D; p++) storyBox_->addItem(QString("0x%1").arg(p, 2, 16, QChar('0')), p);
    storyBox_->setToolTip("Shows each spot's character as at this point of the story (GAME.progress), where its "
                          "conditions say; Test in game starts there too");
    tb->addWidget(storyBox_);
    connect(storyBox_, &QComboBox::currentIndexChanged, this, [this] { applyStoryPoint(); });

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&Controls", [this] {
        QMessageBox::information(this, "Controls",
                                 "Wheel: zoom around the cursor\nMiddle or right button, or Space + left: pan\n"
                                 "Right click: add, duplicate or delete\nF: fit the map\n\n"
                                 "V select and move, B floor, T triggers, A battle areas\n"
                                 "Painting: Shift for a rectangle, Alt to pick the value under the cursor\n"
                                 "Ctrl+Z / Ctrl+Y: undo, redo · Ctrl+S: save · F5: test in game\n"
                                 "2-9: the layers");
    });
    helpMenu->addAction("About &Qt", qApp, &QApplication::aboutQt);
    menuBar()->addAction("&About", this, &MainWindow::showAbout);

    cursorLabel_ = new QLabel(this);
    zoomLabel_ = new QLabel(this);
    statusBar()->addWidget(cursorLabel_, 1);
    statusBar()->addPermanentWidget(zoomLabel_);
    connect(view_, &MapView::zoomChanged, this, [this](double z) { zoomLabel_->setText(QString("%1%").arg(qRound(z * 100))); });
    connect(view_, &MapView::hovered, this, [this](QPointF p, bool inside) {
        if (brush_) {
            int s = sizeBox_->currentData().toInt();
            brush_->setRect(std::floor(p.x()) - s / 2, std::floor(p.y()) - s / 2, s, s);
            brush_->setVisible(inside && tool_ != TOOL_SELECT);
        }
        if (!inside) return cursorLabel_->clear();
        int x = (int)p.x(), y = (int)p.y();
        QStringList parts{QString("%1, %2").arg(x).arg(y)};
        int fl = field_.maps[floor_].at(x, y), tr = field_.maps[MAP_TRIGGERS].at(x, y), ar = field_.maps[MAP_AREAS].at(x, y);
        if (fl >= 0) parts << (fl ? QString("walkable (%1)").arg(fl) : "blocked");
        if (tr > 0) {
            int s = tr & 0x1F;
            parts << QString("trigger: slot %1, %2, facing %3").arg(s).arg(s < (int)field_.stage.slots.size() ? slotType(field_.stage.slots[s].type) : "?").arg(DIRS[tr >> 5]);
        }
        if (ar > 0) parts << QString("battle area %1").arg(ar);
        cursorLabel_->setText(parts.join("   ·   "));
    });
    connect(view_, &MapView::paintPressed, this, &MainWindow::beginStroke);
    connect(view_, &MapView::paintMoved, this, &MainWindow::moveStroke);
    connect(view_, &MapView::paintReleased, this, &MainWindow::endStroke);
    connect(view_, &MapView::dragEnded, this, &MainWindow::commitMoves);
    connect(view_, &MapView::contextMenuAt, this, &MainWindow::contextMenu);
    connect(view_, &MapView::deletePressed, this, &MainWindow::deleteSelection);

    connect(filter_, &QLineEdit::textChanged, this, [this](const QString &f) {
        for (int i = 0; i < stageTree_->topLevelItemCount(); i++) {
            QTreeWidgetItem *area = stageTree_->topLevelItem(i);
            int shown = 0;
            for (int k = 0; k < area->childCount(); k++) {
                QTreeWidgetItem *it = area->child(k);
                bool ok = f.isEmpty() || it->text(0).contains(f, Qt::CaseInsensitive) ||
                          area->text(0).contains(f, Qt::CaseInsensitive) || it->toolTip(0).contains(f, Qt::CaseInsensitive);
                it->setHidden(!ok);
                shown += ok;
            }
            area->setHidden(!shown);
            area->setExpanded(!f.isEmpty() && shown);
        }
    });
    connect(stageTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        if (it && !it->data(0, Qt::UserRole).toString().isEmpty()) openStage(it->data(0, Qt::UserRole).toString());
    });
    connect(scene_, &QGraphicsScene::selectionChanged, this, [this] {
        if (syncing_) return;
        auto sel = scene_->selectedItems();
        if (sel.isEmpty()) return select(KIND_NONE, -1, false);
        int kind = sel[0]->data(0).toInt(), index = sel[0]->data(1).toInt();
        for (const ActorGroup &g : groups_)
            if (g.item == sel[0]) index = g.shown;
        select(kind, index, false);
    });
    connect(entities_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        if (syncing_ || !it || !it->data(0, Qt::UserRole).isValid()) return;
        select(it->data(0, Qt::UserRole).toInt(), it->data(0, Qt::UserRole + 1).toInt(), true);
    });

    buildStageTree();
    resize(1400, 860);
}

void MainWindow::selectByName(const QString &what) {
    QStringList p = what.split(':');
    static const QStringList KINDS{"", "actor", "slot", "object", "start"};
    int kind = (int)KINDS.indexOf(p[0]);
    if (kind > 0) select(kind, p.size() > 1 ? p[1].toInt() : 0, true);
}

void MainWindow::showAbout() {
    QMessageBox box(this);
    box.setWindowTitle("About DW3 Field Editor");
    box.setIconPixmap(QIcon(":/dw3fe.ico").pixmap(96, 96));
    box.setTextFormat(Qt::RichText);
    box.setText(QString("<h2>DW3 Field Editor</h2><p>Version %1</p>"
                        "<p>An editor for the fields of <i>Digimon World 2003</i>, for its native PC port.</p>"
                        "<p>&copy; 2026 Feerdus &middot; MIT License<br>"
                        "<a href='https://github.com/Feerdus95/dw3-field-editor'>github.com/Feerdus95/dw3-field-editor</a></p>"
                        "<p>Built on <a href='https://github.com/ReGame-Labs/dw3_decomp'>dw3_decomp</a> (MIT, juandav) "
                        "and Qt %2 (LGPL v3).</p>"
                        "<p style='color:#999'>Not affiliated with Bandai. No game data is included: "
                        "it reads your own copy of the game.</p>")
                    .arg(DW3FE_VERSION, qVersion()));
    box.setStandardButtons(QMessageBox::Ok);
    QPushButton *qt = box.addButton("About Qt", QMessageBox::HelpRole);
    connect(qt, &QPushButton::clicked, qApp, &QApplication::aboutQt);
    box.exec();
}

QString MainWindow::stageTitle(const Stage &s) const {
    if (s.modes.empty()) return QString::fromStdString(s.name);
    const Stage::Mode &m = s.modes[0];
    QString place = m.place < (int)text_.places.size() ? QString::fromStdString(text_.places[m.place]) : "";
    if (place.isEmpty()) place = QString::fromStdString(s.name);
    return place;
}

QString MainWindow::modeName(int mode) const {
    if (!modeStage_.contains(mode)) return QString("mode %1").arg(mode);
    const Stage &s = stages_[modeStage_[mode]];
    for (auto &m : s.modes)
        if (m.mode == mode) {
            QString area = m.area < (int)text_.areas.size() ? QString::fromStdString(text_.areas[m.area]) : "";
            QString place = m.place < (int)text_.places.size() ? QString::fromStdString(text_.places[m.place]) : "";
            return area.isEmpty() || area == place ? place : place + ", " + area;
        }
    return stageTitle(s);
}

void MainWindow::updateTitle() {
    const Stage &s = field_.stage;
    QString name = s.modes.empty() ? QString::fromStdString(s.name) : modeName(s.modes[0].mode);
    setWindowTitle(QString("%1%2 — DW3 Field Editor").arg(name, undo_->isClean() ? "" : " *"));
}

void MainWindow::buildStageTree() {
    std::map<QString, QTreeWidgetItem *> areas;
    for (const Stage &s : stages_) {
        QString area = "Other";
        bool extra = !s.modes.empty() && s.modes[0].extra;
        if (!s.modes.empty() && s.modes[0].area < (int)text_.areas.size() && !text_.areas[s.modes[0].area].empty())
            area = QString::fromStdString(text_.areas[s.modes[0].area]);
        if (extra) area += " (extra chapter)";
        QTreeWidgetItem *&a = areas[area];
        if (!a) {
            a = new QTreeWidgetItem(stageTree_, {area});
            QFont f = a->font(0);
            f.setBold(true);
            a->setFont(0, f);
            a->setFlags(Qt::ItemIsEnabled);
        }
        QString num = QString::fromStdString(s.name).mid(5);
        auto *it = new QTreeWidgetItem(a, {stageTitle(s)});
        QStringList modes;
        for (auto &m : s.modes) modes << QString::number(m.mode);
        it->setToolTip(0, QString("%1 · WSTAG%2 · mode %3").arg(stageTitle(s), num, modes.join(", ")));
        it->setData(0, Qt::UserRole, QString::fromStdString(s.name));
    }
    stageTree_->sortItems(0, Qt::AscendingOrder);
    /* places with the same name (the two servers' copies, story copies) tell their numbers */
    for (int i = 0; i < stageTree_->topLevelItemCount(); i++) {
        QTreeWidgetItem *a = stageTree_->topLevelItem(i);
        QHash<QString, int> count;
        for (int k = 0; k < a->childCount(); k++) count[a->child(k)->text(0)]++;
        for (int k = 0; k < a->childCount(); k++)
            if (count[a->child(k)->text(0)] > 1)
                a->child(k)->setText(0, a->child(k)->text(0) + "  (" + a->child(k)->data(0, Qt::UserRole).toString().mid(5) + ")");
    }
}

/* Opens a field: the game's tables, then the editor's saved changes over them */
void MainWindow::openStage(const QString &name) {
    const Stage *st = nullptr;
    for (auto &s : stages_)
        if (s.name == name.toStdString()) st = &s;
    if (!st || (field_.stage.name == st->name && !field_.stage.name.empty())) return;
    if (!maybeSave()) {
        /* the tree goes back to the open field */
        QSignalBlocker b(stageTree_);
        for (QTreeWidgetItemIterator it(stageTree_); *it; ++it)
            if ((*it)->data(0, Qt::UserRole).toString().toStdString() == field_.stage.name) stageTree_->setCurrentItem(*it);
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    original_ = *st;
    Stage edited = *st;
    std::ifstream mod(files_.stageModPath(st->name), std::ios::binary);
    if (mod) {
        std::stringstream ss;
        ss << mod.rdbuf();
        Json j;
        if (Json::parse(ss.str(), j)) edited.loadSections(j);
        else statusBar()->showMessage("The field's saved changes can't be read: " + QString::fromStdString(files_.stageModPath(st->name)), 8000);
    }
    field_ = Field();
    if (!field_.load(files_, edited, &animFiles_)) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, "Can't open the field", QString::fromStdString(field_.error));
        return;
    }
    talks_ = text_.stageText(files_, st->textFile);
    QSettings().setValue("stage", name);
    floor_ = 0;
    highlightSlot_ = -1;
    selKind_ = KIND_NONE, selIndex_ = -1;
    for (bool &e : mapEdited_) e = false;
    undo_->clear();

    computeSlotCells();
    buildActorGroups();
    buildScene();
    fillEntities();
    select(KIND_NONE, -1, false);
    refreshPaintOptions();
    updateTitle();
    QTimer::singleShot(0, view_, &MapView::fit);
    QApplication::restoreOverrideCursor();

    /* the tree follows */
    for (QTreeWidgetItemIterator it(stageTree_); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == name && stageTree_->currentItem() != *it) {
            QSignalBlocker b(stageTree_);
            stageTree_->setCurrentItem(*it);
        }
}

/* Each slot's trigger cells, their bounds */
void MainWindow::computeSlotCells() {
    slotCells_.assign(32, QRect());
    const LogicMap &t = field_.maps[MAP_TRIGGERS];
    std::vector<int> x0(32, 1 << 30), y0(32, 1 << 30), x1(32, -1), y1(32, -1);
    for (int y = 0; y < t.h; y++)
        for (int x = 0; x < t.w; x++)
            if (int v = t.px[(size_t)y * t.w + x]) {
                int s = v & 0x1F;
                x0[s] = std::min(x0[s], x), y0[s] = std::min(y0[s], y), x1[s] = std::max(x1[s], x), y1[s] = std::max(y1[s], y);
            }
    for (int s = 0; s < 32; s++)
        if (x1[s] >= 0) slotCells_[s] = QRect(QPoint(x0[s], y0[s]), QPoint(x1[s], y1[s]));
}

/* The characters by place: one group per spot, in the table's order */
void MainWindow::buildActorGroups() {
    groups_.clear();
    const auto &actors = field_.stage.actors;
    actorGroup_.assign(actors.size(), -1);
    for (int i = 0; i < (int)actors.size(); i++) {
        if (actors[i].x == 0 && actors[i].y == 0) continue;
        int g = -1;
        for (int k = 0; k < (int)groups_.size(); k++)
            if (groups_[k].x == actors[i].x && groups_[k].y == actors[i].y) g = k;
        if (g < 0) {
            g = (int)groups_.size();
            groups_.push_back({actors[i].x, actors[i].y, {}, i});
        }
        groups_[g].actors.push_back(i);
        actorGroup_[i] = g;
    }
}

/* A character's name: the speaker of its first line (the text before 02 07), or its number */
QString MainWindow::actorName(int actor) const {
    const Actor &a = field_.stage.actors[actor];
    for (const Talk &t : a.talks) {
        std::string line = talkText(t.entry);
        size_t colon = line.find(": ");
        if (colon != std::string::npos && colon > 0 && colon < 24 && line.find('\n') > colon)
            return QString::fromStdString(line.substr(0, colon));
    }
    return QString("Character 0x%1").arg(a.id, 0, 16);
}

/* A character the game doesn't draw: one without an animation file */
bool MainWindow::invisibleActor(int actor) const {
    int id = field_.stage.actors[actor].id;
    return id >= 0 && id < (int)animFiles_.size() && animFiles_[id] == 0;
}

std::string MainWindow::talkText(int entry) const {
    return entry >= 0 && entry < (int)talks_.size() ? talks_[entry] : "";
}

/* Draws a group as one of its versions */
void MainWindow::showVariant(int group, int actor) {
    ActorGroup &g = groups_[group];
    g.shown = actor;
    if (!g.item) return;
    const Sprite &sp = field_.actors[actor];
    if (sp.img.w) {
        g.item->setPixmap(QPixmap::fromImage(toQImage(sp.img)));
        g.item->setOffset(sp.ox, sp.oy);
    } else {
        /* a character without an animation file isn't drawn by the game: a
           spot the player talks to (a sign, a terminal); a dashed ring with a
           speech mark. A solid dot is one whose sprite couldn't be read. */
        bool invisible = invisibleActor(actor);
        QPixmap mark(20, 20);
        mark.fill(Qt::transparent);
        QPainter p(&mark);
        p.setRenderHint(QPainter::Antialiasing);
        if (invisible) {
            p.setPen(QPen(QColor(255, 255, 255, 230), 1.5, Qt::DashLine));
            p.setBrush(QColor(0, 0, 0, 120));
            p.drawEllipse(2, 2, 16, 16);
            p.setPen(Qt::white);
            p.setFont(QFont("Segoe UI", 8, QFont::Bold));
            p.drawText(QRect(0, 0, 20, 19), Qt::AlignCenter, "...");
        } else {
            p.setPen(QPen(Qt::black, 1.5));
            p.setBrush(QColor(0, 220, 255));
            p.drawEllipse(4, 4, 12, 12);
        }
        p.end();
        g.item->setPixmap(mark);
        g.item->setOffset(-10, -10);
    }
    if (g.badge) g.badge->setPos(g.item->offset() + QPointF(g.item->pixmap().width(), 0));
}

/* Each spot's version as at the story point chosen: the first whose story
   conditions hold (the others it can't know count as holding) */
void MainWindow::applyStoryPoint() {
    int p = storyBox_->currentData().toInt();
    for (int g = 0; g < (int)groups_.size(); g++) {
        int pick = groups_[g].actors[0];
        if (p >= 0)
            for (int i : groups_[g].actors) {
                bool ok = true;
                for (auto &[code, value] : field_.stage.actors[i].conditions)
                    if ((code >> 8 & 0xFE) == 0x60 && ((code & 0x1FF) == p) != (value != 0)) ok = false;
                if (ok) {
                    pick = i;
                    break;
                }
            }
        showVariant(g, pick);
    }
}

void MainWindow::buildScene() {
    syncing_ = true;
    scene_->clear();
    for (auto &v : layerItems_) v.clear();
    for (auto &o : overlay_) o = nullptr;
    rectPreview_ = nullptr, brush_ = nullptr;
    const Field &f = field_;
    scene_->setSceneRect(0, 0, f.width(), f.height());
    scene_->addRect(0, 0, f.width(), f.height(), QPen(Qt::NoPen), QColor(0, 0, 0))->setZValue(-100);

    /* the background's three depths, by their order table depth: the larger
       farther (z = -depth) */
    for (int d = 0; d < 3; d++) {
        QImage layer(f.width(), f.height(), QImage::Format_RGBA8888);
        layer.fill(Qt::transparent);
        QPainter p(&layer);
        bool any = false;
        for (int t = 0; t < f.tilesW * f.tilesH; t++)
            if (f.tiles[d][t].w) p.drawImage(t % f.tilesW * 128, t / f.tilesW * 128, toQImage(f.tiles[d][t])), any = true;
        p.end();
        if (!any) continue;
        auto *it = scene_->addPixmap(QPixmap::fromImage(layer));
        it->setZValue(-TILE_DEPTHS[d] - 0.5);
        it->setAcceptedMouseButtons(Qt::NoButton);
        layerItems_[L_BACKGROUND].push_back(it);
    }
    /* the brush's outline, and a rectangle stroke's */
    brush_ = scene_->addRect(0, 0, 1, 1, QPen(QColor(255, 255, 255, 200), 0, Qt::DashLine));
    brush_->setZValue(2000);
    brush_->setVisible(false);
    brush_->setAcceptedMouseButtons(Qt::NoButton);
    rectPreview_ = scene_->addRect(0, 0, 1, 1, QPen(Qt::white, 0, Qt::DashLine), QColor(255, 255, 255, 50));
    rectPreview_->setZValue(2001);
    rectPreview_->setVisible(false);
    rectPreview_->setAcceptedMouseButtons(Qt::NoButton);
    syncing_ = false;
    buildItems();
    rebuildOverlays();
}

/* The items the tables give: the objects, the slots' markers, the characters and the start */
void MainWindow::buildItems() {
    syncing_ = true;
    const Field &f = field_;
    bool movable = tool_ == TOOL_SELECT;
    for (int i = 0; i < (int)f.stage.objects.size(); i++) {
        const Object &o = f.stage.objects[i];
        const Sprite &s = f.objects[i];
        QGraphicsItem *it;
        if (s.img.w) {
            auto *p = scene_->addPixmap(QPixmap::fromImage(toQImage(s.img)));
            p->setOffset(s.ox, s.oy);
            p->setShapeMode(QGraphicsPixmapItem::MaskShape);
            it = p;
        } else {
            it = scene_->addRect(-6, -6, 12, 12, QPen(QColor(255, 160, 0)), Qt::NoBrush);
        }
        it->setPos(o.x, o.y);
        it->setZValue(-o.depth);
        it->setOpacity(o.visible ? 1 : 0.35);
        it->setFlag(QGraphicsItem::ItemIsSelectable);
        it->setFlag(QGraphicsItem::ItemIsMovable, movable);
        it->setData(0, KIND_OBJECT), it->setData(1, i);
        layerItems_[L_OBJECTS].push_back(it);
    }

    QFont label("Segoe UI", 9, QFont::Bold);
    for (int i = 0; i < (int)f.stage.slots.size(); i++) {
        const Slot &s = f.stage.slots[i];
        if (i >= 32 || slotCells_[i].isNull()) continue;
        QRect r = slotCells_[i];
        QColor c = leaves(s.type) ? QColor(255, 220, 60) : QColor(120, 200, 255);
        auto *box = scene_->addRect(r, QPen(c, 2, Qt::DashLine), QColor(c.red(), c.green(), c.blue(), 40));
        box->setZValue(1000);
        box->setFlag(QGraphicsItem::ItemIsSelectable);
        box->setData(0, KIND_SLOT), box->setData(1, i);
        box->setToolTip(slotType(s.type));
        QString text = leaves(s.type) ? "→ " + modeName((int16_t)s.arg) : slotType(s.type);
        auto *bg = new QGraphicsRectItem(box);
        auto *t = new QGraphicsSimpleTextItem(text, bg);
        t->setFont(label);
        t->setBrush(c);
        t->setPos(4, 1);
        bg->setRect(t->boundingRect().adjusted(0, 0, 8, 2));
        bg->setBrush(QColor(0, 0, 0, 190));
        bg->setPen(Qt::NoPen);
        bg->setPos(r.left(), r.top());
        bg->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        layerItems_[L_EXITS].push_back(box);
    }
    /* the characters, a group per spot: the version shown's standing sprite,
       sorted by y over the map's depth 4 (FIELDSTG_drawActor), and how many
       versions the spot has */
    for (int g = 0; g < (int)groups_.size(); g++) {
        ActorGroup &grp = groups_[g];
        auto *p = scene_->addPixmap(QPixmap());
        p->setPos(grp.x, grp.y);
        p->setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
        p->setZValue(-4 + grp.y / 100000.0);
        p->setFlag(QGraphicsItem::ItemIsSelectable);
        p->setFlag(QGraphicsItem::ItemIsMovable, movable);
        p->setData(0, KIND_ACTOR), p->setData(1, grp.actors[0]);
        grp.item = p;
        grp.badge = nullptr;
        if (grp.actors.size() > 1) {
            auto *bg = new QGraphicsRectItem(p);
            auto *t = new QGraphicsSimpleTextItem(QString("×%1").arg(grp.actors.size()), bg);
            t->setFont(QFont("Segoe UI", 8, QFont::Bold));
            t->setBrush(Qt::white);
            t->setPos(3, 0);
            bg->setRect(t->boundingRect().adjusted(0, 0, 6, 0));
            bg->setBrush(QColor(200, 40, 40, 230));
            bg->setPen(Qt::NoPen);
            bg->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            bg->setAcceptedMouseButtons(Qt::NoButton);
            grp.badge = bg;
        }
        showVariant(g, grp.shown);
        p->setToolTip(grp.actors.size() > 1 ? QString("%1 versions of this spot").arg(grp.actors.size()) : actorName(grp.actors[0]));
        layerItems_[L_ACTORS].push_back(p);
    }
    auto *start = scene_->addEllipse(-9, -9, 18, 18, QPen(Qt::white, 2), QColor(255, 80, 80));
    start->setPos(f.stage.startX / 256.0, f.stage.startY / 256.0);
    start->setZValue(1002);
    start->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    start->setFlag(QGraphicsItem::ItemIsSelectable);
    start->setFlag(QGraphicsItem::ItemIsMovable, movable);
    start->setData(0, KIND_START), start->setData(1, 0);
    start->setToolTip("Where the player starts");
    layerItems_[L_START].push_back(start);
    syncing_ = false;
    applyStoryPoint();
    applyLayers();
}

/* The tables' items again, after the tables changed */
void MainWindow::refreshItems() {
    syncing_ = true;
    for (int l : {L_OBJECTS, L_EXITS, L_ACTORS, L_START}) {
        for (QGraphicsItem *it : layerItems_[l]) delete it;
        layerItems_[l].clear();
    }
    syncing_ = false;
    computeSlotCells();
    buildActorGroups();
    buildItems();
}

void MainWindow::rebuildOverlays() {
    for (int k = 0; k < 3; k++) {
        if (overlay_[k]) delete overlay_[k], overlay_[k] = nullptr;
        overlayImg_[k] = QImage();
        int l = OVERLAY_LAYERS[k];
        if (!layerActions_[l]->isChecked()) continue;
        const LogicMap &m = field_.maps[l == L_FLOOR ? floor_ : l == L_TRIGGERS ? MAP_TRIGGERS : MAP_AREAS];
        if (!m.w) continue;
        QImage img(m.w, m.h, QImage::Format_ARGB32);
        int hl = l == L_TRIGGERS ? highlightSlot_ : -1;
        for (int y = 0; y < m.h; y++) {
            QRgb *line = (QRgb *)img.scanLine(y);
            for (int x = 0; x < m.w; x++) line[x] = overlayColor(l, m.px[(size_t)y * m.w + x], hl);
        }
        overlayImg_[k] = img;
        overlay_[k] = scene_->addPixmap(QPixmap::fromImage(img));
        overlay_[k]->setZValue(500 + k);
        overlay_[k]->setAcceptedMouseButtons(Qt::NoButton);
    }
    applyLayers();
}

/* The overlay of a logic map again in a rectangle, after a stroke */
void MainWindow::updateOverlayRect(int map, const QRect &r) {
    for (int k = 0; k < 3; k++) {
        int l = OVERLAY_LAYERS[k];
        int shows = l == L_FLOOR ? floor_ : l == L_TRIGGERS ? MAP_TRIGGERS : MAP_AREAS;
        if (shows != map || overlayImg_[k].isNull()) continue;
        const LogicMap &m = field_.maps[map];
        int hl = l == L_TRIGGERS ? highlightSlot_ : -1;
        QRect c = r.intersected(QRect(0, 0, m.w, m.h));
        for (int y = c.top(); y <= c.bottom(); y++) {
            QRgb *line = (QRgb *)overlayImg_[k].scanLine(y);
            for (int x = c.left(); x <= c.right(); x++) line[x] = overlayColor(l, m.px[(size_t)y * m.w + x], hl);
        }
        overlay_[k]->setPixmap(QPixmap::fromImage(overlayImg_[k]));
    }
}

void MainWindow::applyLayers() {
    for (int l = 0; l < LAYER_COUNT; l++)
        for (QGraphicsItem *it : layerItems_[l]) it->setVisible(layerActions_[l]->isChecked());
    for (auto *o : overlay_)
        if (o) o->setOpacity(opacity_->value() / 100.0);
}

void MainWindow::fillEntities() {
    syncing_ = true;
    entities_->clear();
    const Stage &s = field_.stage;
    auto group = [&](const QString &name, int n) {
        auto *g = new QTreeWidgetItem(entities_, {QString("%1 (%2)").arg(name).arg(n)});
        g->setFlags(Qt::ItemIsEnabled);
        return g;
    };
    auto actorItem = [&](QTreeWidgetItem *parent, int i, const QString &where) {
        const Actor &a = s.actors[i];
        QString cond = a.conditions.empty() ? "" : "  — if " + QString::fromStdString(describe_codes(a.conditions, text_, false));
        auto *it = new QTreeWidgetItem(parent, {QString("#%1  %2%3").arg(i).arg(actorName(i), cond), where});
        std::string first = a.talks.empty() ? "" : talkText(a.talks[0].entry);
        if (!first.empty()) it->setToolTip(0, QString::fromStdString(first));
        it->setData(0, Qt::UserRole, KIND_ACTOR), it->setData(0, Qt::UserRole + 1, i);
        return it;
    };
    QTreeWidgetItem *g = group("Characters", (int)groups_.size());
    g->setText(0, QString("Characters (%1 places, %2 versions)").arg(groups_.size()).arg(s.actors.size()));
    g->setExpanded(true);
    for (const ActorGroup &grp : groups_) {
        QString where = QString("%1, %2").arg(grp.x).arg(grp.y);
        if (grp.actors.size() == 1) {
            actorItem(g, grp.actors[0], where);
            continue;
        }
        QStringList names;
        for (int i : grp.actors)
            if (!names.contains(actorName(i))) names << actorName(i);
        auto *node = new QTreeWidgetItem(g, {QString("%1  · %2 versions").arg(names.join(", ")).arg(grp.actors.size()), where});
        node->setData(0, Qt::UserRole, KIND_ACTOR), node->setData(0, Qt::UserRole + 1, grp.actors[0]);
        for (int i : grp.actors) actorItem(node, i, "");
    }
    int placed = 0;
    for (int v : actorGroup_) placed += v < 0;
    if (placed) {
        QTreeWidgetItem *e = group("Placed by events", placed);
        e->setToolTip(0, "Characters at 0, 0: the field's events put them where they go");
        for (int i = 0; i < (int)s.actors.size(); i++)
            if (actorGroup_[i] < 0) actorItem(e, i, "by events");
    }
    g = group("Exits and triggers", (int)s.slots.size());
    for (int i = 0; i < (int)s.slots.size(); i++) {
        const Slot &t = s.slots[i];
        QString what = QString("#%1  %2").arg(i).arg(slotType(t.type));
        if (leaves(t.type)) what += " → " + modeName((int16_t)t.arg);
        auto *it = new QTreeWidgetItem(g, {what, i < 32 && !slotCells_[i].isNull() ? "" : "no cells"});
        it->setData(0, Qt::UserRole, KIND_SLOT), it->setData(0, Qt::UserRole + 1, i);
    }
    g = group("Objects", (int)s.objects.size());
    for (int i = 0; i < (int)s.objects.size(); i++) {
        const Object &o = s.objects[i];
        QString what = QString("#%1  %2 %3").arg(i).arg(o.anim == 0xFF ? "hidden spot" : "frame").arg(o.frame);
        auto *it = new QTreeWidgetItem(g, {what, QString("%1, %2").arg(o.x).arg(o.y)});
        it->setData(0, Qt::UserRole, KIND_OBJECT), it->setData(0, Qt::UserRole + 1, i);
    }
    int areas = 0;
    for (auto &p : s.places)
        for (auto &a : p.areas) areas += a.rate != 0;
    auto *battles = new QTreeWidgetItem(entities_, {QString("Battles (%1 places)").arg(s.places.size()), ""});
    battles->setData(0, Qt::UserRole, KIND_BATTLES), battles->setData(0, Qt::UserRole + 1, 0);
    auto *startIt = new QTreeWidgetItem(entities_, {"Start point", QString("%1, %2").arg(s.startX >> 8).arg(s.startY >> 8)});
    startIt->setData(0, Qt::UserRole, KIND_START), startIt->setData(0, Qt::UserRole + 1, 0);
    syncing_ = false;
}

void MainWindow::select(int kind, int index, bool center) {
    syncing_ = true;
    selKind_ = kind, selIndex_ = index;
    /* the map: a character's spot shows the selected version */
    QGraphicsItem *target = nullptr;
    int group = kind == KIND_ACTOR && index >= 0 && index < (int)actorGroup_.size() ? actorGroup_[index] : -1;
    if (group >= 0 && groups_[group].shown != index) showVariant(group, index);
    for (QGraphicsItem *it : scene_->items()) {
        bool me = kind != KIND_NONE && it->data(0).toInt() == kind &&
                  (group >= 0 ? it == groups_[group].item : it->data(1).toInt() == index);
        if (it->flags() & QGraphicsItem::ItemIsSelectable) it->setSelected(me);
        if (me) target = it;
    }
    if (center && target) view_->centerOn(target);
    /* the triggers' highlight follows the selected slot */
    int hl = kind == KIND_SLOT ? index : -1;
    if (hl != highlightSlot_) {
        highlightSlot_ = hl;
        rebuildOverlays();
    }
    /* the list: the deepest item that names it, a version rather than its spot's node */
    QTreeWidgetItem *found = nullptr;
    for (QTreeWidgetItemIterator it(entities_); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).isValid() && (*it)->data(0, Qt::UserRole).toInt() == kind &&
            (*it)->data(0, Qt::UserRole + 1).toInt() == index && (!found || (*it)->parent() == found))
            found = *it;
    if (found) {
        entities_->setCurrentItem(found);
        entities_->scrollToItem(found);
    } else entities_->setCurrentItem(nullptr);
    syncing_ = false;
    showProperties();
}

/* --- editing --- */

void MainWindow::changeTables(const Stage &after, const QString &what) {
    undo_->push(new TablesCommand(this, field_.stage, after, what));
}

void MainWindow::setTables(const Stage &s) {
    int kind = selKind_, index = selIndex_;
    field_.stage = s;
    field_.refreshSprites(files_, &animFiles_);
    refreshItems();
    fillEntities();
    /* the selection stays when its item still is */
    bool still = (kind == KIND_ACTOR && index < (int)s.actors.size()) || (kind == KIND_SLOT && index < (int)s.slots.size()) ||
                 (kind == KIND_OBJECT && index < (int)s.objects.size()) || kind == KIND_START || kind == KIND_BATTLES;
    select(still ? kind : KIND_NONE, still ? index : -1, false);
    updateTitle();
}

void MainWindow::setMapRegion(int map, const QRect &r, const std::vector<uint8_t> &px) {
    LogicMap &m = field_.maps[map];
    for (int y = 0; y < r.height(); y++)
        memcpy(&m.px[(size_t)(r.top() + y) * m.w + r.left()], &px[(size_t)y * r.width()], r.width());
    mapEdited_[map] = true;
    updateOverlayRect(map, r);
    if (map == MAP_TRIGGERS) {
        computeSlotCells();
        refreshItems();
        fillEntities();
    }
    updateTitle();
}

/* A drag in select mode ended: the items whose place differs from their table's move there */
void MainWindow::commitMoves() {
    Stage s = field_.stage;
    bool moved = false;
    for (const ActorGroup &g : groups_) {
        if (!g.item) continue;
        int dx = qRound(g.item->pos().x()) - g.x, dy = qRound(g.item->pos().y()) - g.y;
        if (!dx && !dy) continue;
        for (int i : g.actors) s.actors[i].x += dx, s.actors[i].y += dy;
        moved = true;
    }
    for (QGraphicsItem *it : layerItems_[L_OBJECTS]) {
        int i = it->data(1).toInt();
        int x = qRound(it->pos().x()), y = qRound(it->pos().y());
        if (x != s.objects[i].x || y != s.objects[i].y) s.objects[i].x = x, s.objects[i].y = y, moved = true;
    }
    for (QGraphicsItem *it : layerItems_[L_START]) {
        int x = qRound(it->pos().x() * 256), y = qRound(it->pos().y() * 256);
        if (x != s.startX || y != s.startY) s.startX = x, s.startY = y, moved = true;
    }
    if (moved) changeTables(s, "Move");
}

void MainWindow::deleteSelection() {
    Stage s = field_.stage;
    if (selKind_ == KIND_ACTOR && selIndex_ >= 0 && selIndex_ < (int)s.actors.size()) {
        s.actors.erase(s.actors.begin() + selIndex_);
        changeTables(s, "Delete a character");
    } else if (selKind_ == KIND_OBJECT && selIndex_ >= 0 && selIndex_ < (int)s.objects.size()) {
        s.objects.erase(s.objects.begin() + selIndex_);
        changeTables(s, "Delete an object");
    } else if (selKind_ == KIND_SLOT && selIndex_ >= 0) {
        /* trigger cells name slots by number: only the last one, without cells, can go */
        if (selIndex_ != (int)s.slots.size() - 1 || (selIndex_ < 32 && !slotCells_[selIndex_].isNull())) {
            QMessageBox::information(this, "Delete a slot",
                                     "The trigger cells name the slots by number, so only the last slot can be deleted, "
                                     "and only once no cells set it off. Paint its cells away first.");
            return;
        }
        s.slots.pop_back();
        changeTables(s, "Delete a slot");
    }
}

void MainWindow::contextMenu(QPointF p, QPoint global) {
    if (tool_ != TOOL_SELECT) return;
    /* what is under the click becomes the selection */
    for (QGraphicsItem *it : scene_->items(p))
        if (it->flags() & QGraphicsItem::ItemIsSelectable && it->isVisible()) {
            it->setSelected(true);
            break;
        }
    int x = qRound(p.x()), y = qRound(p.y());
    const Stage &cur = field_.stage;
    QMenu menu(this);
    int sel = selIndex_;
    if (selKind_ == KIND_ACTOR && sel >= 0 && sel < (int)cur.actors.size()) {
        menu.addAction(QString("Add a version to this spot (a copy of #%1)").arg(sel), [this, sel] {
            Stage s = field_.stage;
            Actor a = s.actors[sel];
            /* after the spot's last version, so the game reaches it last */
            int g = actorGroup_[sel], at = g >= 0 ? groups_[g].actors.back() + 1 : sel + 1;
            s.actors.insert(s.actors.begin() + at, a);
            changeTables(s, "Add a version");
            select(KIND_ACTOR, at, false);
        });
        menu.addAction(QString("Duplicate #%1 here").arg(sel), [this, sel, x, y] {
            Stage s = field_.stage;
            Actor a = s.actors[sel];
            a.x = x, a.y = y;
            s.actors.push_back(a);
            changeTables(s, "Duplicate a character");
            select(KIND_ACTOR, (int)s.actors.size() - 1, false);
        });
        menu.addAction(QString("Delete #%1").arg(sel), this, &MainWindow::deleteSelection);
        menu.addSeparator();
    } else if (selKind_ == KIND_OBJECT && sel >= 0 && sel < (int)cur.objects.size()) {
        menu.addAction(QString("Duplicate object #%1 here").arg(sel), [this, sel, x, y] {
            Stage s = field_.stage;
            Object o = s.objects[sel];
            o.x = x, o.y = y;
            s.objects.push_back(o);
            changeTables(s, "Duplicate an object");
            select(KIND_OBJECT, (int)s.objects.size() - 1, false);
        });
        menu.addAction(QString("Delete object #%1").arg(sel), this, &MainWindow::deleteSelection);
        menu.addSeparator();
    }
    menu.addAction("Add a character here", [this, x, y] {
        Stage s = field_.stage;
        /* like the field's first character: its look and image slot are ones the field loads */
        Actor a{0x1B, 0, x, y, 0, {}, {Talk{0, {}, {}}}};
        if (!s.actors.empty()) a.id = s.actors[0].id, a.image = s.actors[0].image;
        s.actors.push_back(a);
        changeTables(s, "Add a character");
        select(KIND_ACTOR, (int)s.actors.size() - 1, false);
    });
    if (!cur.objects.empty())
        menu.addAction("Add an object here", [this, x, y] {
            Stage s = field_.stage;
            Object o = s.objects.back();
            o.x = x, o.y = y, o.visible = 1;
            s.objects.push_back(o);
            changeTables(s, "Add an object");
            select(KIND_OBJECT, (int)s.objects.size() - 1, false);
        });
    menu.addAction("Add a slot (exit, climb, event...)", [this] {
        Stage s = field_.stage;
        if (s.slots.size() >= 31) {
            QMessageBox::information(this, "Add a slot", "A field has at most 31 slots: a trigger cell names one in 5 bits.");
            return;
        }
        Slot t{};
        t.cond[0][0] = t.cond[1][0] = 0xFFFF;
        t.type = 1;
        t.arg = s.modes.empty() ? 0 : s.modes[0].mode;
        s.slots.push_back(t);
        changeTables(s, "Add a slot");
        select(KIND_SLOT, (int)s.slots.size() - 1, false);
    });
    menu.addAction("Move the start here", [this, x, y] {
        Stage s = field_.stage;
        s.startX = x * 256, s.startY = y * 256;
        changeTables(s, "Move the start");
    });
    menu.exec(global);
}

/* --- painting --- */

void MainWindow::setTool(int tool) {
    tool_ = tool;
    toolActions_[tool]->setChecked(true);
    view_->setPaintMode(tool != TOOL_SELECT);
    paintBar_->setVisible(tool != TOOL_SELECT);
    /* the layer painted shows */
    int layer = tool == TOOL_FLOOR ? L_FLOOR : tool == TOOL_TRIGGERS ? L_TRIGGERS : tool == TOOL_AREAS ? L_AREAS : -1;
    if (layer >= 0 && !layerActions_[layer]->isChecked()) layerActions_[layer]->setChecked(true);
    for (int l : {L_OBJECTS, L_ACTORS, L_START})
        for (QGraphicsItem *it : layerItems_[l]) it->setFlag(QGraphicsItem::ItemIsMovable, tool == TOOL_SELECT);
    if (tool != TOOL_SELECT) scene_->clearSelection();
    refreshPaintOptions();
}

/* The values the tool paints: floor heights, slots, areas */
void MainWindow::refreshPaintOptions() {
    QVariant keep = valueBox_->currentData();
    valueBox_->clear();
    facingAction_->setVisible(tool_ == TOOL_TRIGGERS);
    if (tool_ == TOOL_FLOOR) {
        valueBox_->addItem("Blocked (0)", 0);
        valueBox_->addItem("Walkable, flat (1)", 1);
        std::set<int> used(field_.maps[floor_].px.begin(), field_.maps[floor_].px.end());
        for (int v : used)
            if (v > 1) valueBox_->addItem(QString("Slope or height %1 (as the map has it)").arg(v), v);
    } else if (tool_ == TOOL_TRIGGERS) {
        valueBox_->addItem("No trigger (erase)", -1);
        for (int i = 0; i < (int)field_.stage.slots.size() && i < 32; i++) {
            const Slot &t = field_.stage.slots[i];
            QString what = slotType(t.type);
            if (leaves(t.type)) what += " → " + modeName((int16_t)t.arg);
            valueBox_->addItem(QString("Slot %1: %2").arg(i).arg(what), i);
        }
        if (selKind_ == KIND_SLOT && selIndex_ >= 0) keep = selIndex_;
    } else if (tool_ == TOOL_AREAS) {
        valueBox_->addItem("No battles (0)", 0);
        for (int a = 1; a <= 4; a++) valueBox_->addItem(QString("Battle area %1").arg(a), a);
    }
    int at = valueBox_->findData(keep);
    if (at >= 0) valueBox_->setCurrentIndex(at);
    else if (tool_ == TOOL_FLOOR) valueBox_->setCurrentIndex(1);
}

int MainWindow::paintMap() const {
    return tool_ == TOOL_FLOOR ? floor_ : tool_ == TOOL_TRIGGERS ? MAP_TRIGGERS : tool_ == TOOL_AREAS ? MAP_AREAS : -1;
}

/* The byte the tool writes: a trigger's slot and facing (3 bits over 5) */
int MainWindow::paintValue() const {
    int v = valueBox_->currentData().toInt();
    if (tool_ == TOOL_TRIGGERS) return v < 0 ? 0 : (facingBox_->currentData().toInt() << 5) | v;
    return v;
}

void MainWindow::fillRect(const QRect &r, int value) {
    LogicMap &m = field_.maps[strokeMap_];
    QRect c = r.intersected(QRect(0, 0, m.w, m.h));
    if (c.isEmpty()) return;
    for (int y = c.top(); y <= c.bottom(); y++) memset(&m.px[(size_t)y * m.w + c.left()], value, c.width());
    strokeRect_ = strokeRect_.isNull() ? c : strokeRect_.united(c);
    updateOverlayRect(strokeMap_, c);
}

void MainWindow::paintAt(QPointF p) {
    int s = sizeBox_->currentData().toInt();
    fillRect(QRect((int)std::floor(p.x()) - s / 2, (int)std::floor(p.y()) - s / 2, s, s), paintValue());
}

void MainWindow::beginStroke(QPointF p, Qt::KeyboardModifiers mods) {
    int map = paintMap();
    if (map < 0) return;
    LogicMap &m = field_.maps[map];
    if (!m.w) {
        QMessageBox::information(this, "Paint", "This field has no such map to paint.");
        return;
    }
    /* Alt picks the value under the cursor */
    if (mods & Qt::AltModifier) {
        int v = m.at((int)p.x(), (int)p.y());
        if (v < 0) return;
        if (tool_ == TOOL_TRIGGERS) {
            valueBox_->setCurrentIndex(std::max(0, valueBox_->findData(v ? v & 0x1F : -1)));
            facingBox_->setCurrentIndex(v >> 5);
        } else {
            int at = valueBox_->findData(v);
            if (at < 0) valueBox_->addItem(QString("Value %1").arg(v), v), at = valueBox_->count() - 1;
            valueBox_->setCurrentIndex(at);
        }
        return;
    }
    if (tool_ == TOOL_TRIGGERS && paintValue() == 0 && valueBox_->currentData().toInt() == 0) {
        QMessageBox::information(this, "Paint triggers",
                                 "Slot 0 facing S is the byte 0, which means no trigger. Pick another facing for slot 0.");
        return;
    }
    strokeMap_ = map;
    strokeBefore_ = m.px;
    strokeRect_ = QRect();
    strokeStart_ = p;
    rectStroke_ = mods & Qt::ShiftModifier;
    if (rectStroke_) {
        rectPreview_->setRect(QRectF(p, p).normalized());
        rectPreview_->setVisible(true);
    } else paintAt(p);
}

void MainWindow::moveStroke(QPointF p) {
    if (strokeMap_ < 0) return;
    if (rectStroke_) {
        rectPreview_->setRect(QRectF(strokeStart_, p).normalized());
        return;
    }
    /* along the way from the last point, a brush every half size */
    QPointF from = strokeStart_;
    double len = std::hypot(p.x() - from.x(), p.y() - from.y());
    int step = std::max(1, sizeBox_->currentData().toInt() / 2);
    for (double t = step; t < len; t += step) paintAt(from + (p - from) * (t / len));
    paintAt(p);
    strokeStart_ = p;
}

void MainWindow::endStroke(QPointF p) {
    if (strokeMap_ < 0) return;
    if (rectStroke_) {
        rectPreview_->setVisible(false);
        QRectF r = QRectF(strokeStart_, p).normalized();
        fillRect(QRect(QPoint((int)std::floor(r.left()), (int)std::floor(r.top())), QPoint((int)std::floor(r.right()), (int)std::floor(r.bottom()))),
                 paintValue());
    }
    int map = strokeMap_;
    strokeMap_ = -1;
    if (strokeRect_.isNull()) return;
    const LogicMap &m = field_.maps[map];
    QRect r = strokeRect_;
    std::vector<uint8_t> before((size_t)r.width() * r.height()), after(before.size());
    for (int y = 0; y < r.height(); y++) {
        memcpy(&before[(size_t)y * r.width()], &strokeBefore_[(size_t)(r.top() + y) * m.w + r.left()], r.width());
        memcpy(&after[(size_t)y * r.width()], &m.px[(size_t)(r.top() + y) * m.w + r.left()], r.width());
    }
    strokeBefore_.clear();
    if (before == after) return;
    static const char *WHAT[] = {"Paint the floor", "Paint the floor", "", "", "Paint battle areas", "", "", "Paint triggers"};
    mapEdited_[map] = true;
    undo_->push(new PaintCommand(this, map, r, std::move(before), std::move(after), WHAT[map]));
    if (map == MAP_TRIGGERS) {
        computeSlotCells();
        refreshItems();
        fillEntities();
    }
    updateTitle();
}

/* --- saving and testing --- */

/* Writes the field's changes where the port reads them: the tables to
   mods/stages/<stage>.json, the logic maps into mods/<fid>.bin, a copy of the
   game's file with the edited maps in it; what is as the game has it leaves
   no file */
bool MainWindow::save() {
    if (field_.stage.name.empty()) return true;
    QDir().mkpath(QString::fromStdString(files_.dir + "/mods/stages"));
    QString modPath = QString::fromStdString(files_.stageModPath(field_.stage.name));
    std::string json = stage_mod_json(field_.stage, original_);
    if (json.empty()) QFile::remove(modPath);
    else {
        QFile f(modPath);
        if (!f.open(QIODevice::WriteOnly) || f.write(json.data(), (qint64)json.size()) != (qint64)json.size()) {
            QMessageBox::critical(this, "Save", "Can't write " + modPath);
            return false;
        }
    }
    /* the logic maps, by their file */
    std::map<int, std::map<int, std::vector<uint8_t>>> byFile;
    std::set<int> fids;
    for (int i = 0; i < 8; i++) {
        if (!field_.stage.mapFiles[i] || !field_.maps[i].w) continue;
        int fid = field_.stage.mapFiles[i] >> 16, entry = field_.stage.mapFiles[i] & 0xFFFF;
        fids.insert(fid);
        std::vector<uint8_t> orig = files_.readOriginal(fid);
        size_t n;
        const uint8_t *e = archive_entry(orig, entry, &n);
        LogicMap om;
        if (e && om.load(e, n) && om.px == field_.maps[i].px) continue;
        std::string err;
        std::vector<uint8_t> enc = field_.maps[i].encode(&err);
        if (enc.empty()) {
            QMessageBox::critical(this, "Save", "The map can't be saved: " + QString::fromStdString(err) +
                                                    ". Make it simpler (fewer different shapes) and save again.");
            return false;
        }
        byFile[fid][entry] = enc;
    }
    for (int fid : fids) {
        if (byFile[fid].empty()) files_.remove(fid);
        else if (!files_.write(fid, rebuild_archive(files_.readOriginal(fid), byFile[fid]))) {
            QMessageBox::critical(this, "Save", QString("Can't write mods/%1.bin").arg(fid, 4, 10, QChar('0')));
            return false;
        }
    }
    undo_->setClean();
    updateTitle();
    statusBar()->showMessage("Saved: the port loads these changes the next time it enters the field", 6000);
    return true;
}

/* Before the field closes: its unsaved changes saved, dropped, or kept open (false) */
bool MainWindow::maybeSave() {
    if (undo_->isClean() || field_.stage.name.empty()) return true;
    auto r = QMessageBox::question(this, "Unsaved changes", "Save the changes to " + modeName(field_.stage.modes.empty() ? -1 : field_.stage.modes[0].mode) + "?",
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Save) return save();
    if (r == QMessageBox::Discard) {
        undo_->clear();
        return true;
    }
    return false;
}

void MainWindow::revertStage() {
    if (field_.stage.name.empty()) return;
    if (QMessageBox::question(this, "Revert to the game's",
                              "Delete every saved change to this field, and go back to the game's own?") != QMessageBox::Yes)
        return;
    QFile::remove(QString::fromStdString(files_.stageModPath(field_.stage.name)));
    for (int i = 0; i < 8; i++)
        if (field_.stage.mapFiles[i]) files_.remove(field_.stage.mapFiles[i] >> 16);
    QString name = QString::fromStdString(field_.stage.name);
    undo_->clear();
    field_.stage.name.clear();
    openStage(name);
    statusBar()->showMessage("Back to the game's field", 5000);
}

void MainWindow::testInGame() {
    if (field_.stage.modes.empty()) return;
    if (!undo_->isClean() && !save()) return;
    /* the port: the one used last, or next to the data folder */
    QSettings settings;
    QString exe = settings.value("port").toString();
    QDir data(QString::fromStdString(files_.dir));
    for (const QString &c : {data.absoluteFilePath("../dw2003.exe"), data.absoluteFilePath("../build/dw2003.exe")})
        if (exe.isEmpty() || !QFileInfo::exists(exe)) exe = QFileInfo::exists(c) ? c : exe;
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        exe = QFileDialog::getOpenFileName(this, "The native port's dw2003.exe", data.absolutePath(), "dw2003.exe (*.exe)");
        if (exe.isEmpty()) return;
    }
    settings.setValue("port", exe);
    QStringList args{"--field", QString::number(field_.stage.modes[0].mode)};
    if (storyBox_->currentData().toInt() >= 0) args << "--progress" << QString::number(storyBox_->currentData().toInt());
    if (!QProcess::startDetached(exe, args, QFileInfo(exe).absolutePath()))
        QMessageBox::critical(this, "Test in game", "Can't start " + exe);
    else statusBar()->showMessage("The port goes to this field once past its first screen", 6000);
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (maybeSave()) e->accept();
    else e->ignore();
}
