#include "disc.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {

const uint32_t EXE_VRAM = 0x80010000;
const uint32_t FILE_SECTORS = 0x80044F6C, FILE_SECTOR_COUNTS = 0x800474A4; /* s32 and u16 [2382] */
const int FILE_COUNT = 2382;

/* FIELDSTG and the stage overlays, where the game loads them, and what
   the dump reads of FIELDSTG (dw3_decomp's config/eu/symbols_fieldstg.txt) */
const uint32_t FIELDSTG_VRAM = 0x80082CB0, STAGE_VRAM = 0x800A5DE0;
const uint32_t AREA_NAMES = 0x80097098, STATE = 0x80099D80, FILE_ENTRIES = 0x80099E00, ACTOR_WIDTHS = 0x8009A458,
               STAGES = 0x8009A5F0, EU_STAGES = 0x8009A884, MAP = 0x8009B6C4, FIND_BATTLES = 0x800921E4;
const int CHARACTERS = 406;

struct StageSetup {
    const char *name;
    uint32_t setup;
};
#include "stage_setups.inc"

uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* The PSX's 2 MB, and a MIPS R3000 enough for the stages' setups: they
   store the stage's tables in FIELDSTG_state and call FIELDSTG and the
   game's checkCondition, which run here too */
struct Psx {
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x200000);
    uint32_t r[32] = {}, hi = 0, lo = 0, pc = 0;
    bool bad = false;
    uint32_t places = 0; /* the list the stage's setup searched (FieldState.findBattles) */

    uint8_t *at(uint32_t a, int n) {
        a &= 0x1FFFFFFF;
        if (a + n <= ram.size()) return &ram[a];
        bad = true;
        return nullptr;
    }
    uint32_t rd(uint32_t a, int n) {
        uint8_t *p = at(a, n);
        if (p == nullptr) return 0;
        return n == 1 ? p[0] : n == 2 ? p[0] | p[1] << 8 : le32(p);
    }
    void wr(uint32_t a, int n, uint32_t v) {
        if (uint8_t *p = at(a, n))
            for (int i = 0; i < n; i++) p[i] = v >> i * 8;
    }
    int32_t s8(uint32_t a) { return (int8_t)rd(a, 1); }
    int32_t s16(uint32_t a) { return (int16_t)rd(a, 2); }
    uint32_t u8(uint32_t a) { return rd(a, 1); }
    uint32_t u16(uint32_t a) { return rd(a, 2); }
    uint32_t u32(uint32_t a) { return rd(a, 4); }
    void load(uint32_t vram, const std::vector<uint8_t> &data, size_t from = 0) {
        if (uint8_t *p = at(vram, (int)(data.size() - from))) memcpy(p, data.data() + from, data.size() - from);
    }

    /* runs the function at addr until it returns: false if it went astray */
    bool call(uint32_t addr) {
        const uint32_t RETURN = 0x80000000;
        r[29] = 0x801FFF00, r[31] = RETURN, pc = addr;
        uint32_t next = pc + 4;
        for (int steps = 0; steps < 1000000 && !bad; steps++) {
            if (pc == RETURN) return true;
            if (pc == FIND_BATTLES) places = r[4];
            uint32_t op = u32(pc), cur = pc;
            pc = next, next += 4;
            uint32_t rs = op >> 21 & 31, rt = op >> 16 & 31, rd_ = op >> 11 & 31, sa = op >> 6 & 31;
            uint32_t imm = op & 0xFFFF, simm = (uint32_t)(int16_t)imm;
            uint32_t a = r[rs] + simm, branch = cur + 4 + (simm << 2);
            auto set = [&](uint32_t reg, uint32_t v) {
                if (reg) r[reg] = v;
            };
            switch (op >> 26) {
            case 0x00:
                switch (op & 63) {
                case 0x00: set(rd_, r[rt] << sa); break;
                case 0x02: set(rd_, r[rt] >> sa); break;
                case 0x03: set(rd_, (int32_t)r[rt] >> sa); break;
                case 0x04: set(rd_, r[rt] << (r[rs] & 31)); break;
                case 0x06: set(rd_, r[rt] >> (r[rs] & 31)); break;
                case 0x07: set(rd_, (int32_t)r[rt] >> (r[rs] & 31)); break;
                case 0x08: next = r[rs]; break;
                case 0x09: next = r[rs], set(rd_, cur + 8); break;
                case 0x10: set(rd_, hi); break;
                case 0x11: hi = r[rs]; break;
                case 0x12: set(rd_, lo); break;
                case 0x13: lo = r[rs]; break;
                case 0x18: {
                    int64_t m = (int64_t)(int32_t)r[rs] * (int32_t)r[rt];
                    lo = (uint32_t)m, hi = (uint32_t)(m >> 32);
                    break;
                }
                case 0x19: {
                    uint64_t m = (uint64_t)r[rs] * r[rt];
                    lo = (uint32_t)m, hi = (uint32_t)(m >> 32);
                    break;
                }
                case 0x1A:
                    if (r[rt] != 0 && !(r[rs] == 0x80000000 && r[rt] == 0xFFFFFFFF))
                        lo = (int32_t)r[rs] / (int32_t)r[rt], hi = (int32_t)r[rs] % (int32_t)r[rt];
                    break;
                case 0x1B:
                    if (r[rt] != 0) lo = r[rs] / r[rt], hi = r[rs] % r[rt];
                    break;
                case 0x20: case 0x21: set(rd_, r[rs] + r[rt]); break;
                case 0x22: case 0x23: set(rd_, r[rs] - r[rt]); break;
                case 0x24: set(rd_, r[rs] & r[rt]); break;
                case 0x25: set(rd_, r[rs] | r[rt]); break;
                case 0x26: set(rd_, r[rs] ^ r[rt]); break;
                case 0x27: set(rd_, ~(r[rs] | r[rt])); break;
                case 0x2A: set(rd_, (int32_t)r[rs] < (int32_t)r[rt]); break;
                case 0x2B: set(rd_, r[rs] < r[rt]); break;
                default: return false;
                }
                break;
            case 0x01: {
                bool take = (rt & 1) ? (int32_t)r[rs] >= 0 : (int32_t)r[rs] < 0;
                if (rt & 0x10) set(31, cur + 8);
                if (take) next = branch;
                break;
            }
            case 0x02: next = (cur & 0xF0000000) | (op & 0x3FFFFFF) << 2; break;
            case 0x03: set(31, cur + 8), next = (cur & 0xF0000000) | (op & 0x3FFFFFF) << 2; break;
            case 0x04: if (r[rs] == r[rt]) next = branch; break;
            case 0x05: if (r[rs] != r[rt]) next = branch; break;
            case 0x06: if ((int32_t)r[rs] <= 0) next = branch; break;
            case 0x07: if ((int32_t)r[rs] > 0) next = branch; break;
            case 0x08: case 0x09: set(rt, r[rs] + simm); break;
            case 0x0A: set(rt, (int32_t)r[rs] < (int32_t)simm); break;
            case 0x0B: set(rt, r[rs] < simm); break;
            case 0x0C: set(rt, r[rs] & imm); break;
            case 0x0D: set(rt, r[rs] | imm); break;
            case 0x0E: set(rt, r[rs] ^ imm); break;
            case 0x0F: set(rt, imm << 16); break;
            case 0x20: set(rt, s8(a)); break;
            case 0x21: set(rt, s16(a)); break;
            case 0x23: set(rt, u32(a)); break;
            case 0x24: set(rt, u8(a)); break;
            case 0x25: set(rt, u16(a)); break;
            case 0x22: { /* lwl */
                uint32_t w = u32(a & ~3u), sh = (a & 3) * 8;
                set(rt, (r[rt] & (0x00FFFFFFu >> sh)) | w << (24 - sh));
                break;
            }
            case 0x26: { /* lwr */
                uint32_t w = u32(a & ~3u), sh = (a & 3) * 8;
                set(rt, (r[rt] & ~(0xFFFFFFFFu >> sh)) | w >> sh);
                break;
            }
            case 0x28: wr(a, 1, r[rt]); break;
            case 0x29: wr(a, 2, r[rt]); break;
            case 0x2B: wr(a, 4, r[rt]); break;
            case 0x2A: { /* swl */
                uint32_t w = u32(a & ~3u), sh = (a & 3) * 8;
                wr(a & ~3u, 4, (w & ~(0xFFFFFFFFu >> (24 - sh))) | r[rt] >> (24 - sh));
                break;
            }
            case 0x2E: { /* swr */
                uint32_t w = u32(a & ~3u), sh = (a & 3) * 8;
                wr(a & ~3u, 4, (w & (sh ? 0xFFFFFFFFu >> (32 - sh) : 0)) | r[rt] << sh);
                break;
            }
            default: return false; /* the coprocessors: not in a setup */
            }
        }
        return false;
    }
};

