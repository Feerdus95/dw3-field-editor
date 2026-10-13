#include "text.h"
#include <cstdio>
#include <cstring>
#include <map>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

const char *LANGUAGE_NAMES[LANG_COUNT] = {"English", "French", "Italian", "German", "Spanish"};

/* The font's icons 1-58 (FONT_ICON_MAP's Shift-JIS codes, as Unicode), then
   the European font's letters, icons 59-112 */
static const char *ICONS[59] = {
    "",  " ", "、", "。", ",", ".", "・", ":", ";", "?", "!", "^", "_", "ー", "―", "/", "\\", "~", "…", "'",
    "\"", "(", ")", "「", "」", "+", "-", "×", "=", "<", ">", "≦", "≧", "∞", "∴", "♂", "♀", "℃", "¥", "$",
    "¢", "£", "%", "&", "§", "☆", "★", "○", "●", "◆", "□", "■", "△", "▲", "▼", "→", "←", "↑", "↓",
};
static const char *EU_LETTERS[] = {"¡", "£", "§", "µ", "¿", "Ã", "Ä", "Å", "Æ", "Ç", "Ñ", "Õ", "Ö", "Ø", "Ü", "ß", "à", "â",
                                   "ã", "ä", "ç", "è", "é", "ê", "ë", "ì", "î", "ï", "ñ", "ò", "ô", "õ", "ö", "ù", "û", "ü",
                                   "á", "í", "ó", "ú", "Â", "Ê", "Ô", "Ë", "Ï", "Á", "É", "Í", "Ó", "Ú", "À", "È", "Ì", "Ò",
                                   "Ù"};
/* the glyphs after the kana: 229-233 */
static const char *PUNCTUATION[5] = {"･", "?", "!", "—", "〜"};

/* The control codes' lengths with their arguments (FONT_CODE_LENGTHS), at least the 02 and the code */
static const int CODE_LENGTHS[11] = {1, 2, 3, 2, 5, 3, 3, 2, 3, 2, 0};
static const uint8_t PAGE[5] = {2, 2, 2, 2, 3}, END[3] = {2, 2, 2}, PLAYER[6] = {2, 8, 1, 2, 5, 1};

/* Each character's bytes, and each one-character byte sequence's character:
   the glyphs first, so where a character has two ways the game's usual one wins */
struct Charset {
    std::map<std::string, std::vector<uint8_t>> enc;
    std::map<std::vector<uint8_t>, std::string> dec;
    void add(const std::string &c, std::vector<uint8_t> b) {
        if (c.empty()) return;
        enc.insert({c, b});
        dec.insert({b, c});
    }
    Charset() {
        for (int i = 0; i < 10; i++) add(std::string(1, (char)('0' + i)), {(uint8_t)(4 + i)});
        for (int i = 0; i < 26; i++) add(std::string(1, (char)('A' + i)), {(uint8_t)(14 + i)});
        for (int i = 0; i < 26; i++) add(std::string(1, (char)('a' + i)), {(uint8_t)(40 + i)});
        for (int i = 0; i < 5; i++) add(PUNCTUATION[i], {(uint8_t)(229 + i)});
        for (int i = 1; i < 59; i++) add(ICONS[i], {1, (uint8_t)i});
        for (int i = 0; i < (int)(sizeof EU_LETTERS / sizeof *EU_LETTERS); i++) add(EU_LETTERS[i], {1, (uint8_t)(59 + i)});
    }
};
static const Charset &charset() {
    static Charset c;
    return c;
}

/* The bytes of one character or code at s[i] */
static size_t unit(const std::vector<uint8_t> &s, size_t i) {
    size_t n = 1;
    if (s[i] == 1 || s[i] == 3) n = 2;
    else if (s[i] == 2 && i + 1 < s.size()) n = s[i + 1] < 11 && CODE_LENGTHS[s[i + 1]] > 2 ? CODE_LENGTHS[s[i + 1]] : 2;
    return std::min(n, s.size() - i);
}

static std::string token(const uint8_t *p, size_t n) {
    std::string t = "{";
    char b[4];
    for (size_t k = 0; k < n; k++) snprintf(b, sizeof b, k ? " %02X" : "%02X", p[k]), t += b;
    return t + "}";
}

static bool at(const std::vector<uint8_t> &s, size_t i, const uint8_t *seq, size_t n) {
    return i + n <= s.size() && memcmp(&s[i], seq, n) == 0;
}

