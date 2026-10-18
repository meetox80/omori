#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../js/bind.h"
#include "../js/dataview.h"
#include "../js/vm.h"
#include "battler.h"

namespace rpg {

class GameCharacter;
class GameCharacterBase;

namespace bind {

using js::Val;
using Args = std::vector<js::Val>;

template <class T>
Val arr_of(const std::vector<T*>& v) {
    std::vector<Val> out;
    out.reserve(v.size());
    for (T* x : v) out.push_back(x ? x->to_js() : Val::null());
    return js::make_array(std::move(out));
}

Val ints_of(const std::vector<int>& v);
Val obj_js(js::JsObject* o);
GameBattler* battler_of(const Val& v);
GameCharacter* character_of(const Val& v);
ItemRef item_of(const Val& v);
Val item_js(const ItemRef& r);
Val data_js(const Value& v);
Val native(std::function<Val(Args&)> fn);
std::vector<int> ints_from(const Val& v);
std::string str_arg(const Args& a, size_t i, const std::string& def = "");

js::Klass& klass(const char* name, const char* parent = nullptr);
js::Klass* find_klass(const char* name);

struct StaticObj : js::JsObject {
    js::Klass* k;
    explicit StaticObj(js::Klass* kl) : k(kl) {}
    js::HostClass* js_class() const override { return &k->hc; }
};

Val static_object(const char* name);

void hold(const std::shared_ptr<js::JsObject>& p);
std::shared_ptr<js::JsObject> held(js::JsObject* raw);
void purge_held();

template <class T>
std::shared_ptr<T> held_ptr(T* raw) {
    return std::dynamic_pointer_cast<T>(held(raw));
}

template <class T>
Val held_js(const std::shared_ptr<T>& p) {
    if (!p) return Val::null();
    hold(p);
    return p->to_js();
}

void bind_core();
void bind_characters();
void bind_battlers();
void bind_interpreter();
void bind_managers();
void bind_extras();
bool add_binder(std::function<void()> fn);
void run_binders();

}

}
