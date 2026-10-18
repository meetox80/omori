#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "../platform/platform.h"
#include "random.h"
#include "regexp.h"
#include "vm.h"

namespace js {

static Val arg(std::vector<Val>& a, size_t i) { return i < a.size() ? a[i] : Val(); }

static long clamp_index(double v, long len) {
    if (std::isnan(v)) return 0;
    if (v < 0) {
        double r = len + std::trunc(v);
        return r < 0 ? 0 : (long)r;
    }
    double t = std::trunc(v);
    return t > len ? len : (long)t;
}

static std::string to_upper(std::string s) {
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static std::string to_lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

static std::string trim_str(const std::string& s, bool left, bool right) {
    size_t a = 0, b = s.size();
    if (left)
        while (a < b && is_space(s[a])) a++;
    if (right)
        while (b > a && is_space(s[b - 1])) b--;
    return s.substr(a, b - a);
}

static std::string pad_zero(std::string s, int len) {
    while ((int)s.size() < len) s = "0" + s;
    return s;
}

Val Vm::string_method(const Val& self, uint32_t key, std::vector<Val>& args, bool& found) {
    found = true;
    const std::string& s = self.s->data;
    const std::string& k = atom_name(key);
    long len = (long)s.size();
    if (k == "charAt") {
        long i = (long)arg(args, 0).to_num();
        if (std::isnan(arg(args, 0).to_num())) i = 0;
        return Val::str(i >= 0 && i < len ? std::string(1, s[(size_t)i]) : std::string());
    }
    if (k == "charCodeAt") {
        long i = args.empty() ? 0 : (long)arg(args, 0).to_num();
        return Val::num(i >= 0 && i < len ? (double)(unsigned char)s[(size_t)i] : NAN);
    }
    if (k == "indexOf" || k == "includes" || k == "contains") {
        std::string needle = arg(args, 0).to_str();
        long from = args.size() > 1 ? clamp_index(arg(args, 1).to_num(), len) : 0;
        size_t p = s.find(needle, (size_t)from);
        if (k == "indexOf") return Val::num(p == std::string::npos ? -1 : (double)p);
        return Val::boolean(p != std::string::npos);
    }
    if (k == "lastIndexOf") {
        size_t p = s.rfind(arg(args, 0).to_str());
        return Val::num(p == std::string::npos ? -1 : (double)p);
    }
    if (k == "startsWith") {
        std::string n = arg(args, 0).to_str();
        return Val::boolean(s.compare(0, n.size(), n) == 0);
    }
    if (k == "endsWith") {
        std::string n = arg(args, 0).to_str();
        return Val::boolean(s.size() >= n.size() && s.compare(s.size() - n.size(), n.size(), n) == 0);
    }
    if (k == "slice") {
        long a = args.empty() ? 0 : clamp_index(arg(args, 0).to_num(), len);
        long b = args.size() < 2 || arg(args, 1).is_undef() ? len : clamp_index(arg(args, 1).to_num(), len);
        return Val::str(b > a ? s.substr((size_t)a, (size_t)(b - a)) : std::string());
    }
    if (k == "substring") {
        double av = arg(args, 0).to_num(), bv = args.size() < 2 || arg(args, 1).is_undef() ? (double)len : arg(args, 1).to_num();
        long a = std::isnan(av) ? 0 : (long)std::min<double>(std::max<double>(av, 0), (double)len);
        long b = std::isnan(bv) ? 0 : (long)std::min<double>(std::max<double>(bv, 0), (double)len);
        if (a > b) std::swap(a, b);
        return Val::str(s.substr((size_t)a, (size_t)(b - a)));
    }
    if (k == "substr") {
        long a = clamp_index(arg(args, 0).to_num(), len);
        double lv = args.size() < 2 || arg(args, 1).is_undef() ? (double)(len - a) : arg(args, 1).to_num();
        long l = std::isnan(lv) ? 0 : (long)std::min<double>(std::max<double>(lv, 0), (double)(len - a));
        return Val::str(s.substr((size_t)a, (size_t)l));
    }
    if (k == "toUpperCase") return Val::str(to_upper(s));
    if (k == "toLowerCase") return Val::str(to_lower(s));
    if (k == "trim") return Val::str(trim_str(s, true, true));
    if (k == "trimLeft") return Val::str(trim_str(s, true, false));
    if (k == "trimRight") return Val::str(trim_str(s, false, true));
    if (k == "toString" || k == "valueOf") return self;
    if (k == "concat") {
        std::string r = s;
        for (auto& a : args) r += a.to_str();
        return Val::str(r);
    }
    if (k == "repeat") {
        double n = arg(args, 0).to_num();
        if (n < 0 || std::isinf(n)) throw_error("RangeError: Invalid count value");
        std::string r;
        for (long i = 0; i < (long)n; i++) r += s;
        return Val::str(r);
    }
    if (k == "padZero") return Val::str(pad_zero(s, arg(args, 0).to_int()));
    if (k == "padStart" || k == "padEnd") {
        int target = arg(args, 0).to_int();
        std::string fill = args.size() > 1 && !arg(args, 1).is_undef() ? arg(args, 1).to_str() : " ";
        if ((int)s.size() >= target || fill.empty()) return self;
        std::string pad;
        while ((int)(pad.size() + s.size()) < target) pad += fill;
        pad.resize((size_t)target - s.size());
        return Val::str(k == "padStart" ? pad + s : s + pad);
    }
    if (k == "format") {
        std::string r;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '%' && i + 1 < s.size() && isdigit((unsigned char)s[i + 1])) {
                size_t j = i + 1;
                int num = 0;
                while (j < s.size() && isdigit((unsigned char)s[j])) num = num * 10 + (s[j++] - '0');
                r += arg(args, (size_t)(num - 1)).to_str();
                i = j - 1;
            } else {
                r += s[i];
            }
        }
        return Val::str(r);
    }
    if (k == "match") return string_match(*this, s, arg(args, 0));
    if (k == "search") return string_search(*this, s, arg(args, 0));
    if (k == "split" && is_regexp(arg(args, 0))) {
        long limit = args.size() > 1 && !arg(args, 1).is_undef() ? (long)arg(args, 1).to_num() : -1;
        return string_split_regex(*this, s, arg(args, 0), limit);
    }
    if (k == "split") {
        std::vector<Val> parts;
        if (args.empty() || arg(args, 0).is_undef()) {
            parts.push_back(self);
            return make_array(std::move(parts));
        }
        std::string sep = arg(args, 0).to_str();
        long limit = args.size() > 1 && !arg(args, 1).is_undef() ? (long)arg(args, 1).to_num() : -1;
        if (sep.empty()) {
            for (char c : s) parts.push_back(Val::str(std::string(1, c)));
        } else {
            size_t start = 0;
            for (;;) {
                size_t p = s.find(sep, start);
                if (p == std::string::npos) {
                    parts.push_back(Val::str(s.substr(start)));
                    break;
                }
                parts.push_back(Val::str(s.substr(start, p - start)));
                start = p + sep.size();
            }
        }
        if (limit >= 0 && (long)parts.size() > limit) parts.resize((size_t)limit);
        return make_array(std::move(parts));
    }
    if (k == "replace") return string_replace(*this, s, arg(args, 0), arg(args, 1));
    if (k == "localeCompare") {
        std::string o = arg(args, 0).to_str();
        return Val::num(s < o ? -1 : s > o ? 1 : 0);
    }
    found = false;
    return Val();
}

