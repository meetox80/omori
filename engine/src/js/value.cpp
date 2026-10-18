#include "value.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace js {

Val::Val(const Val& v) : t(v.t), n(v.n) {
    if (t == T::Str) str_retain(s);
    else if (t == T::Obj) obj_retain(o);
}

Val::Val(Val&& v) noexcept : t(v.t), n(v.n) { v.t = T::Undef; }

Val& Val::operator=(const Val& v) {
    if (this == &v) return *this;
    if (v.t == T::Str) str_retain(v.s);
    else if (v.t == T::Obj) obj_retain(v.o);
    T ot = t;
    Str* os = s;
    Obj* oo = o;
    t = v.t;
    n = v.n;
    if (ot == T::Str) str_release(os);
    else if (ot == T::Obj) obj_release(oo);
    return *this;
}

Val& Val::operator=(Val&& v) noexcept {
    if (this == &v) return *this;
    T ot = t;
    Str* os = s;
    Obj* oo = o;
    t = v.t;
    n = v.n;
    v.t = T::Undef;
    if (ot == T::Str) str_release(os);
    else if (ot == T::Obj) obj_release(oo);
    return *this;
}

Val::~Val() {
    if (t == T::Str) str_release(s);
    else if (t == T::Obj) obj_release(o);
}

Val Val::str(const std::string& x) {
    Val v;
    v.t = T::Str;
    v.s = str_new(x);
    str_retain(v.s);
    return v;
}

Val Val::str(const char* x) { return str(std::string(x ? x : "")); }

Val Val::str(Str* s) {
    Val v;
    v.t = T::Str;
    v.s = s;
    str_retain(s);
    return v;
}

Val Val::obj(Obj* o) {
    Val v;
    if (!o) return Val::null();
    v.t = T::Obj;
    v.o = o;
    obj_retain(o);
    return v;
}

bool Val::is_array() const { return t == T::Obj && o->kind == Kind::Array; }
bool Val::is_function() const { return t == T::Obj && (o->kind == Kind::Function || o->kind == Kind::Native || (o->kind == Kind::Host && o->cls && o->cls->call)); }

bool Val::truthy() const {
    switch (t) {
    case T::Undef:
    case T::Null: return false;
    case T::Bool: return b;
    case T::Num: return n != 0 && !std::isnan(n);
    case T::Str: return !s->data.empty();
    case T::Obj: return true;
    }
    return false;
}

double string_to_number(const std::string& in) {
    size_t a = 0, e = in.size();
    while (a < e && (in[a] == ' ' || in[a] == '\t' || in[a] == '\n' || in[a] == '\r' || in[a] == '\f' || in[a] == '\v')) a++;
    while (e > a && (in[e - 1] == ' ' || in[e - 1] == '\t' || in[e - 1] == '\n' || in[e - 1] == '\r' || in[e - 1] == '\f' || in[e - 1] == '\v')) e--;
    if (a == e) return 0;
    std::string s = in.substr(a, e - a);
    if (s == "Infinity" || s == "+Infinity") return INFINITY;
    if (s == "-Infinity") return -INFINITY;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        double v = 0;
        for (size_t i = 2; i < s.size(); i++) {
            char c = s[i];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) return NAN;
            v = v * 16 + d;
        }
        return v;
    }
    size_t i = 0;
    if (s[i] == '+' || s[i] == '-') i++;
    bool digits = false;
    while (i < s.size() && isdigit((unsigned char)s[i])) {
        i++;
        digits = true;
    }
    if (i < s.size() && s[i] == '.') {
        i++;
        while (i < s.size() && isdigit((unsigned char)s[i])) {
            i++;
            digits = true;
        }
    }
    if (!digits) return NAN;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        size_t k = i + 1;
        if (k < s.size() && (s[k] == '+' || s[k] == '-')) k++;
        bool ed = false;
        while (k < s.size() && isdigit((unsigned char)s[k])) {
            k++;
            ed = true;
        }
        if (!ed) return NAN;
        i = k;
    }
    if (i != s.size()) return NAN;
    return std::strtod(s.c_str(), nullptr);
}

double Val::to_num() const {
    switch (t) {
    case T::Undef: return NAN;
    case T::Null: return 0;
    case T::Bool: return b ? 1 : 0;
    case T::Num: return n;
    case T::Str: return string_to_number(s->data);
    case T::Obj:
        if (o->kind == Kind::Array) {
            if (o->items.empty()) return 0;
            if (o->items.size() == 1) return o->items[0].to_num();
        }
        return NAN;
    }
    return NAN;
}

