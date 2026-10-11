/* The condition and action codes of the fields' tables (FLAGS_00's
   checkCondition and applyAction): what each one is, in words, and what the
   game's own tables and code say about them (the story's steps, the special
   conditions, the flags its code sets) */
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

using Codes = std::vector<std::pair<int, int>>;

/* The kinds of code, code >> 8 & 0xFE (items: 0x80-0x8E) */
enum {
    CODE_FLAG = 0x00, /* 0x00-0x40, the flag groups */
    CODE_STORY = 0x60,
    CODE_SPECIAL = 0x70,
    CODE_PARTY_STAT = 0x72,
    CODE_EVENT_BATTLE = 0x74,
    CODE_CARD_BATTLE = 0x76, /* and 0x78 */
    CODE_WARP_ARG = 0x7E,
    CODE_ITEM = 0x80,
    CODE_START_EVENT = 0x90,
    CODE_CARD = 0x92,
};
inline int code_kind(int code) { return code >> 8 & 0xFE; }
inline int code_id(int code) { return code & 0x1FF; }

/* The flags: group (even, 0x00-0x40) << 8 | bit (9 bits). Group 0x00 is
   FLAGS_00's, which a field change may clear; the others are saved */
inline bool is_flag(int code) { return code >= 0 && code < 0x4100; }
/* the bits a group has (the European GAME.flagsNN), 0 for a group the game doesn't have */
int flag_group_bits(int group);
/* "1A:1F" */
std::string flag_ref(int code);
/* A field's flag, set as the player enters it (FIELD_VISITED_FLAG): its
   mode, or -1. Modes 0x200-0x2EF have one */
inline int visited_mode(int code) { return code >= 0x2000 && code < 0x20F0 ? 0x200 + (code - 0x2000) : -1; }
inline int visited_flag(int mode) { return mode >= 0x200 && mode < 0x2F0 ? 0x2000 + mode - 0x200 : -1; }

/* The story's points (GAME.progress): 0 to 0x2D, the extra chapter's last */
static const int STORY_POINTS = 0x2E;
/* where the story moves on to a point: the stage, the event whose end does
   it, who is in it ("" for the player alone) and what it gives */
struct StoryStep {
    int point;
    const char *stage;
    int event;
    const char *who, *gift;
};
const StoryStep *story_step(int point);

/* a use of a flag by the game's code: it sets it to value at the end of an event, or checks it */
struct GameFlagUse {
    int code, set, value;
    const char *stage; /* "" for the field's own code */
    int event;         /* -1: the stage's code as it runs */
};
std::vector<GameFlagUse> game_flag_uses(int code);
/* every flag the game's code uses */
std::vector<int> game_flags();

/* The special conditions (SPECIAL_CONDITIONS), by id */
std::vector<int> special_ids();
/* a special one's test: its kind (0x10 partners, 0x20 money, 0x30 the story's range...) | op, or -1 */
int special_test(int id);
/* whether a special one changes something as an action (money, a partner) rather than testing */
bool special_is_action(int id);
/* a special one's story range: [min, max] (max 0x63: on), or false */
bool special_story_range(int id, int *min, int *max);

/* checkPartyStat's thresholds: the party's charisma */
static const int PARTY_STAT_COUNT = 15;
int party_stat_threshold(int index);

/* The names the words use */
struct CodeWords {
    std::vector<std::string> items, cards, partners; /* partners: the 8, by GAME.partners' index */
    std::vector<std::string> story;                  /* by point: "0x05, after Seiryu Leader" */
    std::map<int, std::string> fields;               /* by mode */
    std::map<int, std::string> flags;                /* by code: the user's name for it, or what it is */
};
/* a story point's label: words.story's, or "0x2A" */
std::string story_label(int point, const CodeWords &w);
/* special condition id in words: "the story is between ... and ...", "Agumon is in the party" */
std::string special_words(int id, const CodeWords &w);
/* one code in words, as a condition ("flag 1A:1F is set") or an action ("set flag 1A:1F") */
std::string describe_code(int code, int value, const CodeWords &w, bool actions);
/* a list: "a and b" (conditions; "always" for none), "a, b" (actions; "nothing") */
std::string describe_codes(const Codes &codes, const CodeWords &w, bool actions);
