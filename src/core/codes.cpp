#include "codes.h"
#include <cstdio>

#include "game_codes.inc"

/* The European GAME.flagsNN's sizes, in bytes; FLAGS_00's 4 */
int flag_group_bits(int group) {
    switch (group) {
    case 0x00: return 32;
    case 0x02: return 0x12 * 8;
    case 0x04: return 0x2 * 8;
    case 0x06: return 0x1 * 8;
    case 0x08: return 0x1 * 8;
    case 0x0A: return 0x4 * 8;
    case 0x0C: return 0x8 * 8;
    case 0x0E: return 0xC * 8;
    case 0x10: return 0x4 * 8;
    case 0x18: return 0x2 * 8;
    case 0x1A: return 0x9 * 8;
    case 0x1C: return 0xB * 8;
    case 0x20: return 0x1E * 8;
    case 0x40: return 0x1A * 8;
    }
    return 0;
}

std::string flag_ref(int code) {
    char b[16];
    snprintf(b, sizeof b, "%02X:%02X", code_kind(code), code_id(code));
    return b;
}

const StoryStep *story_step(int point) {
    for (const StoryStep &s : STORY_STEPS)
        if (s.point == point) return &s;
    return nullptr;
}

std::vector<GameFlagUse> game_flag_uses(int code) {
    std::vector<GameFlagUse> out;
    for (const GameFlagUse &u : GAME_FLAG_USES)
        if (u.code == code) out.push_back(u);
    return out;
}

std::vector<int> game_flags() {
    std::vector<int> out;
    for (const GameFlagUse &u : GAME_FLAG_USES)
        if (out.empty() || out.back() != u.code) out.push_back(u.code);
    return out;
}

