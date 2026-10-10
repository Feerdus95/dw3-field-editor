#pragma once
#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QRect>
#include <vector>
#include "field.h"
#include "text.h"

class MapView;
class QAction;
class QComboBox;
class QFormLayout;
class QGraphicsItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QLabel;
class QLineEdit;
class QScrollArea;
class QSlider;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoStack;

/* What a map item is, in its data(0) and data(1) */
enum ItemKind { KIND_NONE, KIND_ACTOR, KIND_SLOT, KIND_OBJECT, KIND_START, KIND_BATTLES };

/* The layers the user turns on and off */
enum Layer { L_BACKGROUND, L_OBJECTS, L_FLOOR, L_TRIGGERS, L_AREAS, L_ACTORS, L_EXITS, L_START, LAYER_COUNT };

/* The tools: selecting and moving, or painting one of the logic maps */
enum Tool { TOOL_SELECT, TOOL_FLOOR, TOOL_TRIGGERS, TOOL_AREAS, TOOL_COUNT };

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(const Files &files, std::vector<Stage> stages, const GameText &text, std::vector<int> animFiles);
    void openStage(const QString &name);
    /* selects "actor:N", "slot:N", "object:N" or "start" (the command line's --select) */
    void selectByName(const QString &what);

    /* the undo commands' way in */
    void setTables(const Stage &s);
    void setMapRegion(int map, const QRect &r, const std::vector<uint8_t> &px);

  protected:
    void closeEvent(QCloseEvent *e) override;

  private:
    /* the window */
    void showAbout();
    void buildStageTree();
    QString stageTitle(const Stage &s) const;
    QString modeName(int mode) const;
    void updateTitle();

    /* the map */
    void buildScene();
    void buildItems();
    void refreshItems();
    void buildActorGroups();
    void computeSlotCells();
    void showVariant(int group, int actor);
    void rebuildOverlays();
    void applyLayers();
    void fillEntities();
    void select(int kind, int index, bool center);
    void applyStoryPoint();
    QString actorName(int actor) const;
    bool invisibleActor(int actor) const;
    std::string talkText(int entry) const;

    /* editing */
    void changeTables(const Stage &after, const QString &what);
    void commitMoves();
    void deleteSelection();
    void contextMenu(QPointF p, QPoint global);
    void setTool(int tool);
    void refreshPaintOptions();
    int paintMap() const;
    int paintValue() const;
    void paintAt(QPointF p);
    void beginStroke(QPointF p, Qt::KeyboardModifiers mods);
    void moveStroke(QPointF p);
    void endStroke(QPointF p);
    void fillRect(const QRect &r, int value);
    void updateOverlayRect(int map, const QRect &r);
    bool save();
    bool maybeSave();
    void revertStage();
    void testInGame();

    /* the properties (properties.cpp) */
    void showProperties();
    void actorForm(QFormLayout *f, int index);
    void slotForm(QFormLayout *f, int index);
    void objectForm(QFormLayout *f, int index);
    void startForm(QFormLayout *f);
    void stageForm(QFormLayout *f);
    void battlesForm(QFormLayout *f);
    QLabel *richLabel(const QString &html);
    void linkClicked(const QString &link);

    Files files_;
    std::vector<Stage> stages_; /* as the game has them (the dump) */
    GameText text_;
    std::vector<int> animFiles_; /* by character: its animation file entry */
    QHash<int, int> modeStage_; /* mode -> index in stages_ */

    Field field_;            /* the open field, its tables as edited */
    Stage original_;         /* its tables as the game has them */
    std::vector<std::string> talks_; /* the stage's text file */
    std::vector<QRect> slotCells_;   /* each slot's trigger cells: their bounds */
    bool mapEdited_[8] = {};

    /* the characters by place: the versions of one spot (the game creates the
       first whose conditions hold), each drawn as one item showing one version;
       the ones at 0, 0, which the events place, are in no group */
    struct ActorGroup {
        int x, y;
        std::vector<int> actors;
        int shown; /* the version drawn */
        QGraphicsPixmapItem *item = nullptr;
        QGraphicsItem *badge = nullptr;
    };
    std::vector<ActorGroup> groups_;
    std::vector<int> actorGroup_; /* by actor: its group, or -1 (placed by events) */

    MapView *view_;
    QGraphicsScene *scene_;
    std::vector<QGraphicsItem *> layerItems_[LAYER_COUNT];
    QGraphicsPixmapItem *overlay_[3] = {};
    QImage overlayImg_[3];
    QAction *layerActions_[LAYER_COUNT] = {};
    QSlider *opacity_;
    int floor_ = 0;
    int highlightSlot_ = -1;
    int selKind_ = KIND_NONE, selIndex_ = -1;

    QUndoStack *undo_;
    int tool_ = TOOL_SELECT;
    QAction *toolActions_[TOOL_COUNT] = {};
    QToolBar *paintBar_;
    QComboBox *valueBox_, *facingBox_, *sizeBox_, *storyBox_;
    QAction *facingAction_ = nullptr;
    /* the stroke being painted: its map, the bounds it touched and their
       pixels before; a Shift stroke fills a rectangle */
    int strokeMap_ = -1;
    QRect strokeRect_;
    std::vector<uint8_t> strokeBefore_;
    QPointF strokeStart_;
    bool rectStroke_ = false;
    QGraphicsRectItem *rectPreview_ = nullptr, *brush_ = nullptr;

    QTreeWidget *stageTree_, *entities_;
    QLineEdit *filter_;
    QScrollArea *props_;
    QLabel *cursorLabel_, *zoomLabel_;
    bool syncing_ = false;
};