int Val::to_int() const {
    double d = to_num();
    if (std::isnan(d) || std::isinf(d)) return 0;
    double m = std::fmod(std::trunc(d), 4294967296.0);
    if (m < 0) m += 4294967296.0;
    uint32_t u = (uint32_t)m;
    return (int32_t)u;
}

std::string number_to_string(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return "0";
    if (std::fabs(d) < 1e21 && d == std::trunc(d)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f", d);
        return buf;
    }
    char buf[64];
    for (int prec = 1; prec <= 17; prec++) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    std::string s = buf;
    size_t e = s.find('e');
    if (e == std::string::npos) return s;
    std::string mant = s.substr(0, e);
    int ex = std::atoi(s.c_str() + e + 1);
    bool neg = !mant.empty() && mant[0] == '-';
    if (neg) mant = mant.substr(1);
    std::string digits;
    for (char c : mant) {
        if (c != '.') digits += c;
    }
    int point = 1 + ex;
    std::string out;
    if (ex >= 21 || ex <= -7) {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        out += "e";
        out += ex >= 0 ? "+" : "-";
        out += std::to_string(ex >= 0 ? ex : -ex);
    } else if (point <= 0) {
        out = "0." + std::string((size_t)-point, '0') + digits;
    } else if ((size_t)point >= digits.size()) {
        out = digits + std::string((size_t)point - digits.size(), '0');
    } else {
        out = digits.substr(0, (size_t)point) + "." + digits.substr((size_t)point);
    }
    return neg ? "-" + out : out;
}

static std::string array_to_string(const Obj* o) {
    std::string out;
    for (size_t i = 0; i < o->items.size(); i++) {
        if (i) out += ",";
        const Val& v = o->items[i];
        if (!v.is_nullish()) out += v.to_str();
    }
    return out;
}

std::string Val::to_str() const {
    switch (t) {
    case T::Undef: return "undefined";
    case T::Null: return "null";
    case T::Bool: return b ? "true" : "false";
    case T::Num: return number_to_string(n);
    case T::Str: return s->data;
    case T::Obj:
        if (o->kind == Kind::Array) return array_to_string(o);
        if (o->kind == Kind::Function || o->kind == Kind::Native) return "function () { [native code] }";
        if (o->kind == Kind::Error) {
            const Val* m = o->find(atom("message"));
            return std::string("Error: ") + (m ? m->to_str() : "");
        }
        return "[object Object]";
    }
    return "";
}

static const std::string g_empty;

const std::string& Val::str_ref() const { return t == T::Str ? s->data : g_empty; }

Str* str_new(const std::string& s) {
    Str* r = new Str();
    r->data = s;
    return r;
}

void str_retain(Str* s) {
    if (s) s->refs++;
}

void str_release(Str* s) {
    if (s && --s->refs == 0) delete s;
}

struct AtomTable {
    std::unordered_map<std::string, uint32_t> ids;
    std::vector<Str*> names;
    AtomTable() {
        Str* empty = str_new("");
        empty->refs = 1;
        names.push_back(empty);
        ids[""] = 0;
    }
};

static AtomTable& atoms() {
    static AtomTable t;
    return t;
}

uint32_t atom(const std::string& s) {
    AtomTable& t = atoms();
    auto it = t.ids.find(s);
    if (it != t.ids.end()) return it->second;
    uint32_t id = (uint32_t)t.names.size();
    Str* st = str_new(s);
    st->refs = 1;
    st->atom = id;
    t.names.push_back(st);
    t.ids.emplace(s, id);
    return id;
}

uint32_t atom(const char* s) {
    if (!s) s = "";
    struct Entry {
        const char* p;
        uint32_t id;
    };
    static Entry cache[512];
    Entry& e = cache[((uintptr_t)s >> 2) & 511];
    if (e.p == s && std::strcmp(atoms().names[e.id]->data.c_str(), s) == 0) return e.id;
    uint32_t id = atom(std::string(s));
    e.p = s;
    e.id = id;
    return id;
}

const std::string& atom_name(uint32_t a) {
    AtomTable& t = atoms();
    return a < t.names.size() ? t.names[a]->data : g_empty;
}

Str* atom_str(uint32_t a) {
    AtomTable& t = atoms();
    return a < t.names.size() ? t.names[a] : t.names[0];
}

Val* Obj::find(uint32_t key) {
    for (auto& p : props) {
        if (p.key == key) return &p.v;
    }
    return nullptr;
}

const Val* Obj::find(uint32_t key) const {
    for (auto& p : props) {
        if (p.key == key) return &p.v;
    }
    return nullptr;
}