static std::string to_fixed(double v, int digits) {
    if (std::isnan(v)) return "NaN";
    if (std::fabs(v) >= 1e21) return number_to_string(v);
    char exact[512];
    std::snprintf(exact, sizeof(exact), "%.*f", digits + 30, std::fabs(v));
    std::string e = exact;
    size_t dot = e.find('.');
    std::string ip = e.substr(0, dot), fp = e.substr(dot + 1);
    std::string keep = ip + fp.substr(0, (size_t)digits);
    bool up = fp[(size_t)digits] >= '5';
    if (up) {
        int k = (int)keep.size() - 1;
        while (k >= 0) {
            if (keep[(size_t)k] == '9') {
                keep[(size_t)k] = '0';
                k--;
            } else {
                keep[(size_t)k]++;
                break;
            }
        }
        if (k < 0) keep = "1" + keep;
    }
    size_t int_len = keep.size() - (size_t)digits;
    std::string s = (v < 0 ? "-" : "") + keep.substr(0, int_len);
    if (digits > 0) s += "." + keep.substr(int_len);
    if (s[0] == '-') {
        bool zero = true;
        for (char c : s.substr(1)) {
            if (c != '0' && c != '.') zero = false;
        }
        if (zero) s = s.substr(1);
    }
    return s;
}

static std::string to_radix(double v, int radix) {
    if (radix == 10 || std::isnan(v) || std::isinf(v)) return number_to_string(v);
    bool neg = v < 0;
    double iv = std::floor(std::fabs(v));
    std::string out;
    do {
        int d = (int)std::fmod(iv, radix);
        out.insert(out.begin(), (char)(d < 10 ? '0' + d : 'a' + d - 10));
        iv = std::floor(iv / radix);
    } while (iv > 0);
    return neg ? "-" + out : out;
}

