#include "evalctx.h"

#include "data.h"
#include "game.h"

namespace rpg {

JsEval::JsEval(const js::Val& self) : scope(std::make_shared<js::Scope>()), self_(self) { scope->function_scope = true; }

JsEval& JsEval::set(const char* name, const js::Val& v) {
    if (js::Val* cur = scope->find(js::atom(name))) *cur = v;
    else scope->declare(js::atom(name), v);
    return *this;
}

JsEval& JsEval::sv() {
    set("s", switches_proxy());
    set("v", variables_proxy());
    return *this;
}

bool JsEval::run(const Value* ast, const char* where) {
    if (!ast || !ast->is_arr()) return true;
    bool ok = js::vm().run(*ast, self_, scope, &completion_, data::keep());
    if (!ok) script_error(where, js::vm().last_error);
    return ok;
}

js::Val JsEval::get(const char* name) const {
    js::Val* v = scope->find(js::atom(name));
    return v ? *v : js::Val();
}

}
