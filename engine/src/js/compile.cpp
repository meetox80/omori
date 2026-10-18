#include "compile.h"

#include <cmath>
#include <set>
#include <unordered_map>

#include "js.h"
#include "node.h"
#include "script.h"
#include "dataview.h"
#include "vm.h"

namespace js {

std::shared_ptr<Document> program_doc(Node* program) {
    Buf b = {0};
    vbin_dump(program, &b);
    auto doc = std::make_shared<Document>();
    std::vector<uint8_t> data(b.data, b.data + b.len);
    buf_free(&b);
    if (!doc->load_memory(std::move(data))) return nullptr;
    return doc;
}

std::shared_ptr<Document> compile_doc(const std::string& src, std::string& err) {
    char e[256];
    Node* ast = js_compile(src.data(), src.size(), e, sizeof(e));
    if (!ast) {
        err = e;
        return nullptr;
    }
    auto doc = program_doc(ast);
    node_free(ast);
    if (!doc) err = "could not encode program";
    return doc;
}

std::shared_ptr<const ::Value> compile_program(const std::string& src, std::string& err) {
    auto doc = compile_doc(src, err);
    if (!doc) return nullptr;
    return std::shared_ptr<const ::Value>(doc, &doc->root());
}

std::shared_ptr<const ::Value> compile_cached(const std::string& src) {
    static std::unordered_map<std::string, std::shared_ptr<const ::Value>> cache;
    auto it = cache.find(src);
    if (it != cache.end()) return it->second;
    std::string err;
    auto p = compile_program(src, err);
    if (cache.size() > 2048) cache.clear();
    cache.emplace(src, p);
    return p;
}

void install_runtime_eval() {
    vm().set_eval_compiler([](const std::string& code, std::string& err) -> std::shared_ptr<const ::Value> {
        auto p = compile_cached(code);
        if (!p) compile_program(code, err);
        return p;
    });
}

Node* value_to_node(const ::Value& v) {
    switch (v.t) {
    case ::Value::Null: return node_null();
    case ::Value::Bool: return node_bool(v.b);
    case ::Value::Int:
    case ::Value::Num: return node_num(v.num());
    case ::Value::Str: return node_strn(v.str(), v.size());
    case ::Value::Bytes: return node_bytes(v.bytes, v.size());
    case ::Value::Arr: {
        Node* a = node_arr();
        for (size_t i = 0; i < v.size(); i++) node_push(a, value_to_node(v[i]));
        return a;
    }
    case ::Value::Obj: {
        Node* o = node_obj();
        for (const Member* m = v.begin_members(); m != v.end_members(); ++m) node_set(o, m->key, value_to_node(m->v));
        return o;
    }
    }
    return node_null();
}

static Node* to_node(const Val& v, bool in_array, std::set<const Obj*>& stack) {
    switch (v.t) {
    case T::Undef: return in_array ? node_null() : nullptr;
    case T::Null: return node_null();
    case T::Bool: return node_bool(v.b);
    case T::Num: return std::isfinite(v.n) ? node_num(v.n) : node_null();
    case T::Str: return node_strn(v.s->data.data(), v.s->data.size());
    case T::Obj: break;
    }
    Obj* o = v.o;
    if (o->kind == Kind::Function || o->kind == Kind::Native) return in_array ? node_null() : nullptr;
    if (o->kind == Kind::Host) {
        if (const ::Value* d = data_of(v)) return value_to_node(*d);
        return in_array ? node_null() : nullptr;
    }
    if (stack.count(o)) return node_null();
    stack.insert(o);
    Node* out;
    if (o->kind == Kind::Array) {
        out = node_arr();
        for (auto& it : o->items) node_push(out, to_node(it, true, stack));
    } else {
        out = node_obj();
        std::vector<uint32_t> keys;
        ordered_keys(o, keys);
        for (uint32_t k : keys) {
            const Val* pv = o->find(k);
            if (!pv) continue;
            Node* c = to_node(*pv, false, stack);
            if (c) node_set(out, atom_name(k).c_str(), c);
        }
    }
    stack.erase(o);
    return out;
}

Node* val_to_node(const Val& v) {
    std::set<const Obj*> stack;
    Node* n = to_node(v, false, stack);
    return n ? n : node_null();
}

std::shared_ptr<Document> route_doc(const Val& route) {
    Node* n = val_to_node(route);
    if (n->type == N_OBJ) script_compile_route_list(node_get(n, "list"), "route", 0);
    auto doc = program_doc(n);
    node_free(n);
    return doc;
}

std::shared_ptr<Document> list_doc(const Val& list, const char* where) {
    Node* n = val_to_node(list);
    script_compile_list(n, where);
    auto doc = program_doc(n);
    node_free(n);
    return doc;
}

}
