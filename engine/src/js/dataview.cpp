#include "dataview.h"

#include <unordered_map>

#include "vm.h"

namespace js {

namespace {

struct ViewNode {
    const ::Value* v;
    std::shared_ptr<const void> keep;
    bool permanent;
    Expando overlay;
};

Val scalar(const ::Value& v) {
    switch (v.t) {
    case ::Value::Null: return Val::null();
    case ::Value::Bool: return Val::boolean(v.b);
    case ::Value::Int: return Val::num(v.i);
    case ::Value::Num: return Val::num(v.dbl());
    case ::Value::Str: return Val::str(std::string(v.s, v.n));
    default: return Val();
    }
}

std::unordered_map<const ::Value*, Val>& array_cache() {
    static std::unordered_map<const ::Value*, Val> c;
    return c;
}

HostClass& view_class();

bool view_get(Vm&, Obj* self, uint32_t key, Val& out) {
    ViewNode* n = static_cast<ViewNode*>(self->ptr);
    if (Val* o = n->overlay.find(key)) {
        out = *o;
        return true;
    }
    const std::string& name = atom_name(key);
    const ::Value* f = n->v->find(name.c_str());
    if (!f) return true;
    out = data_view(*f, n->keep, n->permanent);
    return true;
}

bool view_set(Vm&, Obj* self, uint32_t key, const Val& v) {
    static_cast<ViewNode*>(self->ptr)->overlay.set(key, v);
    return true;
}

void view_keys(Obj* self, std::vector<uint32_t>& keys) {
    ViewNode* n = static_cast<ViewNode*>(self->ptr);
    for (const Member& m : n->v->members()) keys.push_back(atom(m.key));
    for (auto& p : n->overlay.props) keys.push_back(p.key);
}

HostClass& view_class() {
    static HostClass hc = [] {
        HostClass c;
        c.name = "DataObject";
        c.get = view_get;
        c.set = view_set;
        c.keys = view_keys;
        c.finalize = [](Obj* self) {
            delete static_cast<ViewNode*>(self->ptr);
            self->ptr = nullptr;
        };
        return c;
    }();
    return hc;
}

}

Val data_view(const ::Value& v, std::shared_ptr<const void> keep, bool permanent) {
    if (v.t == ::Value::Arr) {
        if (permanent) {
            auto it = array_cache().find(&v);
            if (it != array_cache().end()) return it->second;
        }
        std::vector<Val> items;
        items.reserve(v.size());
        for (size_t i = 0; i < v.size(); i++) items.push_back(data_view(v[i], keep, permanent));
        Val arr = make_array(std::move(items));
        if (permanent) array_cache()[&v] = arr;
        return arr;
    }
    if (v.t == ::Value::Obj) {
        ViewNode* n = new ViewNode{&v, std::move(keep), permanent, {}};
        Obj* o = obj_new(Kind::Host);
        o->cls = &view_class();
        o->ptr = n;
        o->id = (int64_t)(intptr_t)&v;
        return Val::obj(o);
    }
    if (v.t == ::Value::Bytes) return Val();
    return scalar(v);
}

const ::Value* data_of(const Val& v) {
    if (v.t != T::Obj || v.o->kind != Kind::Host || v.o->cls != &view_class() || !v.o->ptr) return nullptr;
    return static_cast<ViewNode*>(v.o->ptr)->v;
}

Val value_to_js(const ::Value& v) {
    if (v.t == ::Value::Arr) {
        std::vector<Val> items;
        items.reserve(v.size());
        for (size_t i = 0; i < v.size(); i++) items.push_back(value_to_js(v[i]));
        return make_array(std::move(items));
    }
    if (v.t == ::Value::Obj) {
        Val o = make_object();
        for (const Member& m : v.members()) o.o->set(atom(m.key), value_to_js(m.v));
        return o;
    }
    if (v.t == ::Value::Bytes) return Val();
    return scalar(v);
}

}
