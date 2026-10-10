#include "text.h"
#include <cstdio>

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* The font's icons 1-58 (FONT_ICON_MAP's Shift-JIS codes, as Unicode) */
static const char *ICONS[59] = {
    "",  " ", "、", "。", ",", ".", "・", ":", ";", "?", "!", "^", "_", "ー", "―", "/", "\\", "~", "...", "'",
    "\"", "(", ")", "「", "」", "+", "-", "×", "=", "<", ">", "≦", "≧", "∞", "∴", "♂", "♀", "°C", "¥", "$",
    "¢", "£", "%", "&", "§", "☆", "★", "○", "●", "◆", "□", "■", "△", "▲", "▼", "→", "←", "↑", "↓",
};

/* The control codes' lengths with their arguments (FONT_CODE_LENGTHS), at least the 02 and the code */
static const int CODE_LENGTHS[11] = {1, 2, 3, 2, 5, 3, 3, 2, 3, 2, 0};

std::string decode_text(const uint8_t *s, size_t n) {
    std::string out;
    for (size_t i = 0; i < n && s[i];) {
        int c = s[i];
        if (c >= 4 && c <= 13) out += (char)('0' + c - 4), i++;
        else if (c >= 14 && c <= 39) out += (char)('A' + c - 14), i++;
        else if (c >= 40 && c <= 65) out += (char)('a' + c - 40), i++;
        else if (c == 1 && i + 1 < n) {
            int k = s[i + 1];
            if (k < 59) out += ICONS[k];
            else out += "{" + std::to_string(k) + "}";
            i += 2;
        } else if (c == 2 && i + 1 < n) {
            int k = s[i + 1];
            if (k == 1) out += "\n";                /* a new line */
            else if (k == 0 || k == 3) out += "\n\n"; /* the next page */
            else if (k == 7 && !out.empty()) out += ": "; /* 02 07 name 02 07: a speaker's name */
            else if (k == 8) out += "<player>";
            i += k < 11 && CODE_LENGTHS[k] > 2 ? CODE_LENGTHS[k] : 2;
        } else if (c == 3) i += 2;
        else out += "{" + std::to_string(c) + "}", i++;
    }
    /* runs of the narrow space glyph are one space; no trailing blank lines */
    std::string clean;
    for (char ch : out)
        if (!(ch == ' ' && !clean.empty() && clean.back() == ' ')) clean += ch;
    while (!clean.empty() && (clean.back() == '\n' || clean.back() == ' ')) clean.pop_back();
    return clean;
}

std::vector<std::string> read_strings(const std::vector<uint8_t> &d) {
    std::vector<std::string> out;
    if (d.size() < 4) return out;
    uint32_t n = rd32(d.data());
    for (uint32_t i = 0; i < n && 8 + (size_t)i * 4 <= d.size(); i++) {
        uint32_t o = rd32(d.data() + 4 + i * 4);
        out.push_back(o < d.size() ? decode_text(d.data() + o, d.size() - o) : "");
    }
    return out;
}

/* TEXT_AREA_NAMES (STAREA), TEXT_STAGE_NAMES (STNAME), TEXT_ITEM_NAMES (ITMNAM), English copy */
void GameText::load(const Files &files) {
    areas = read_strings(files.read(0xAA + 1));
    places = read_strings(files.read(0xB8 + 1));
    items = read_strings(files.read(0x6B + 1));
}

std::string describe_codes(const std::vector<std::pair<int, int>> &codes, const GameText &text, bool actions) {
    std::string out;
    char b[128];
    for (auto &[code, value] : codes) {
        int kind = code >> 8 & 0xFE, id = code & 0x1FF;
        if (code < 0x4100) {
            if (actions) snprintf(b, sizeof b, "set flag %X:%02X to %d", code >> 8, code & 0xFF, value);
            else snprintf(b, sizeof b, "flag %X:%02X is %d", code >> 8, code & 0xFF, value);
        } else if (kind == 0x60) snprintf(b, sizeof b, "story point %s 0x%X", value ? "is" : "isn't", id);
        else if (kind == 0x70) snprintf(b, sizeof b, actions ? "special %d" : "special condition %d %s", id, value ? "holds" : "doesn't hold");
        else if (kind == 0x72) snprintf(b, sizeof b, "party stat %d %s", id, value ? "holds" : "doesn't hold");
        else if (kind == 0x74) snprintf(b, sizeof b, "start event battle %d", id);
        else if ((kind == 0x76 || kind == 0x78) && actions) snprintf(b, sizeof b, "card battle with %d (%d)", code & 0xFF, (code - 0x7600) >> 9);
        else if (kind == 0x7E) snprintf(b, sizeof b, "warp arg %s %d", value ? "is" : "isn't", id);
        else if (code >= 0x8000 && code < 0x9000) {
            int item = code & 0x1FF;
            std::string name = item < (int)text.items.size() && !text.items[item].empty() ? text.items[item] : "item " + std::to_string(item);
            if (actions) snprintf(b, sizeof b, "%s %s", value ? "give" : "take", name.c_str());
            else snprintf(b, sizeof b, "%s %s", value ? "has" : "hasn't", name.c_str());
        } else if (kind == 0x90) snprintf(b, sizeof b, "start event %d", id);
        else if (kind == 0x92) snprintf(b, sizeof b, actions ? "%s card %d" : "%s card %d", value ? (actions ? "give" : "has") : (actions ? "take" : "hasn't"), id);
        else snprintf(b, sizeof b, "code %04X = %d", code, value);
        out += (out.empty() ? "" : actions ? ", " : " and ") + std::string(b);
    }
    return out.empty() ? (actions ? "nothing" : "always") : out;
}
