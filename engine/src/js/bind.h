#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "value.h"
#include "vm.h"

namespace js {

struct JsObject {
    Expando expando;
    Obj* js_obj = nullptr;
    JsObject() = default;
    JsObject(const JsObject& o) : expando(o.expando) {}
    JsObject& operator=(const JsObject& o) {
        expando = o.expando;
        return *this;
    }
    virtual ~JsObject();
    virtual HostClass* js_class() const = 0;
    Val to_js();
    Val prop(const char* name);
    void set_prop(const char* name, const Val& v);
};

using MethodFn = std::function<Val(JsObject& self, Vm& vm, std::vector<Val>& args)>;
using GetterFn = std::function<Val(JsObject& self)>;
using SetterFn = std::function<void(JsObject& self, const Val& v)>;

struct Klass {
    HostClass hc;
    std::string name;
    std::unordered_map<uint32_t, Val> methods;
    std::unordered_map<uint32_t, GetterFn> getters;
    std::unordered_map<uint32_t, SetterFn> setters;
    std::vector<std::string> js_names;
    explicit Klass(const char* n, Klass* parent = nullptr);
    Klass(const Klass&) = delete;
    Klass& operator=(const Klass&) = delete;

    void method(const char* n, MethodFn fn);
    void getter(const char* n, GetterFn g);
    void field(const char* n, GetterFn g, SetterFn s);
};

double arg_num(const std::vector<Val>& a, size_t i, double def = 0);
int arg_int(const std::vector<Val>& a, size_t i, int def = 0);
std::string arg_str(const std::vector<Val>& a, size_t i, const std::string& def = "");
bool arg_bool(const std::vector<Val>& a, size_t i, bool def = false);
const Val& arg(const std::vector<Val>& a, size_t i);
bool has_arg(const std::vector<Val>& a, size_t i);

bool is_klass(const HostClass* cls);

template <class T>
T* host_cast(const Val& v) {
    if (v.t != js::T::Obj || v.o->kind != js::Kind::Host || !v.o->ptr || !is_klass(v.o->cls)) return nullptr;
    return dynamic_cast<T*>(static_cast<JsObject*>(v.o->ptr));
}

template <class C>
struct KlassOf {
    Klass& k;
    void method(const char* n, std::function<Val(C&, Vm&, std::vector<Val>&)> fn) {
        k.method(n, [fn](JsObject& s, Vm& vm, std::vector<Val>& a) { return fn(static_cast<C&>(s), vm, a); });
    }
    void call(const char* n, std::function<void(C&, std::vector<Val>&)> fn) {
        k.method(n, [fn](JsObject& s, Vm&, std::vector<Val>& a) {
            fn(static_cast<C&>(s), a);
            return Val();
        });
    }
    void num(const char* n, std::function<double(C&, std::vector<Val>&)> fn) {
        k.method(n, [fn](JsObject& s, Vm&, std::vector<Val>& a) { return Val::num(fn(static_cast<C&>(s), a)); });
    }
    void boolean(const char* n, std::function<bool(C&, std::vector<Val>&)> fn) {
        k.method(n, [fn](JsObject& s, Vm&, std::vector<Val>& a) { return Val::boolean(fn(static_cast<C&>(s), a)); });
    }
    void str(const char* n, std::function<std::string(C&, std::vector<Val>&)> fn) {
        k.method(n, [fn](JsObject& s, Vm&, std::vector<Val>& a) { return Val::str(fn(static_cast<C&>(s), a)); });
    }
    template <class M>
    void num_field(const char* n, M C::*m) {
        k.field(
            n, [m](JsObject& s) { return Val::num((double)(static_cast<C&>(s).*m)); },
            [m](JsObject& s, const Val& v) { static_cast<C&>(s).*m = (M)v.to_num(); });
    }
    void bool_field(const char* n, bool C::*m) {
        k.field(
            n, [m](JsObject& s) { return Val::boolean(static_cast<C&>(s).*m); },
            [m](JsObject& s, const Val& v) { static_cast<C&>(s).*m = v.truthy(); });
    }
    void str_field(const char* n, std::string C::*m) {
        k.field(
            n, [m](JsObject& s) { return Val::str(static_cast<C&>(s).*m); },
            [m](JsObject& s, const Val& v) { static_cast<C&>(s).*m = v.to_str(); });
    }
    void val_field(const char* n, Val C::*m) {
        k.field(
            n, [m](JsObject& s) { return static_cast<C&>(s).*m; }, [m](JsObject& s, const Val& v) { static_cast<C&>(s).*m = v; });
    }
    void get(const char* n, std::function<Val(C&)> g) {
        k.getter(n, [g](JsObject& s) { return g(static_cast<C&>(s)); });
    }
    void prop(const char* n, std::function<Val(C&)> g, std::function<void(C&, const Val&)> st) {
        k.field(
            n, [g](JsObject& s) { return g(static_cast<C&>(s)); }, [st](JsObject& s, const Val& v) { st(static_cast<C&>(s), v); });
    }
};

template <class C>
KlassOf<C> klass_of(Klass& k) {
    return KlassOf<C>{k};
}

void report_missing(const char* cls, uint32_t key);
JsObject* as_object(const Val& v);
using JsMethodResolver = std::function<bool(const std::string& cls, uint32_t key, Val& out)>;
void set_js_method_resolver(JsMethodResolver r);

}
