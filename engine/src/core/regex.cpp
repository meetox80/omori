#include "regex.h"

#include <cstdlib>
#include <unordered_map>

#include "rx.h"

namespace re {

static Rx* compiled(const char* pattern, const char* flags) {
    static std::unordered_map<std::string, Rx*> cache;
    std::string key = std::string(flags) + '/' + pattern;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Rx* rx = rx_compile(pattern, flags);
    cache[key] = rx;
    return rx;
}

static const std::string g_empty;

const std::string& Groups::operator[](size_t i) const { return i < g.size() ? g[i] : g_empty; }

int Groups::num(size_t i, int def) const {
    if (!has(i)) return def;
    const std::string& s = g[i];
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    return end == s.c_str() ? def : (int)v;
}

bool search(const char* pattern, const char* flags, const std::string& s, Groups* out, size_t from) {
    Rx* rx = compiled(pattern, flags);
    if (!rx) return false;
    RxMatch m;
    if (!rx_search(rx, s.data(), s.size(), from, &m)) return false;
    if (out) {
        int n = rx_groups(rx);
        out->g.assign((size_t)n + 1, std::string());
        out->matched.assign((size_t)n + 1, false);
        for (int i = 0; i <= n; i++) {
            if (m.start[i] == (size_t)-1 || m.end[i] == (size_t)-1 || m.end[i] < m.start[i]) continue;
            out->g[(size_t)i] = s.substr(m.start[i], m.end[i] - m.start[i]);
            out->matched[(size_t)i] = true;
        }
        out->index = m.start[0];
        out->length = m.end[0] - m.start[0];
    }
    return true;
}

std::vector<std::string> all(const char* pattern, const char* flags, const std::string& s) {
    std::vector<std::string> out;
    size_t from = 0;
    Groups g;
    while (from <= s.size() && search(pattern, flags, s, &g, from)) {
        out.push_back(g[0]);
        from = g.index + (g.length ? g.length : 1);
    }
    return out;
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\r' || s[i] == '\n') {
            out.push_back(s.substr(start, i - start));
            while (i < s.size() && (s[i] == '\r' || s[i] == '\n')) i++;
            start = i;
        } else {
            i++;
        }
    }
    out.push_back(s.substr(start));
    return out;
}

std::string replace_all(const char* pattern, const char* flags, const std::string& s, const std::string& with) {
    std::string out;
    size_t from = 0;
    Groups g;
    while (from <= s.size() && search(pattern, flags, s, &g, from)) {
        out += s.substr(from, g.index - from);
        out += with;
        if (g.length == 0) {
            if (g.index < s.size()) out += s[g.index];
            from = g.index + 1;
        } else {
            from = g.index + g.length;
        }
    }
    if (from < s.size()) out += s.substr(from);
    return out;
}

std::string replace_fn(const char* pattern, const char* flags, const std::string& s, const std::function<std::string(const Groups&)>& fn, bool global) {
    std::string out;
    size_t from = 0;
    Groups g;
    while (from <= s.size() && search(pattern, flags, s, &g, from)) {
        out += s.substr(from, g.index - from);
        out += fn(g);
        if (g.length == 0) {
            if (g.index < s.size()) out += s[g.index];
            from = g.index + 1;
        } else {
            from = g.index + g.length;
        }
        if (!global) break;
    }
    if (from < s.size()) out += s.substr(from);
    return out;
}

int groups_of(const char* pattern, const char* flags) {
    Rx* rx = compiled(pattern, flags);
    return rx ? rx_groups(rx) : 0;
}

}
