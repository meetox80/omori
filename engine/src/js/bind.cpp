#include "bind.h"

#include <cstdio>
#include <set>

namespace js {

JsObject::~JsObject() {
    if (js_obj) {
        js_obj->ptr = nullptr;
        obj_release(js_obj);
        js_obj = nullptr;
    }
}

Val JsObject::to_js() {
    if (!js_obj) {
        Val v = make_host(js_class(), static_cast<JsObject*>(this));
        js_obj = v.o;
        obj_retain(js_obj);
        return v;
    }
    return Val::obj(js_obj);
}

Val JsObject::prop(const char* name) {
    Val* v = expando.find(atom(name));
    return v ? *v : Val();
}

void JsObject::set_prop(const char* name, const Val& v) { expando.set(atom(name), v); }

void report_missing(const char* cls, uint32_t key) {
    static std::set<std::pair<std::string, uint32_t>> seen;
    if (seen.insert({cls, key}).second) std::fprintf(stderr, "[js] %s has no member '%s'\n", cls, atom_name(key).c_str());
}

static std::set<const HostClass*>& klass_set_registry() {
    static std::set<const HostClass*> s;
    return s;
}

bool is_klass(const HostClass* cls) { return cls && klass_set_registry().count(cls) != 0; }

JsObject* as_object(const Val& v) {
    if (v.t != T::Obj || v.o->kind != Kind::Host || !v.o->ptr || !is_klass(v.o->cls)) return nullptr;
    return static_cast<JsObject*>(v.o->ptr);
}

static JsMethodResolver g_js_methods;

void set_js_method_resolver(JsMethodResolver r) { g_js_methods = std::move(r); }

static bool klass_get(Klass* k, Obj* self, uint32_t key, Val& out) {
    JsObject* o = static_cast<JsObject*>(self->ptr);
    if (!o) return true;
    auto g = k->getters.find(key);
    if (g != k->getters.end()) {
        out = g->second(*o);
        return true;
    }
    if (Val* v = o->expando.find(key)) {
        out = *v;
        return true;
    }
    auto m = k->methods.find(key);
    if (m != k->methods.end()) {
        out = m->second;
        return true;
    }
    if (g_js_methods) {
        for (auto& n : k->js_names) {
            if (g_js_methods(n, key, out)) {
                k->methods[key] = out;
                return true;
            }
        }
    }
    report_missing(k->name.c_str(), key);
    return true;
}

static bool klass_set(Klass* k, Obj* self, uint32_t key, const Val& v) {
    JsObject* o = static_cast<JsObject*>(self->ptr);
    if (!o) return true;
    auto s = k->setters.find(key);
    if (s != k->setters.end()) {
        s->second(*o, v);
        return true;
    }
    o->expando.set(key, v);
    return true;
}

static void klass_keys(Klass* k, Obj* self, std::vector<uint32_t>& keys) {
    JsObject* o = static_cast<JsObject*>(self->ptr);
    if (!o) return;
    for (auto& g : k->getters) {
        if (k->setters.count(g.first)) keys.push_back(g.first);
    }
    for (auto& p : o->expando.props) keys.push_back(p.key);
}

Klass::Klass(const char* n, Klass* parent) : name(n) {
    hc.name = name.c_str();
    hc.get = [this](Vm&, Obj* self, uint32_t key, Val& out) { return klass_get(this, self, key, out); };
    hc.set = [this](Vm&, Obj* self, uint32_t key, const Val& v) { return klass_set(this, self, key, v); };
    hc.keys = [this](Obj* self, std::vector<uint32_t>& keys) { klass_keys(this, self, keys); };
    js_names.push_back(n);
    klass_set_registry().insert(&hc);
    if (parent) {
        methods = parent->methods;
        getters = parent->getters;
        setters = parent->setters;
        for (auto& p : parent->js_names) js_names.push_back(p);
    }
}

void Klass::method(const char* n, MethodFn fn) {
    std::string cls = name;
    std::string mname = n;
    methods[atom(n)] = make_native([fn, cls, mname](Vm& vm, const Val& self, std::vector<Val>& args) -> Val {
        if (self.t != T::Obj || self.o->kind != Kind::Host || !self.o->ptr) vm.throw_type_error(cls + "." + mname + " called on an invalid object");
        return fn(*static_cast<JsObject*>(self.o->ptr), vm, args);
    });
}

void Klass::getter(const char* n, GetterFn g) { getters[atom(n)] = std::move(g); }

void Klass::field(const char* n, GetterFn g, SetterFn s) {
    getters[atom(n)] = std::move(g);
    setters[atom(n)] = std::move(s);
}

static const Val g_undef;

const Val& arg(const std::vector<Val>& a, size_t i) { return i < a.size() ? a[i] : g_undef; }
bool has_arg(const std::vector<Val>& a, size_t i) { return i < a.size() && !a[i].is_undef(); }
double arg_num(const std::vector<Val>& a, size_t i, double def) { return has_arg(a, i) ? a[i].to_num() : def; }
int arg_int(const std::vector<Val>& a, size_t i, int def) { return has_arg(a, i) ? a[i].to_int() : def; }
std::string arg_str(const std::vector<Val>& a, size_t i, const std::string& def) { return has_arg(a, i) ? a[i].to_str() : def; }
bool arg_bool(const std::vector<Val>& a, size_t i, bool def) { return has_arg(a, i) ? a[i].truthy() : def; }

}
