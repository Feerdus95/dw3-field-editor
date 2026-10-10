/* A small JSON reader for the stage files (dw2003 --dump-stages): numbers,
   strings, arrays and objects; no escapes beyond \" and \\. */
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
    enum Type { Null, Number, String, Array, Object } type = Null;
    double num = 0;
    std::string str;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    int i() const { return (int)num; }
    const Json &operator[](int k) const {
        static Json none;
        return k >= 0 && (size_t)k < items.size() ? items[k] : none;
    }
    const Json &operator[](const char *k) const {
        static Json none;
        auto it = fields.find(k);
        return it != fields.end() ? it->second : none;
    }
    size_t size() const { return items.size(); }

    /* Parses text; false on a malformed document */
    static bool parse(const std::string &text, Json &out) {
        size_t p = 0;
        return value(text, p, out);
    }

  private:
    static void ws(const std::string &s, size_t &p) {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++;
    }
    static bool value(const std::string &s, size_t &p, Json &out) {
        ws(s, p);
        if (p >= s.size()) return false;
        char c = s[p];
        if (c == '{' || c == '[') {
            bool obj = c == '{';
            out.type = obj ? Object : Array;
            p++;
            ws(s, p);
            if (p < s.size() && s[p] == (obj ? '}' : ']')) return ++p, true;
            for (;;) {
                std::string key;
                if (obj) {
                    Json k;
                    if (!value(s, p, k) || k.type != String) return false;
                    key = k.str;
                    ws(s, p);
                    if (p >= s.size() || s[p++] != ':') return false;
                }
                Json v;
                if (!value(s, p, v)) return false;
                if (obj) out.fields[key] = std::move(v);
                else out.items.push_back(std::move(v));
                ws(s, p);
                if (p >= s.size()) return false;
                if (s[p] == ',') { p++; continue; }
                if (s[p] == (obj ? '}' : ']')) return ++p, true;
                return false;
            }
        }
        if (c == '"') {
            out.type = String;
            for (p++; p < s.size() && s[p] != '"'; p++) {
                if (s[p] == '\\' && p + 1 < s.size()) p++;
                out.str += s[p];
            }
            return p++ < s.size();
        }
        if (s.compare(p, 4, "null") == 0) return p += 4, true;
        char *end;
        out.num = strtod(s.c_str() + p, &end);
        if (end == s.c_str() + p) return false;
        out.type = Number;
        p = end - s.c_str();
        return true;
    }
};