/* A stretch of bytes as text: a character where the font has one that writes
   back the same, a token otherwise */
static std::string decode_part(const std::vector<uint8_t> &s, size_t from, size_t to, bool body) {
    std::string out;
    for (size_t i = from; i < to;) {
        if (body && at(s, i, PAGE, 5) && i + 5 <= to && (out.empty() || out.back() != '\n')) {
            out += "\n\n", i += 5;
            continue;
        }
        if (at(s, i, PLAYER, 6) && i + 6 <= to) {
            out += "{player}", i += 6;
            continue;
        }
        size_t n = std::min(unit(s, i), to - i);
        std::vector<uint8_t> u(s.begin() + i, s.begin() + i + n);
        if (body && n == 2 && u[0] == 2 && u[1] == 1 && (out.empty() || out.back() != '\n')) out += "\n";
        else {
            auto it = charset().dec.find(u);
            if (it != charset().dec.end() && charset().enc.at(it->second) == u) out += it->second;
            else out += token(u.data(), n);
        }
        i += n;
    }
    return out;
}

GameString bytes_to_text(const std::vector<uint8_t> &s) {
    GameString g;
    size_t from = 0, to = s.size();
    /* 02 07 name 02 07: the speaker */
    if (s.size() >= 4 && s[0] == 2 && s[1] == 7)
        for (size_t i = 2; i + 1 < s.size(); i += unit(s, i))
            if (s[i] == 2 && s[i + 1] == 7) {
                g.speaker = decode_part(s, 2, i, false);
                from = i + 2;
                break;
            }
    bool ends = to >= from + 3 && at(s, to - 3, END, 3);
    if (ends) to -= 3;
    g.text = decode_part(s, from, to, true);
    if (!ends) g.text += "{nowait}";
    /* what a speaker's name can't be written as: the whole of it in tokens */
    std::string err;
    if (text_to_bytes(g, &err) != s) g = {"", token(s.data(), s.size()) + "{nowait}"};
    return g;
}

/* text as bytes, with \n codes in a body; false at a character the font lacks */
static bool encode_part(const std::string &t, bool body, std::vector<uint8_t> &out, std::string *err) {
    for (size_t i = 0; i < t.size();) {
        if (body && t.compare(i, 2, "\n\n") == 0) {
            out.insert(out.end(), PAGE, PAGE + 5), i += 2;
            continue;
        }
        if (body && t[i] == '\n') {
            out.push_back(2), out.push_back(1), i++;
            continue;
        }
        if (t[i] == '\r') {
            i++;
            continue;
        }
        if (t[i] == '{') {
            size_t e = t.find('}', i);
            if (e == std::string::npos) {
                if (err) *err = "a { without its }";
                return false;
            }
            std::string in = t.substr(i + 1, e - i - 1);
            if (in == "player") out.insert(out.end(), PLAYER, PLAYER + 6);
            else if (in != "nowait") {
                unsigned v;
                int n = 0;
                for (const char *p = in.c_str(); sscanf(p, "%2x%n", &v, &n) == 1; p += n) {
                    out.push_back((uint8_t)v);
                    while (p[n] == ' ') n++;
                }
            }
            i = e + 1;
            continue;
        }
        /* one UTF-8 character */
        size_t n = (uint8_t)t[i] < 0x80 ? 1 : (uint8_t)t[i] < 0xE0 ? 2 : (uint8_t)t[i] < 0xF0 ? 3 : 4;
        std::string c = t.substr(i, n);
        auto it = charset().enc.find(c);
        if (it == charset().enc.end()) {
            if (err) *err = "the font has no \"" + c + "\"";
            return false;
        }
        out.insert(out.end(), it->second.begin(), it->second.end());
        i += n;
    }
    return true;
}

std::vector<uint8_t> text_to_bytes(const GameString &g, std::string *err) {
    std::vector<uint8_t> out;
    if (!g.speaker.empty()) {
        out.push_back(2), out.push_back(7);
        if (!encode_part(g.speaker, false, out, err)) return {};
        out.push_back(2), out.push_back(7);
    }
    if (!encode_part(g.text, true, out, err)) return {};
    size_t n = g.text.size();
    if (n < 8 || g.text.compare(n - 8, 8, "{nowait}") != 0) out.insert(out.end(), END, END + 3);
    return out;
}