void out(std::string &s, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    s.append(buf, n < (int)sizeof buf ? n : sizeof buf - 1);
}

/* An event script's words, its end (0) included (the port's native_script_length) */
int script_length(Psx &m, uint32_t s) {
    for (int i = 0;;) {
        int w = m.s16(s + i * 2), kind = w >> 8, sub = w & 0xFF;
        switch (kind) {
        case 1: i += sub == 2 ? 5 : 4; break;
        case 2: i += 5; break;
        case 3: i += sub == 1 ? 1 : sub == 4 ? 5 : 2; break;
        case 6: i += sub == 1 ? 4 : 3; break;
        default: return i + 1;
        }
        if (m.bad) return i;
    }
}

void codes(std::string &s, Psx &m, uint32_t p) {
    s += '[';
    for (int i = 0; p != 0 && m.u16(p + i * 2) != 0xFFFF && i < 64; i += 2)
        out(s, "%s[%u,%u]", i ? "," : "", m.u16(p + i * 2), m.u16(p + i * 2 + 2));
    s += ']';
}

void places(std::string &s, Psx &m, uint32_t b, bool list) {
    s += '[';
    for (int i = 0; b != 0 && i < (list ? 30 : 1); i++, b += 28) {
        int32_t serial = m.u32(b), id = m.u32(b + 4);
        if (list && (serial <= 0 || serial > 448 || m.u32(b + 12) == 0)) break;
        out(s, "%s{\"serial\":%d,\"id\":%d,\"areas\":[", i ? "," : "", serial, id);
        for (int a = 0; a < 4; a++) {
            uint32_t l = m.u32(b + 12 + a * 4);
            out(s, "%s{\"rate\":%d,\"battles\":[", a ? "," : "", l ? (int32_t)m.u32(l) : 0);
            for (int k = 0; l != 0 && k < 8; k++) {
                uint32_t t = m.u32(l + 4 + k * 4);
                out(s, "%s[%d,%d,%d]", k ? "," : "", t ? (int32_t)m.u32(t) : 0, t ? (int32_t)m.u32(t + 4) : 0,
                    t ? (int32_t)m.u32(t + 8) : 0);
            }
            s += "]}";
        }
        s += "]}";
    }
    s += ']';
}

