#include "vm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../../shared/jsops.h"
#include "../platform/platform.h"
#include "random.h"
#include "regexp.h"

namespace js {

Val* Scope::find(uint32_t key) {
    for (auto& p : vars) {
        if (p.key == key) return &p.v;
    }
    return nullptr;
}

Val* Scope::lookup(uint32_t key) {
    for (Scope* s = this; s; s = s->parent.get()) {
        if (Val* v = s->find(key)) return v;
    }
    return nullptr;
}

void Scope::declare(uint32_t key, const Val& v) {
    if (Val* e = find(key)) {
        *e = v;
        return;
    }
    vars.push_back({key, v});
}

void closure_free(Closure* c) { delete c; }

static uint32_t A_length, A_prototype, A_message, A_name, A_call, A_apply, A_bind, A_arguments, A_constructor;

uint32_t key_of(const Val& k) {
    if (k.t == T::Str) return k.s->atom ? k.s->atom : atom(k.s->data);
    return atom(k.to_str());
}

static bool index_of_atom(uint32_t key, size_t& out) {
    const std::string& s = atom_name(key);
    if (s.empty() || s.size() > 9) return false;
    size_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (size_t)(c - '0');
    }
    if (s.size() > 1 && s[0] == '0') return false;
    out = v;
    return true;
}

static bool index_of_val(const Val& k, size_t& out) {
    if (k.t == T::Num) {
        if (k.n >= 0 && k.n == std::floor(k.n) && k.n < 4294967295.0) {
            out = (size_t)k.n;
            return true;
        }
        return false;
    }
    if (k.t == T::Str) return index_of_atom(key_of(k), out);
    return false;
}

static int op_of(const ::Value& n) { return n.is_arr() && n.size() ? n[0].as_int() : 0; }

static uint32_t node_atom(const ::Value& v) { return atom(std::string(v.str(), v.size())); }

Vm::Vm() {
    A_length = atom("length");
    A_prototype = atom("prototype");
    A_message = atom("message");
    A_name = atom("name");
    A_call = atom("call");
    A_apply = atom("apply");
    A_bind = atom("bind");
    A_arguments = atom("arguments");
    A_constructor = atom("constructor");
    install_builtins();
}

void Vm::set_global(const char* name, const Val& v) { set_global(atom(name), v); }

void Vm::set_global(uint32_t key, const Val& v) {
    for (auto& p : globals_) {
        if (p.key == key) {
            p.v = v;
            return;
        }
    }
    globals_.push_back({key, v});
}

Val* Vm::global(uint32_t key) {
    for (auto& p : globals_) {
        if (p.key == key) return &p.v;
    }
    return nullptr;
}

void Vm::throw_error(const std::string& msg) {
    Obj* e = obj_new(Kind::Error);
    Val ev = Val::obj(e);
    e->set(A_message, Val::str(msg));
    e->set(A_name, Val::str("Error"));
    throw Throw{ev};
}

void Vm::throw_type_error(const std::string& msg) {
    Obj* e = obj_new(Kind::Error);
    Val ev = Val::obj(e);
    e->set(A_message, Val::str(msg));
    e->set(A_name, Val::str("TypeError"));
    throw Throw{ev};
}

static std::string describe_error(const Val& v) {
    if (v.is_obj() && v.o->kind == Kind::Error) {
        const Val* n = v.o->find(atom("name"));
        const Val* m = v.o->find(atom("message"));
        return (n ? n->to_str() : std::string("Error")) + ": " + (m ? m->to_str() : std::string());
    }
    return v.to_str();
}

bool Vm::run(const ::Value& program, const Val& self, const ScopePtr& locals, Val* result, std::shared_ptr<const void> owner) {
    try {
        Val r = run_or_throw(program, self, locals, std::move(owner));
        if (result) *result = r;
        return true;
    } catch (Throw& t) {
        last_error = describe_error(t.value);
        plat::log("script error: %s", last_error.c_str());
        depth = 0;
        return false;
    }
}

Val Vm::run_or_throw(const ::Value& program, const Val& self, const ScopePtr& locals, std::shared_ptr<const void> owner) {
    if (op_of(program) != JS_PROGRAM) throw_error("SyntaxError: script could not be compiled");
    Frame f;
    f.scope = std::make_shared<Scope>();
    f.scope->parent = locals;
    f.scope->function_scope = true;
    f.self = self;
    f.owner = std::move(owner);
    const ::Value& body = program[1];
    hoist(body, f);
    exec_list(body, f);
    return f.completion;
}

Val Vm::run_in_scope(const ::Value& program, const Val& self, const ScopePtr& scope, std::shared_ptr<const void> owner) {
    if (op_of(program) != JS_PROGRAM) throw_error("SyntaxError: script could not be compiled");
    Frame f;
    f.scope = scope;
    f.self = self;
    f.owner = std::move(owner);
    const ::Value& body = program[1];
    hoist(body, f);
    exec_list(body, f);
    return f.completion;
}