/* SPECIAL_CONDITIONS (European): id, the test's kind | op, its argument */
static const unsigned char SPECIALS[][3] = {
    {0x3A, 0x00, 0x00}, {0x96, 0x01, 0x00}, {0x00, 0x10, 0x00}, {0x01, 0x10, 0x01}, {0x02, 0x10, 0x02}, {0x10, 0x11, 0x00},
    {0x12, 0x11, 0x01}, {0x11, 0x11, 0x02}, {0x0B, 0x11, 0x03}, {0x0D, 0x11, 0x04}, {0x0C, 0x11, 0x05}, {0x0F, 0x11, 0x06},
    {0x0E, 0x11, 0x07}, {0x42, 0x12, 0x00}, {0x44, 0x12, 0x01}, {0x43, 0x12, 0x02}, {0x47, 0x12, 0x03}, {0x46, 0x12, 0x04},
    {0x45, 0x12, 0x05}, {0x49, 0x12, 0x06}, {0x48, 0x12, 0x07}, {0x37, 0x13, 0x00}, {0x39, 0x13, 0x01}, {0x38, 0x13, 0x02},
    {0x31, 0x13, 0x03}, {0x32, 0x13, 0x04}, {0x33, 0x13, 0x05}, {0x35, 0x13, 0x06}, {0x34, 0x13, 0x07}, {0x4A, 0x14, 0x00},
    {0x4C, 0x14, 0x01}, {0x4B, 0x14, 0x02}, {0x4F, 0x14, 0x03}, {0x4E, 0x14, 0x04}, {0x4D, 0x14, 0x05}, {0x51, 0x14, 0x06},
    {0x50, 0x14, 0x07}, {0x25, 0x15, 0x00}, {0x26, 0x15, 0x01}, {0x27, 0x15, 0x02}, {0x28, 0x15, 0x03}, {0x29, 0x15, 0x04},
    {0x2E, 0x16, 0x00}, {0x30, 0x16, 0x01}, {0x2F, 0x16, 0x02}, {0x2A, 0x16, 0x03}, {0x2B, 0x16, 0x04}, {0x2C, 0x16, 0x05},
    {0x36, 0x16, 0x06}, {0x2D, 0x16, 0x07}, {0x95, 0x17, 0x00}, {0x53, 0x18, 0x00}, {0x55, 0x19, 0x00}, {0x74, 0x20, 0x00},
    {0x75, 0x20, 0x01}, {0x76, 0x20, 0x02}, {0x77, 0x20, 0x03}, {0x78, 0x20, 0x04}, {0x79, 0x20, 0x05}, {0x7A, 0x20, 0x06},
    {0x7B, 0x20, 0x07}, {0x7C, 0x20, 0x08}, {0x7D, 0x20, 0x09}, {0x8B, 0x21, 0x00}, {0x8C, 0x21, 0x01}, {0x8D, 0x21, 0x02},
    {0x8E, 0x21, 0x03}, {0x8F, 0x21, 0x04}, {0x90, 0x21, 0x05}, {0x91, 0x21, 0x06}, {0x92, 0x21, 0x07}, {0x7E, 0x22, 0x00},
    {0x7F, 0x22, 0x01}, {0x80, 0x22, 0x02}, {0x81, 0x22, 0x03}, {0x82, 0x22, 0x04}, {0x83, 0x22, 0x05}, {0x84, 0x22, 0x06},
    {0x85, 0x22, 0x07}, {0x86, 0x22, 0x08}, {0x87, 0x22, 0x09}, {0x06, 0x30, 0x00}, {0x03, 0x30, 0x01}, {0x0A, 0x30, 0x02},
    {0x07, 0x30, 0x03}, {0x04, 0x30, 0x04}, {0x05, 0x30, 0x05}, {0x09, 0x30, 0x06}, {0x14, 0x30, 0x07}, {0x08, 0x30, 0x08},
    {0x3F, 0x30, 0x09}, {0x40, 0x30, 0x0A}, {0x41, 0x30, 0x0B}, {0x93, 0x30, 0x0C}, {0x94, 0x30, 0x0D}, {0x15, 0x30, 0x0E},
    {0x16, 0x30, 0x0F}, {0x17, 0x30, 0x10}, {0x18, 0x30, 0x11}, {0x19, 0x30, 0x12}, {0x1A, 0x30, 0x13}, {0x1C, 0x30, 0x14},
    {0x1D, 0x30, 0x15}, {0x21, 0x30, 0x16}, {0x1E, 0x30, 0x17}, {0x1F, 0x30, 0x18}, {0x20, 0x30, 0x19}, {0x22, 0x30, 0x1A},
    {0x23, 0x30, 0x1B}, {0x24, 0x30, 0x1C}, {0x3B, 0x30, 0x1D}, {0x3C, 0x30, 0x1E}, {0x3D, 0x30, 0x1F}, {0x3E, 0x30, 0x20},
    {0x71, 0x40, 0x00}, {0x70, 0x40, 0x01}, {0x73, 0x40, 0x02}, {0x72, 0x40, 0x03}, {0x54, 0x41, 0x00}, {0x13, 0x50, 0x00},
};
/* PROGRESS_RANGES: checkProgressRange's { min, max } */
static const unsigned char STORY_RANGES[][2] = {
    {0x02, 0x12}, {0x04, 0x17}, {0x04, 0x2C}, {0x14, 0x17}, {0x18, 0x25}, {0x27, 0x2A}, {0x04, 0x63}, {0x07, 0x63}, {0x18, 0x63},
    {0x16, 0x2A}, {0x12, 0x63}, {0x14, 0x63}, {0x0F, 0x63}, {0x1E, 0x63}, {0x05, 0x0A}, {0x0F, 0x12}, {0x14, 0x16}, {0x18, 0x1A},
    {0x1B, 0x25}, {0x27, 0x28}, {0x25, 0x26}, {0x1B, 0x24}, {0x22, 0x26}, {0x1B, 0x26}, {0x1C, 0x25}, {0x1B, 0x21}, {0x04, 0x26},
    {0x04, 0x15}, {0x0E, 0x26}, {0x11, 0x26}, {0x0A, 0x0D}, {0x1D, 0x26}, {0x0C, 0x16}, {0x00, 0x00},
};
static const int MONEY_REQUIRED[] = {800, 1600, 2700, 4000, 6000, 8700, 11500, 17500, 24000, 32000};
static const int MONEY_GAINS[] = {100, 300, 600, 1000, 1600, 3000, 5000, 8500, 0, 0};
static const int PARTY_STAT_THRESHOLDS[PARTY_STAT_COUNT] = {60, 150, 210, 285, 378, 492, 630, 795, 990, 1218, 1482, 1785, 2049, 2277, 2472};
/* STARTER_PARTIES: the partners of the packs A, B and C */
static const int STARTER_PARTIES[3][3] = {{0, 6, 7}, {2, 3, 6}, {1, 5, 7}};

