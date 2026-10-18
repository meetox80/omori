#pragma once
#include <functional>
#include <string>
#include <vector>

namespace re {

struct Groups {
    std::vector<std::string> g;
    std::vector<bool> matched;
    size_t index = 0;
    size_t length = 0;
    const std::string& operator[](size_t i) const;
    bool has(size_t i) const { return i < matched.size() && matched[i]; }
    int num(size_t i, int def = 0) const;
};

bool search(const char* pattern, const char* flags, const std::string& s, Groups* out = nullptr, size_t from = 0);
inline bool test(const char* pattern, const std::string& s, Groups* out = nullptr) { return search(pattern, "i", s, out); }
std::vector<std::string> all(const char* pattern, const char* flags, const std::string& s);
std::vector<std::string> split_lines(const std::string& s);
std::string replace_all(const char* pattern, const char* flags, const std::string& s, const std::string& with);
int groups_of(const char* pattern, const char* flags);
std::string replace_fn(const char* pattern, const char* flags, const std::string& s, const std::function<std::string(const Groups&)>& fn, bool global = true);

}
