#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace js {

struct Str;
struct Obj;
struct HostClass;
class Vm;

enum class T : uint8_t { Undef, Null, Bool, Num, Str, Obj };

struct Val {
    T t = T::Undef;
    union {
        bool b;
        double n;
        Str* s;
        Obj* o;
    };
    Val() : n(0) {}
    Val(const Val& v);
    Val(Val&& v) noexcept;
    Val& operator=(const Val& v);
    Val& operator=(Val&& v) noexcept;
    ~Val();

    static Val undef() { return Val(); }
    static Val null() {
        Val v;
        v.t = T::Null;
        return v;
    }
    static Val boolean(bool x) {
        Val v;
        v.t = T::Bool;
        v.b = x;
        return v;
    }
    static Val num(double x) {
        Val v;
        v.t = T::Num;
        v.n = x;
        return v;
    }
    static Val str(const std::string& x);
    static Val str(const char* x);
    static Val str(Str* s);
    static Val obj(Obj* o);

    bool is_undef() const { return t == T::Undef; }
    bool is_null() const { return t == T::Null; }
    bool is_nullish() const { return t == T::Undef || t == T::Null; }
    bool is_num() const { return t == T::Num; }
    bool is_str() const { return t == T::Str; }
    bool is_bool() const { return t == T::Bool; }
    bool is_obj() const { return t == T::Obj; }
    bool is_array() const;
    bool is_function() const;
    bool truthy() const;
    double to_num() const;
    int to_int() const;
    std::string to_str() const;
    const std::string& str_ref() const;
    Obj* as_obj() const { return t == T::Obj ? o : nullptr; }
    template <class X>
    X* host() const;
};

struct Str {
    int refs = 0;
    std::string data;
    uint32_t atom = 0;
};

Str* str_new(const std::string& s);
void str_retain(Str* s);
void str_release(Str* s);

uint32_t atom(const std::string& s);
uint32_t atom(const char* s);
const std::string& atom_name(uint32_t a);
Str* atom_str(uint32_t a);

enum class Kind : uint8_t { Plain, Array, Function, Native, Host, Error };

struct Closure;

using NativeFn = std::function<Val(Vm& vm, const Val& self, std::vector<Val>& args)>;

struct Prop {
    uint32_t key;
    Val v;
};

struct Obj {
    int refs = 0;
    Kind kind = Kind::Plain;
    std::vector<Prop> props;
    std::vector<Val> items;
    Closure* closure = nullptr;
    NativeFn native;
    HostClass* cls = nullptr;
    void* ptr = nullptr;
    int64_t id = 0;
    Val proto;

    Val* find(uint32_t key);
    const Val* find(uint32_t key) const;
    void set(uint32_t key, const Val& v);
    bool remove(uint32_t key);
};

Obj* obj_new(Kind k);
void ordered_keys(const Obj* o, std::vector<uint32_t>& out);
void obj_retain(Obj* o);
void obj_release(Obj* o);

Val make_object();
Val make_array(std::vector<Val> items = {});
Val make_native(NativeFn fn);

std::string number_to_string(double d);
double string_to_number(const std::string& s);
bool strict_equals(const Val& a, const Val& b);
bool loose_equals(const Val& a, const Val& b);
const char* type_of(const Val& v);

struct HostClass {
    const char* name;
    std::function<bool(Vm& vm, Obj* self, uint32_t key, Val& out)> get;
    std::function<bool(Vm& vm, Obj* self, uint32_t key, const Val& v)> set;
    std::function<void(Obj* self)> finalize;
    std::function<void(Obj* self, std::vector<uint32_t>& keys)> keys;
    std::function<Val(Vm& vm, const Val& self, std::vector<Val>& args)> call;
};

struct Expando {
    std::vector<Prop> props;
    Val* find(uint32_t key);
    void set(uint32_t key, const Val& v);
    void clear() { props.clear(); }
};

Val make_host(HostClass* cls, void* ptr, int64_t id = 0);

template <class X>
X* Val::host() const {
    if (t != T::Obj || o->kind != Kind::Host) return nullptr;
    return static_cast<X*>(o->ptr);
}

}