/* [mode, overlay file, area name, place name] of each mode the stage (its fid) runs */
void modes(std::string &s, Psx &m, uint32_t e, int fid) {
    int n = 0;
    for (; m.u32(e) != 0 && !m.bad; e += 12) {
        if ((int32_t)m.u32(e + 4) != fid) continue;
        int mode = m.u32(e), area = 0, place = 0;
        for (uint32_t a = AREA_NAMES; m.s16(a + 2) != 0; a += 4)
            if (m.s16(a + 2) == mode) {
                area = m.u8(a), place = m.u8(a + 1);
                break;
            }
        out(s, "%s[%d,%d,%d,%d]", n++ ? "," : "", mode, fid, area, place);
    }
}

std::string dump(Psx &m, const char *name, int fid) {
    const uint32_t st = STATE;
    int images = 6;
    std::string s;
    auto i32 = [&](uint32_t a) { return (int32_t)m.u32(a); };

    out(s, "{\"name\":\"%s\",\"modes\":[", name);
    modes(s, m, STAGES, fid);
    s += "],\"euModes\":[";
    modes(s, m, EU_STAGES, fid);
    out(s, "],\n\"mapFile\":%d,\"sheetEntry\":%d,\"imageEntry\":%d,\"imageFile\":%d,\"textFile\":%d,"
           "\"soundBank\":%d,\"music\":%d,\"start\":[%d,%d],\"startDir\":%d,\"spriteColor\":[%u,%u,%u,%u],\n"
           "\"mapFiles\":[",
        i32(st + 0x08), i32(st + 0x0C), i32(st + 0x18), i32(st + 0x1C), i32(st + 0x44), i32(st + 0x3C), i32(st + 0x40),
        i32(st + 0x2C), i32(st + 0x30), i32(st + 0x34), m.u8(st + 0x38), m.u8(st + 0x39), m.u8(st + 0x3A), m.u8(st + 0x3B));
    for (int i = 0; i < 8; i++) out(s, "%s%d", i ? "," : "", i32(MAP + i * 4));

    /* the objects end with margin 0 */
    s += "],\n\"objects\":[";
    uint32_t first = m.u32(st + 0x10);
    for (uint32_t o = first; o != 0 && m.u8(o + 2) != 0 && !m.bad; o += 18) {
        out(s, "%s\n[", o == first ? "" : ",");
        for (int i = 0; i < 10; i++) out(s, "%u,", m.u8(o + i));
        out(s, "%d,%d,%d,%d]", m.s16(o + 10), m.s16(o + 12), m.s16(o + 14), m.s16(o + 16));
    }

    /* the slots end with type 0 */
    s += "],\n\"slots\":[";
    first = m.u32(st + 0x14);
    for (uint32_t t = first; t != 0 && m.u16(t + 8) != 0 && !m.bad; t += 24) {
        out(s, "%s\n[", t == first ? "" : ",");
        for (int i = 0; i < 12; i++) out(s, "%s%u", i ? "," : "", m.u16(t + i * 2));
        s += ']';
    }

    /* the characters, up to NULL; their talks up to the first without conditions */
    s += "],\n\"actors\":[";
    first = m.u32(st + 0x4C);
    for (uint32_t a = first; a != 0 && m.u32(a) != 0 && !m.bad; a += 4) {
        uint32_t e = m.u32(a);
        int image = m.s16(e + 10);
        out(s, "%s\n{\"id\":%d,\"image\":%d,\"x\":%d,\"y\":%d,\"dir\":%d,\"conditions\":", a == first ? "" : ",",
            m.s16(e + 8), image, m.s16(e + 12), m.s16(e + 14), m.s16(e + 16));
        codes(s, m, m.u32(e));
        s += ",\"talks\":[";
        uint32_t talks = m.u32(e + 4);
        for (uint32_t t = talks; t != 0 && !m.bad; t += 12) {
            out(s, "%s{\"entry\":%d,\"conditions\":", t == talks ? "" : ",", i32(t + 8));
            codes(s, m, m.u32(t));
            s += ",\"actions\":";
            codes(s, m, m.u32(t + 4));
            s += '}';
            if (m.u32(t) == 0) break;
        }
        s += "]}";
        if (image + 3 > images) images = image + 3;
    }

    /* the images: the field's two, then the characters' as far as they use */
    s += "],\n\"images\":[";
    uint32_t img = m.u32(st + 0x28);
    for (int i = 0; img != 0 && i < images; i++) {
        out(s, "%s[", i ? "," : "");
        for (int k = 0; k < 8; k++) out(s, "%s%d", k ? "," : "", m.s16(img + i * 16 + k * 2));
        s += ']';
    }

    s += "],\n\"places\":";
    places(s, m, m.places != 0 ? m.places : m.u32(st + 0x20), m.places != 0);

    /* the events end with id -1 */
    s += ",\n\"events\":[";
    first = m.u32(st + 0x24);
    for (uint32_t e = first; e != 0 && i32(e) != -1 && !m.bad; e += 20) {
        out(s, "%s\n{\"id\":%d,\"text\":%d,\"start\":%d,\"end\":%d,\"script\":", e == first ? "" : ",", i32(e),
            i32(e + 8), m.u32(e + 12) != 0, m.u32(e + 16) != 0);
        uint32_t script = m.u32(e + 4);
        if (script == 0) {
            s += "null}";
            continue;
        }
        int n = script_length(m, script);
        for (int i = 0; i < n; i++) out(s, "%s%d", i ? "," : "[", m.s16(script + i * 2));
        s += "]}";
    }
    s += "]}\n";
    return s;
}

