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
/* An event: its script (FIELDSTG_runEvent's words, the end's 0 last) and
   its text, the archive entry of the event text file (fid << 16 | entry,
   the fid less the language); some have code that starts them instead of a
   script, or that runs at their end */
struct Event {
    int id = 0, text = 0;
    bool scripted = false, start = false, end = false;
    std::vector<int> script;
};

/* One command of an event script: its op (kind << 8 | sub) and arguments */
struct Command {
    int op = 0;
    std::vector<int> args;
};
/* How many arguments an op takes (FIELDSTG_runEvent); -1 for the end */
int command_args(int op);
/* The commands of a script, its end left out */
std::vector<Command> parse_script(const std::vector<int> &script);
/* The script of commands, with its end */
std::vector<int> build_script(const std::vector<Command> &commands);
/* The script commands' ids an event's character can't be (FIELDSTG_findEventActor): 0x320 on */
static const int SCRIPT_COMMANDS = 0x320, FIELD_COMMANDS_TASK = 0x32D;
/* The character ids of the party in the scripts */
static const int CHARACTER_PLAYER = 2, CHARACTER_PARTNERS = 3;
/* The editor's events: 9000 on, started by START_EVENT(0x100 + n) (the port) */
static const int EDITOR_EVENTS = 9000;

/* The slot types whose arg is a mode to go to, and x, y where they arrive there */
inline bool slot_leaves(int type) { return type == 1 || type == 9 || type == 10 || type == 13 || type == 14; }

struct Stage {
    std::string name;
    /* a new field's (the editor's, mods/stages/new<mode>.json): the stage
       whose code it runs; empty for the game's own */
    std::string templ;
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
       actors, images, start, startDir, places, events, and the files a mod
       gives (mapFile, sheetEntry, mapFiles) */
    void loadSections(const Json &j);
    bool isNew() const { return !templ.empty(); }
};

/* The file of the background a field has as its own once changed (the
   game's are shared with the story copies): 3000 + the stage's number, and
   for a new field 4000 + twice its mode's field number, its logic maps' file
   the next one */
int own_tiles_fid(const Stage &s);
/* The file of the objects' images a field has as its own once a sprite is
   imported: 5000 + the stage's number, 6000 + a new field's mode's number */
int own_images_fid(const Stage &s);

/* The editor's changes to a stage, as the port reads them
   (native/platform/stagemods.c): the sections where `edited` differs from
   `original`, in the dump's form; "" when none does */
std::string stage_mod_json(const Stage &edited, const Stage &original);
/* A section of a stage in the dump's form */
std::string section_json(const Stage &s, const std::string &section);
/* A new field's file: the whole stage, with its template */
std::string new_field_json(const Stage &s);

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

/* The background's limits: the streamer names a tile in a byte (0xFF for
   none), and the header holds each tile's size in 16 bits */
static const int MAX_TILES = 255, MAX_TILE_BYTES = 0xF800;

/* A tile of the background from the three depths' pictures (128x128 each,
   by TILE_DEPTHS' order): what can't be seen (under a nearer depth's
   opaque pixels) dropped, the rest cut in up to four strips a depth, packed
   in an 8-bit image of at most 256x128 with its own 256 colors, the
   sprites listed, RLEN-packed and padded to whole sectors. Empty when all
   three are clear; empty with err when it doesn't fit. color: the field's
   sprite color, which the game tints the tile with (taken off here) */
std::vector<uint8_t> encode_tile(const Image layers[3], const int color[4], std::string *err = nullptr);
/* A tile's three depths as the game draws them (FIELDSTG_drawStream), tinted by color */
void decode_tile(const std::vector<uint8_t> &chunk, const int color[4], Image out[3]);

