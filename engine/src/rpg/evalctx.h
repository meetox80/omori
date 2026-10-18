#pragma once
#include <string>

#include "../core/value.h"
#include "../js/vm.h"

namespace rpg {

class JsEval {
public:
    explicit JsEval(const js::Val& self);
    JsEval& set(const char* name, const js::Val& v);
    JsEval& num(const char* name, double v) { return set(name, js::Val::num(v)); }
    JsEval& sv();
    bool run(const Value* ast, const char* where);
    js::Val get(const char* name) const;
    const js::Val& completion() const { return completion_; }
    js::ScopePtr scope;

private:
    js::Val self_;
    js::Val completion_;
};

}
