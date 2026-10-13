/* A file as what it shares with others (the game's files, which whoever
   reads it has from their own disc) and the rest: a shared field carries
   only the bytes its maker made. */
#pragma once
#include <cstdint>
#include <vector>

/* target as copies from bases (runs of 16 bytes or more, 4-aligned in the
   base) and its own bytes */
std::vector<uint8_t> delta_encode(const std::vector<uint8_t> &target, const std::vector<const std::vector<uint8_t> *> &bases);
/* the file again from the same bases; false when the delta is broken or a base too short */
bool delta_decode(const std::vector<uint8_t> &delta, const std::vector<const std::vector<uint8_t> *> &bases, std::vector<uint8_t> *out);
/* zlib's CRC-32 */
uint32_t crc32(const std::vector<uint8_t> &data);