std::string decode_text(const uint8_t *s, size_t n) {
    std::vector<uint8_t> v(s, s + strnlen((const char *)s, n));
    GameString g = bytes_to_text(v);
    /* the tokens left out, the player's name named */
    auto clean = [](const std::string &t) {
        std::string out;
        for (size_t i = 0; i < t.size(); i++) {
            if (t[i] != '{') {
                if (!(t[i] == ' ' && !out.empty() && out.back() == ' ')) out += t[i];
                continue;
            }
            size_t e = t.find('}', i);
            if (e == std::string::npos) break;
            if (t.compare(i, e - i + 1, "{player}") == 0) out += "<player>";
            i = e;
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
        return out;
    };
    std::string speaker = clean(g.speaker), text = clean(g.text);
    return speaker.empty() ? text : speaker + ": " + text;
}

/* The European font's advances (FONT_GLYPHS_1, FONT_ICONS_1: the talk box's
   style): glyphs 4-233, icons 1-114 */
static const uint8_t GLYPH_ADVANCE[230] = {
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 6, 5, 2, 4, 5, 3, 6, 5, 5, 5, 5, 4, 5, 4, 6, 5, 6, 5,
    5, 5, 8, 9, 8, 9, 6, 7, 8, 9, 9, 9, 10, 10, 9, 9, 6, 8, 9, 10, 8, 9, 8, 10, 8, 8, 8, 9, 9, 9,
    9, 9, 9, 9, 8, 9, 7, 9, 10, 9, 9, 8, 10, 10, 9, 10, 10, 10, 9, 10, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    9, 10, 10, 9, 10, 10, 10, 9, 8, 10, 7, 10, 8, 9, 9, 7, 9, 10, 9, 7, 10, 9, 10, 8, 10, 7, 9, 7, 9, 7,
    10, 8, 10, 9, 10, 10, 10, 9, 10, 10, 10, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 9, 10, 8, 10, 10, 10, 7, 9, 10,
    10, 10, 5, 6, 10, 10, 9, 9, 7, 9, 10, 10, 8, 9, 9, 9, 10, 10, 10, 10, 10, 9, 10, 10, 10, 7, 10, 8, 10, 7,
    10, 8, 10, 6, 8, 9, 7, 10, 8, 8, 7, 9, 8, 9, 10, 7, 7, 4, 5, 6};
static const uint8_t ICON_ADVANCE[114] = {
    6, 7, 7, 7, 7, 7, 7, 7, 7, 4, 4, 7, 5, 5, 6, 6, 6, 12, 7, 4, 4, 4, 4, 4, 8, 5, 11, 7, 6, 6,
    7, 7, 7, 7, 12, 12, 12, 12, 12, 12, 12, 12, 8, 12, 12, 12, 11, 12, 12, 11, 12, 11, 12, 12, 7, 7, 8, 8, 4, 6,
    5, 6, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5, 6, 5, 6, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,
    6, 6, 6, 6, 5, 5, 6, 6, 6, 6, 6, 6, 5, 6, 6, 5, 6, 6, 6, 6, 5, 6, 6, 5};

std::vector<std::vector<int>> text_widths(const std::vector<uint8_t> &s, int playerName) {
    std::vector<std::vector<int>> pages(1, std::vector<int>(1, 0));
    size_t i = 0;
    if (s.size() >= 4 && s[0] == 2 && s[1] == 7) /* the speaker's name is in a box of its own */
        for (i = 2; i + 1 < s.size() && !(s[i] == 2 && s[i + 1] == 7); i += unit(s, i)) {}
    for (i = i ? i + 2 : 0; i < s.size(); i += unit(s, i)) {
        int &w = pages.back().back();
        if (s[i] >= 4 && s[i] <= 233) w += GLYPH_ADVANCE[s[i] - 4];
        else if (s[i] == 1 && i + 1 < s.size() && s[i + 1] >= 1 && s[i + 1] <= 114) w += ICON_ADVANCE[s[i + 1] - 1];
        else if (s[i] == 2 && i + 1 < s.size()) {
            if (s[i + 1] == 1) pages.back().push_back(0);
            else if (s[i + 1] == 3) pages.push_back(std::vector<int>(1, 0));
            else if (s[i + 1] == 5) w += playerName;
        }
    }
    return pages;
}

std::vector<std::vector<uint8_t>> read_raw_strings(const std::vector<uint8_t> &d) {
    std::vector<std::vector<uint8_t>> out;
    if (d.size() < 4) return out;
    uint32_t n = rd32(d.data());
    for (uint32_t i = 0; i < n && 8 + (size_t)i * 4 <= d.size(); i++) {
        uint32_t o = rd32(d.data() + 4 + i * 4);
        if (o >= d.size()) {
            out.push_back({});
            continue;
        }
        size_t e = o;
        while (e < d.size() && d[e]) e++;
        out.emplace_back(d.begin() + o, d.begin() + e);
    }
    return out;
}

std::vector<uint8_t> build_text_file(const std::vector<std::vector<uint8_t>> &strings, bool pad) {
    std::vector<uint8_t> out(4 + strings.size() * 4, 0);
    uint32_t n = (uint32_t)strings.size();
    memcpy(out.data(), &n, 4);
    for (size_t i = 0; i < strings.size(); i++) {
        uint32_t o = (uint32_t)out.size();
        memcpy(&out[4 + i * 4], &o, 4);
        out.insert(out.end(), strings[i].begin(), strings[i].end());
        out.push_back(0);
        while (out.size() % 4) out.push_back(0);
    }
    if (pad) out.resize((out.size() + 2047) / 2048 * 2048, 0);
    return out;
}

std::vector<std::string> read_strings(const std::vector<uint8_t> &d) {
    std::vector<std::string> out;
    for (auto &s : read_raw_strings(d)) out.push_back(decode_text(s.data(), s.size()));
    return out;
}

/* TEXT_AREA_NAMES (STAREA), TEXT_STAGE_NAMES (STNAME), TEXT_ITEM_NAMES (ITMNAM), TEXT_DIGIMON_NAMES (DIGNAM), TEXT_CARD_NAMES (CARDNM), in the language */
void GameText::load(const Files &files, int language) {
    lang = language;
    areas = read_strings(files.read(fid(AREA_NAMES_FILE)));
    places = read_strings(files.read(fid(PLACE_NAMES_FILE)));
    items = read_strings(files.read(fid(0x6B + 1)));
    digimon = read_strings(files.read(fid(0x4F + 1)));
    cards = read_strings(files.read(fid(0x17 + 1)));
    read_encounters(files);
}

/* FIELDSTG (fid 0x166, at 0x80082CB0): FIELDSTG_encounters (0x80094680,
   0x24A4 bytes of 0x1C), each three pointers to FIELDSTG_enemies'
   BattleEnemy; the names through FILE_BATTLE_TABLE (0x1CF, entries of 0x46
   to an id of 0), whose nameId is a DIGNAM string, as
   FIGHTSTG_showFighterNames reads it */
void GameText::read_encounters(const Files &files) {
    const uint32_t BASE = 0x80082CB0, TABLE = 0x80094680 - BASE, COUNT = 0x24A4 / 0x1C;
    std::vector<uint8_t> fs = files.read(0x166), bt = files.read(0x1CF);
    encounters.clear();
    if (fs.size() < TABLE + COUNT * 0x1C) return;
    auto s16 = [](const uint8_t *p) { return (int)(int16_t)(p[0] | p[1] << 8); };
    std::map<int, std::string> names;
    for (size_t at = 0; at + 0x46 <= bt.size() && s16(&bt[at]); at += 0x46) {
        int name = s16(&bt[at + 6]);
        names[s16(&bt[at])] = name >= 0 && name < (int)digimon.size() ? digimon[name] : "";
    }
    for (uint32_t k = 0; k < COUNT; k++) {
        const uint8_t *e = &fs[TABLE + k * 0x1C];
        GameEncounter g{{}, e[0xC], e[0xD]};
        for (int i = 0; i < 3; i++) {
            uint32_t at = rd32(e + i * 4) - BASE;
            if (at + 12 > fs.size()) continue;
            const uint8_t *b = &fs[at];
            int fighter = (int32_t)rd32(b);
            if (!fighter) continue;
            std::string name = names[fighter];
            g.enemies.push_back({fighter, s16(b + 4), s16(b + 6), s16(b + 8), s16(b + 10),
                                 name.empty() ? "#" + std::to_string(fighter) : name});
        }
        encounters.push_back(g);
    }
}

std::string encounter_text(const GameEncounter &e) {
    std::string out;
    for (auto &x : e.enemies) out += (out.empty() ? "" : ", ") + x.name + " Lv " + std::to_string(x.level);
    return out.empty() ? "Nothing" : out;
}
