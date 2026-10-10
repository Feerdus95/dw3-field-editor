/* The game's text files, decoded to UTF-8, and the condition and action codes
   of the fields' tables in words. */
#pragma once
#include <string>
#include <utility>
#include <vector>
#include "field.h"

/* A text file's strings: a count, then each string's offset. */
std::vector<std::string> read_strings(const std::vector<uint8_t> &file);

/* One string decoded: the font's glyphs (4 on: digits, capitals, small
   letters), 01 xx its icons (punctuation, symbols), 02 xx control codes
   (new lines, pages, a speaker's name before 02 07), up to 0 */
std::string decode_text(const uint8_t *s, size_t n);

/* The text files the editor reads, in English: the European disc keeps each
   one per language in a row (TEXT_FILE), the first copy at the USA number,
   English the second */
struct GameText {
    std::vector<std::string> areas, places, items;
    void load(const Files &files);
    /* a stage's talk file (FieldState.textFile, already the copy it loads) */
    std::vector<std::string> stageText(const Files &files, int fid) const { return read_strings(files.read(fid)); }
};

/* (code, value) pairs as words: "flag 1A:1F set", "has item Mega Hand"... */
std::string describe_codes(const std::vector<std::pair<int, int>> &codes, const GameText &text, bool actions);