int party_stat_threshold(int index) { return index >= 0 && index < PARTY_STAT_COUNT ? PARTY_STAT_THRESHOLDS[index] : 0; }

static const unsigned char *special(int id) {
    for (auto &s : SPECIALS)
        if (s[0] == id) return s;
    return nullptr;
}

std::vector<int> special_ids() {
    std::vector<int> out;
    for (auto &s : SPECIALS) out.push_back(s[0]);
    return out;
}

int special_test(int id) {
    const unsigned char *s = special(id);
    return s ? s[1] : -1;
}

bool special_is_action(int id) {
    const unsigned char *s = special(id);
    return s && (s[1] == 0x13 || s[1] == 0x18 || s[1] == 0x21 || s[1] == 0x22 || s[1] == 0x50);
}

bool special_story_range(int id, int *min, int *max) {
    const unsigned char *s = special(id);
    if (!s || (s[1] & 0xF0) != 0x30 || s[2] >= sizeof STORY_RANGES / 2) return false;
    *min = STORY_RANGES[s[2]][0], *max = STORY_RANGES[s[2]][1];
    return true;
}

std::string story_label(int point, const CodeWords &w) {
    if (point >= 0 && point < (int)w.story.size() && !w.story[point].empty()) return w.story[point];
    char b[16];
    snprintf(b, sizeof b, "0x%02X", point);
    return b;
}

static std::string partner(int i, const CodeWords &w) {
    return i >= 0 && i < (int)w.partners.size() && !w.partners[i].empty() ? w.partners[i] : "partner " + std::to_string(i + 1);
}

static std::string item(int i, const CodeWords &w) {
    return i >= 0 && i < (int)w.items.size() && !w.items[i].empty() ? w.items[i] : "item " + std::to_string(i);
}

std::string special_words(int id, const CodeWords &w) {
    const unsigned char *s = special(id);
    char b[256];
    if (!s) return snprintf(b, sizeof b, "special %d (none: always false)", id), b;
    int op = s[1] & 0xF, arg = s[2];
    switch (s[1] & 0xF0) {
    case 0x00:
        if (op == 0) return "has " + item(7, w) + ", " + item(0x59, w) + " and " + item(0xA7, w);
        return "has " + item(0x69, w) + ", " + item(0x77, w) + ", " + item(0x83, w) + ", " + item(0x90, w) + " and " + item(0x9C, w);
    case 0x10:
        switch (op) {
        case 0:
            if (arg < 3) {
                const int *p = STARTER_PARTIES[arg];
                return std::string("started with pack ") + char('A' + arg) + " (" + partner(p[0], w) + ", " + partner(p[1], w) + ", " +
                       partner(p[2], w) + ")";
            }
            break;
        case 1: return partner(arg, w) + " has joined";
        case 2: return partner(arg, w) + " is in the party";
        case 3: return partner(arg, w) + " joins";
        case 4: return partner(arg, w) + " has joined and is level 45 or more";
        case 5: return snprintf(b, sizeof b, "the party's levels add up to %d or more", arg * 15 + 30), b;
        case 6: return partner(arg, w) + " hasn't joined";
        case 7: return "every partner who has joined is level 45 or more";
        case 8: return "the party's HP and MP are restored";
        case 9: return "all 8 partners have joined";
        }
        break;
    case 0x20:
        if (arg < 10) {
            if (op == 0) return snprintf(b, sizeof b, "has %d bits or more", MONEY_REQUIRED[arg]), b;
            if (op == 1) return snprintf(b, sizeof b, "the player gets %d bits", MONEY_GAINS[arg]), b;
            if (op == 2) return snprintf(b, sizeof b, "the player pays %d bits", MONEY_REQUIRED[arg]), b;
        }
        break;
    case 0x30: {
        int lo, hi;
        special_story_range(id, &lo, &hi);
        if (hi >= 0x63) return "the story is at " + story_label(lo, w) + " or later";
        if (lo == hi) return "the story is at " + story_label(lo, w);
        return "the story is from " + story_label(lo, w) + " to " + story_label(hi, w);
    }
    case 0x40:
        if (op == 1) return "flags 10:0D to 10:10 are all set";
        if (arg == 0) return "any of flags 1C:27 to 1C:2D is set";
        if (arg == 1) return "two or more of flags 1C:27 to 1C:2D are clear";
        if (arg == 2) return "one of flags 1C:27 to 1C:2D is clear";
        return "never";
    case 0x50: return "the player's icon shows (an item got)";
    }
    return snprintf(b, sizeof b, "special %d", id), b;
}