struct Field {
    Stage stage;
    int tilesW = 0, tilesH = 0; /* in 128-pixel tiles */
    std::vector<Image> tiles[3]; /* each depth's tiles, tilesW * tilesH, empty for none */
    std::vector<std::vector<uint8_t>> tileData; /* each tile as the map file has it, empty for none */
    int tileBytes = 2048;                        /* the room of a tile in the file */
    bool bgEdited = false;                       /* the tiles or their grid changed */
    /* how far the map's content moved right and down since the field opened,
       by tiles added or taken on the left and top: the exits that arrive here
       move with it */
    int shiftX = 0, shiftY = 0;
    LogicMap maps[8];
    std::vector<Sprite> objects; /* by stage.objects */
    std::vector<Sprite> actors;  /* by stage.actors: the standing frame, facing their way */
    Vram vram;
    std::string error;
    std::vector<uint8_t> sheetFile_, spritesFile_; /* the objects' sheets */
    /* the objects' images, the archive of pages loaded from VRAM's (0x140,
       0x100) (FieldState.imageFile, or imageEntry's) */
    std::vector<uint8_t> imagesData;
    bool sheetEdited = false, imagesEdited = false;

    /* animFiles: each character's animation file entry (characters.json), for the actors' sprites */
    bool load(const Files &files, const Stage &stage, const std::vector<int> *animFiles = nullptr);
    /* the objects' and characters' sprites again, after stage's tables changed */
    void refreshSprites(const Files &files, const std::vector<int> *animFiles);
    int width() const { return tilesW * 128; }
    int height() const { return tilesH * 128; }

    /* The map file: a header sector (the tiles' count, the grid, the room
       of a tile and each one's size), then each tile in its room */
    std::vector<uint8_t> mapFileData() const;
    /* Tiles added (or, negative, taken) on each side: the background, the
       logic maps (new cells blocked, without triggers or battles) and what
       stands on the map move with the content */
    void resize(int left, int top, int right, int bottom);
    /* The background from three pictures of the map's size (TILE_DEPTHS'
       order, nullptr keeps a depth): only the tiles that differ are encoded
       again. False with err (the tiles that don't fit) and nothing changed */
    bool importLayers(const Image *layers[3], std::string *err, int *changed = nullptr);
    /* A depth of the background as one picture */
    Image layerImage(int depth) const;
    /* VRAM again from the field's sprites and imagesData, and the sprites */
    void reloadImages(const Files &files, const std::vector<int> *animFiles);
    /* Pictures (the frames of an animation, each anchored at its bottom
       middle) as new frames of the sheet, 4-bit with 15 colors of their
       own, in a page of the images the editor made or a new one (at most
       three pages): the first frame's id, or -1 with err */
    int importSprite(const std::vector<Image> &frames, std::string *err);
};

/* A new field from an open one: its stage (named new<mode>, running
   templ's code, the banner's area and place names) and its files, by fid:
   the background and a copy of the logic maps' file. size w x h tiles: an
   empty map (walkable all over, no characters, objects or slots); 0: a copy
   of `from` as it is */
struct NewField {
    Stage stage;
    std::map<int, std::vector<uint8_t>> files;
};
bool make_new_field(const Files &files, const Field &from, int mode, int area, int place, int w, int h, NewField *out,
                    std::string *err);
/* The first mode no stage has, for a new field: 0x2F0 on, then 0x301 on
   (0x300 is the stage select's); 0 when none is left */
int free_field_mode(const std::vector<Stage> &stages);
/* A new field's files to mods/, its stage to mods/stages/<name>.json */
bool write_new_field(const Files &files, const NewField &nf, std::string *err);

/* A character's first standing frame (ACTOR_ANIM_STAND) facing dir (0-7, 5-7
   the mirror of 3-1), as FIELDSTG_animateActor and FIELDSTG_drawActor do */
Sprite actor_sprite(const Files &files, int animFile, int dir);

/* Frame `frame` of a sprite sheet drawn as spriteDrawerDraw would, with VRAM's
   textures: the drawer's texture page (tx, ty), its other CLUT area (ax, ay)
   and CLUT row, tinted by color (128 neutral) */
Sprite draw_sheet_frame(const uint8_t *sheet, size_t size, int frame, Vram &vram, int tx, int ty, int ax, int ay,
                        int clutRow, const int color[3]);