Val Vm::number_method(const Val& self, uint32_t key, std::vector<Val>& args, bool& found) {
    found = true;
    double v = self.n;
    const std::string& k = atom_name(key);
    if (k == "toFixed") {
        int d = args.empty() ? 0 : arg(args, 0).to_int();
        if (d < 0 || d > 100) throw_error("RangeError: toFixed() digits argument must be between 0 and 100");
        return Val::str(to_fixed(v, d));
    }
    if (k == "toString") return Val::str(args.empty() || arg(args, 0).is_undef() ? number_to_string(v) : to_radix(v, arg(args, 0).to_int()));
    if (k == "valueOf") return self;
    if (k == "clamp") return Val::num(std::min(std::max(v, arg(args, 0).to_num()), arg(args, 1).to_num()));
    if (k == "mod") {
        double n = arg(args, 0).to_num();
        return Val::num(std::fmod(std::fmod(v, n) + n, n));
    }
    if (k == "padZero") return Val::str(pad_zero(number_to_string(v), arg(args, 0).to_int()));
    if (k == "toPrecision") {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*g", std::max(1, arg(args, 0).to_int()), v);
        return Val::str(buf);
    }
    found = false;
    return Val();
}

static int compare_default(const Val& a, const Val& b) {
    if (a.is_undef() && b.is_undef()) return 0;
    if (a.is_undef()) return 1;
    if (b.is_undef()) return -1;
    std::string x = a.to_str(), y = b.to_str();
    return x < y ? -1 : x > y ? 1 : 0;
}

