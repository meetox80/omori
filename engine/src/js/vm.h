#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "value.h"

namespace js {

struct Scope {
    std::shared_ptr<Scope> parent;
    std::vector<Prop> vars;
    bool function_scope = false;
    Val* find(uint32_t key);
    Val* lookup(uint32_t key);
    void declare(uint32_t key, const Val& v);
};

using ScopePtr = std::shared_ptr<Scope>;

struct Closure {
    const ::Value* node = nullptr;
    ScopePtr env;
    Val self;
    bool arrow = false;
    std::shared_ptr<const void> owner;
};

void closure_free(Closure* c);

struct Throw {
    Val value;
};

using GlobalResolver = std::function<bool(uint32_t key, Val& out)>;

class Vm {
public:
    Vm();
    void set_global(const char* name, const Val& v);
    void set_global(uint32_t key, const Val& v);
    Val* global(uint32_t key);
    void set_resolver(GlobalResolver r) { resolver_ = std::move(r); }
    void set_regexp_ctor(const Val& v) { regexp_ctor_ = v; }

    bool run(const ::Value& program, const Val& self, const ScopePtr& locals, Val* result, std::shared_ptr<const void> owner = nullptr);
    Val run_or_throw(const ::Value& program, const Val& self, const ScopePtr& locals, std::shared_ptr<const void> owner = nullptr);
    Val run_in_scope(const ::Value& program, const Val& self, const ScopePtr& scope, std::shared_ptr<const void> owner = nullptr);
    using EvalCompiler = std::function<std::shared_ptr<const ::Value>(const std::string& code, std::string& err)>;
    void set_eval_compiler(EvalCompiler c) { eval_compiler_ = std::move(c); }
    bool has_eval() const { return (bool)eval_compiler_; }

    Val call(const Val& fn, const Val& self, std::vector<Val>& args);
    Val call(const Val& fn, const Val& self, std::initializer_list<Val> args);
    Val get(const Val& obj, uint32_t key);
    Val get(const Val& obj, const Val& key);
    void put(const Val& obj, uint32_t key, const Val& v);
    void put(const Val& obj, const Val& key, const Val& v);
    Val call_method(const Val& obj, uint32_t key, std::vector<Val>& args);

    Val binop(int op, const Val& a, const Val& b);
    [[noreturn]] void throw_error(const std::string& msg);
    [[noreturn]] void throw_type_error(const std::string& msg);

    std::string last_error;
    int depth = 0;

private:
    enum class Flow { Normal, Break, Continue, Return };

    struct Frame {
        ScopePtr scope;
        Val self;
        Val ret;
        Val completion;
        std::shared_ptr<const void> owner;
    };

    std::vector<Prop> globals_;
    EvalCompiler eval_compiler_;
    std::vector<std::shared_ptr<const ::Value>> eval_programs_;
    Val eval_native_;
    Val regexp_ctor_;
    Val proto_string_, proto_number_, proto_boolean_, proto_array_, proto_function_, proto_object_;
    GlobalResolver resolver_;

    Val eval(const ::Value& n, Frame& f);
    Flow exec(const ::Value& n, Frame& f);
    Flow exec_list(const ::Value& list, Frame& f);
    void hoist(const ::Value& list, Frame& f);
    Val lookup(uint32_t key, Frame& f, bool for_typeof);
    void assign_name(uint32_t key, const Val& v, Frame& f);
    void assign(const ::Value& target, const Val& v, Frame& f);
    Val make_function(const ::Value& node, Frame& f);
    Val call_closure(Obj* fn, const Val& self, std::vector<Val>& args);
    void eval_args(const ::Value& list, Frame& f, std::vector<Val>& out);
    Val builtin_member(const Val& obj, uint32_t key);
    Val string_method(const Val& self, uint32_t key, std::vector<Val>& args, bool& found);
    Val number_method(const Val& self, uint32_t key, std::vector<Val>& args, bool& found);
    Val array_method(const Val& self, uint32_t key, std::vector<Val>& args, bool& found);
    Val object_method(const Val& self, uint32_t key, std::vector<Val>& args, bool& found);
    void install_builtins();
    Val eval_code(const std::string& code, Frame* f);
};

Vm& vm();
uint32_t key_of(const Val& k);
std::string json_stringify(const Val& v);
Val json_parse_text(const std::string& s);

}
