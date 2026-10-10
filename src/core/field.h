/* A field (a stage of FIELDSTG) as the editor holds it: its tables, from
   the port's stage dump (dw2003 --dump-stages), and the map, read from the
   game's files: the background's 128-pixel tiles, the logic maps (floor,
   triggers, battle areas) and the objects' sprites. */
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "json.h"

/* The game's files: DIR/mods/NNNN.bin wins over DIR/files/NNNN.bin, as in the port */
struct Files {
    std::string dir;
    std::vector<uint8_t> read(int fid) const;
    /* the game's own file, without the mods */
    std::vector<uint8_t> readOriginal(int fid) const;
    /* DIR/mods/NNNN.bin, or false */
    bool write(int fid, const std::vector<uint8_t> &data) const;
    bool remove(int fid) const;
    /* DIR/mods/stages/<stage>.json: the editor's changes to a stage's tables */
    std::string stageModPath(const std::string &stage) const { return dir + "/mods/stages/" + stage + ".json"; }
};

/* An archive with some of its entries replaced (by index): the same table,
   the entries one after another, 4-aligned, the file padded to 2048 bytes */
std::vector<uint8_t> rebuild_archive(const std::vector<uint8_t> &a, const std::map<int, std::vector<uint8_t>> &replace);

/* Entry i of an archive (a table of u32 offsets from its start), or nullptr */
const uint8_t *archive_entry(const std::vector<uint8_t> &a, int i, size_t *size = nullptr);
/* RLEN-packed data unpacked; plain data as it is */
std::vector<uint8_t> unrlen(const uint8_t *p, size_t n);

/* An RGBA picture, 0 for transparent */
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    void resize(int width, int height) { w = width, h = height, px.assign((size_t)w * h, 0); }
};

/* The PSX's 1024x512 VRAM, for the sprites drawn through texture pages and CLUTs */
struct Vram {
    std::vector<uint16_t> px = std::vector<uint16_t>(1024 * 512);
    uint16_t &at(int x, int y) { return px[(y & 511) * 1024 + (x & 1023)]; }
    void loadTim(const uint8_t *tim, int ix, int iy, int cx, int cy);
    /* every TIM of an archive side by side, 0x40 halfwords apart (timLoaderLoadArchive) */
    void loadArchive(const uint8_t *archive, size_t size, int ix, int iy, int cx, int cy);
};

uint32_t rgba15(uint16_t c, int r = 128, int g = 128, int b = 128);

/* What a logic map holds at each pixel (FIELDSTG_getMapCell), unpacked */
struct LogicMap {
    int w = 0, h = 0; /* in pixels */
    std::vector<uint8_t> px;
    bool load(const uint8_t *entry, size_t size);
    /* the file entry that load reads back: the tree of cells, each level's
       blocks shared where they are the same. Empty, with err, when the map
       has more distinct 64 or 128-pixel cells than the format's bytes hold */
    std::vector<uint8_t> encode(std::string *err = nullptr) const;
    int at(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h ? px[(size_t)y * w + x] : -1; }
};

/* The tables of the stage dump */
struct Object { int visible, anim, margin, depth, frame, cycle, cycleFirst, cycleLast, cycleDelay, clutRow, x, y, sortY, cycleTime; };
struct Slot { int cond[2][2], type, arg, x, y, dir, hideAnim, place, placeArg; };
struct Talk { int entry; std::vector<std::pair<int, int>> conditions, actions; };
struct Actor { int id, image, x, y, dir; std::vector<std::pair<int, int>> conditions; std::vector<Talk> talks; };
struct Battle { int encounter, stage, music; };
struct Area { int rate; Battle battles[8]; };
struct Place { int serial, id; Area areas[4]; };
struct Event { int id, script, text; };

struct Stage {
    std::string name;
    /* the modes that run it, with the banner's names (text indices); extra:
       only in the European version's extra chapter (FIELDSTG_stages, not
       FIELDSTG_euStages) */
    struct Mode { int mode, file, area, place; bool extra; };
    std::vector<Mode> modes;
    int mapFile = 0, sheetEntry = 0, imageEntry = 0, imageFile = 0, textFile = 0, soundBank = 0, music = 0;
    int startX = 0, startY = 0, startDir = 0, color[4] = {};
    int mapFiles[8] = {};
    std::vector<Object> objects;
    std::vector<Slot> slots;
    std::vector<Actor> actors;
    std::vector<std::array<int, 8>> images; /* ActorImage: 0 and 1 the field's, then the characters' */
    std::vector<Place> places;
    std::vector<Event> events;
    bool load(const Json &j);
    /* the editable sections present in j (a dump or a mod): objects, slots,
       actors, images, start, startDir, places */
    void loadSections(const Json &j);
};

/* The editor's changes to a stage, as the port reads them
   (native/platform/stagemods.c): the sections where `edited` differs from
   `original`, in the dump's form; "" when none does */
std::string stage_mod_json(const Stage &edited, const Stage &original);
/* A section of a stage in the dump's form */
std::string section_json(const Stage &s, const std::string &section);

/* The logic maps an editor shows, by FieldMap index */
enum { MAP_FLOOR0 = 0, MAP_FLOOR1 = 1, MAP_AREAS = 4, MAP_TRIGGERS = 7 };

/* A sprite drawn from a sheet: its picture and where its top left goes from
   the point it is drawn at */
struct Sprite {
    Image img;
    int ox = 0, oy = 0;
};

/* The map's three tile layers, by the order tables' depths: the background
   tiles' sprites go to 7, 3 and 11 (FIELDSTG_spriteDepths) */
static const int TILE_DEPTHS[3] = {7, 3, 11};

struct Field {
    Stage stage;
    int tilesW = 0, tilesH = 0; /* in 128-pixel tiles */
    std::vector<Image> tiles[3]; /* each depth's tiles, tilesW * tilesH, empty for none */
    LogicMap maps[8];
    std::vector<Sprite> objects; /* by stage.objects */
    std::vector<Sprite> actors;  /* by stage.actors: the standing frame, facing their way */
    Vram vram;
    std::string error;
    std::vector<uint8_t> sheetFile_, spritesFile_; /* the objects' sheets */

    /* animFiles: each character's animation file entry (characters.json), for the actors' sprites */
    bool load(const Files &files, const Stage &stage, const std::vector<int> *animFiles = nullptr);
    /* the objects' and characters' sprites again, after stage's tables changed */
    void refreshSprites(const Files &files, const std::vector<int> *animFiles);
    int width() const { return tilesW * 128; }
    int height() const { return tilesH * 128; }
};

/* A character's first standing frame (ACTOR_ANIM_STAND) facing dir (0-7, 5-7
   the mirror of 3-1), as FIELDSTG_animateActor and FIELDSTG_drawActor do */
Sprite actor_sprite(const Files &files, int animFile, int dir);

/* Frame `frame` of a sprite sheet drawn as spriteDrawerDraw would, with VRAM's
   textures: the drawer's texture page (tx, ty), its other CLUT area (ax, ay)
   and CLUT row, tinted by color (128 neutral) */
Sprite draw_sheet_frame(const uint8_t *sheet, size_t size, int frame, Vram &vram, int tx, int ty, int ax, int ay,
                        int clutRow, const int color[3]);