Val Vm::array_method(const Val& self, uint32_t key, std::vector<Val>& args, bool& found) {
    found = true;
    std::vector<Val>& items = self.o->items;
    const std::string& k = atom_name(key);
    long len = (long)items.size();
    if (k == "push") {
        for (auto& a : args) items.push_back(a);
        return Val::num((double)items.size());
    }
    if (k == "pop") {
        if (items.empty()) return Val();
        Val v = items.back();
        items.pop_back();
        return v;
    }
    if (k == "shift") {
        if (items.empty()) return Val();
        Val v = items.front();
        items.erase(items.begin());
        return v;
    }
    if (k == "unshift") {
        items.insert(items.begin(), args.begin(), args.end());
        return Val::num((double)items.size());
    }
    if (k == "slice" || k == "clone") {
        long a = args.empty() ? 0 : clamp_index(arg(args, 0).to_num(), len);
        long b = args.size() < 2 || arg(args, 1).is_undef() ? len : clamp_index(arg(args, 1).to_num(), len);
        std::vector<Val> out;
        for (long i = a; i < b; i++) out.push_back(items[(size_t)i]);
        return make_array(std::move(out));
    }
    if (k == "splice") {
        long start = args.empty() ? 0 : clamp_index(arg(args, 0).to_num(), len);
        long del = args.size() < 2 ? len - start : (long)std::min<double>(std::max<double>(arg(args, 1).to_num(), 0), (double)(len - start));
        if (args.size() >= 2 && std::isnan(arg(args, 1).to_num())) del = 0;
        std::vector<Val> removed(items.begin() + start, items.begin() + start + del);
        items.erase(items.begin() + start, items.begin() + start + del);
        if (args.size() > 2) items.insert(items.begin() + start, args.begin() + 2, args.end());
        return make_array(std::move(removed));
    }
    if (k == "indexOf" || k == "contains" || k == "includes") {
        Val needle = arg(args, 0);
        long from = args.size() > 1 ? clamp_index(arg(args, 1).to_num(), len) : 0;
        for (long i = from; i < len; i++) {
            bool eq = strict_equals(items[(size_t)i], needle);
            if (!eq && k == "includes" && needle.is_num() && std::isnan(needle.n) && items[(size_t)i].is_num() && std::isnan(items[(size_t)i].n))
                eq = true;
            if (eq) return k == "indexOf" ? Val::num((double)i) : Val::boolean(true);
        }
        return k == "indexOf" ? Val::num(-1) : Val::boolean(false);
    }
    if (k == "lastIndexOf") {
        Val needle = arg(args, 0);
        for (long i = len - 1; i >= 0; i--) {
            if (strict_equals(items[(size_t)i], needle)) return Val::num((double)i);
        }
        return Val::num(-1);
    }
    if (k == "join") {
        std::string sep = args.empty() || arg(args, 0).is_undef() ? "," : arg(args, 0).to_str();
        std::string out;
        for (long i = 0; i < len; i++) {
            if (i) out += sep;
            if (!items[(size_t)i].is_nullish()) out += items[(size_t)i].to_str();
        }
        return Val::str(out);
    }
    if (k == "toString") return Val::str(self.to_str());
    if (k == "concat") {
        std::vector<Val> out = items;
        for (auto& a : args) {
            if (a.is_array()) out.insert(out.end(), a.o->items.begin(), a.o->items.end());
            else out.push_back(a);
        }
        return make_array(std::move(out));
    }
    if (k == "reverse") {
        std::reverse(items.begin(), items.end());
        return self;
    }
    if (k == "fill") {
        Val v = arg(args, 0);
        long a = args.size() > 1 ? clamp_index(arg(args, 1).to_num(), len) : 0;
        long b = args.size() > 2 ? clamp_index(arg(args, 2).to_num(), len) : len;
        for (long i = a; i < b; i++) items[(size_t)i] = v;
        return self;
    }
    if (k == "equals") {
        Val o = arg(args, 0);
        if (!o.is_array() || o.o->items.size() != items.size()) return Val::boolean(false);
        for (size_t i = 0; i < items.size(); i++) {
            const Val& x = items[i];
            const Val& y = o.o->items[i];
            if (x.is_array() && y.is_array()) {
                std::vector<Val> a1{y};
                bool f2;
                if (!array_method(x, key, a1, f2).truthy()) return Val::boolean(false);
            } else if (!strict_equals(x, y)) {
                return Val::boolean(false);
            }
        }
        return Val::boolean(true);
    }
    if (k == "remove") {
        Val needle = arg(args, 0);
        for (size_t i = 0; i < items.size();) {
            if (strict_equals(items[i], needle)) items.erase(items.begin() + (long)i);
            else i++;
        }
        return self;
    }
    if (k == "sort") {
        Val cmp = arg(args, 0);
        std::vector<Val> copy = items;
        if (cmp.is_function()) {
            std::stable_sort(copy.begin(), copy.end(), [&](const Val& a, const Val& b) {
                if (a.is_undef()) return false;
                if (b.is_undef()) return true;
                return call(cmp, Val(), {a, b}).to_num() < 0;
            });
        } else {
            std::stable_sort(copy.begin(), copy.end(), [](const Val& a, const Val& b) { return compare_default(a, b) < 0; });
        }
        self.o->items = copy;
        return self;
    }
    Val fn = arg(args, 0);
    if (k == "forEach" || k == "map" || k == "filter" || k == "some" || k == "every" || k == "find" || k == "findIndex") {
        if (!fn.is_function()) throw_type_error(arg(args, 0).to_str() + " is not a function");
        Val thisArg = arg(args, 1);
        std::vector<Val> out;
        for (long i = 0; i < len && i < (long)items.size(); i++) {
            Val item = items[(size_t)i];
            Val r = call(fn, thisArg, {item, Val::num((double)i), self});
            if (k == "map") out.push_back(r);
            else if (k == "filter") {
                if (r.truthy()) out.push_back(item);
            } else if (k == "some") {
                if (r.truthy()) return Val::boolean(true);
            } else if (k == "every") {
                if (!r.truthy()) return Val::boolean(false);
            } else if (k == "find") {
                if (r.truthy()) return item;
            } else if (k == "findIndex") {
                if (r.truthy()) return Val::num((double)i);
            }
        }
        if (k == "map" || k == "filter") return make_array(std::move(out));
        if (k == "some") return Val::boolean(false);
        if (k == "every") return Val::boolean(true);
        if (k == "findIndex") return Val::num(-1);
        return Val();
    }
    if (k == "reduce") {
        if (!fn.is_function()) throw_type_error("reduce callback is not a function");
        long i = 0;
        Val acc;
        if (args.size() > 1) acc = args[1];
        else {
            if (items.empty()) throw_type_error("Reduce of empty array with no initial value");
            acc = items[0];
            i = 1;
        }
        for (; i < (long)items.size(); i++) acc = call(fn, Val(), {acc, items[(size_t)i], Val::num((double)i), self});
        return acc;
    }
    found = false;
    return Val();
}

Val Vm::object_method(const Val& self, uint32_t key, std::vector<Val>& args, bool& found) {
    found = true;
    const std::string& k = atom_name(key);
    if (k == "hasOwnProperty") return Val::boolean(self.o->find(key_of(arg(args, 0))) != nullptr);
    if (k == "toString") return Val::str(self.to_str());
    found = false;
    return Val();
}

static void json_quote(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    out += '"';
}