bool write(const std::string &path, const std::string &s) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
    return fclose(f) == 0 && ok;
}

} // namespace

std::string Disc::open(const std::string &image) {
    path = image;
    if (path.size() > 4 && (path.substr(path.size() - 4) == ".cue" || path.substr(path.size() - 4) == ".CUE"))
        path = path.substr(0, path.size() - 4) + ".bin";
    FILE *f = fopen(path.c_str(), "rb");
    if (f == nullptr) return "can't open " + path;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    raw = size % 2352 == 0;
    sectorCount = (int)(size / (raw ? 2352 : 2048));

    std::vector<uint8_t> pvd = sectors(16, 1);
    if (pvd.size() < 2048 || memcmp(&pvd[1], "CD001", 5) != 0) return "not a CD image";
    std::vector<uint8_t> root = sectors(le32(&pvd[158]), (le32(&pvd[166]) + 2047) / 2048);
    for (size_t i = 0; i < root.size();) {
        if (root[i] == 0) {
            i = (i / 2048 + 1) * 2048;
            continue;
        }
        if (root[i + 32] >= 11 && memcmp(&root[i + 33], "SLES_039.36", 11) == 0) {
            uint32_t len = le32(&root[i + 10]);
            exe = sectors(le32(&root[i + 2]), (len + 2047) / 2048);
            exe.resize(len);
            break;
        }
        i += root[i];
    }
    if (exe.size() < 0x800 + (FILE_SECTOR_COUNTS - EXE_VRAM) + FILE_COUNT * 2)
        return "no SLES_039.36: this is Digimon World 2003 (Europe, SLES-03936) only";
    starts.resize(FILE_COUNT), counts.resize(FILE_COUNT);
    for (int i = 0; i < FILE_COUNT; i++) {
        const uint8_t *p = &exe[0x800 + FILE_SECTORS - EXE_VRAM + i * 4];
        starts[i] = (int32_t)le32(p);
        p = &exe[0x800 + FILE_SECTOR_COUNTS - EXE_VRAM + i * 2];
        counts[i] = p[0] | p[1] << 8;
    }
    return "";
}