Val Vm::eval_code(const std::string& code, Frame* f) {
    if (!eval_compiler_) throw_error("EvalError: eval is not available");
    std::string err;
    std::shared_ptr<const ::Value> prog = eval_compiler_(code, err);
    if (!prog) throw_error("SyntaxError: " + err);
    eval_programs_.push_back(prog);
    const ::Value& program = *prog;
    if (op_of(program) != JS_PROGRAM) throw_error("SyntaxError: bad eval program");
    Frame nf;
    if (f) {
        nf.scope = f->scope;
        nf.self = f->self;
        nf.owner = f->owner;
    } else {
        nf.scope = std::make_shared<Scope>();
        nf.scope->function_scope = true;
    }
    const ::Value& body = program[1];
    hoist(body, nf);
    exec_list(body, nf);
    return nf.completion;
}

Val Vm::lookup(uint32_t key, Frame& f, bool for_typeof) {
    if (Val* v = f.scope->lookup(key)) return *v;
    if (Val* g = global(key)) return *g;
    Val out;
    if (resolver_ && resolver_(key, out)) return out;
    if (for_typeof) return Val();
    throw_error("ReferenceError: " + atom_name(key) + " is not defined");
}

void Vm::assign_name(uint32_t key, const Val& v, Frame& f) {
    if (Val* s = f.scope->lookup(key)) {
        *s = v;
        return;
    }
    set_global(key, v);
}

static void collect_vars(const ::Value& n, std::vector<uint32_t>& vars, std::vector<const ::Value*>& funcs) {
    if (!n.is_arr() || !n.size()) return;
    int op = op_of(n);
    switch (op) {
    case JS_VAR:
        if (n[1].as_int() == 0) {
            const ::Value& list = n[2];
            for (size_t i = 0; i + 1 < list.size(); i += 2) vars.push_back(node_atom(list[i]));
        }
        return;
    case JS_FUNCDECL: funcs.push_back(&n); return;
    case JS_IF:
        collect_vars(n[2], vars, funcs);
        collect_vars(n[3], vars, funcs);
        return;
    case JS_FOR:
        collect_vars(n[1], vars, funcs);
        collect_vars(n[4], vars, funcs);
        return;
    case JS_FORIN:
    case JS_FOROF:
        if (n[1].as_int() == 0) vars.push_back(node_atom(n[2][1]));
        collect_vars(n[4], vars, funcs);
        return;
    case JS_WHILE: collect_vars(n[2], vars, funcs); return;
    case JS_DOWHILE: collect_vars(n[1], vars, funcs); return;
    case JS_BLOCK:
        for (size_t i = 0; i < n[1].size(); i++) collect_vars(n[1][i], vars, funcs);
        return;
    case JS_SWITCH:
        for (size_t i = 0; i < n[2].size(); i++) {
            const ::Value& body = n[2][i][1];
            for (size_t k = 0; k < body.size(); k++) collect_vars(body[k], vars, funcs);
        }
        return;
    case JS_TRY:
        for (int part : {1, 3, 4}) {
            const ::Value& b = n[part];
            for (size_t k = 0; k < b.size(); k++) collect_vars(b[k], vars, funcs);
        }
        return;
    default: return;
    }
}

void Vm::hoist(const ::Value& list, Frame& f) {
    std::vector<uint32_t> vars;
    std::vector<const ::Value*> funcs;
    for (size_t i = 0; i < list.size(); i++) collect_vars(list[i], vars, funcs);
    for (uint32_t v : vars) {
        if (!f.scope->find(v)) f.scope->vars.push_back({v, Val()});
    }
    for (const ::Value* fn : funcs) f.scope->declare(node_atom((*fn)[1]), make_function((*fn)[2], f));
}

Val Vm::make_function(const ::Value& node, Frame& f) {
    Obj* o = obj_new(Kind::Function);
    Closure* c = new Closure();
    c->node = &node;
    c->env = f.scope;
    c->arrow = node[3].as_int() != 0;
    if (c->arrow) c->self = f.self;
    c->owner = f.owner;
    o->closure = c;
    Val fv = Val::obj(o);
    if (!c->arrow) o->set(A_prototype, make_object());
    return fv;
}

Val Vm::call_closure(Obj* fn, const Val& self, std::vector<Val>& args) {
    Closure* c = fn->closure;
    if (++depth > 200) {
        depth = 0;
        throw_error("RangeError: Maximum call stack size exceeded");
    }
    Frame f;
    f.scope = std::make_shared<Scope>();
    f.scope->parent = c->env;
    f.scope->function_scope = true;
    f.self = c->arrow ? c->self : self;
    f.owner = c->owner;
    const ::Value& params = (*c->node)[1];
    for (size_t i = 0; i < params.size(); i++) f.scope->declare(node_atom(params[i]), i < args.size() ? args[i] : Val());
    if (!c->arrow) f.scope->declare(A_arguments, make_array(args));
    const ::Value& name = (*c->node)[4];
    if (name.is_str() && name.size()) {
        uint32_t na = node_atom(name);
        if (!f.scope->find(na)) f.scope->declare(na, Val::obj(fn));
    }
    const ::Value& body = (*c->node)[2];
    hoist(body, f);
    exec_list(body, f);
    depth--;
    return f.ret;
}

Val Vm::call(const Val& fn, const Val& self, std::vector<Val>& args) {
    if (fn.t != T::Obj) throw_type_error("value is not a function");
    Obj* o = fn.o;
    if (o->kind == Kind::Native) return o->native(*this, self, args);
    if (o->kind == Kind::Host && o->cls && o->cls->call) return o->cls->call(*this, self, args);
    if (o->kind == Kind::Function) return call_closure(o, self, args);
    throw_type_error("object is not a function");
}