static void json_write(const Val& v, std::string& out, int depth) {
    if (depth > 64) {
        out += "null";
        return;
    }
    switch (v.t) {
    case T::Undef:
    case T::Null: out += "null"; return;
    case T::Bool: out += v.b ? "true" : "false"; return;
    case T::Num: out += std::isfinite(v.n) ? number_to_string(v.n) : "null"; return;
    case T::Str: json_quote(v.s->data, out); return;
    case T::Obj: break;
    }
    if (v.o->kind == Kind::Array) {
        out += '[';
        for (size_t i = 0; i < v.o->items.size(); i++) {
            if (i) out += ',';
            const Val& x = v.o->items[i];
            if (x.is_undef() || x.is_function()) out += "null";
            else json_write(x, out, depth + 1);
        }
        out += ']';
        return;
    }
    if (v.is_function()) {
        out += "null";
        return;
    }
    out += '{';
    bool first = true;
    std::vector<uint32_t> keys;
    ordered_keys(v.o, keys);
    for (uint32_t k : keys) {
        const Val* pv = v.o->find(k);
        if (!pv || pv->is_undef() || pv->is_function()) continue;
        if (!first) out += ',';
        first = false;
        json_quote(atom_name(k), out);
        out += ':';
        json_write(*pv, out, depth + 1);
    }
    out += '}';
}

std::string json_stringify(const Val& v) {
    std::string out;
    json_write(v, out, 0);
    return out;
}

static size_t json_skip(const std::string& s, size_t i) {
    while (i < s.size() && is_space(s[i])) i++;
    return i;
}

static Val json_parse(Vm& vm, const std::string& s, size_t& i) {
    i = json_skip(s, i);
    if (i >= s.size()) vm.throw_error("SyntaxError: Unexpected end of JSON input");
    char c = s[i];
    if (c == '{') {
        Val o = make_object();
        i = json_skip(s, i + 1);
        if (i < s.size() && s[i] == '}') {
            i++;
            return o;
        }
        for (;;) {
            Val k = json_parse(vm, s, i);
            i = json_skip(s, i);
            if (i >= s.size() || s[i] != ':') vm.throw_error("SyntaxError: bad JSON");
            i++;
            Val v = json_parse(vm, s, i);
            o.o->set(key_of(k), v);
            i = json_skip(s, i);
            if (i < s.size() && s[i] == ',') {
                i++;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                i++;
                return o;
            }
            vm.throw_error("SyntaxError: bad JSON");
        }
    }
    if (c == '[') {
        std::vector<Val> items;
        i = json_skip(s, i + 1);
        if (i < s.size() && s[i] == ']') {
            i++;
            return make_array();
        }
        for (;;) {
            items.push_back(json_parse(vm, s, i));
            i = json_skip(s, i);
            if (i < s.size() && s[i] == ',') {
                i++;
                continue;
            }
            if (i < s.size() && s[i] == ']') {
                i++;
                return make_array(std::move(items));
            }
            vm.throw_error("SyntaxError: bad JSON");
        }
    }
    if (c == '"') {
        std::string out;
        i++;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[++i];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int k = 0; k < 4 && i + 1 < s.size(); k++) {
                        char h = s[++i];
                        cp = cp * 16 + (unsigned)(isdigit((unsigned char)h) ? h - '0' : (tolower(h) - 'a' + 10));
                    }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += e;
                }
                i++;
            } else {
                out += s[i++];
            }
        }
        i++;
        return Val::str(out);
    }
    if (!s.compare(i, 4, "true")) {
        i += 4;
        return Val::boolean(true);
    }
    if (!s.compare(i, 5, "false")) {
        i += 5;
        return Val::boolean(false);
    }
    if (!s.compare(i, 4, "null")) {
        i += 4;
        return Val::null();
    }
    size_t start = i;
    while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
    if (start == i) vm.throw_error("SyntaxError: Unexpected token in JSON");
    return Val::num(std::strtod(s.substr(start, i - start).c_str(), nullptr));
}

Val json_parse_text(const std::string& s) {
    size_t i = 0;
    return json_parse(vm(), s, i);
}

static Val native(NativeFn fn) { return make_native(std::move(fn)); }

static double parse_int(const std::string& in, int radix) {
    size_t i = 0;
    while (i < in.size() && is_space(in[i])) i++;
    bool neg = false;
    if (i < in.size() && (in[i] == '+' || in[i] == '-')) neg = in[i++] == '-';
    if (radix == 0) {
        radix = 10;
        if (i + 1 < in.size() && in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')) {
            radix = 16;
            i += 2;
        }
    } else if (radix == 16 && i + 1 < in.size() && in[i] == '0' && (in[i + 1] == 'x' || in[i + 1] == 'X')) {
        i += 2;
    }
    if (radix < 2 || radix > 36) return NAN;
    double v = 0;
    bool any = false;
    for (; i < in.size(); i++) {
        int c = tolower((unsigned char)in[i]);
        int d = isdigit(c) ? c - '0' : (c >= 'a' && c <= 'z') ? c - 'a' + 10 : 99;
        if (d >= radix) break;
        v = v * radix + d;
        any = true;
    }
    if (!any) return NAN;
    return neg ? -v : v;
}

