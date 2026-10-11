/* Characters of your own, made to the game's standard for its NPCs: a stand
   (set 1) and a walk (set 4), for 5 directions (S, SW, W, NW, N; the game
   mirrors the rest) or, as most of its NPCs, a front (down-left: S, SW, W)
   and a back (up-left: NW, N). Each animation plays a few drawings in an
   order (the walk A B C D E C, the stand A B C B) at its times; each frame a
   4-bit picture of 15 colors, the feet at the anchor, without a shadow (the
   game draws it).

   A template is a sheet of cells, a row per animation and direction and a
   column per drawing, with a JSON of its layout beside it. Exported from a
   game character it shows what to draw; filled in, it is imported as an
   animation file of the character's own. The guides (cell borders, the
   anchor) are faint: what is under half opaque doesn't count. */
#pragma once
#include <string>
#include <vector>
#include "field.h"
#include "json.h"

struct CharTemplate {
    bool full = false; /* 5 directions, or a front and a back */
    int cellW = 32, cellH = 40, anchorX = 16, anchorY = 36, width = 32;
    struct Anim {
        int set;
        std::vector<int> order, times; /* the drawing each frame shows, and for how long */
    };
    std::vector<Anim> anims;
    int dirs() const { return full ? 5 : 2; }
    int drawings(int a) const;
    int cols() const;
    int rows() const { return (int)anims.size() * dirs(); }
    std::string json() const;
    bool parse(const Json &j);
};

/* The standard: a front and a back, a stand of 3 drawings and a walk of 5, at the game's usual times */
CharTemplate standard_template();
/* What a row is called: "Walk, front" */
std::string row_name(const CharTemplate &t, int row);

/* A game character's stand and walk as a template sheet; false with err when it has neither */
bool export_character(const Files &files, int animFile, int width, CharTemplate *t, Image *sheet, std::string *err);
/* the same from the animation file's bytes */
bool export_character(const std::vector<uint8_t> &file, int entry, int width, CharTemplate *t, Image *sheet, std::string *err);
/* An empty sheet of a template, with its guides */
Image blank_sheet(const CharTemplate &t);

/* A filled sheet as an animation file. Every cell must be drawn and have
   at most 15 colors; problems lists what isn't so. reuse: an empty cell
   takes the drawing before it in its row (a deliberate exception) */
std::vector<uint8_t> import_character(const Image &sheet, const CharTemplate &t, bool reuse, std::vector<std::string> *problems);

/* What the field takes of VRAM's (320..639, 256..511), by halfword (320 x
   256): its objects' pages (with them or not), the field's sprites, and each
   character image slot's frames (as big as its characters' biggest) and
   palette; a slot no character takes is free. imagesData: the objects' pages */
std::vector<bool> field_vram(const Files &files, const Stage &st, const std::vector<uint8_t> &imagesData, const std::vector<int> &animFiles,
                             bool objectPages);
/* A character's biggest frame, in halfwords and lines */
void character_size(const Files &files, int animFile, int *w, int *h);
/* A character image slot of its own for frames of w halfwords by h lines,
   where the field leaves VRAM free: the number Actor.image takes, or -1 */
int add_character_slot(Stage &st, const std::vector<bool> &taken, int w, int h);
/* The room a field leaves in VRAM: how many more characters of frames w x h
   would get a slot of their own (up to 20), how many new pages of object
   images would go (Field::importSprite: three pages at most, side by side),
   and the share of the 320 x 256 taken, in percent */
void vram_room(const Files &files, const Stage &st, const std::vector<uint8_t> &imagesData, const std::vector<int> &animFiles,
               int w, int h, int *characters, int *pages, int *percent);

/* The editor's characters: ids from the game's count on, their files 7000 + id - CHARACTERS */
static const int GAME_CHARACTERS = 406, CHARACTER_FILES = 7000;
/* The editor's characters, as mods/characters.json lists them for the port */
struct OwnCharacter {
    int id, animFile, width;
    std::string name;
};
std::vector<OwnCharacter> read_own_characters(const Files &files);
bool write_own_characters(const Files &files, const std::vector<OwnCharacter> &list, std::string *err);
/* An animation file as a new character: its file to mods/, its entry to
   the list, the number past all the others (first: the count of the
   game's characters' files). The id, or -1 with err */
int register_character(const Files &files, const std::vector<uint8_t> &file, int width, int first, const std::string &name,
                       std::string *err);
/* A character of the editor's out of the list, and its file */
bool delete_character(const Files &files, int id, std::string *err);