std::vector<uint8_t> Disc::sectors(int lba, int count) const {
    std::vector<uint8_t> out;
    FILE *f = fopen(path.c_str(), "rb");
    if (f == nullptr) return out;
    out.resize((size_t)count * 2048);
    for (int n = 0; n < count; n++) {
        fseek(f, raw ? (long)(lba + n) * 2352 + 24 : (long)(lba + n) * 2048, SEEK_SET);
        if (fread(&out[(size_t)n * 2048], 1, 2048, f) != 2048) {
            out.clear();
            break;
        }
    }
    fclose(f);
    return out;
}

std::vector<uint8_t> Disc::file(int fid) const {
    if (fid < 0 || fid >= (int)starts.size() || starts[fid] <= 0 || counts[fid] == 0 ||
        starts[fid] + counts[fid] > sectorCount)
        return {};
    return sectors(starts[fid], counts[fid]);
}

int Disc::find(const std::string &dir, const std::string &name, int *size) const {
    std::vector<uint8_t> pvd = sectors(16, 1);
    if (pvd.size() < 2048) return -1;
    uint32_t ptSize = le32(&pvd[132]);
    std::vector<uint8_t> pt = sectors(le32(&pvd[140]), (ptSize + 2047) / 2048);
    if (pt.size() < ptSize) return -1;
    /* the directories by number (1 the root), each one's path */
    std::vector<std::string> paths{""};
    for (size_t i = 0; i + 8 <= ptSize;) {
        int n = pt[i], parent = pt[i + 6] | pt[i + 7] << 8;
        std::string d((const char *)&pt[i + 8], n);
        std::string p = paths.size() == 1 ? "" : (parent >= 1 && parent < (int)paths.size() ? paths[parent] : "") + "/" + d;
        if (p.size() && p[0] == '/') p.erase(0, 1);
        paths.push_back(p);
        if (p == dir) {
            uint32_t lba = le32(&pt[i + 2]);
            std::vector<uint8_t> first = sectors(lba, 1);
            if (first.size() < 2048) return -1;
            std::vector<uint8_t> d2 = sectors(lba, (le32(&first[10]) + 2047) / 2048);
            for (size_t k = 0; k < d2.size();) {
                if (d2[k] == 0) {
                    k = (k / 2048 + 1) * 2048;
                    continue;
                }
                std::string id((const char *)&d2[k + 33], d2[k + 32]);
                if (id.substr(0, id.find(';')) == name) {
                    if (size) *size = (int)le32(&d2[k + 10]);
                    return (int)le32(&d2[k + 2]);
                }
                k += d2[k];
            }
            return -1;
        }
        i += 8 + n + (n & 1);
    }
    return -1;
}