static double parse_float(const std::string& in) {
    size_t i = 0;
    while (i < in.size() && is_space(in[i])) i++;
    std::string rest = in.substr(i);
    if (!rest.compare(0, 8, "Infinity") || !rest.compare(0, 9, "+Infinity")) return INFINITY;
    if (!rest.compare(0, 9, "-Infinity")) return -INFINITY;
    size_t j = 0;
    if (j < rest.size() && (rest[j] == '+' || rest[j] == '-')) j++;
    bool digits = false;
    while (j < rest.size() && isdigit((unsigned char)rest[j])) {
        j++;
        digits = true;
    }
    if (j < rest.size() && rest[j] == '.') {
        j++;
        while (j < rest.size() && isdigit((unsigned char)rest[j])) {
            j++;
            digits = true;
        }
    }
    if (!digits) return NAN;
    if (j < rest.size() && (rest[j] == 'e' || rest[j] == 'E')) {
        size_t k = j + 1;
        if (k < rest.size() && (rest[k] == '+' || rest[k] == '-')) k++;
        if (k < rest.size() && isdigit((unsigned char)rest[k])) {
            while (k < rest.size() && isdigit((unsigned char)rest[k])) k++;
            j = k;
        }
    }
    return std::strtod(rest.substr(0, j).c_str(), nullptr);
}

