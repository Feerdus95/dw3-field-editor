#pragma once
#include <QHash>
#include <QImage>
#include <QMainWindow>
#include <QRect>
#include <functional>
#include <map>
#include <vector>
#include "characters.h"
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

QImage toQImage(const Image &img);
/* A template sheet as a PNG with its layout inside; false with err */
bool writeTemplateSheet(const QString &path, const Image &sheet, const CharTemplate &t, QString *err);
/* A template sheet and its layout: the PNG's, a .json beside it (older
   sheets), or the standard's when it is its size. TEMPLATE_NONE with err
   when the picture can't be read or has no layout */
enum { TEMPLATE_NONE, TEMPLATE_READ };
int readTemplateSheet(const QString &path, Image *sheet, CharTemplate *t, QString *err);
/* the speaker's name of a line as it reads ("Name: text"), or "" */
QString speakerOf(const std::string &line);
/* A picture as the editor's, w x h: what it lacks is clear */
Image fromQImage(const QImage &q, int w, int h);

/* What a map item is, in its data(0) and data(1) */
enum ItemKind { KIND_NONE, KIND_ACTOR, KIND_SLOT, KIND_OBJECT, KIND_START, KIND_BATTLES, KIND_EVENT };

/* The layers the user turns on and off */
enum Layer { L_BACKGROUND, L_OBJECTS, L_FLOOR, L_TRIGGERS, L_AREAS, L_ACTORS, L_EXITS, L_START, LAYER_COUNT };

/* The tools: selecting and moving, or painting one of the logic maps */
enum Tool { TOOL_SELECT, TOOL_FLOOR, TOOL_TRIGGERS, TOOL_AREAS, TOOL_COUNT };

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(const Files &files, std::vector<Stage> stages, const GameText &text, std::vector<int> animFiles);
    ~MainWindow() override;
    void openStage(const QString &name);
    /* an object at `at` showing a picture (count frames side by side), an
       undo step; false (and why, shown or in err) when it can't be */
    bool addObjectFromFile(const QString &path, int count, QPointF at, QString *err = nullptr);
    bool save();
    /* a character at `at` with an image slot (giveSlot) and a line of its own (asked for, unless
       ask is false: "..."), an undo step, selected; false with err ("" when cancelled) */
    bool addCharacter(int id, QPointF at, QString *err, bool ask = true);
    /* selects "actor:N", "slot:N", "object:N" or "start" (the command line's --select) */
    void selectByName(const QString &what);

    /* the undo commands' way in */
    void setTables(const Stage &s);
    void setMapRegion(int map, const QRect &r, const std::vector<uint8_t> &px);
    /* the whole map: its grid, background, logic maps and tables */
    struct MapState {
        int tilesW, tilesH;
        std::vector<Image> tiles[3];
        std::vector<std::vector<uint8_t>> tileData;
        LogicMap maps[8];
        Stage stage;
        int shiftX, shiftY;
        bool bgEdited;
        std::vector<uint8_t> sheetFile, imagesData;
        bool sheetEdited, imagesEdited;
    };
    MapState mapState() const;
    void setMapState(const MapState &m);
    /* the text files edited, by fid: their strings */
    using Texts = std::map<int, std::vector<std::vector<uint8_t>>>;
    void setTexts(const Texts &t);

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
    bool maybeSave();
    void revertStage();
    void testInGame();

    /* the map as a whole (Map menu) and new fields */
    void resizeMap();
    void importBackground();
    void exportBackground();
    void newField();
    /* applies change to the map as one undo step; false (and nothing changed) when it fails */
    bool changeMap(const QString &what, const std::function<bool(QString *err)> &change);
    bool canEditBackground();
    /* the stage with its saved changes; a new field's file is the whole of it */
    Stage savedStage(int index) const;
    void rebuildModes();

    /* the texts: a text file's strings by its key, fid << 16 for a text file,
       and | entry + 1 for one in an archive (the events') */
    std::vector<std::vector<uint8_t>> textStrings(int key) const;
    /* the key of the field's talk file, and of an event's texts, in a language */
    int talkKey(int lang) const;
    int eventKey(int text, int lang) const;
    void reloadTalks();
    void setLanguage(int lang);
    /* the dialog that writes a string; false when cancelled */
    bool textDialog(GameString &g, const QString &title, const QString &note);
    /* edits string entry of a text file (an undo step) */
    void editString(int key, int entry, const QString &note);
    /* a new string at the end of a text file, in every language (written in a dialog, unless
       ask is false): its entry, or -1; then runs in the same undo step (to use the entry) */
    int appendString(const std::function<int(int lang)> &keyOf, const GameString &start, const QString &note,
                     const std::function<void(int entry)> &then = nullptr, bool ask = true);
    void editTalkText(int entry);
    void newTalkText(int actor, int talk);
    /* a name added to a names file (AREA_NAMES_FILE...) in every language, written at once: its index, or -1 */
    int addName(int english, const QString &name, QString *err);

    /* the events */
    QString characterName(int id) const;
    void addEvent(QPointF at);
    void addPictureObject(QPointF at);
    /* the characters (characters_ui.cpp): each one's look and name, from
       the game's fields (who says its lines, where it stands) or yours */
    struct CharacterInfo {
        int id;
        QString name;
        QStringList fields;
        QPixmap thumb;
        bool mine;
    };
    const std::vector<CharacterInfo> &characters() const;
    mutable std::vector<CharacterInfo> characters_; /* built when first asked; cleared when they change */
    QString characterLabel(int id) const;            /* "Name (0x1b)", or "Character 0x1b" */
    QPixmap characterThumb(int id) const;
    /* a character picked by its look: its id, or -1 */
    int pickCharacter(const QString &title, int current, bool onlyMine = false);
    /* actor k of s an image slot: a character of the same look's, else one of
       its own where VRAM has room (own: always); false with err (its slot as it was) */
    bool giveSlot(Stage &s, int k, QString *err, bool own = false);
    /* the other looks drawn through an actor's image slot */
    QStringList slotSharers(int actor) const;
    void addCharacterAt(QPointF at);
    void placeCharacter(int id);
    /* a template of a character, -1 for the blank standard */
    void exportTemplate(int id);
    /* a filled template as a new character, or replace's new drawings */
    void importCharacter(int replace = -1);
    void manageCharacters();
    void showEventMarks();
    std::vector<QGraphicsItem *> eventMarks_;

    /* the properties (properties.cpp) */
    void showProperties();
    void actorForm(QFormLayout *f, int index);
    void slotForm(QFormLayout *f, int index);
    void objectForm(QFormLayout *f, int index);
    void startForm(QFormLayout *f);
    void stageForm(QFormLayout *f);
    void battlesForm(QFormLayout *f);
    void eventForm(QFormLayout *f, int index);
    QLabel *richLabel(const QString &html);
    void linkClicked(const QString &link);

    Files files_;
    std::vector<Stage> stages_; /* as the game has them (the dump) */
    GameText text_;
    std::vector<int> animFiles_; /* by character: its animation file entry */
    QHash<int, int> modeStage_; /* mode -> index in stages_ */
    int savedShiftX_ = 0, savedShiftY_ = 0; /* field_.shift at the last save: the exits arriving here are moved by the rest */

    Field field_;            /* the open field, its tables as edited */
    Stage original_;         /* its tables as the game has them */
    std::vector<std::string> talks_; /* the stage's text file, in the language shown */
    Texts texts_;                    /* the text files edited since the field opened */
    int textLimit_ = 0;              /* the widest line of the stage's text file, in pixels */
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
    QAction *revertAct_ = nullptr;
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