std::string dump_stages(const Disc &disc, const std::string &outDir, int *count) {
    Psx m;
    int size = 0;
    int lba = disc.find("AAA/PRO", "FIELDSTG.PRO", &size);
    if (lba < 0) return "no AAA/PRO/FIELDSTG.PRO on the disc";
    std::vector<uint8_t> fieldstg = disc.sectors(lba, (size + 2047) / 2048);
    fieldstg.resize(size);
    m.load(EXE_VRAM, disc.exe, 0x800);
    m.load(FIELDSTG_VRAM, fieldstg);

    *count = 0;
    for (const StageSetup &s : STAGE_SETUPS) {
        char file[16];
        snprintf(file, sizeof file, "%s.PRO", s.name);
        for (char *c = file; *c; c++) *c = (char)toupper(*c);
        lba = disc.find("AAA/PRO", file, &size);
        int fid = -1;
        for (int i = 0; i < (int)disc.starts.size(); i++)
            if (disc.starts[i] == lba) fid = i;
        if (lba < 0 || fid < 0) return std::string("no ") + file + " on the disc";
        std::vector<uint8_t> stage = disc.sectors(lba, (size + 2047) / 2048);
        stage.resize(size);

        /* a fresh stage (its data and bss), state and map's files, as the port's dump */
        memset(&m.ram[STAGE_VRAM & 0x1FFFFF], 0, 0x10000);
        m.load(STAGE_VRAM, stage);
        memset(&m.ram[STATE & 0x1FFFFF], 0, 0x64);
        memset(&m.ram[MAP & 0x1FFFFF], 0, 0x20);
        m.places = 0;
        if (!m.call(s.setup)) {
            char why[96];
            snprintf(why, sizeof why, "%s: its setup stopped at %08X", s.name, m.pc);
            return why;
        }
        std::string json = dump(m, s.name, fid);
        if (m.bad) return std::string(s.name) + ": its tables point outside the memory";
        if (!write(outDir + "/" + s.name + ".json", json)) return "can't write " + outDir + "/" + s.name + ".json";
        ++*count;
    }

    /* the characters, for their sprites */
    std::string c = "{\"animFiles\":[";
    for (int i = 0; i < CHARACTERS; i++) out(c, "%s%d", i ? "," : "", (int32_t)m.u32(FILE_ENTRIES + i * 4));
    c += "],\n\"widths\":[";
    for (int i = 0; i < CHARACTERS; i++) out(c, "%s%u", i ? "," : "", m.u8(ACTOR_WIDTHS + i));
    c += "]}\n";
    if (!write(outDir + "/characters.json", c)) return "can't write " + outDir + "/characters.json";
    return "";
}
