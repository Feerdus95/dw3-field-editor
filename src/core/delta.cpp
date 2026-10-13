#include "delta.h"
#include <cstring>
#include <unordered_map>

static const size_t BLOCK = 16;

static uint64_t hash_block(const uint8_t *p) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < BLOCK; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

static void put_varint(std::vector<uint8_t> &o, uint64_t v) {
    for (; v >= 0x80; v >>= 7) o.push_back((uint8_t)(v | 0x80));
    o.push_back((uint8_t)v);
}

static bool get_varint(const std::vector<uint8_t> &d, size_t &p, uint64_t &v) {
    v = 0;
    for (int shift = 0; p < d.size() && shift < 64; shift += 7) {
        uint8_t b = d[p++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return true;
    }
    return false;
}

/* the layout: the file's size, then ops: 0 n bytes (its own), 1 base offset n (a copy) */
std::vector<uint8_t> delta_encode(const std::vector<uint8_t> &target, const std::vector<const std::vector<uint8_t> *> &bases) {
    /* each base's 16-byte blocks at 4-aligned offsets, the first of each content */
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> index;
    for (uint32_t b = 0; b < bases.size(); b++)
        for (size_t o = 0; o + BLOCK <= bases[b]->size(); o += 4) index.emplace(hash_block(bases[b]->data() + o), std::make_pair(b, (uint32_t)o));
    std::vector<uint8_t> out;
    put_varint(out, target.size());
    size_t lit = 0, i = 0;
    auto flush = [&](size_t end) {
        if (end <= lit) return;
        out.push_back(0);
        put_varint(out, end - lit);
        out.insert(out.end(), target.begin() + lit, target.begin() + end);
    };
    while (i + BLOCK <= target.size()) {
        auto it = index.find(hash_block(&target[i]));
        if (it == index.end() || memcmp(bases[it->second.first]->data() + it->second.second, &target[i], BLOCK) != 0) {
            i++;
            continue;
        }
        const std::vector<uint8_t> &base = *bases[it->second.first];
        size_t o = it->second.second, n = BLOCK;
        while (i + n < target.size() && o + n < base.size() && target[i + n] == base[o + n]) n++;
        flush(i);
        out.push_back(1);
        put_varint(out, it->second.first), put_varint(out, o), put_varint(out, n);
        i += n, lit = i;
    }
    flush(target.size());
    return out;
}

bool delta_decode(const std::vector<uint8_t> &delta, const std::vector<const std::vector<uint8_t> *> &bases, std::vector<uint8_t> *out) {
    size_t p = 0;
    uint64_t size, n, b, o;
    if (!get_varint(delta, p, size) || size > (1u << 30)) return false;
    out->clear();
    out->reserve(size);
    while (p < delta.size()) {
        uint8_t op = delta[p++];
        if (op == 0) {
            if (!get_varint(delta, p, n) || n > delta.size() - p) return false;
            out->insert(out->end(), delta.begin() + p, delta.begin() + p + n);
            p += n;
        } else if (op == 1) {
            if (!get_varint(delta, p, b) || !get_varint(delta, p, o) || !get_varint(delta, p, n) || b >= bases.size() ||
                o > bases[b]->size() || n > bases[b]->size() - o)
                return false;
            out->insert(out->end(), bases[b]->begin() + o, bases[b]->begin() + o + n);
        } else return false;
        if (out->size() > size) return false;
    }
    return out->size() == size;
}

uint32_t crc32(const std::vector<uint8_t> &data) {
    uint32_t c = 0xFFFFFFFF;
    for (uint8_t v : data) {
        c ^= v;
        for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320 & (0 - (c & 1)));
    }
    return ~c;
}
