#include "mainwindow.h"
#include "mapview.h"
#include "characters.h"
#include <array>
#include <QAbstractItemView>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRadioButton>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QFormLayout>
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
QString commandName(int op); /* properties.cpp */
/* the slot types whose arg is a mode to go to */
bool leaves(int t) { return slot_leaves(t); }
const char *DIRS[8] = {"S", "SW", "W", "NW", "N", "NE", "E", "SE"};

QImage toQImage(const Image &img) {
    if (!img.w) return QImage();
    return QImage((const uchar *)img.px.data(), img.w, img.h, img.w * 4, QImage::Format_RGBA8888).copy();
}

Image fromQImage(const QImage &q, int w, int h) {
    Image out;
    out.resize(w, h);
    QImage c = q.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < std::min(h, c.height()); y++)
        memcpy(&out.px[(size_t)y * w], c.constScanLine(y), (size_t)std::min(w, c.width()) * 4);
    return out;
}

QColor slotColor(int i) {
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

/* A change to the map as a whole (its size, its background): the whole map
   before and after; the change is done already when it is pushed */
class MapCommand : public QUndoCommand {
  public:
    MapCommand(MainWindow *w, MainWindow::MapState before, MainWindow::MapState after, const QString &what)
        : QUndoCommand(what), w_(w), before_(std::move(before)), after_(std::move(after)) {}
    void redo() override {
        if (first_) first_ = false;
        else w_->setMapState(after_);
    }
    void undo() override { w_->setMapState(before_); }

  private:
    MainWindow *w_;
    MainWindow::MapState before_, after_;
    bool first_ = true;
};

/* A change to the text files: the edited ones before and after */
class TextCommand : public QUndoCommand {
  public:
    TextCommand(MainWindow *w, MainWindow::Texts before, MainWindow::Texts after, const QString &what)
        : QUndoCommand(what), w_(w), before_(std::move(before)), after_(std::move(after)) {}
    void redo() override { w_->setTexts(after_); }
    void undo() override { w_->setTexts(before_); }

  private:
    MainWindow *w_;
    MainWindow::Texts before_, after_;
};

/* --- the window --- */

void MainWindow::rebuildModes() {
    modeStage_.clear();
    for (int i = 0; i < (int)stages_.size(); i++)
        for (auto &m : stages_[i].modes) modeStage_.insert(m.mode, i);
}

MainWindow::MainWindow(const Files &files, std::vector<Stage> stages, const GameText &text, std::vector<int> animFiles)
    : files_(files), stages_(std::move(stages)), text_(text), animFiles_(std::move(animFiles)) {
    setWindowTitle("DW3 Field Editor");
    rebuildModes();
    loadFlagNames();
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
    test->setToolTip("Saves the field and opens the port on it (F5): with an event selected, the port plays it; with a character, an object or a slot, the player starts beside it");
    fileMenu->addSeparator();
    fileMenu->addAction("&New field...", QKeySequence::New, this, &MainWindow::newField)
        ->setToolTip("A field of your own: a copy of this one, or an empty map");
    revertAct_ = fileMenu->addAction("&Revert to the game's...", this, &MainWindow::revertStage);
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
    /* Add: what the right click adds, in the middle of what the map shows */
    QMenu *addMenu = menuBar()->addMenu("&Add");
    connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] {
        addMenu->clear();
        if (field_.stage.name.empty()) return;
        QPointF c = view_->mapToScene(view_->viewport()->rect().center());
        c.setX(std::clamp(c.x(), 0.0, (double)field_.width())), c.setY(std::clamp(c.y(), 0.0, (double)field_.height()));
        addItems(addMenu, c);
    });
    QMenu *charMenu = menuBar()->addMenu("&Characters");
    charMenu->addAction("&Export a template...", this, [this] {
        int cur = selKind_ == KIND_ACTOR && selIndex_ >= 0 && selIndex_ < (int)field_.stage.actors.size() ? field_.stage.actors[selIndex_].id : 0x1B;
        int id = pickCharacter("Export a template: which character?", cur);
        if (id >= 0) exportTemplate(id);
    })->setToolTip("A game character's stand and walk as a sheet to draw over, its layout inside");
    charMenu->addAction("&Blank template...", this, [this] { exportTemplate(-1); })
        ->setToolTip("An empty sheet of the game's standard NPC: a front and a back, a stand and a walk");
    charMenu->addAction("&Import a character...", this, [this] { importCharacter(); })
        ->setToolTip("A filled template as a character of your own, for the fields' characters to be");
    charMenu->addSeparator();
    charMenu->addAction("&Your characters...", this, &MainWindow::manageCharacters)
        ->setToolTip("The characters you imported: place, redraw, export, rename or delete them");
    QMenu *storyMenu = menuBar()->addMenu("&Story");
    storyMenu->addAction("&Flags...", this, &MainWindow::manageFlags)
        ->setToolTip("What the game remembers: every flag the fields and the game use, by what uses it; name yours, or make new ones");
    QMenu *mapMenu = menuBar()->addMenu("&Map");
    mapMenu->addAction("&Resize...", this, &MainWindow::resizeMap)
        ->setToolTip("Add or take away 128-pixel tiles on any side; what is on the map moves with it");
    mapMenu->addSeparator();
    mapMenu->addAction("&Export background...", this, &MainWindow::exportBackground)
        ->setToolTip("The background's three depths as pictures, to draw over");
    mapMenu->addAction("&Import background...", this, &MainWindow::importBackground)
        ->setToolTip("The background from pictures: the three depths exported, or one picture for the whole of it");

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
    QMenu *langMenu = viewMenu->addMenu("Text &language");
    langMenu->setToolTipsVisible(true);
    auto *langGroup = new QActionGroup(this);
    for (int l = 0; l < LANG_COUNT; l++) {
        QAction *a = langMenu->addAction(LANGUAGE_NAMES[l]);
        a->setCheckable(true);
        a->setChecked(l == text_.lang);
        a->setToolTip("The language of the texts shown and written: the lines, the names, the items");
        langGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, l] { setLanguage(l); });
    }
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
    for (int p = 0; p < STORY_POINTS; p++) storyBox_->addItem(storyName(p, true), p);
    storyBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    storyBox_->setMinimumContentsLength(30);
    storyBox_->view()->setMinimumWidth(560);
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
    vramLabel_ = new QLabel(this);
    statusBar()->addWidget(cursorLabel_, 1);
    statusBar()->addPermanentWidget(vramLabel_);
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
        if (ar > 0) parts << (ar == 4 ? QString("event battles (area 4)") : QString("battle area %1").arg(ar));
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

MainWindow::~MainWindow() {
    /* the children outlive the members: a scene cleared on its way out must not select into them */
    for (QObject *c : findChildren<QObject *>()) c->disconnect(this);
}

void MainWindow::selectByName(const QString &what) {
    QStringList p = what.split(':');
    static const QStringList KINDS{"", "actor", "slot", "object", "start", "battles", "event"};
    int kind = (int)KINDS.indexOf(p[0]);
    if (kind > 0) select(kind, p.size() > 1 ? p[1].toInt() : 0, true);
}