void Vm::install_builtins() {
    install_regexp(*this);
    eval_native_ = native([](Vm& vm, const Val&, std::vector<Val>& a) -> Val {
        if (a.empty() || !a[0].is_str()) return a.empty() ? Val() : a[0];
        return vm.eval_code(a[0].s->data, nullptr);
    });
    set_global("eval", eval_native_);
    Val math = make_object();
    auto m1 = [&](const char* name, double (*fn)(double)) {
        math.o->set(atom(name), native([fn](Vm&, const Val&, std::vector<Val>& a) { return Val::num(fn(arg(a, 0).to_num())); }));
    };
    m1("abs", [](double x) { return std::fabs(x); });
    m1("floor", [](double x) { return std::floor(x); });
    m1("ceil", [](double x) { return std::ceil(x); });
    m1("round", [](double x) { return std::floor(x + 0.5); });
    m1("trunc", [](double x) { return std::trunc(x); });
    m1("sign", [](double x) { return std::isnan(x) ? x : (double)((x > 0) - (x < 0)); });
    m1("sqrt", [](double x) { return std::sqrt(x); });
    m1("sin", [](double x) { return std::sin(x); });
    m1("cos", [](double x) { return std::cos(x); });
    m1("tan", [](double x) { return std::tan(x); });
    m1("log", [](double x) { return std::log(x); });
    m1("exp", [](double x) { return std::exp(x); });
    m1("asin", [](double x) { return std::asin(x); });
    m1("acos", [](double x) { return std::acos(x); });
    m1("atan", [](double x) { return std::atan(x); });
    math.o->set(atom("PI"), Val::num(M_PI));
    math.o->set(atom("E"), Val::num(M_E));
    math.o->set(atom("pow"), native([](Vm&, const Val&, std::vector<Val>& a) { return Val::num(std::pow(arg(a, 0).to_num(), arg(a, 1).to_num())); }));
    math.o->set(atom("atan2"), native([](Vm&, const Val&, std::vector<Val>& a) { return Val::num(std::atan2(arg(a, 0).to_num(), arg(a, 1).to_num())); }));
    math.o->set(atom("hypot"), native([](Vm&, const Val&, std::vector<Val>& a) {
        double s = 0;
        for (auto& v : a) s += v.to_num() * v.to_num();
        return Val::num(std::sqrt(s));
    }));
    math.o->set(atom("min"), native([](Vm&, const Val&, std::vector<Val>& a) {
        double r = INFINITY;
        for (auto& v : a) {
            double x = v.to_num();
            if (std::isnan(x)) return Val::num(NAN);
            if (x < r) r = x;
        }
        return Val::num(r);
    }));
    math.o->set(atom("max"), native([](Vm&, const Val&, std::vector<Val>& a) {
        double r = -INFINITY;
        for (auto& v : a) {
            double x = v.to_num();
            if (std::isnan(x)) return Val::num(NAN);
            if (x > r) r = x;
        }
        return Val::num(r);
    }));
    math.o->set(atom("random"), native([](Vm&, const Val&, std::vector<Val>&) { return Val::num(rng::next()); }));
    math.o->set(atom("randomInt"), native([](Vm&, const Val&, std::vector<Val>& a) { return Val::num(std::floor(arg(a, 0).to_num() * rng::next())); }));
    set_global("Math", math);

    Val json = make_object();
    json.o->set(atom("stringify"), native([](Vm&, const Val&, std::vector<Val>& a) {
        if (arg(a, 0).is_undef()) return Val();
        return Val::str(json_stringify(arg(a, 0)));
    }));
    json.o->set(atom("parse"), native([](Vm& vm, const Val&, std::vector<Val>& a) {
        std::string s = arg(a, 0).to_str();
        size_t i = 0;
        Val v = json_parse(vm, s, i);
        return v;
    }));
    set_global("JSON", json);

    Val object = native([](Vm&, const Val&, std::vector<Val>& a) {
        if (!a.empty() && a[0].is_obj()) return a[0];
        return make_object();
    });
    object.o->set(atom("keys"), native([](Vm& vm, const Val&, std::vector<Val>& a) {
        Val o = arg(a, 0);
        std::vector<Val> keys;
        if (o.is_array()) {
            for (size_t i = 0; i < o.o->items.size(); i++) keys.push_back(Val::str(std::to_string(i)));
        } else if (o.is_obj()) {
            if (o.o->kind == Kind::Host && o.o->cls->keys) {
                std::vector<uint32_t> ks;
                o.o->cls->keys(o.o, ks);
                for (uint32_t k : ks) keys.push_back(Val::str(atom_str(k)));
            }
            std::vector<uint32_t> own;
            ordered_keys(o.o, own);
            for (uint32_t k : own) keys.push_back(Val::str(atom_str(k)));
        } else if (o.is_nullish()) {
            vm.throw_type_error("Cannot convert undefined or null to object");
        }
        return make_array(std::move(keys));
    }));
    object.o->set(atom("values"), native([](Vm& vm, const Val&, std::vector<Val>& a) {
        Val o = arg(a, 0);
        std::vector<Val> vals;
        if (o.is_array()) vals = o.o->items;
        else if (o.is_obj()) {
            if (o.o->kind == Kind::Host && o.o->cls->keys) {
                std::vector<uint32_t> ks;
                o.o->cls->keys(o.o, ks);
                for (uint32_t k : ks) vals.push_back(vm.get(o, k));
            }
            std::vector<uint32_t> own;
            ordered_keys(o.o, own);
            for (uint32_t k : own) vals.push_back(*o.o->find(k));
        }
        return make_array(std::move(vals));
    }));
    object.o->set(atom("entries"), native([](Vm& vm, const Val&, std::vector<Val>& a) {
        Val o = arg(a, 0);
        std::vector<Val> out;
        if (o.is_array()) {
            for (size_t i = 0; i < o.o->items.size(); i++) out.push_back(make_array({Val::str(number_to_string((double)i)), o.o->items[i]}));
        } else if (o.is_obj()) {
            if (o.o->kind == Kind::Host && o.o->cls->keys) {
                std::vector<uint32_t> ks;
                o.o->cls->keys(o.o, ks);
                for (uint32_t k : ks) out.push_back(make_array({Val::str(atom_name(k)), vm.get(o, k)}));
            }
            std::vector<uint32_t> own;
            ordered_keys(o.o, own);
            for (uint32_t k : own) out.push_back(make_array({Val::str(atom_name(k)), *o.o->find(k)}));
        }
        return make_array(std::move(out));
    }));
    object.o->set(atom("assign"), native([](Vm& vm, const Val&, std::vector<Val>& a) {
        Val target = arg(a, 0);
        for (size_t i = 1; i < a.size(); i++) {
            if (!a[i].is_obj()) continue;
            for (auto& p : a[i].o->props) vm.put(target, p.key, p.v);
        }
        return target;
    }));
    proto_object_ = make_object();
    object.o->set(atom("prototype"), proto_object_);
    set_global("Object", object);

    Val array = native([](Vm&, const Val&, std::vector<Val>& a) {
        if (a.size() == 1 && a[0].is_num()) {
            std::vector<Val> items((size_t)std::max(0.0, a[0].n));
            return make_array(std::move(items));
        }
        return make_array(a);
    });
    array.o->set(atom("isArray"), native([](Vm&, const Val&, std::vector<Val>& a) { return Val::boolean(arg(a, 0).is_array()); }));
    array.o->set(atom("from"), native([](Vm& vm, const Val&, std::vector<Val>& a) {
        Val src = arg(a, 0);
        Val fn = arg(a, 1);
        std::vector<Val> items;
        if (src.is_array()) items = src.o->items;
        else if (src.is_str()) {
            for (char c : src.s->data) items.push_back(Val::str(std::string(1, c)));
        } else if (src.is_obj()) {
            double n = vm.get(src, atom("length")).to_num();
            if (n > 0 && n < 1000000) items.resize((size_t)n);
        }
        if (fn.is_function()) {
            for (size_t i = 0; i < items.size(); i++) items[i] = vm.call(fn, Val(), {items[i], Val::num((double)i)});
        }
        return make_array(std::move(items));
    }));
    proto_array_ = make_object();
    array.o->set(atom("prototype"), proto_array_);
    set_global("Array", array);

    set_global("parseInt", native([](Vm&, const Val&, std::vector<Val>& a) { return Val::num(parse_int(arg(a, 0).to_str(), arg(a, 1).to_int())); }));
    set_global("parseFloat", native([](Vm&, const Val&, std::vector<Val>& a) { return Val::num(parse_float(arg(a, 0).to_str())); }));
    set_global("isNaN", native([](Vm&, const Val&, std::vector<Val>& a) { return Val::boolean(std::isnan(arg(a, 0).to_num())); }));
    set_global("isFinite", native([](Vm&, const Val&, std::vector<Val>& a) { return Val::boolean(std::isfinite(arg(a, 0).to_num())); }));
    Val number = native([](Vm&, const Val&, std::vector<Val>& a) { return Val::num(a.empty() ? 0 : a[0].to_num()); });
    number.o->set(atom("isInteger"), native([](Vm&, const Val&, std::vector<Val>& a) {
        Val v = arg(a, 0);
        return Val::boolean(v.is_num() && std::isfinite(v.n) && v.n == std::floor(v.n));
    }));
    number.o->set(atom("MAX_SAFE_INTEGER"), Val::num(9007199254740991.0));
    proto_number_ = make_object();
    number.o->set(atom("prototype"), proto_number_);
    set_global("Number", number);
    Val string_ctor = native([](Vm&, const Val&, std::vector<Val>& a) { return Val::str(a.empty() ? std::string() : a[0].to_str()); });
    proto_string_ = make_object();
    string_ctor.o->set(atom("prototype"), proto_string_);
    set_global("String", string_ctor);
    Val boolean_ctor = native([](Vm&, const Val&, std::vector<Val>& a) { return Val::boolean(!a.empty() && a[0].truthy()); });
    proto_boolean_ = make_object();
    boolean_ctor.o->set(atom("prototype"), proto_boolean_);
    set_global("Boolean", boolean_ctor);
    Val function_ctor = native([](Vm& vm, const Val&, std::vector<Val>&) -> Val { vm.throw_error("EvalError: Function constructor is not available"); });
    proto_function_ = make_object();
    function_ctor.o->set(atom("prototype"), proto_function_);
    set_global("Function", function_ctor);
    set_global("Infinity", Val::num(INFINITY));
    set_global("NaN", Val::num(NAN));
    set_global("undefined", Val());
    Val console = make_object();
    Val logfn = native([](Vm&, const Val&, std::vector<Val>& a) {
        std::string s;
        for (size_t i = 0; i < a.size(); i++) {
            if (i) s += " ";
            s += a[i].is_obj() && !a[i].is_function() ? json_stringify(a[i]) : a[i].to_str();
        }
        plat::log("console: %s", s.c_str());
        return Val();
    });
    console.o->set(atom("log"), logfn);
    console.o->set(atom("warn"), logfn);
    console.o->set(atom("error"), logfn);
    set_global("console", console);
    Val error = native([](Vm&, const Val& self, std::vector<Val>& a) {
        Obj* e = obj_new(Kind::Error);
        Val ev = Val::obj(e);
        e->set(atom("message"), Val::str(arg(a, 0).is_undef() ? std::string() : arg(a, 0).to_str()));
        e->set(atom("name"), Val::str("Error"));
        (void)self;
        return ev;
    });
    set_global("Error", error);
    Val date = native([](Vm&, const Val&, std::vector<Val>&) { return Val::num(std::floor(plat::time_seconds() * 1000.0)); });
    date.o->set(atom("now"), native([](Vm&, const Val&, std::vector<Val>&) { return Val::num(std::floor(plat::time_seconds() * 1000.0)); }));
    set_global("Date", date);
}

}
