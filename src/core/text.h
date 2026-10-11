/* The game's text files, decoded to UTF-8 and written back, and the
   condition and action codes of the fields' tables in words. */
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "field.h"

/* The European disc's languages, in the order of each text file's copies
   (TEXT_FILE: the USA number + LANGUAGE - 1, LANGUAGE 2 English to 6
   Spanish): English is the USA number + 1 */
enum { LANG_ENGLISH, LANG_FRENCH, LANG_ITALIAN, LANG_GERMAN, LANG_SPANISH, LANG_COUNT };
extern const char *LANGUAGE_NAMES[LANG_COUNT];

/* A text file's strings, raw: a count, then each string's offset, each up to a 0 */
std::vector<std::vector<uint8_t>> read_raw_strings(const std::vector<uint8_t> &file);
/* The file of those strings, each with its 0, 4-aligned, padded to 2048
   bytes (not for an archive's entry) */
std::vector<uint8_t> build_text_file(const std::vector<std::vector<uint8_t>> &strings, bool pad = true);

/* A string as the editor writes it: the speaker's name (02 07 name 02 07),
   and the text, a line break for a new line (02 01), an empty line for a
   new page (the wait for a button and the page, 02 02 02 02 03); the wait
   that ends a line (02 02 02) is implied. The font's characters are the
   letters and digits, punctuation and the European letters (the icons 59 on:
   ¡ £ § µ ¿ À... ü); whatever else is a {hex} token of its bytes, {player}
   the player's name. Lossless: text_to_bytes(bytes_to_text(s)) == s */
struct GameString {
    std::string speaker, text;
};
GameString bytes_to_text(const std::vector<uint8_t> &s);
/* Empty with err (the characters the font lacks) when it can't be written */
std::vector<uint8_t> text_to_bytes(const GameString &g, std::string *err = nullptr);

/* How wide each line of each page is, in the talk box's pixels (its
   speaker's name aside); playerName: the width the player's name takes */
std::vector<std::vector<int>> text_widths(const std::vector<uint8_t> &s, int playerName = 48);
/* The talk box's lines a page */
static const int TALK_LINES = 3;

/* One string as it reads: the speaker's name and ": ", the text with the
   tokens left out */
std::string decode_text(const uint8_t *s, size_t n);

/* A text file's strings, as they read (decode_text) */
std::vector<std::string> read_strings(const std::vector<uint8_t> &file);

/* The text files the editor reads, in a language */
struct GameText {
    int lang = LANG_ENGLISH;
    std::vector<std::string> areas, places, items;
    std::vector<std::string> digimon, cards; /* DIGNAM (a partner's name: its index + 1), CARDNM */
    void load(const Files &files, int language = LANG_ENGLISH);
    /* a text file's copy in the language: fid is the English one (the
       stage dump's textFile, run in English) */
    int fid(int english) const { return english + lang; }
    std::vector<std::string> stageText(const Files &files, int english) const { return read_strings(files.read(fid(english))); }
};
/* TEXT_AREA_NAMES (STAREA) and TEXT_STAGE_NAMES (STNAME), English copies: the banner's names */
static const int AREA_NAMES_FILE = 0xAA + 1, PLACE_NAMES_FILE = 0xB8 + 1;