void MainWindow::showAbout() {
    QMessageBox box(this);
    box.setWindowTitle("About DW3 Field Editor");
    box.setIconPixmap(QIcon(":/dw3fe.ico").pixmap(96, 96));
    box.setTextFormat(Qt::RichText);
    box.setText(QString("<h2>DW3 Field Editor</h2><p>Version %1</p>"
                        "<p>An editor for the fields of <i>Digimon World 2003</i>, read from your disc, for its native PC port.</p>"
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
    return s.isNew() ? place + " (new)" : place;
}

QString MainWindow::modeName(int mode) const {
    if (!modeStage_.contains(mode)) return QString("mode %1").arg(mode);
    const Stage &s = stages_[modeStage_[mode]];
    for (auto &m : s.modes)
        if (m.mode == mode) {
            QString area = m.area < (int)text_.areas.size() ? QString::fromStdString(text_.areas[m.area]) : "";
            QString place = m.place < (int)text_.places.size() ? QString::fromStdString(text_.places[m.place]) : "";
            if (s.isNew()) place += " (new)";
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
        QString num = s.isNew() ? QString::fromStdString(s.name).mid(3) : QString::fromStdString(s.name).mid(5);
        auto *it = new QTreeWidgetItem(a, {stageTitle(s)});
        QStringList modes;
        for (auto &m : s.modes) modes << QString::number(m.mode);
        it->setToolTip(0, s.isNew() ? QString("%1 · a new field, on %2's code · mode %3").arg(stageTitle(s), QString::fromStdString(s.templ).toUpper(), modes.join(", "))
                                    : QString("%1 · WSTAG%2 · mode %3").arg(stageTitle(s), num, modes.join(", ")));
        it->setData(0, Qt::UserRole, QString::fromStdString(s.name));
        it->setData(0, Qt::UserRole + 1, num);
    }
    stageTree_->sortItems(0, Qt::AscendingOrder);
    /* places with the same name (the two servers' copies, story copies) tell their numbers */
    for (int i = 0; i < stageTree_->topLevelItemCount(); i++) {
        QTreeWidgetItem *a = stageTree_->topLevelItem(i);
        QHash<QString, int> count;
        for (int k = 0; k < a->childCount(); k++) count[a->child(k)->text(0)]++;
        for (int k = 0; k < a->childCount(); k++)
            if (count[a->child(k)->text(0)] > 1)
                a->child(k)->setText(0, a->child(k)->text(0) + "  (" + a->child(k)->data(0, Qt::UserRole + 1).toString() + ")");
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
    Stage edited = savedStage((int)(st - stages_.data()));
    field_ = Field();
    invalidateCodes(); /* the uses of the fields but the open one's */
    savedShiftX_ = savedShiftY_ = 0;
    revertAct_->setText(st->isNew() ? "&Delete this new field..." : "&Revert to the game's...");
    if (!field_.load(files_, edited, &animFiles_)) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, "Can't open the field", QString::fromStdString(field_.error));
        return;
    }
    texts_.clear();
    reloadTalks();
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

Stage MainWindow::savedStage(int index) const {
    Stage s = stages_[index];
    if (s.isNew()) return s; /* its file is the whole of it, read already */
    std::ifstream mod(files_.stageModPath(s.name), std::ios::binary);
    if (mod) {
        std::stringstream ss;
        ss << mod.rdbuf();
        Json j;
        if (Json::parse(ss.str(), j)) s.loadSections(j);
        else statusBar()->showMessage("The field's saved changes can't be read: " + QString::fromStdString(files_.stageModPath(s.name)), 8000);
    }
    return s;
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
        QString who = speakerOf(talkText(t.entry));
        if (!who.isEmpty()) return who;
    }
    return characterLabel(a.id);
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
/* The first version of spot g whose story conditions hold at story point p
   (the others taken as holding), or -1 */
int MainWindow::storyVersion(int g, int p) const {
    for (int i : groups_[g].actors) {
        bool ok = true;
        for (auto &[code, value] : field_.stage.actors[i].conditions) {
            int lo, hi;
            if (code_kind(code) == CODE_STORY && (code_id(code) == p) != (value != 0)) ok = false;
            if (code_kind(code) == CODE_SPECIAL && special_story_range(code_id(code), &lo, &hi) && (p >= lo && p <= hi) != (value != 0)) ok = false;
        }
        if (ok) return i;
    }
    return -1;
}

void MainWindow::applyStoryPoint() {
    int p = storyBox_->currentData().toInt();
    for (int g = 0; g < (int)groups_.size(); g++) {
        int v = p >= 0 ? storyVersion(g, p) : -1;
        showVariant(g, v >= 0 ? v : groups_[g].actors[0]);
        /* no version of the spot is there at this point of the story: faint */
        bool absent = p >= 0 && v < 0;
        for (QGraphicsItem *it : {(QGraphicsItem *)groups_[g].item, groups_[g].badge})
            if (it) it->setOpacity(absent ? 0.3 : 1);
        if (groups_[g].item)
            groups_[g].item->setToolTip(absent ? "Not here at this story point: none of its versions' conditions hold" : "");
    }
}

void MainWindow::buildScene() {
    syncing_ = true;
    eventMarks_.clear();
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

void MainWindow::updateVram() {
    if (field_.stage.name.empty()) return vramLabel_->clear();
    int chars, pages, percent;
    vram_room(files_, field_.stage, field_.imagesData, animFiles_, 8, 40, &chars, &pages, &percent);
    vramLabel_->setText(QString("Sprite memory %1% · room for %2%3 characters · %4 object pages ")
                            .arg(percent).arg(chars).arg(chars == 20 ? "+" : "").arg(pages));
    vramLabel_->setStyleSheet(chars == 0 && pages == 0 ? "color:#e66" : chars < 2 || pages == 0 ? "color:#e0b040" : "");
    vramLabel_->setToolTip("What the field leaves free of the VRAM its characters and objects share (320 x 256 halfwords): "
                           "how many more characters of a game NPC's size (32 x 40) would get an image slot of their own, "
                           "and how many new pages of object images would go (a page holds many pictures)");
}

void MainWindow::fillEntities() {
    updateVram();
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
        QString cond = a.conditions.empty() ? "" : "  — if " + QString::fromStdString(describe_codes(a.conditions, words(), false));
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
    g = group("Events", (int)s.events.size());
    for (int i = 0; i < (int)s.events.size(); i++) {
        const Event &e = s.events[i];
        QString what = QString("Event %1").arg(e.id);
        QString how = e.scripted ? QString("%1 commands").arg(parse_script(e.script).size()) : "code";
        auto *it = new QTreeWidgetItem(g, {what, how});
        it->setData(0, Qt::UserRole, KIND_EVENT), it->setData(0, Qt::UserRole + 1, i);
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
    showEventMarks();
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
                 (kind == KIND_OBJECT && index < (int)s.objects.size()) || (kind == KIND_EVENT && index < (int)s.events.size()) ||
                 kind == KIND_START || kind == KIND_BATTLES;
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
    /* an area painted or cleared: its card in Battles */
    if (map == MAP_AREAS && selKind_ == KIND_BATTLES) showProperties();
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
    /* the selected event's places: where characters are put and walk to, where the camera goes */
    if (selKind_ == KIND_EVENT && selIndex_ >= 0 && selIndex_ < (int)s.events.size()) {
        auto commands = parse_script(s.events[selIndex_].script);
        bool changed = false;
        for (QGraphicsItem *it : eventMarks_) {
            if (!it->data(2).isValid()) continue;
            int k = it->data(2).toInt(), ax = it->data(3).toInt();
            int x = std::clamp(qRound(it->pos().x()), 0, field_.width()), y = std::clamp(qRound(it->pos().y()), 0, field_.height());
            if (k < (int)commands.size() && (commands[k].args[ax] != x || commands[k].args[ax + 1] != y))
                commands[k].args[ax] = x, commands[k].args[ax + 1] = y, changed = true;
        }
        if (changed) s.events[selIndex_].script = build_script(commands), moved = true;
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
    } else if (selKind_ == KIND_EVENT && selIndex_ >= 0 && selIndex_ < (int)s.events.size()) {
        int used = 0;
        for (const Slot &t : s.slots) used += t.type == 8 && (int16_t)t.arg == s.events[selIndex_].id;
        if (used && QMessageBox::question(this, "Delete an event",
                                          QString("%1 slots start this event: they'd start nothing. Delete it?").arg(used)) !=
                        QMessageBox::Yes)
            return;
        s.events.erase(s.events.begin() + selIndex_);
        changeTables(s, "Delete an event");
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

void MainWindow::addItems(QMenu *menu, QPointF p) {
    menu->setToolTipsVisible(true);
    int x = qRound(p.x()), y = qRound(p.y());
    menu->addAction("Add a character...", [this, p] { addCharacterAt(p); })->setToolTip("Pick one by its look; it gets an image slot of its own");
    if (!field_.stage.objects.empty())
        menu->addAction("Add an object (a copy of the last)", [this, x, y] {
            Stage s = field_.stage;
            Object o = s.objects.back();
            o.x = x, o.y = y, o.visible = 1;
            s.objects.push_back(o);
            changeTables(s, "Add an object");
            select(KIND_OBJECT, (int)s.objects.size() - 1, false);
        });
    menu->addAction("Add an object from a picture...", [this, p] { addPictureObject(p); })
        ->setToolTip("A sprite of your own: a picture, or the frames of an animation side by side");
    menu->addAction("Add an object from another field...", [this, p] { addLibraryObject(p); })
        ->setToolTip("Any object of the game's fields, copied into this one");
    menu->addAction("Add an event", [this, p] { addEvent(p); })->setToolTip("A script that a slot of type Event or a line starts");
    menu->addAction("Add a slot (exit, climb, event...)", [this] {
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
    })->setToolTip("Then paint its cells with Paint triggers");
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
    addItems(&menu, p);
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
        for (int a = 1; a <= 3; a++) valueBox_->addItem(QString("Battle area %1").arg(a), a);
        valueBox_->addItem("Event battles (area 4)", 4);
        valueBox_->setItemData(4, "The battles events start; walking on it starts them at random too", Qt::ToolTipRole);
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
    /* an area painted or erased: its card in Battles */
    if (map == MAP_AREAS && selKind_ == KIND_BATTLES) showProperties();
    updateTitle();
}

/* --- saving and testing --- */

/* Writes the field's changes where the port reads them: the tables to
   mods/stages/<stage>.json, the logic maps into mods/<fid>.bin, a copy of the
   game's file with the edited maps in it; what is as the game has it leaves
   no file */
static bool write_text(const QString &path, const std::string &text) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(text.data(), (qint64)text.size()) == (qint64)text.size();
}

bool MainWindow::save() {
    if (field_.stage.name.empty()) return true;
    const Stage &st = field_.stage;
    QDir().mkpath(QString::fromStdString(files_.dir + "/mods/stages"));
    /* the logic maps first, by their file: the game's file with the edited
       maps in it (a new field's is its own copy) */
    std::map<int, std::map<int, std::vector<uint8_t>>> byFile;
    std::set<int> fids;
    for (int i = 0; i < 8; i++) {
        if (!st.mapFiles[i] || !field_.maps[i].w) continue;
        int fid = st.mapFiles[i] >> 16, entry = st.mapFiles[i] & 0xFFFF;
        fids.insert(fid);
        std::vector<uint8_t> orig = files_.readOriginal(fid);
        size_t n;
        const uint8_t *e = archive_entry(orig, entry, &n);
        LogicMap om;
        if (e && om.load(e, n) && om.w == field_.maps[i].w && om.px == field_.maps[i].px) continue;
        std::string err;
        std::vector<uint8_t> enc = field_.maps[i].encode(&err);
        if (enc.empty()) {
            QMessageBox::critical(this, "Save", "The map can't be saved: " + QString::fromStdString(err) +
                                                    ". Make it simpler (fewer different shapes) and save again.");
            return false;
        }
        byFile[fid][entry] = enc;
    }
    /* the sprite sheet, an entry of the same file: with the frames imported
       (now or before) whenever it isn't the game's */
    if (st.sheetEntry) {
        size_t n, on = 0;
        std::vector<uint8_t> orig = files_.readOriginal(st.sheetEntry >> 16);
        const uint8_t *e = archive_entry(field_.sheetFile_, st.sheetEntry & 0xFFFF, &n);
        const uint8_t *o = archive_entry(orig, st.sheetEntry & 0xFFFF, &on);
        if (e && !(o && on >= n && memcmp(e, o, n) == 0)) {
            byFile[st.sheetEntry >> 16][st.sheetEntry & 0xFFFF].assign(e, e + n);
            fids.insert(st.sheetEntry >> 16);
        }
    }
    /* the objects' images, to the field's own file */
    if (field_.imagesEdited && !files_.write(st.imageFile, field_.imagesData)) {
        QMessageBox::critical(this, "Save", QString("Can't write mods/%1.bin").arg(st.imageFile, 4, 10, QChar('0')));
        return false;
    }
    for (int fid : fids) {
        std::vector<uint8_t> base = files_.readOriginal(fid);
        if (byFile[fid].empty() && !base.empty()) files_.remove(fid);
        else if (!byFile[fid].empty() && !files_.write(fid, rebuild_archive(base.empty() ? files_.read(fid) : base, byFile[fid]))) {
            QMessageBox::critical(this, "Save", QString("Can't write mods/%1.bin").arg(fid, 4, 10, QChar('0')));
            return false;
        }
    }
    /* the background, to the field's own file */
    if (field_.bgEdited && st.mapFile && !files_.write(st.mapFile, field_.mapFileData())) {
        QMessageBox::critical(this, "Save", QString("Can't write mods/%1.bin").arg(st.mapFile, 4, 10, QChar('0')));
        return false;
    }
    /* the texts: each file edited, the game's again when it is the same; the
       events' are entries of archives */
    std::map<int, std::map<int, std::vector<uint8_t>>> entries;
    for (auto &[key, strings] : texts_)
        if ((key & 0xFFFF) != 0) entries[key >> 16][(key & 0xFFFF) - 1] = build_text_file(strings, false);
    for (auto &[fid, repl] : entries) {
        std::vector<uint8_t> data = rebuild_archive(files_.read(fid), repl), orig = files_.readOriginal(fid);
        if (data == orig) files_.remove(fid);
        else if (!files_.write(fid, data)) {
            QMessageBox::critical(this, "Save", QString("Can't write mods/%1.bin").arg(fid, 4, 10, QChar('0')));
            return false;
        }
    }
    for (auto &[key, strings] : texts_) {
        if (key & 0xFFFF) continue;
        int fid = key >> 16;
        std::vector<uint8_t> data = build_text_file(strings), orig = files_.readOriginal(fid);
        if (data == orig) files_.remove(fid);
        else if (!files_.write(fid, data)) {
            QMessageBox::critical(this, "Save", QString("Can't write mods/%1.bin").arg(fid, 4, 10, QChar('0')));
            return false;
        }
    }
    /* the tables: a new field's file is the whole of it */
    QString modPath = QString::fromStdString(files_.stageModPath(st.name));
    std::string json = st.isNew() ? new_field_json(st) : stage_mod_json(st, original_);
    if (json.empty()) QFile::remove(modPath);
    else if (!write_text(modPath, json)) {
        QMessageBox::critical(this, "Save", "Can't write " + modPath);
        return false;
    }
    if (st.isNew()) {
        original_ = st;
        stages_[modeStage_[st.modes[0].mode]] = st;
    }
    /* the exits of every field that arrive here, moved as the map's content moved */
    int dx = field_.shiftX - savedShiftX_, dy = field_.shiftY - savedShiftY_, moved = 0;
    for (int k = 0; (dx || dy) && k < (int)stages_.size(); k++) {
        if (stages_[k].name == st.name) continue;
        Stage s = savedStage(k);
        bool changed = false;
        for (Slot &t : s.slots)
            for (auto &m : st.modes)
                if (slot_leaves(t.type) && (int16_t)t.arg == m.mode) {
                    t.x = (t.x + dx) & 0xFFFF, t.y = (t.y + dy) & 0xFFFF;
                    changed = true, moved++;
                    break;
                }
        if (!changed) continue;
        QString path = QString::fromStdString(files_.stageModPath(s.name));
        std::string j = s.isNew() ? new_field_json(s) : stage_mod_json(s, stages_[k]);
        if (s.isNew()) stages_[k] = s;
        if (j.empty()) QFile::remove(path);
        else if (!write_text(path, j)) {
            QMessageBox::critical(this, "Save", "Can't write " + path);
            return false;
        }
    }
    savedShiftX_ = field_.shiftX, savedShiftY_ = field_.shiftY;
    undo_->setClean();
    updateTitle();
    statusBar()->showMessage(QString("Saved: the port loads these changes the next time it enters the field%1")
                                 .arg(moved ? QString("; %1 exits of other fields that arrive here moved with the map").arg(moved) : ""),
                             8000);
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
    const Stage &st = field_.stage;
    if (st.isNew()) {
        int arrive = 0;
        for (int k = 0; k < (int)stages_.size(); k++)
            for (const Slot &t : savedStage(k).slots) arrive += slot_leaves(t.type) && (int16_t)t.arg == st.modes[0].mode && stages_[k].name != st.name;
        if (QMessageBox::question(this, "Delete this new field",
                                  QString("Delete %1 and its files for good?%2")
                                      .arg(modeName(st.modes[0].mode),
                                           arrive ? QString("\n\n%1 exits of other fields lead here: change them, or the game "
                                                            "hangs when they are used.").arg(arrive)
                                                  : "")) != QMessageBox::Yes)
            return;
        QFile::remove(QString::fromStdString(files_.stageModPath(st.name)));
        files_.remove(own_tiles_fid(st));
        files_.remove(own_tiles_fid(st) + 1);
        files_.remove(own_images_fid(st));
        QString templ = QString::fromStdString(st.templ);
        stages_.erase(stages_.begin() + modeStage_[st.modes[0].mode]);
        rebuildModes();
        undo_->clear();
        field_.stage.name.clear();
        stageTree_->clear();
        buildStageTree();
        openStage(templ);
        statusBar()->showMessage("The new field is deleted", 5000);
        return;
    }
    if (QMessageBox::question(this, "Revert to the game's",
                              "Delete every saved change to this field, and go back to the game's own?") != QMessageBox::Yes)
        return;
    QFile::remove(QString::fromStdString(files_.stageModPath(st.name)));
    for (int i = 0; i < 8; i++)
        if (st.mapFiles[i]) files_.remove(st.mapFiles[i] >> 16);
    files_.remove(own_tiles_fid(st));
    files_.remove(own_images_fid(st));
    QString name = QString::fromStdString(st.name);
    undo_->clear();
    field_.stage.name.clear();
    openStage(name);
    statusBar()->showMessage("Back to the game's field", 5000);
}

void MainWindow::testInGame() {
    if (field_.stage.modes.empty()) return;
    if (files_.disc) {
        /* the editor's own folder, made from the disc: the port plays its own data folder */
        QMessageBox::information(this, "Test in game",
                                 "Test in game runs the native port, with the changes in its data folder: start the editor "
                                 "with the port's data folder (dw3fe --data DIR) to test there.");
        return;
    }
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
    if (int p = storyBox_->currentData().toInt(); p >= 0) {
        args << "--progress" << QString::number(p);
        /* the story point view takes a version's flags and items as holding:
           the game gets the ones they need set (a fresh game has none), unless
           another version shown needs them clear */
        std::set<int> need, clear;
        for (int g = 0; g < (int)groups_.size(); g++)
            if (int v = storyVersion(g, p); v >= 0)
                for (auto &[code, value] : field_.stage.actors[v].conditions)
                    if ((is_flag(code) && code_kind(code) != 0) || (code_kind(code) >= CODE_ITEM && code_kind(code) <= 0x8E) ||
                        code_kind(code) == CODE_CARD)
                        (value ? need : clear).insert(code);
        for (int code : need)
            if (!clear.count(code)) args << "--set" << QString("%1=1").arg(code, 4, 16, QChar('0'));
    }
    /* an event selected: the port starts it once the field runs */
    if (selKind_ == KIND_EVENT && selIndex_ >= 0 && selIndex_ < (int)field_.stage.events.size())
        args << "--event" << QString::number(field_.stage.events[selIndex_].id);
    /* a character, an object or a slot's cells selected: the player starts beside it */
    QPoint at(-1, -1);
    const Stage &s = field_.stage;
    if (selKind_ == KIND_ACTOR && selIndex_ >= 0 && selIndex_ < (int)s.actors.size() && actorGroup_[selIndex_] >= 0)
        at = QPoint(s.actors[selIndex_].x, s.actors[selIndex_].y + 24);
    else if (selKind_ == KIND_OBJECT && selIndex_ >= 0 && selIndex_ < (int)s.objects.size())
        at = QPoint(s.objects[selIndex_].x, s.objects[selIndex_].y + 24);
    else if (selKind_ == KIND_SLOT && selIndex_ >= 0 && selIndex_ < (int)slotCells_.size() && !slotCells_[selIndex_].isNull())
        at = slotCells_[selIndex_].center() + QPoint(0, slotCells_[selIndex_].height() / 2 + 16);
    if (at.x() >= 0) args << "--at" << QString("%1,%2").arg(std::clamp(at.x(), 0, field_.width() - 1)).arg(std::clamp(at.y(), 0, field_.height() - 1));
    if (!QProcess::startDetached(exe, args, QFileInfo(exe).absolutePath()))
        QMessageBox::critical(this, "Test in game", "Can't start " + exe);
    else statusBar()->showMessage("The port goes to this field once past its first screen", 6000);
}

/* --- the texts --- */

std::vector<std::vector<uint8_t>> MainWindow::textStrings(int key) const {
    auto it = texts_.find(key);
    if (it != texts_.end()) return it->second;
    int fid = key >> 16, entry = (key & 0xFFFF) - 1;
    std::vector<uint8_t> f = files_.read(fid);
    if (entry < 0) return read_raw_strings(f);
    size_t n;
    const uint8_t *e = archive_entry(f, entry, &n);
    return e ? read_raw_strings(std::vector<uint8_t>(e, e + n)) : std::vector<std::vector<uint8_t>>();
}

bool MainWindow::untranslated(const std::function<int(int lang)> &keyOf, int entry) const {
    std::vector<std::vector<uint8_t>> first = textStrings(keyOf(0));
    if (entry < 0 || entry >= (int)first.size()) return false;
    /* the game's own strings, from its file */
    int key = keyOf(0), fid = key >> 16, sub = (key & 0xFFFF) - 1;
    std::vector<uint8_t> f = files_.readOriginal(fid);
    size_t n;
    const uint8_t *e = sub < 0 ? nullptr : archive_entry(f, sub, &n);
    size_t game = sub < 0 ? read_raw_strings(f).size() : e ? read_raw_strings(std::vector<uint8_t>(e, e + n)).size() : 0;
    if (entry < (int)game) return false;
    for (int l = 1; l < LANG_COUNT; l++) {
        std::vector<std::vector<uint8_t>> other = textStrings(keyOf(l));
        if (entry >= (int)other.size() || other[entry] != first[entry]) return false;
    }
    return true;
}

int MainWindow::talkKey(int lang) const { return (field_.stage.textFile + lang) << 16; }

/* an event's text: the archive (its fid less the language, plus LANGUAGE: 2
   English) and its entry */
int MainWindow::eventKey(int text, int lang) const { return ((text >> 16) + 2 + lang) << 16 | ((text & 0xFFFF) + 1); }

void MainWindow::setTexts(const Texts &t) {
    texts_ = t;
    reloadTalks();
    refreshItems();
    fillEntities();
    showProperties();
    updateTitle();
}

/* The field's lines in the language shown, and the widest line they have */
void MainWindow::reloadTalks() {
    talks_.clear();
    textLimit_ = 0;
    if (field_.stage.name.empty()) return;
    for (auto &s : textStrings(talkKey(text_.lang))) {
        talks_.push_back(decode_text(s.data(), s.size()));
        for (auto &page : text_widths(s, 0))
            for (int w : page) textLimit_ = std::max(textLimit_, w);
    }
}

void MainWindow::setLanguage(int lang) {
    text_.load(files_, lang);
    invalidateCodes();
    for (int i = 1; i < storyBox_->count(); i++) storyBox_->setItemText(i, storyName(storyBox_->itemData(i).toInt(), true));
    QSettings().setValue("language", lang);
    reloadTalks();
    stageTree_->clear();
    buildStageTree();
    refreshItems();
    fillEntities();
    showProperties();
    updateTitle();
    statusBar()->showMessage(QString("Texts in %1").arg(LANGUAGE_NAMES[lang]), 4000);
}

bool MainWindow::textDialog(GameString &g, const QString &title, const QString &note) {
    QDialog d(this);
    d.setWindowTitle(title);
    d.resize(560, 420);
    auto *form = new QFormLayout(&d);
    form->addRow(richLabel(note));
    auto *speaker = new QLineEdit(QString::fromStdString(g.speaker), &d);
    speaker->setPlaceholderText("nobody: a sign, a thought");
    form->addRow("Speaker", speaker);
    auto *text = new QPlainTextEdit(QString::fromStdString(g.text), &d);
    text->setFont(QFont("Consolas", 10));
    form->addRow("Text", text);
    form->addRow(richLabel("<span style='color:#999'>A line break is a new line, an empty line a new page; the box shows "
                           "three lines a page. {player} is the player's name; other {..} are codes the editor keeps as "
                           "they are.</span>"));
    auto *check = new QLabel(&d);
    check->setWordWrap(true);
    form->addRow(check);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto update = [&] {
        std::string err;
        GameString now{speaker->text().toStdString(), text->toPlainText().toStdString()};
        std::vector<uint8_t> b = text_to_bytes(now, &err);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(err.empty());
        if (!err.empty()) {
            check->setText("<span style='color:#f66'>Can't be written: " + QString::fromStdString(err).toHtmlEscaped() + "</span>");
            return;
        }
        QStringList warn;
        auto pages = text_widths(b);
        for (int p = 0; p < (int)pages.size(); p++) {
            if ((int)pages[p].size() > TALK_LINES)
                warn << QString("Page %1 has %2 lines: the game turns the page after the third.").arg(p + 1).arg(pages[p].size());
            for (int l = 0; l < (int)pages[p].size(); l++)
                if (textLimit_ && pages[p][l] > textLimit_)
                    warn << QString("Page %1, line %2 is %3 pixels wide; the widest of this field's texts is %4.")
                                .arg(p + 1).arg(l + 1).arg(pages[p][l]).arg(textLimit_);
        }
        check->setText(warn.isEmpty() ? "<span style='color:#7c7'>Fits the talk box.</span>"
                                      : "<span style='color:#fc6'>" + warn.join("<br>") + "</span>");
    };
    connect(speaker, &QLineEdit::textChanged, &d, update);
    connect(text, &QPlainTextEdit::textChanged, &d, update);
    update();
    if (d.exec() != QDialog::Accepted) return false;
    g = {speaker->text().toStdString(), text->toPlainText().toStdString()};
    return true;
}

void MainWindow::editString(int key, int entry, const QString &note) {
    auto strings = textStrings(key);
    if (entry < 0 || entry >= (int)strings.size()) return;
    GameString g = bytes_to_text(strings[entry]);
    if (!textDialog(g, "Edit the text", note)) return;
    std::vector<uint8_t> b = text_to_bytes(g);
    if (b == strings[entry]) return;
    Texts after = texts_;
    after[key] = strings;
    after[key][entry] = b;
    undo_->push(new TextCommand(this, texts_, after, "Edit a text"));
}

int MainWindow::appendString(const std::function<int(int lang)> &keyOf, const GameString &start, const QString &note,
                             const std::function<void(int entry)> &then, bool ask) {
    GameString g = start;
    if (ask && !textDialog(g, "New text", note)) return -1;
    std::vector<uint8_t> b = text_to_bytes(g);
    Texts after = texts_;
    int entry = -1;
    for (int l = 0; l < LANG_COUNT; l++) {
        int key = keyOf(l);
        after[key] = textStrings(key);
        if (entry < 0) entry = (int)after[key].size();
        /* the same entry in every language */
        if ((int)after[key].size() != entry) {
            QMessageBox::warning(this, "New text", "The languages' copies of the text file differ in length.");
            return -1;
        }
        after[key].push_back(b);
    }
    undo_->beginMacro("New text");
    undo_->push(new TextCommand(this, texts_, after, "New text"));
    if (then) then(entry);
    undo_->endMacro();
    return entry;
}

void MainWindow::editTalkText(int entry) {
    /* who else says it: the file is shared by many fields */
    int uses = 0, fields = 0;
    for (int k = 0; k < (int)stages_.size(); k++) {
        if (stages_[k].textFile != field_.stage.textFile) continue;
        const Stage s = stages_[k].name == field_.stage.name ? field_.stage : savedStage(k);
        int before = uses;
        for (auto &a : s.actors)
            for (auto &t : a.talks) uses += t.entry == entry;
        fields += uses > before;
    }
    QString note = QString("Text %1 of the field's text file, in %2.").arg(entry).arg(LANGUAGE_NAMES[text_.lang]);
    if (uses > 1 || fields > 1) note += QString(" <b>%1 lines in %2 fields say it</b>: they all change.").arg(uses).arg(fields);
    editString(talkKey(text_.lang), entry, note);
}

void MainWindow::newTalkText(int actor, int talk) {
    GameString g;
    /* the speaker of the line it replaces */
    auto strings = textStrings(talkKey(text_.lang));
    int e = field_.stage.actors[actor].talks[talk].entry;
    if (e >= 0 && e < (int)strings.size()) g.speaker = bytes_to_text(strings[e]).speaker;
    appendString([this](int l) { return talkKey(l); }, g,
                 "A new text at the end of the field's text file, for this line. It goes to every language as you write "
                 "it here: switch the text language (View) to translate it.",
                 [this, actor, talk](int entry) {
                     Stage s = field_.stage;
                     s.actors[actor].talks[talk].entry = entry;
                     changeTables(s, "Change a line's text");
                 });
}

int MainWindow::addName(int english, const QString &name, QString *err) {
    std::string e;
    std::vector<uint8_t> b = text_to_bytes({"", name.toStdString() + "{nowait}"}, &e);
    if (!e.empty()) {
        *err = QString::fromStdString(e);
        return -1;
    }
    int index = -1;
    for (int l = 0; l < LANG_COUNT; l++) {
        auto strings = read_raw_strings(files_.read(english + l));
        if (index < 0) index = (int)strings.size();
        if ((int)strings.size() != index) {
            *err = "the languages' name files differ in length";
            return -1;
        }
        strings.push_back(b);
        if (!files_.write(english + l, build_text_file(strings))) {
            *err = QString("can't write mods/%1.bin").arg(english + l);
            return -1;
        }
    }
    text_.load(files_, text_.lang);
    return index;
}

/* --- the events --- */

void MainWindow::addEvent(QPointF at) {
    Stage s = field_.stage;
    int id = EDITOR_EVENTS;
    for (bool used = true; used && id < EDITOR_EVENTS + 0x100;) {
        used = false;
        for (const Event &e : s.events) used |= e.id == id;
        id += used;
    }
    if (id >= EDITOR_EVENTS + 0x100) {
        QMessageBox::information(this, "Add an event", "The field has every event the editor can number.");
        return;
    }
    /* the texts of the field's events, if it has any */
    Event e;
    e.id = id, e.scripted = true;
    for (const Event &o : s.events)
        if (o.text && !e.text) e.text = o.text;
    /* the player walks to where it was asked for, and the event ends */
    e.script = build_script({{0x102, {CHARACTER_PLAYER, qRound(at.x()), qRound(at.y()), 0}}, {0x302, {CHARACTER_PLAYER}}});
    s.events.push_back(e);
    changeTables(s, "Add an event");
    select(KIND_EVENT, (int)s.events.size() - 1, false);
}

void MainWindow::addPictureObject(QPointF at) {
    if (field_.stage.name.empty()) return;
    QSettings settings;
    QString path = QFileDialog::getOpenFileName(this, "A picture for the object", settings.value("pictures", QDir::homePath()).toString(),
                                                "Pictures (*.png *.bmp)");
    if (path.isEmpty()) return;
    settings.setValue("pictures", QFileInfo(path).absolutePath());
    QImage pic(path);
    if (pic.isNull()) {
        QMessageBox::warning(this, "Add an object", "Can't read " + path);
        return;
    }
    /* the frames as they'll be cut, each one's anchor (its bottom middle, where you clicked) and the colors */
    QDialog d(this);
    d.setWindowTitle("Add an object");
    auto *v = new QVBoxLayout(&d);
    auto *view = new QLabel;
    view->setAlignment(Qt::AlignCenter);
    view->setMinimumSize(320, 160);
    std::set<QRgb> colors;
    QImage rgba = pic.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < rgba.height(); y++)
        for (int x = 0; x < rgba.width(); x++)
            if (qAlpha(rgba.pixel(x, y)) >= 128) colors.insert(rgba.pixel(x, y) | 0xFF000000);
    auto *row = new QHBoxLayout;
    auto *count = new QSpinBox;
    count->setRange(1, 32);
    count->setSuffix(" frames side by side");
    row->addWidget(count);
    row->addStretch();
    auto *info = new QLabel;
    info->setWordWrap(true);
    v->addWidget(view, 1);
    v->addLayout(row);
    v->addWidget(info);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto draw = [&] {
        int n = count->value(), fw = pic.width() / n;
        int scale = std::clamp(512 / std::max(1, pic.width()), 1, 4);
        QPixmap pm(pic.width() * scale, pic.height() * scale);
        pm.fill(QColor(60, 60, 60));
        QPainter p(&pm);
        p.drawImage(QRect(0, 0, pm.width(), pm.height()), pic);
        p.setPen(QPen(QColor(255, 200, 60), 1, Qt::DashLine));
        for (int i = 1; i < n; i++) p.drawLine(i * fw * scale, 0, i * fw * scale, pm.height());
        p.setPen(QPen(QColor(255, 80, 80), 2));
        for (int i = 0; i < n; i++) {
            int ax = (i * fw + fw / 2) * scale, ay = pm.height() - 1;
            p.drawLine(ax - 5, ay, ax + 5, ay), p.drawLine(ax, ay - 8, ax, ay);
        }
        p.end();
        view->setPixmap(pm);
        info->setText(QString("%1 frames of %2 × %3 pixels, each standing on its red mark (where you clicked)%4. %5 colors%6.")
                          .arg(n).arg(fw).arg(pic.height())
                          .arg(pic.width() % n ? QString(", %1 pixels on the right left out").arg(pic.width() % n) : QString())
                          .arg(colors.size())
                          .arg(colors.size() > 15 ? ": they'll be cut down to 15, so some will merge" : ""));
    };
    connect(count, &QSpinBox::valueChanged, &d, draw);
    draw();
    if (d.exec() == QDialog::Accepted) addObjectFromFile(path, count->value(), at);
}

bool MainWindow::addObjectFromFile(const QString &path, int count, QPointF at, QString *errOut) {
    QImage pic(path);
    if (pic.isNull() || count < 1) {
        if (errOut) *errOut = "can't read " + path;
        return false;
    }
    int fw = pic.width() / count;
    std::vector<Image> frames;
    for (int i = 0; i < count; i++) frames.push_back(fromQImage(pic.copy(i * fw, 0, fw, pic.height()), fw, pic.height()));
    return addObjectFrames("Add an object from a picture", frames, at, 8, 5, 64, errOut);
}

bool MainWindow::addObjectFrames(const QString &what, const std::vector<Image> &frames, QPointF at, int delay, int depth,
                                 int margin, QString *errOut) {
    int count = (int)frames.size();
    return changeMap(what, [&](QString *err) {
        std::string e;
        field_.vramTaken = field_vram(files_, field_.stage, field_.imagesData, animFiles_, false);
        int id = field_.importSprite(frames, &e);
        if (id < 0) {
            *err = "The picture can't be added: " + QString::fromStdString(e) + ".";
            if (errOut) *errOut = *err, *err = QString(); /* told to the caller, not in a box */
            return false;
        }
        /* the images are the field's own file from now on */
        Stage &s = field_.stage;
        s.imageFile = own_images_fid(s), s.imageEntry = 0;
        Object o{1, 0, margin, depth, id, count > 1, id, id + count - 1, delay, 0, qRound(at.x()), qRound(at.y()), 0, 0};
        s.objects.push_back(o);
        return true;
    });
}

/* The places the selected event's script names: where characters are put
   and walk to, and where the camera goes, numbered by step */
void MainWindow::showEventMarks() {
    for (QGraphicsItem *it : eventMarks_) delete it;
    eventMarks_.clear();
    if (selKind_ != KIND_EVENT || selIndex_ < 0 || selIndex_ >= (int)field_.stage.events.size()) return;
    auto commands = parse_script(field_.stage.events[selIndex_].script);
    QPainterPath path;
    for (int k = 0; k < (int)commands.size(); k++) {
        const Command &c = commands[k];
        int ax = -1;
        if (c.op == 0x100 || c.op == 0x102) ax = 1;
        else if (c.op == 0x601) ax = 1;
        if (ax < 0 || (int)c.args.size() <= ax + 1) continue;
        QPointF p(c.args[ax], c.args[ax + 1]);
        QColor col = c.op == 0x601 ? QColor(120, 200, 255) : c.op == 0x100 ? QColor(255, 200, 60) : QColor(255, 120, 200);
        auto *dot = scene_->addEllipse(-7, -7, 14, 14, QPen(Qt::black, 1), col);
        dot->setPos(p);
        dot->setZValue(1500);
        dot->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        dot->setToolTip(QString("%1. %2 %3 (drag to move it)").arg(k + 1).arg(commandName(c.op)).arg(c.op == 0x601 ? "" : characterName(c.args[0])));
        /* dragged, its command's place changes (commitMoves) */
        dot->setFlag(QGraphicsItem::ItemIsMovable, tool_ == TOOL_SELECT);
        dot->setCursor(Qt::SizeAllCursor);
        dot->setData(2, k), dot->setData(3, ax);
        auto *num = new QGraphicsSimpleTextItem(QString::number(k + 1), dot);
        num->setFont(QFont("Segoe UI", 7, QFont::Bold));
        num->setPos(-num->boundingRect().width() / 2, -num->boundingRect().height() / 2);
        eventMarks_.push_back(dot);
        if (c.op != 0x601) path.isEmpty() ? path.moveTo(p) : path.lineTo(p);
    }
    if (!path.isEmpty()) {
        auto *line = scene_->addPath(path, QPen(QColor(255, 120, 200, 200), 0, Qt::DashLine));
        line->setZValue(1499);
        eventMarks_.push_back(line);
    }
}

/* --- the map as a whole --- */

MainWindow::MapState MainWindow::mapState() const {
    MapState m{field_.tilesW, field_.tilesH, {field_.tiles[0], field_.tiles[1], field_.tiles[2]}, field_.tileData, {},
               field_.stage, field_.shiftX, field_.shiftY, field_.bgEdited, field_.sheetFile_, field_.imagesData,
               field_.sheetEdited, field_.imagesEdited};
    for (int i = 0; i < 8; i++) m.maps[i] = field_.maps[i];
    return m;
}

void MainWindow::setMapState(const MapState &m) {
    field_.tilesW = m.tilesW, field_.tilesH = m.tilesH;
    for (int d = 0; d < 3; d++) field_.tiles[d] = m.tiles[d];
    field_.tileData = m.tileData;
    for (int i = 0; i < 8; i++) {
        mapEdited_[i] |= field_.maps[i].px != m.maps[i].px;
        field_.maps[i] = m.maps[i];
    }
    field_.stage = m.stage;
    field_.shiftX = m.shiftX, field_.shiftY = m.shiftY, field_.bgEdited = m.bgEdited;
    field_.sheetFile_ = m.sheetFile, field_.imagesData = m.imagesData;
    field_.sheetEdited = m.sheetEdited, field_.imagesEdited = m.imagesEdited;
    field_.reloadImages(files_, &animFiles_);
    int kind = selKind_, index = selIndex_;
    computeSlotCells();
    buildActorGroups();
    buildScene();
    fillEntities();
    select(kind, index, false);
    refreshPaintOptions();
    updateTitle();
    view_->fit();
}

bool MainWindow::changeMap(const QString &what, const std::function<bool(QString *err)> &change) {
    MapState before = mapState();
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = change(&err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        setMapState(before);
        if (!err.isEmpty()) QMessageBox::warning(this, what, err);
        return false;
    }
    /* the background is the field's own file from now on (the game's are shared) */
    if (field_.bgEdited && field_.stage.mapFile && !field_.stage.isNew()) field_.stage.mapFile = own_tiles_fid(field_.stage);
    undo_->push(new MapCommand(this, std::move(before), mapState(), what));
    setMapState(mapState());
    return true;
}

bool MainWindow::canEditBackground() {
    if (field_.stage.name.empty()) return false;
    if (!field_.stage.mapFile) {
        QMessageBox::information(this, "Background", "This field has no background of its own: the game draws its "
                                                     "map with the field it belongs to.");
        return false;
    }
    return true;
}

void MainWindow::resizeMap() {
    if (field_.stage.name.empty()) return;
    QDialog d(this);
    d.setWindowTitle("Resize the map");
    auto *form = new QFormLayout(&d);
    form->addRow(richLabel(QString("The map is <b>%1 × %2</b> tiles of 128 pixels (%3 × %4). Add tiles on a side, or take "
                                   "them away with a negative number.<br>What stands on the map moves with it, and so do the "
                                   "exits of every field that arrive here.")
                               .arg(field_.tilesW).arg(field_.tilesH).arg(field_.width()).arg(field_.height())));
    QSpinBox *side[4];
    static const char *SIDES[4] = {"Left", "Top", "Right", "Bottom"};
    for (int k = 0; k < 4; k++) {
        side[k] = new QSpinBox(&d);
        side[k]->setRange(-((k & 1) ? field_.tilesH : field_.tilesW) + 1, 64);
        form->addRow(SIDES[k], side[k]);
    }
    auto *result = new QLabel(&d);
    form->addRow("Result", result);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto update = [&] {
        int w = field_.tilesW + side[0]->value() + side[2]->value(), h = field_.tilesH + side[1]->value() + side[3]->value();
        bool ok = w >= 1 && h >= 1 && w * h <= MAX_TILES;
        result->setText(QString("%1 × %2 tiles (%3 × %4 pixels)%5")
                            .arg(w).arg(h).arg(w * 128).arg(h * 128)
                            .arg(ok ? "" : QString(" — too big: the game takes at most %1 tiles").arg(MAX_TILES)));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(ok && (side[0]->value() || side[1]->value() || side[2]->value() || side[3]->value()));
    };
    for (QSpinBox *s : side) connect(s, &QSpinBox::valueChanged, &d, update);
    update();
    if (d.exec() != QDialog::Accepted) return;
    int l = side[0]->value(), t = side[1]->value(), r = side[2]->value(), b = side[3]->value();
    changeMap("Resize the map", [&](QString *) {
        field_.resize(l, t, r, b);
        return true;
    });
}

/* The depths' pictures' names: base-back.png, base-middle.png, base-front.png (TILE_DEPTHS' order: 7, 3, 11) */
static const char *DEPTH_NAMES[3] = {"middle", "front", "back"};

void MainWindow::exportBackground() {
    if (!canEditBackground()) return;
    QSettings settings;
    QString start = QDir(settings.value("pictures", QDir::homePath()).toString()).filePath(QString::fromStdString(field_.stage.name) + ".png");
    QString path = QFileDialog::getSaveFileName(this, "Export the background", start, "PNG pictures (*.png)");
    if (path.isEmpty()) return;
    QString base = path.endsWith(".png", Qt::CaseInsensitive) ? path.left(path.size() - 4) : path;
    settings.setValue("pictures", QFileInfo(path).absolutePath());
    QStringList written;
    for (int d = 0; d < 3; d++) {
        QString p = base + "-" + DEPTH_NAMES[d] + ".png";
        if (!toQImage(field_.layerImage(d)).save(p)) {
            QMessageBox::critical(this, "Export the background", "Can't write " + p);
            return;
        }
        written << QFileInfo(p).fileName();
    }
    QMessageBox::information(this, "Export the background",
                             QString("Written: %1.\n\n"
                                     "back: the ground, behind everything\n"
                                     "middle: in front of the ground, behind the characters (unless a depth trigger puts "
                                     "the player behind it)\n"
                                     "front: over the characters (roofs, treetops)\n\n"
                                     "Draw over them, keep the sizes, and import any of the three: the editor reads the "
                                     "others next to it. Only the tiles that changed are made again.")
                                 .arg(written.join(", ")));
}

void MainWindow::importBackground() {
    if (!canEditBackground()) return;
    QSettings settings;
    QString path = QFileDialog::getOpenFileName(this, "Import the background", settings.value("pictures", QDir::homePath()).toString(),
                                                "Pictures (*.png *.bmp *.jpg)");
    if (path.isEmpty()) return;
    settings.setValue("pictures", QFileInfo(path).absolutePath());
    /* an exported set (base-back/middle/front.png): each one there; else one picture, the whole background */
    QImage pics[3];
    QRegularExpressionMatch m = QRegularExpression("^(.*)-(back|middle|front)\\.png$", QRegularExpression::CaseInsensitiveOption).match(path);
    bool set = m.hasMatch();
    if (set)
        for (int d = 0; d < 3; d++) pics[d] = QImage(m.captured(1) + "-" + DEPTH_NAMES[d] + ".png");
    else pics[0] = QImage(path);
    QSize size;
    for (auto &p : pics)
        if (!p.isNull()) size = size.expandedTo(p.size());
    if (size.isEmpty()) {
        QMessageBox::warning(this, "Import the background", "Can't read " + path);
        return;
    }
    int w = (size.width() + 127) / 128, h = (size.height() + 127) / 128;
    if (w != field_.tilesW || h != field_.tilesH) {
        QMessageBox box(QMessageBox::Question, "Import the background",
                        QString("The picture is %1 × %2 pixels, the map %3 × %4.")
                            .arg(size.width()).arg(size.height()).arg(field_.width()).arg(field_.height()),
                        QMessageBox::Cancel, this);
        QPushButton *grow = box.addButton(QString("Make the map %1 × %2 tiles").arg(w).arg(h), QMessageBox::AcceptRole);
        QPushButton *fit = box.addButton("Fit the picture to the map", QMessageBox::AcceptRole);
        fit->setToolTip("Scaled to cover the map, its middle kept");
        if (w * h > MAX_TILES) grow->setEnabled(false);
        box.exec();
        if (box.clickedButton() == fit) {
            /* scaled to cover the map, the overflow cut evenly */
            for (auto &p : pics) {
                if (p.isNull()) continue;
                QImage q = p.scaled(field_.width(), field_.height(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                p = q.copy((q.width() - field_.width()) / 2, (q.height() - field_.height()) / 2, field_.width(), field_.height());
            }
            w = field_.tilesW, h = field_.tilesH;
        } else if (box.clickedButton() != grow) return;
    }
    int changed = 0;
    if (!changeMap("Import the background", [&](QString *err) {
            if (w != field_.tilesW || h != field_.tilesH) field_.resize(0, 0, w - field_.tilesW, h - field_.tilesH);
            Image layers[3];
            const Image *use[3] = {};
            for (int d = 0; d < 3; d++) {
                /* one picture is the whole background: the other depths are cleared */
                if (!pics[d].isNull() || !set) layers[d] = fromQImage(pics[d], field_.width(), field_.height());
                if (layers[d].w) use[d] = &layers[d];
            }
            std::string e;
            if (!field_.importLayers(use, &e, &changed)) return *err = QString::fromStdString(e), false;
            return true;
        }))
        return;
    statusBar()->showMessage(QString("%1 tiles made from the pictures").arg(changed), 8000);
}

void MainWindow::newField() {
    if (field_.stage.name.empty()) return;
    int mode = free_field_mode(stages_);
    if (!mode) {
        QMessageBox::information(this, "New field", "Every mode a new field can take is used.");
        return;
    }
    QDialog d(this);
    d.setWindowTitle("New field");
    auto *form = new QFormLayout(&d);
    QString templ = QString::fromStdString(field_.stage.isNew() ? field_.stage.templ : field_.stage.name).toUpper();
    form->addRow(richLabel(QString("A field of your own, mode %1 (0x%2). It runs the code of <b>%3</b>, the field open now: "
                                   "its sprites, music and texts, and its events, which check the story as they do there.")
                               .arg(mode).arg(mode, 0, 16).arg(templ)));
    auto names = [&](const std::vector<std::string> &list, int current) {
        auto *c = new QComboBox(&d);
        c->setEditable(true);
        c->setInsertPolicy(QComboBox::NoInsert);
        for (int i = 0; i < (int)list.size(); i++)
            if (!list[i].empty()) c->addItem(QString::fromStdString(list[i]), i);
        c->completer()->setFilterMode(Qt::MatchContains);
        c->completer()->setCompletionMode(QCompleter::PopupCompletion);
        c->setCurrentIndex(std::max(0, c->findData(current)));
        return c;
    };
    const Stage::Mode cur = field_.stage.modes.empty() ? Stage::Mode{} : field_.stage.modes[0];
    QComboBox *area = names(text_.areas, cur.area), *place = names(text_.places, cur.place);
    area->setToolTip("The area's name the banner shows on arrival: one of the game's, or a new one typed here");
    place->setToolTip("The place's name the banner shows on arrival: one of the game's, or a new one typed here");
    form->addRow("Area name", area);
    form->addRow("Place name", place);
    auto *copy = new QRadioButton("A copy of this field: its map, characters, objects and exits", &d);
    auto *empty = new QRadioButton("An empty map, walkable all over:", &d);
    copy->setChecked(true);
    auto *w = new QSpinBox(&d), *h = new QSpinBox(&d);
    w->setRange(1, MAX_TILES), h->setRange(1, MAX_TILES);
    w->setValue(std::max(3, field_.tilesW)), h->setValue(std::max(2, field_.tilesH));
    w->setSuffix(" tiles wide"), h->setSuffix(" tiles high");
    auto *sizeRow = new QHBoxLayout;
    sizeRow->addWidget(w);
    sizeRow->addWidget(h);
    form->addRow(copy);
    form->addRow(empty);
    form->addRow("", sizeRow);
    /* the way in: one of this field's exits, led to it */
    auto *link = new QComboBox(&d);
    link->addItem("None yet (lead an exit to it later)", -1);
    for (int i = 0; i < (int)field_.stage.slots.size(); i++) {
        const Slot &t = field_.stage.slots[i];
        if (slot_leaves(t.type)) link->addItem(QString("Slot %1: %2, now to %3").arg(i).arg(slotType(t.type)).arg(modeName((int16_t)t.arg)), i);
    }
    link->setToolTip("One of this field's exits leads to the new field instead, arriving at its start: the way to reach it in the game");
    form->addRow("Reach it from", link);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto update = [&] {
        w->setEnabled(empty->isChecked()), h->setEnabled(empty->isChecked());
        buttons->button(QDialogButtonBox::Ok)->setEnabled(copy->isChecked() || w->value() * h->value() <= MAX_TILES);
    };
    connect(empty, &QRadioButton::toggled, &d, update);
    connect(w, &QSpinBox::valueChanged, &d, update);
    connect(h, &QSpinBox::valueChanged, &d, update);
    update();
    if (d.exec() != QDialog::Accepted) return;
    /* a name typed that the game hasn't: a new one, in every language's names file */
    int nameAt[2];
    QComboBox *boxes[2] = {area, place};
    const int FILES[2] = {AREA_NAMES_FILE, PLACE_NAMES_FILE};
    for (int k = 0; k < 2; k++) {
        QString typed = boxes[k]->currentText().trimmed();
        int at = boxes[k]->findText(typed);
        nameAt[k] = at >= 0 ? boxes[k]->itemData(at).toInt() : -1;
        if (nameAt[k] >= 0) continue;
        QString e;
        if ((nameAt[k] = addName(FILES[k], typed, &e)) < 0) {
            QMessageBox::critical(this, "New field", "The name \"" + typed + "\" can't be written: " + e);
            return;
        }
    }
    NewField nf;
    std::string err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = make_new_field(files_, field_, mode, nameAt[0], nameAt[1],
                             empty->isChecked() ? w->value() : 0, empty->isChecked() ? h->value() : 0, &nf, &err) &&
              write_new_field(files_, nf, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(this, "New field", QString::fromStdString(err));
        return;
    }
    stages_.push_back(nf.stage);
    rebuildModes();
    int from = link->currentData().toInt();
    if (from >= 0) {
        /* the exit, saved with this field: it arrives at the new one's start */
        Stage s = field_.stage;
        s.slots[from].arg = mode & 0xFFFF;
        s.slots[from].x = (nf.stage.startX >> 8) & 0xFFFF, s.slots[from].y = (nf.stage.startY >> 8) & 0xFFFF;
        s.slots[from].dir = nf.stage.startDir;
        changeTables(s, "Lead an exit to a new field");
        save();
    }
    stageTree_->clear();
    buildStageTree();
    openStage(QString::fromStdString(nf.stage.name));
    statusBar()->showMessage(from >= 0 ? QString("New field made: slot %1 of the field before leads to it").arg(from)
                                       : "New field made. Lead an exit of another field to it to reach it in the game",
                             10000);
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (maybeSave()) e->accept();
    else e->ignore();
}