void Obj::set(uint32_t key, const Val& v) {
    for (auto& p : props) {
        if (p.key == key) {
            p.v = v;
            return;
        }
    }
    props.push_back({key, v});
}

bool Obj::remove(uint32_t key) {
    for (size_t i = 0; i < props.size(); i++) {
        if (props[i].key == key) {
            props.erase(props.begin() + (long)i);
            return true;
        }
    }
    return false;
}

Val* Expando::find(uint32_t key) {
    for (auto& p : props) {
        if (p.key == key) return &p.v;
    }
    return nullptr;
}

void Expando::set(uint32_t key, const Val& v) {
    for (auto& p : props) {
        if (p.key == key) {
            p.v = v;
            return;
        }
    }
    props.push_back({key, v});
}

static bool array_index_key(uint32_t key, uint32_t& out) {
    const std::string& s = atom_name(key);
    if (s.empty() || s.size() > 10 || (s.size() > 1 && s[0] == '0')) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (uint64_t)(c - '0');
    }
    if (v >= 4294967295ull) return false;
    out = (uint32_t)v;
    return true;
}

void ordered_keys(const Obj* o, std::vector<uint32_t>& out) {
    std::vector<std::pair<uint32_t, uint32_t>> ints;
    for (auto& p : o->props) {
        uint32_t idx;
        if (array_index_key(p.key, idx)) ints.push_back({idx, p.key});
    }
    std::sort(ints.begin(), ints.end());
    for (auto& i : ints) out.push_back(i.second);
    for (auto& p : o->props) {
        uint32_t idx;
        if (!array_index_key(p.key, idx)) out.push_back(p.key);
    }
}

Obj* obj_new(Kind k) {
    Obj* o = new Obj();
    o->kind = k;
    return o;
}

void obj_retain(Obj* o) {
    if (o) o->refs++;
}

struct Closure;
void closure_free(Closure* c);

void obj_release(Obj* o) {
    if (!o || --o->refs > 0) return;
    if (o->kind == Kind::Host && o->cls && o->cls->finalize) o->cls->finalize(o);
    if (o->closure) closure_free(o->closure);
    delete o;
}

Val make_object() { return Val::obj(obj_new(Kind::Plain)); }

Val make_array(std::vector<Val> items) {
    Obj* o = obj_new(Kind::Array);
    o->items = std::move(items);
    return Val::obj(o);
}

Val make_native(NativeFn fn) {
    Obj* o = obj_new(Kind::Native);
    o->native = std::move(fn);
    return Val::obj(o);
}

Val make_host(HostClass* cls, void* ptr, int64_t id) {
    Obj* o = obj_new(Kind::Host);
    o->cls = cls;
    o->ptr = ptr;
    o->id = id;
    return Val::obj(o);
}

bool strict_equals(const Val& a, const Val& b) {
    if (a.t != b.t) return false;
    switch (a.t) {
    case T::Undef:
    case T::Null: return true;
    case T::Bool: return a.b == b.b;
    case T::Num: return a.n == b.n;
    case T::Str: return a.s == b.s || a.s->data == b.s->data;
    case T::Obj:
        if (a.o == b.o) return true;
        if (a.o->kind == Kind::Host && b.o->kind == Kind::Host) return a.o->cls == b.o->cls && (a.o->id || b.o->id ? a.o->id == b.o->id : a.o->ptr == b.o->ptr);
        return false;
    }
    return false;
}

static Val to_primitive(const Val& v) {
    if (v.t != T::Obj) return v;
    return Val::str(v.to_str());
}

bool loose_equals(const Val& a, const Val& b) {
    if (a.t == b.t) return strict_equals(a, b);
    if (a.is_nullish() && b.is_nullish()) return true;
    if (a.is_nullish() || b.is_nullish()) return false;
    if (a.t == T::Num && b.t == T::Str) return a.n == b.to_num();
    if (a.t == T::Str && b.t == T::Num) return a.to_num() == b.n;
    if (a.t == T::Bool) return loose_equals(Val::num(a.b ? 1 : 0), b);
    if (b.t == T::Bool) return loose_equals(a, Val::num(b.b ? 1 : 0));
    if (a.t == T::Obj && b.t != T::Obj) return loose_equals(to_primitive(a), b);
    if (b.t == T::Obj && a.t != T::Obj) return loose_equals(a, to_primitive(b));
    return false;
}

const char* type_of(const Val& v) {
    switch (v.t) {
    case T::Undef: return "undefined";
    case T::Null: return "object";
    case T::Bool: return "boolean";
    case T::Num: return "number";
    case T::Str: return "string";
    case T::Obj: return v.is_function() ? "function" : "object";
    }
    return "undefined";
}

}