Val Vm::call(const Val& fn, const Val& self, std::initializer_list<Val> args) {
    std::vector<Val> a(args);
    return call(fn, self, a);
}

void Vm::eval_args(const ::Value& list, Frame& f, std::vector<Val>& out) {
    out.reserve(list.size());
    for (size_t i = 0; i < list.size(); i++) {
        const ::Value& a = list[i];
        if (op_of(a) == JS_SPREAD) {
            Val v = eval(a[1], f);
            if (v.is_array()) {
                for (auto& x : v.o->items) out.push_back(x);
            } else if (v.is_str()) {
                for (char c : v.s->data) out.push_back(Val::str(std::string(1, c)));
            } else {
                throw_type_error("spread of non-iterable");
            }
        } else {
            out.push_back(eval(a, f));
        }
    }
}

static double to_int32(double d) {
    if (std::isnan(d) || std::isinf(d)) return 0;
    double m = std::fmod(std::trunc(d), 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return (double)(int32_t)(uint32_t)m;
}

static uint32_t to_uint32(double d) {
    if (std::isnan(d) || std::isinf(d)) return 0;
    double m = std::fmod(std::trunc(d), 4294967296.0);
    if (m < 0) m += 4294967296.0;
    return (uint32_t)m;
}

static Val prim(const Val& v) {
    if (v.t == T::Obj) return Val::str(v.to_str());
    return v;
}

static bool less_than(const Val& a0, const Val& b0, bool& undef) {
    Val a = prim(a0), b = prim(b0);
    undef = false;
    if (a.t == T::Str && b.t == T::Str) return a.s->data < b.s->data;
    double x = a.to_num(), y = b.to_num();
    if (std::isnan(x) || std::isnan(y)) {
        undef = true;
        return false;
    }
    return x < y;
}

Val Vm::binop(int op, const Val& a, const Val& b) {
    switch (op) {
    case JB_ADD: {
        Val pa = prim(a), pb = prim(b);
        if (pa.t == T::Str || pb.t == T::Str) return Val::str(pa.to_str() + pb.to_str());
        return Val::num(pa.to_num() + pb.to_num());
    }
    case JB_SUB: return Val::num(a.to_num() - b.to_num());
    case JB_MUL: return Val::num(a.to_num() * b.to_num());
    case JB_DIV: return Val::num(a.to_num() / b.to_num());
    case JB_MOD: return Val::num(std::fmod(a.to_num(), b.to_num()));
    case JB_POW: return Val::num(std::pow(a.to_num(), b.to_num()));
    case JB_EQ: return Val::boolean(loose_equals(a, b));
    case JB_NE: return Val::boolean(!loose_equals(a, b));
    case JB_SEQ: return Val::boolean(strict_equals(a, b));
    case JB_SNE: return Val::boolean(!strict_equals(a, b));
    case JB_LT: {
        bool u;
        bool r = less_than(a, b, u);
        return Val::boolean(!u && r);
    }
    case JB_GT: {
        bool u;
        bool r = less_than(b, a, u);
        return Val::boolean(!u && r);
    }
    case JB_LE: {
        bool u;
        bool r = less_than(b, a, u);
        return Val::boolean(!u && !r);
    }
    case JB_GE: {
        bool u;
        bool r = less_than(a, b, u);
        return Val::boolean(!u && !r);
    }
    case JB_SHL: return Val::num((double)(int32_t)((uint32_t)to_int32(a.to_num()) << (to_uint32(b.to_num()) & 31)));
    case JB_SHR: return Val::num((double)((int32_t)to_int32(a.to_num()) >> (to_uint32(b.to_num()) & 31)));
    case JB_USHR: return Val::num((double)(to_uint32(a.to_num()) >> (to_uint32(b.to_num()) & 31)));
    case JB_BAND: return Val::num((double)((int32_t)to_int32(a.to_num()) & (int32_t)to_int32(b.to_num())));
    case JB_BOR: return Val::num((double)((int32_t)to_int32(a.to_num()) | (int32_t)to_int32(b.to_num())));
    case JB_BXOR: return Val::num((double)((int32_t)to_int32(a.to_num()) ^ (int32_t)to_int32(b.to_num())));
    case JB_IN: {
        if (b.t != T::Obj) throw_type_error("cannot use 'in' on a non-object");
        uint32_t k = key_of(a);
        if (b.o->kind == Kind::Array) {
            size_t idx;
            if (index_of_atom(k, idx)) return Val::boolean(idx < b.o->items.size());
            if (k == A_length) return Val::boolean(true);
        }
        if (b.o->find(k)) return Val::boolean(true);
        if (b.o->kind == Kind::Host) {
            Val out;
            return Val::boolean(b.o->cls->get(*this, b.o, k, out) && !out.is_undef());
        }
        return Val::boolean(false);
    }
    case JB_INSTANCEOF: {
        if (a.t != T::Obj || b.t != T::Obj) return Val::boolean(false);
        const Val* p = b.o->find(A_prototype);
        if (!p) return Val::boolean(false);
        for (Val cur = a.o->proto; cur.is_obj(); cur = cur.o->proto) {
            if (strict_equals(cur, *p)) return Val::boolean(true);
        }
        return Val::boolean(false);
    }
    }
    throw_error("bad operator");
}

Val Vm::get(const Val& obj, const Val& key) {
    if (obj.t == T::Obj && obj.o->kind == Kind::Array) {
        size_t idx;
        if (index_of_val(key, idx)) return idx < obj.o->items.size() ? obj.o->items[idx] : Val();
    }
    if (obj.t == T::Str && key.t == T::Num) {
        size_t idx;
        if (index_of_val(key, idx)) return idx < obj.s->data.size() ? Val::str(std::string(1, obj.s->data[idx])) : Val();
    }
    return get(obj, key_of(key));
}

Val Vm::get(const Val& obj, uint32_t key) {
    switch (obj.t) {
    case T::Undef:
    case T::Null: throw_type_error("Cannot read property '" + atom_name(key) + "' of " + obj.to_str());
    case T::Str: {
        if (key == A_length) return Val::num((double)obj.s->data.size());
        size_t idx;
        if (index_of_atom(key, idx)) return idx < obj.s->data.size() ? Val::str(std::string(1, obj.s->data[idx])) : Val();
        return builtin_member(obj, key);
    }
    case T::Num:
    case T::Bool: return builtin_member(obj, key);
    case T::Obj: break;
    }
    Obj* o = obj.o;
    switch (o->kind) {
    case Kind::Array: {
        if (key == A_length) return Val::num((double)o->items.size());
        size_t idx;
        if (index_of_atom(key, idx)) return idx < o->items.size() ? o->items[idx] : Val();
        if (Val* v = o->find(key)) return *v;
        return builtin_member(obj, key);
    }
    case Kind::Host: {
        Val out;
        if (o->cls->get && o->cls->get(*this, o, key, out)) return out;
        return Val();
    }
    case Kind::Native:
        if (regexp_ctor_.is_obj() && o == regexp_ctor_.o) {
            bool found = false;
            Val v = regexp_static(key, found);
            if (found) return v;
        }
        break;
    default: break;
    }
    if (Val* v = o->find(key)) return *v;
    for (Val p = o->proto; p.is_obj(); p = p.o->proto) {
        if (Val* v = p.o->find(key)) return *v;
    }
    return builtin_member(obj, key);
}

void Vm::put(const Val& obj, const Val& key, const Val& v) {
    if (obj.t == T::Obj && obj.o->kind == Kind::Array) {
        size_t idx;
        if (index_of_val(key, idx)) {
            auto& items = obj.o->items;
            if (idx >= items.size()) {
                if (idx > 1000000) throw_error("RangeError: array index too large");
                items.resize(idx + 1);
            }
            items[idx] = v;
            return;
        }
    }
    put(obj, key_of(key), v);
}

void Vm::put(const Val& obj, uint32_t key, const Val& v) {
    switch (obj.t) {
    case T::Undef:
    case T::Null: throw_type_error("Cannot set property '" + atom_name(key) + "' of " + obj.to_str());
    case T::Obj: break;
    default: return;
    }
    Obj* o = obj.o;
    if (o->kind == Kind::Array) {
        if (key == A_length) {
            double n = v.to_num();
            if (n < 0 || n > 1000000 || n != std::floor(n)) throw_error("RangeError: Invalid array length");
            o->items.resize((size_t)n);
            return;
        }
        size_t idx;
        if (index_of_atom(key, idx)) {
            if (idx >= o->items.size()) o->items.resize(idx + 1);
            o->items[idx] = v;
            return;
        }
    }
    if (o->kind == Kind::Host) {
        if (o->cls->set) o->cls->set(*this, o, key, v);
        return;
    }
    o->set(key, v);
}

Val Vm::call_method(const Val& obj, uint32_t key, std::vector<Val>& args) {
    bool found = false;
    Val r;
    switch (obj.t) {
    case T::Undef:
    case T::Null: throw_type_error("Cannot read property '" + atom_name(key) + "' of " + obj.to_str());
    case T::Str:
        r = string_method(obj, key, args, found);
        if (found) return r;
        break;
    case T::Num:
        r = number_method(obj, key, args, found);
        if (found) return r;
        break;
    case T::Bool: break;
    case T::Obj:
        if (obj.o->kind == Kind::Array) {
            if (!obj.o->find(key)) {
                r = array_method(obj, key, args, found);
                if (found) return r;
            }
        } else if (obj.o->kind == Kind::Function || obj.o->kind == Kind::Native) {
            if (key == A_call) {
                Val self = args.empty() ? Val() : args[0];
                std::vector<Val> rest(args.size() > 1 ? args.begin() + 1 : args.end(), args.end());
                return call(obj, self, rest);
            }
            if (key == A_apply) {
                Val self = args.empty() ? Val() : args[0];
                std::vector<Val> rest;
                if (args.size() > 1 && args[1].is_array()) rest = args[1].o->items;
                return call(obj, self, rest);
            }
        } else if (obj.o->kind == Kind::Plain || obj.o->kind == Kind::Error) {
            if (!obj.o->find(key)) {
                bool in_proto = false;
                for (Val p = obj.o->proto; p.is_obj(); p = p.o->proto) in_proto |= p.o->find(key) != nullptr;
                if (!in_proto) {
                    r = object_method(obj, key, args, found);
                    if (found) return r;
                }
            }
        }
        break;
    }
    Val fn = get(obj, key);
    if (!fn.is_function()) throw_type_error(atom_name(key) + " is not a function");
    return call(fn, obj, args);
}

Val Vm::builtin_member(const Val& obj, uint32_t key) {
    if (obj.t == T::Obj && (obj.o->kind == Kind::Function || obj.o->kind == Kind::Native)) {
        if (key == A_name) return Val::str("");
        if (key == A_length) {
            if (obj.o->closure) return Val::num((double)(*obj.o->closure->node)[1].size());
            return Val::num(0);
        }
        if (key == A_bind) {
            Val target = obj;
            return make_native([target](Vm& vm, const Val&, std::vector<Val>& args) -> Val {
                Val self = args.empty() ? Val() : args[0];
                std::vector<Val> pre(args.size() > 1 ? args.begin() + 1 : args.end(), args.end());
                return make_native([target, self, pre](Vm& vm2, const Val&, std::vector<Val>& a2) -> Val {
                    std::vector<Val> all = pre;
                    all.insert(all.end(), a2.begin(), a2.end());
                    return vm2.call(target, self, all);
                });
            });
        }
    }
    uint32_t k = key;
    bool known = false;
    if (obj.t == T::Str) {
        static const char* names[] = {"charAt", "charCodeAt", "indexOf", "lastIndexOf", "slice", "substring", "substr", "toUpperCase", "toLowerCase",
                                      "trim", "split", "replace", "match", "search", "includes", "contains", "startsWith", "endsWith", "format", "padZero",
                                      "repeat", "concat", "toString", "localeCompare", "trimLeft", "trimRight", "padStart", "padEnd", "valueOf",
                                      nullptr};
        for (int i = 0; names[i]; i++) known |= atom(names[i]) == k;
    } else if (obj.t == T::Num) {
        static const char* names[] = {"toFixed", "toString", "clamp", "mod", "padZero", "valueOf", "toPrecision", nullptr};
        for (int i = 0; names[i]; i++) known |= atom(names[i]) == k;
    } else if (obj.is_array()) {
        static const char* names[] = {"push", "pop", "shift", "unshift", "slice", "splice", "indexOf", "lastIndexOf", "contains", "includes", "forEach",
                                      "map", "filter", "some", "every", "reduce", "join", "find", "findIndex", "concat", "reverse", "sort",
                                      "clone", "equals", "remove", "toString", "fill", nullptr};
        for (int i = 0; names[i]; i++) known |= atom(names[i]) == k;
    } else if (obj.t == T::Obj) {
        static const char* names[] = {"hasOwnProperty", "toString", nullptr};
        for (int i = 0; names[i]; i++) known |= atom(names[i]) == k;
    }
    if (!known) {
        const Val* proto = nullptr;
        if (obj.t == T::Str) proto = &proto_string_;
        else if (obj.t == T::Num) proto = &proto_number_;
        else if (obj.t == T::Bool) proto = &proto_boolean_;
        else if (obj.is_array()) proto = &proto_array_;
        else if (obj.is_function()) proto = &proto_function_;
        else if (obj.t == T::Obj) proto = &proto_object_;
        if (proto && proto->is_obj()) {
            if (Val* v = proto->o->find(key)) return *v;
            if (proto != &proto_object_ && proto_object_.is_obj()) {
                if (Val* v = proto_object_.o->find(key)) return *v;
            }
        }
        return Val();
    }
    return make_native([this, k](Vm& vm, const Val& self, std::vector<Val>& args) -> Val {
        (void)vm;
        return call_method(self, k, args);
    });
}

void Vm::assign(const ::Value& target, const Val& v, Frame& f) {
    switch (op_of(target)) {
    case JS_NAME: assign_name(node_atom(target[1]), v, f); return;
    case JS_GET: {
        Val o = eval(target[1], f);
        put(o, node_atom(target[2]), v);
        return;
    }
    case JS_INDEX: {
        Val o = eval(target[1], f);
        Val k = eval(target[2], f);
        put(o, k, v);
        return;
    }
    }
    throw_error("SyntaxError: invalid assignment target");
}

Val Vm::eval(const ::Value& n, Frame& f) {
    switch (op_of(n)) {
    case JS_NUM: return Val::num(n[1].num());
    case JS_STR: return Val::str(std::string(n[1].str(), n[1].size()));
    case JS_REGEX: return make_regexp(*this, std::string(n[1].str(), n[1].size()), std::string(n[2].str(), n[2].size()));
    case JS_TRUE: return Val::boolean(true);
    case JS_FALSE: return Val::boolean(false);
    case JS_NULL: return Val::null();
    case JS_UNDEF: return Val();
    case JS_NAME: return lookup(node_atom(n[1]), f, false);
    case JS_THIS: return f.self;
    case JS_ARR: {
        std::vector<Val> items;
        eval_args(n[1], f, items);
        return make_array(std::move(items));
    }
    case JS_OBJ: {
        Val o = make_object();
        const ::Value& props = n[1];
        for (size_t i = 0; i + 1 < props.size(); i += 2) {
            const ::Value& k = props[i];
            if (k.is_str()) {
                put(o, node_atom(k), eval(props[i + 1], f));
            } else if (op_of(k) == JS_SPREAD) {
                Val src = eval(props[i + 1], f);
                if (src.is_obj() && src.o->kind == Kind::Plain) {
                    for (auto& p : src.o->props) o.o->set(p.key, p.v);
                } else if (src.is_array()) {
                    for (size_t j = 0; j < src.o->items.size(); j++) o.o->set(atom(std::to_string(j)), src.o->items[j]);
                }
            } else {
                Val kv = eval(k, f);
                put(o, kv, eval(props[i + 1], f));
            }
        }
        return o;
    }
    case JS_FUNC: return make_function(n, f);
    case JS_GET: {
        Val o = eval(n[1], f);
        return get(o, node_atom(n[2]));
    }
    case JS_INDEX: {
        Val o = eval(n[1], f);
        Val k = eval(n[2], f);
        return get(o, k);
    }
    case JS_OPTGET: {
        Val o = eval(n[1], f);
        if (o.is_nullish()) return Val();
        Val k = eval(n[2], f);
        return get(o, k);
    }
    case JS_CALL: {
        const ::Value& callee = n[1];
        int cop = op_of(callee);
        std::vector<Val> args;
        if (cop == JS_GET || cop == JS_INDEX) {
            Val o = eval(callee[1], f);
            uint32_t key = cop == JS_GET ? node_atom(callee[2]) : key_of(eval(callee[2], f));
            eval_args(n[2], f, args);
            return call_method(o, key, args);
        }
        Val fn = eval(callee, f);
        eval_args(n[2], f, args);
        if (cop == JS_NAME && eval_native_.is_obj() && fn.is_obj() && fn.o == eval_native_.o) {
            if (args.empty() || !args[0].is_str()) return args.empty() ? Val() : args[0];
            return eval_code(args[0].s->data, &f);
        }
        if (!fn.is_function()) {
            std::string what = cop == JS_NAME ? atom_name(node_atom(callee[1])) : std::string("expression");
            throw_type_error(what + " is not a function");
        }
        return call(fn, Val(), args);
    }
    case JS_OPTCALL: {
        Val fn = eval(n[1], f);
        if (fn.is_nullish()) return Val();
        std::vector<Val> args;
        eval_args(n[2], f, args);
        return call(fn, Val(), args);
    }
    case JS_NEW: {
        Val fn = eval(n[1], f);
        std::vector<Val> args;
        eval_args(n[2], f, args);
        if (!fn.is_function()) throw_type_error("not a constructor");
        if (fn.o->kind == Kind::Host) return fn.o->cls->call(*this, Val(), args);
        if (fn.o->kind == Kind::Native) {
            Val marker = make_object();
            marker.o->set(A_constructor, fn);
            Val r = fn.o->native(*this, marker, args);
            return r.is_undef() ? marker : r;
        }
        Val inst = make_object();
        if (Val* p = fn.o->find(A_prototype)) inst.o->proto = *p;
        Val r = call_closure(fn.o, inst, args);
        return r.is_obj() ? r : inst;
    }
    case JS_UNARY: {
        int u = n[1].as_int();
        if (u == JU_TYPEOF) {
            const ::Value& e = n[2];
            Val v = op_of(e) == JS_NAME ? lookup(node_atom(e[1]), f, true) : eval(e, f);
            return Val::str(type_of(v));
        }
        Val v = eval(n[2], f);
        switch (u) {
        case JU_NOT: return Val::boolean(!v.truthy());
        case JU_NEG: return Val::num(-v.to_num());
        case JU_PLUS: return Val::num(v.to_num());
        case JU_BNOT: return Val::num((double)~(int32_t)to_int32(v.to_num()));
        case JU_VOID: return Val();
        }
        return Val();
    }
    case JS_DELETE: {
        const ::Value& t = n[1];
        int top = op_of(t);
        if (top == JS_GET || top == JS_INDEX) {
            Val o = eval(t[1], f);
            uint32_t k = top == JS_GET ? node_atom(t[2]) : key_of(eval(t[2], f));
            if (o.is_obj() && o.o->kind != Kind::Host) {
                if (o.o->kind == Kind::Array) {
                    size_t idx;
                    if (index_of_atom(k, idx) && idx < o.o->items.size()) {
                        o.o->items[idx] = Val();
                        return Val::boolean(true);
                    }
                }
                o.o->remove(k);
            } else if (o.is_obj() && o.o->cls && o.o->cls->set) {
                o.o->cls->set(*this, o.o, k, Val());
            }
        }
        return Val::boolean(true);
    }
    case JS_INC: {
        int kind = n[1].as_int();
        const ::Value& t = n[2];
        int top = op_of(t);
        Val obj, keyv;
        uint32_t name = 0;
        Val old;
        if (top == JS_NAME) {
            name = node_atom(t[1]);
            old = lookup(name, f, false);
        } else {
            obj = eval(t[1], f);
            keyv = top == JS_GET ? Val::str(atom_str(node_atom(t[2]))) : eval(t[2], f);
            old = get(obj, keyv);
        }
        double ov = old.to_num();
        double nv = (kind == JI_PREINC || kind == JI_POSTINC) ? ov + 1 : ov - 1;
        Val nvv = Val::num(nv);
        if (top == JS_NAME) assign_name(name, nvv, f);
        else put(obj, keyv, nvv);
        return (kind == JI_POSTINC || kind == JI_POSTDEC) ? Val::num(ov) : nvv;
    }
    case JS_BIN: {
        Val a = eval(n[2], f);
        Val b = eval(n[3], f);
        return binop(n[1].as_int(), a, b);
    }
    case JS_AND: {
        Val a = eval(n[1], f);
        return a.truthy() ? eval(n[2], f) : a;
    }
    case JS_OR: {
        Val a = eval(n[1], f);
        return a.truthy() ? a : eval(n[2], f);
    }
    case JS_NULLISH: {
        Val a = eval(n[1], f);
        return a.is_nullish() ? eval(n[2], f) : a;
    }
    case JS_COND: return eval(n[1], f).truthy() ? eval(n[2], f) : eval(n[3], f);
    case JS_ASSIGN: {
        int aop = n[1].as_int();
        const ::Value& t = n[2];
        if (aop == 0) {
            int top = op_of(t);
            if (top == JS_NAME) {
                Val v = eval(n[3], f);
                assign_name(node_atom(t[1]), v, f);
                return v;
            }
            if (top == JS_GET) {
                Val o = eval(t[1], f);
                Val v = eval(n[3], f);
                put(o, node_atom(t[2]), v);
                return v;
            }
            if (top == JS_INDEX) {
                Val o = eval(t[1], f);
                Val k = eval(t[2], f);
                Val v = eval(n[3], f);
                put(o, k, v);
                return v;
            }
            throw_error("SyntaxError: invalid assignment target");
        }
        int top = op_of(t);
        if (top == JS_NAME) {
            uint32_t name = node_atom(t[1]);
            Val old = lookup(name, f, false);
            Val r = binop(aop, old, eval(n[3], f));
            assign_name(name, r, f);
            return r;
        }
        Val o = eval(t[1], f);
        Val k = top == JS_GET ? Val::str(atom_str(node_atom(t[2]))) : eval(t[2], f);
        Val old = get(o, k);
        Val r = binop(aop, old, eval(n[3], f));
        put(o, k, r);
        return r;
    }
    case JS_SEQ: {
        Val last;
        const ::Value& list = n[1];
        for (size_t i = 0; i < list.size(); i++) last = eval(list[i], f);
        return last;
    }
    }
    throw_error("SyntaxError: unsupported expression");
}

Vm::Flow Vm::exec_list(const ::Value& list, Frame& f) {
    for (size_t i = 0; i < list.size(); i++) {
        Flow fl = exec(list[i], f);
        if (fl != Flow::Normal) return fl;
    }
    return Flow::Normal;
}

static bool has_lexical(const ::Value& list) {
    for (size_t i = 0; i < list.size(); i++) {
        const ::Value& s = list[i];
        if (op_of(s) == JS_VAR && s[1].as_int() != 0) return true;
        if (op_of(s) == JS_FUNCDECL) return true;
    }
    return false;
}

static Val for_in_keys(Vm& vm, const Val& o) {
    std::vector<Val> keys;
    std::vector<uint32_t> own;
    if (o.is_array()) {
        for (size_t i = 0; i < o.o->items.size(); i++) keys.push_back(Val::str(std::to_string(i)));
    } else if (o.is_str()) {
        for (size_t i = 0; i < o.s->data.size(); i++) keys.push_back(Val::str(std::to_string(i)));
    }
    if (o.is_obj()) {
        if (o.o->kind == Kind::Host && o.o->cls->keys) {
            std::vector<uint32_t> ks;
            o.o->cls->keys(o.o, ks);
            for (uint32_t k : ks) keys.push_back(Val::str(atom_str(k)));
        }
        ordered_keys(o.o, own);
        for (uint32_t k : own) keys.push_back(Val::str(atom_str(k)));
    }
    (void)vm;
    return make_array(std::move(keys));
}

Vm::Flow Vm::exec(const ::Value& n, Frame& f) {
    switch (op_of(n)) {
    case JS_EMPTY: return Flow::Normal;
    case JS_EXPR: f.completion = eval(n[1], f); return Flow::Normal;
    case JS_VAR: {
        int kind = n[1].as_int();
        const ::Value& list = n[2];
        for (size_t i = 0; i + 1 < list.size(); i += 2) {
            uint32_t name = node_atom(list[i]);
            const ::Value& init = list[i + 1];
            if (kind == 0) {
                if (init.is_null()) {
                    if (!f.scope->lookup(name)) f.scope->declare(name, Val());
                } else {
                    Val v = eval(init, f);
                    Scope* s = f.scope.get();
                    while (s && !s->find(name) && !s->function_scope) s = s->parent.get();
                    if (s && s->find(name)) *s->find(name) = v;
                    else f.scope->declare(name, v);
                }
            } else {
                f.scope->declare(name, init.is_null() ? Val() : eval(init, f));
            }
        }
        return Flow::Normal;
    }
    case JS_IF: {
        if (eval(n[1], f).truthy()) return exec(n[2], f);
        if (!n[3].is_null()) return exec(n[3], f);
        return Flow::Normal;
    }
    case JS_BLOCK: {
        const ::Value& list = n[1];
        if (!has_lexical(list)) return exec_list(list, f);
        ScopePtr saved = f.scope;
        f.scope = std::make_shared<Scope>();
        f.scope->parent = saved;
        for (size_t i = 0; i < list.size(); i++) {
            if (op_of(list[i]) == JS_FUNCDECL) f.scope->declare(node_atom(list[i][1]), make_function(list[i][2], f));
        }
        Flow fl = exec_list(list, f);
        f.scope = saved;
        return fl;
    }
    case JS_FUNCDECL: return Flow::Normal;
    case JS_FOR: {
        ScopePtr saved = f.scope;
        bool lexical = op_of(n[1]) == JS_VAR && n[1][1].as_int() != 0;
        if (lexical) {
            f.scope = std::make_shared<Scope>();
            f.scope->parent = saved;
        }
        if (!n[1].is_null()) exec(n[1], f);
        int guard = 0;
        Flow result = Flow::Normal;
        auto copy_iter = [&]() {
            if (!lexical) return;
            ScopePtr iter = std::make_shared<Scope>();
            iter->parent = saved;
            iter->vars = f.scope->vars;
            f.scope = iter;
        };
        copy_iter();
        for (;;) {
            if (!n[2].is_null() && !eval(n[2], f).truthy()) break;
            Flow fl = exec(n[4], f);
            if (fl == Flow::Break) break;
            if (fl == Flow::Return) {
                result = fl;
                break;
            }
            copy_iter();
            if (!n[3].is_null()) eval(n[3], f);
            if (++guard > 1000000) throw_error("RangeError: loop limit");
        }
        f.scope = saved;
        return result;
    }
    case JS_FORIN:
    case JS_FOROF: {
        bool of = op_of(n) == JS_FOROF;
        Val src = eval(n[3], f);
        Val list;
        if (of) {
            if (src.is_array()) list = make_array(src.o->items);
            else if (src.is_str()) {
                std::vector<Val> chars;
                for (char c : src.s->data) chars.push_back(Val::str(std::string(1, c)));
                list = make_array(std::move(chars));
            } else throw_type_error("value is not iterable");
        } else {
            if (src.is_nullish()) return Flow::Normal;
            list = for_in_keys(*this, src);
        }
        ScopePtr saved = f.scope;
        int kind = n[1].as_int();
        Flow result = Flow::Normal;
        for (auto& item : list.o->items) {
            if (kind > 0) {
                f.scope = std::make_shared<Scope>();
                f.scope->parent = saved;
                f.scope->declare(node_atom(n[2][1]), item);
            } else {
                assign(n[2], item, f);
            }
            Flow fl = exec(n[4], f);
            f.scope = saved;
            if (fl == Flow::Break) break;
            if (fl == Flow::Return) {
                result = fl;
                break;
            }
        }
        f.scope = saved;
        return result;
    }
    case JS_WHILE: {
        int guard = 0;
        while (eval(n[1], f).truthy()) {
            Flow fl = exec(n[2], f);
            if (fl == Flow::Break) break;
            if (fl == Flow::Return) return fl;
            if (++guard > 1000000) throw_error("RangeError: loop limit");
        }
        return Flow::Normal;
    }
    case JS_DOWHILE: {
        int guard = 0;
        do {
            Flow fl = exec(n[1], f);
            if (fl == Flow::Break) break;
            if (fl == Flow::Return) return fl;
            if (++guard > 1000000) throw_error("RangeError: loop limit");
        } while (eval(n[2], f).truthy());
        return Flow::Normal;
    }
    case JS_BREAK: return Flow::Break;
    case JS_CONTINUE: return Flow::Continue;
    case JS_RETURN:
        f.ret = n[1].is_null() ? Val() : eval(n[1], f);
        return Flow::Return;
    case JS_THROW: throw Throw{eval(n[1], f)};
    case JS_SWITCH: {
        Val d = eval(n[1], f);
        const ::Value& cases = n[2];
        long start = -1;
        for (size_t i = 0; i < cases.size(); i++) {
            const ::Value& test = cases[i][0];
            if (test.is_null()) continue;
            if (strict_equals(d, eval(test, f))) {
                start = (long)i;
                break;
            }
        }
        if (start < 0) {
            for (size_t i = 0; i < cases.size(); i++) {
                if (cases[i][0].is_null()) start = (long)i;
            }
        }
        if (start < 0) return Flow::Normal;
        for (size_t i = (size_t)start; i < cases.size(); i++) {
            Flow fl = exec_list(cases[i][1], f);
            if (fl == Flow::Break) return Flow::Normal;
            if (fl != Flow::Normal) return fl;
        }
        return Flow::Normal;
    }
    case JS_TRY: {
        Flow result = Flow::Normal;
        bool rethrow = false;
        Throw pending;
        try {
            result = exec_list(n[1], f);
        } catch (Throw& t) {
            if (!n[3].is_null()) {
                ScopePtr saved = f.scope;
                f.scope = std::make_shared<Scope>();
                f.scope->parent = saved;
                if (!n[2].is_null()) f.scope->declare(node_atom(n[2]), t.value);
                try {
                    result = exec_list(n[3], f);
                } catch (Throw& t2) {
                    f.scope = saved;
                    if (n[4].is_null()) throw;
                    pending = t2;
                    rethrow = true;
                }
                f.scope = saved;
            } else {
                pending = t;
                rethrow = true;
            }
        }
        if (!n[4].is_null()) {
            Flow ff = exec_list(n[4], f);
            if (ff != Flow::Normal) return ff;
        }
        if (rethrow) throw pending;
        return result;
    }
    }
    throw_error("SyntaxError: unsupported statement");
}

static Vm* g_vm = nullptr;

Vm& vm() {
    if (!g_vm) g_vm = new Vm();
    return *g_vm;
}

}