std::string describe_code(int code, int value, const CodeWords &w, bool actions) {
    int kind = code_kind(code), id = code_id(code);
    char b[256];
    if (is_flag(code)) {
        std::string name = "flag " + flag_ref(code);
        auto f = w.flags.find(code);
        if (f != w.flags.end() && !f->second.empty()) name += " (" + f->second + ")";
        int mode = visited_mode(code);
        if (mode >= 0) {
            auto fl = w.fields.find(mode);
            std::string field = fl != w.fields.end() ? fl->second : "mode " + std::to_string(mode);
            if (actions) return (value ? "mark " : "unmark ") + field + " as visited";
            return (value ? "has been to " : "hasn't been to ") + field;
        }
        if (!flag_group_bits(kind) || id >= flag_group_bits(kind)) name += " [not a flag of the game's]";
        if (actions) return (value ? "set " : "clear ") + name;
        return name + (value ? " is set" : " isn't set");
    }
    switch (kind) {
    case CODE_STORY: return std::string("the story ") + (value ? "is at " : "isn't at ") + story_label(id, w);
    case CODE_SPECIAL:
        if (actions) return special_words(id, w);
        return value ? special_words(id, w) : "not: " + special_words(id, w);
    case CODE_PARTY_STAT:
        return snprintf(b, sizeof b, "the party's charisma adds up to %s %d", value ? "at least" : "less than", party_stat_threshold(id)), b;
    case CODE_EVENT_BATTLE: return snprintf(b, sizeof b, "fight the stage's event battle %d", id), b;
    case CODE_CARD_BATTLE:
    case CODE_CARD_BATTLE + 2: return snprintf(b, sizeof b, "a card battle with opponent %d (%d)", code & 0xFF, (code - 0x7600) >> 9), b;
    case CODE_WARP_ARG:
        if (id < 30) return snprintf(b, sizeof b, "the player %s through place %d", value ? "came" : "didn't come", id + 1), b;
        return snprintf(b, sizeof b, "the place's argument %s %d", value ? "is" : "isn't", id - 29), b;
    case 0x7A:
        if (id < 30) return snprintf(b, sizeof b, "open item shop %d", id), b;
        if ((id >= 0x31 && id < 0x44) || (id >= 0x46 && id < 0x4B)) return snprintf(b, sizeof b, "open card shop %d", id), b;
        return "open the inn";
    case 0x7C: return id == 0 ? "open the Digi-Lab" : id == 1 ? "open the Digimon naming" : "nothing";
    case 0x94: return snprintf(b, sizeof b, "open training gym %d", id), b;
    case CODE_START_EVENT:
        if (id >= 0x100) return snprintf(b, sizeof b, "start event %d", 9000 + id - 0x100), b;
        return snprintf(b, sizeof b, "start the game's event %d", id), b;
    case CODE_CARD: {
        std::string card = id < (int)w.cards.size() && !w.cards[id].empty() ? w.cards[id] : "card " + std::to_string(id);
        if (actions) return (value ? "give the card " : "take the card ") + card;
        return (value ? "has the card " : "hasn't the card ") + card;
    }
    }
    if (kind >= CODE_ITEM && kind < 0x8F) {
        if (actions) return (value ? "give " : "take ") + item(id, w);
        return (value ? "has " : "hasn't ") + item(id, w);
    }
    return snprintf(b, sizeof b, "code %04X = %d", code, value), b;
}

std::string describe_codes(const Codes &codes, const CodeWords &w, bool actions) {
    std::string out;
    for (auto &[code, value] : codes) out += (out.empty() ? "" : actions ? ", " : " and ") + describe_code(code, value, w, actions);
    return out.empty() ? (actions ? "nothing" : "always") : out;
}
