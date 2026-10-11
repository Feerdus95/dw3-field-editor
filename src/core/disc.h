/* The game's disc image, Digimon World 2003 (Europe, SLES-03936): its
   files by fid, through the executable's tables, as the game finds them; and
   the fields' tables read from it, the stages' setups run as the game does. */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Disc {
    std::string path; /* the .bin (2352-byte sectors) or .iso (2048) */
    bool raw = false;
    int sectorCount = 0;
    std::vector<uint8_t> exe; /* SLES_039.36 */
    std::vector<int32_t> starts; /* each fid's first sector, or <= 0 */
    std::vector<uint16_t> counts; /* and its sectors */

    /* "" or what's wrong (a .cue opens its .bin) */
    std::string open(const std::string &image);
    std::vector<uint8_t> sectors(int lba, int count) const;
    /* a file by fid, empty if there's none */
    std::vector<uint8_t> file(int fid) const;
    /* a file by its path on the disc ("AAA/PRO/FIELDSTG.PRO"), through the
       path table (the root lists none of AAA): its first sector and size, or -1 */
    int find(const std::string &dir, const std::string &name, int *size) const;
};

/* Writes OUT/<stage>.json for every stage and OUT/characters.json, as the
   port's --dump-stages does: "" or what went wrong */
std::string dump_stages(const Disc &disc, const std::string &out, int *count);
