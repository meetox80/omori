#include "regexp.h"

#include <unordered_map>

#include "rx.h"
#include "vm.h"

namespace js {

namespace {

struct RegexObj {
    Rx* rx;
    std::string source;
    std::string flags;
    bool global;
    bool icase;
    bool multiline;
    double last_index;
};

struct Statics {
    std::string groups[10];
    std::string input;
    std::string last_match;
};

Statics& statics() {
    static Statics s;
    return s;
}

Rx* compile_cached(const std::string& source, const std::string& flags) {
    static std::unordered_map<std::string, Rx*> cache;
    std::string key = flags + "/" + source;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    std::string f;
    if (flags.find('i') != std::string::npos) f += 'i';
    if (flags.find('m') != std::string::npos) f += 'm';
    Rx* rx = rx_compile(source.c_str(), f.c_str());
    cache[key] = rx;
    return rx;
}

HostClass& regexp_class();

RegexObj* get_re(const Val& v) {
    if (v.t != T::Obj || v.o->kind != Kind::Host || v.o->cls != &regexp_class()) return nullptr;
    return static_cast<RegexObj*>(v.o->ptr);
}

bool regexp_get(Vm& vm, Obj* self, uint32_t key, Val& out) {
    RegexObj* r = static_cast<RegexObj*>(self->ptr);
    const std::string& k = atom_name(key);
    if (k == "source") out = Val::str(r->source);
    else if (k == "flags") out = Val::str(r->flags);
    else if (k == "global") out = Val::boolean(r->global);
    else if (k == "ignoreCase") out = Val::boolean(r->icase);
    else if (k == "multiline") out = Val::boolean(r->multiline);
    else if (k == "lastIndex") out = Val::num(r->last_index);
    else if (k == "test") {
        out = make_native([](Vm& vm2, const Val& s, std::vector<Val>& a) { return regexp_test(vm2, s, a.empty() ? std::string("undefined") : a[0].to_str()); });
    } else if (k == "exec") {
        out = make_native([](Vm& vm2, const Val& s, std::vector<Val>& a) { return regexp_exec(vm2, s, a.empty() ? std::string("undefined") : a[0].to_str()); });
    } else if (k == "toString") {
        out = make_native([](Vm&, const Val& s, std::vector<Val>&) {
            RegexObj* r2 = get_re(s);
            return Val::str(r2 ? "/" + r2->source + "/" + r2->flags : std::string());
        });
    }
    (void)vm;
    return true;
}

bool regexp_set(Vm&, Obj* self, uint32_t key, const Val& v) {
    RegexObj* r = static_cast<RegexObj*>(self->ptr);
    if (atom_name(key) == "lastIndex") r->last_index = v.to_num();
    return true;
}

HostClass& regexp_class() {
    static HostClass hc = [] {
        HostClass c;
        c.name = "RegExp";
        c.get = regexp_get;
        c.set = regexp_set;
        c.finalize = [](Obj* self) {
            delete static_cast<RegexObj*>(self->ptr);
            self->ptr = nullptr;
        };
        return c;
    }();
    return hc;
}

bool search_at(RegexObj* r, const std::string& s, size_t from, RxMatch& m) { return rx_search(r->rx, s.data(), s.size(), from, &m) != 0; }

void set_statics(RegexObj* r, const std::string& s, const RxMatch& m) {
    Statics& st = statics();
    int n = rx_groups(r->rx);
    for (int i = 1; i <= 9; i++) {
        if (i <= n && m.start[i] != (size_t)-1 && m.end[i] != (size_t)-1) st.groups[i] = s.substr(m.start[i], m.end[i] - m.start[i]);
        else st.groups[i].clear();
    }
    st.input = s;
    st.last_match = s.substr(m.start[0], m.end[0] - m.start[0]);
}

Val match_array(RegexObj* r, const std::string& s, const RxMatch& m) {
    int n = rx_groups(r->rx);
    std::vector<Val> items;
    for (int i = 0; i <= n; i++) {
        if (m.start[i] != (size_t)-1 && m.end[i] != (size_t)-1) items.push_back(Val::str(s.substr(m.start[i], m.end[i] - m.start[i])));
        else items.push_back(Val());
    }
    Val arr = make_array(std::move(items));
    arr.o->set(atom("index"), Val::num((double)m.start[0]));
    arr.o->set(atom("input"), Val::str(s));
    return arr;
}

std::string expand_replacement(const std::string& rep, const std::string& s, const RxMatch& m, int ngroups) {
    std::string out;
    for (size_t i = 0; i < rep.size(); i++) {
        char c = rep[i];
        if (c != '$' || i + 1 >= rep.size()) {
            out += c;
            continue;
        }
        char d = rep[i + 1];
        if (d == '$') {
            out += '$';
            i++;
        } else if (d == '&') {
            out += s.substr(m.start[0], m.end[0] - m.start[0]);
            i++;
        } else if (d == '`') {
            out += s.substr(0, m.start[0]);
            i++;
        } else if (d == '\'') {
            out += s.substr(m.end[0]);
            i++;
        } else if (d >= '0' && d <= '9') {
            int g = d - '0';
            size_t used = 1;
            if (i + 2 < rep.size() && rep[i + 2] >= '0' && rep[i + 2] <= '9') {
                int g2 = g * 10 + (rep[i + 2] - '0');
                if (g2 >= 1 && g2 <= ngroups) {
                    g = g2;
                    used = 2;
                }
            }
            if (g >= 1 && g <= ngroups) {
                if (m.start[g] != (size_t)-1 && m.end[g] != (size_t)-1) out += s.substr(m.start[g], m.end[g] - m.start[g]);
                i += used;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

}

Val make_regexp(Vm& vm, const std::string& source, const std::string& flags) {
    Rx* rx = compile_cached(source, flags);
    if (!rx) vm.throw_error("SyntaxError: Invalid regular expression: /" + source + "/");
    RegexObj* r = new RegexObj{rx, source, flags, flags.find('g') != std::string::npos, flags.find('i') != std::string::npos,
                               flags.find('m') != std::string::npos, 0};
    Obj* o = obj_new(Kind::Host);
    o->cls = &regexp_class();
    o->ptr = r;
    return Val::obj(o);
}

bool is_regexp(const Val& v) { return get_re(v) != nullptr; }

Val regexp_exec(Vm& vm, const Val& re, const std::string& s) {
    RegexObj* r = get_re(re);
    if (!r) vm.throw_type_error("RegExp.prototype.exec called on incompatible receiver");
    size_t from = 0;
    if (r->global) {
        if (r->last_index < 0 || r->last_index > (double)s.size()) {
            r->last_index = 0;
            return Val::null();
        }
        from = (size_t)r->last_index;
    }
    RxMatch m;
    if (!search_at(r, s, from, m)) {
        if (r->global) r->last_index = 0;
        return Val::null();
    }
    if (r->global) r->last_index = (double)(m.end[0] == m.start[0] && m.end[0] == from ? m.end[0] : m.end[0]);
    set_statics(r, s, m);
    return match_array(r, s, m);
}

Val regexp_test(Vm& vm, const Val& re, const std::string& s) { return Val::boolean(!regexp_exec(vm, re, s).is_null()); }

Val string_match(Vm& vm, const std::string& s, const Val& re0) {
    Val re = is_regexp(re0) ? re0 : make_regexp(vm, re0.is_undef() ? std::string() : re0.to_str(), "");
    RegexObj* r = get_re(re);
    if (!r->global) return regexp_exec(vm, re, s);
    std::vector<Val> all;
    size_t from = 0;
    RxMatch m;
    RxMatch last;
    bool any = false;
    while (from <= s.size() && search_at(r, s, from, m)) {
        all.push_back(Val::str(s.substr(m.start[0], m.end[0] - m.start[0])));
        last = m;
        any = true;
        from = m.end[0] > m.start[0] ? m.end[0] : m.end[0] + 1;
    }
    r->last_index = 0;
    if (!any) return Val::null();
    set_statics(r, s, last);
    return make_array(std::move(all));
}

Val string_replace(Vm& vm, const std::string& s, const Val& pattern, const Val& replacement) {
    RegexObj* r = get_re(pattern);
    std::string out;
    if (!r) {
        std::string pat = pattern.to_str();
        size_t p = s.find(pat);
        if (p == std::string::npos) return Val::str(s);
        std::string rep;
        if (replacement.is_function()) {
            rep = vm.call(replacement, Val(), {Val::str(pat), Val::num((double)p), Val::str(s)}).to_str();
        } else {
            RxMatch m;
            m.start[0] = p;
            m.end[0] = p + pat.size();
            rep = expand_replacement(replacement.to_str(), s, m, 0);
        }
        return Val::str(s.substr(0, p) + rep + s.substr(p + pat.size()));
    }
    int ngroups = rx_groups(r->rx);
    size_t from = 0, copied = 0;
    RxMatch m;
    std::string rep_str = replacement.is_function() ? std::string() : replacement.to_str();
    while (from <= s.size() && search_at(r, s, from, m)) {
        out += s.substr(copied, m.start[0] - copied);
        set_statics(r, s, m);
        if (replacement.is_function()) {
            std::vector<Val> args;
            for (int i = 0; i <= ngroups; i++) {
                if (m.start[i] != (size_t)-1 && m.end[i] != (size_t)-1) args.push_back(Val::str(s.substr(m.start[i], m.end[i] - m.start[i])));
                else args.push_back(Val());
            }
            args.push_back(Val::num((double)m.start[0]));
            args.push_back(Val::str(s));
            out += vm.call(replacement, Val(), args).to_str();
        } else {
            out += expand_replacement(rep_str, s, m, ngroups);
        }
        copied = m.end[0];
        if (!r->global) break;
        if (m.end[0] == m.start[0]) {
            if (m.end[0] < s.size()) out += s[m.end[0]];
            copied = m.end[0] + 1;
            from = m.end[0] + 1;
        } else {
            from = m.end[0];
        }
    }
    if (copied < s.size()) out += s.substr(copied);
    if (r->global) r->last_index = 0;
    return Val::str(out);
}

Val string_split_regex(Vm& vm, const std::string& s, const Val& re, long limit) {
    RegexObj* r = get_re(re);
    (void)vm;
    std::vector<Val> parts;
    if (limit == 0) return make_array();
    if (s.empty()) {
        RxMatch m;
        if (search_at(r, s, 0, m)) return make_array();
        parts.push_back(Val::str(s));
        return make_array(std::move(parts));
    }
    int ngroups = rx_groups(r->rx);
    size_t p = 0, q = 0;
    RxMatch m;
    while (q < s.size()) {
        if (!search_at(r, s, q, m)) break;
        size_t e = m.end[0];
        if (m.start[0] >= s.size()) break;
        if (e == p || (e == m.start[0] && m.start[0] == q && e == p)) {
            q = m.start[0] + 1;
            continue;
        }
        if (m.end[0] == m.start[0] && m.start[0] == p) {
            q = m.start[0] + 1;
            continue;
        }
        parts.push_back(Val::str(s.substr(p, m.start[0] - p)));
        if (limit > 0 && (long)parts.size() >= limit) return make_array(std::move(parts));
        for (int i = 1; i <= ngroups; i++) {
            if (m.start[i] != (size_t)-1 && m.end[i] != (size_t)-1) parts.push_back(Val::str(s.substr(m.start[i], m.end[i] - m.start[i])));
            else parts.push_back(Val());
            if (limit > 0 && (long)parts.size() >= limit) return make_array(std::move(parts));
        }
        p = e;
        q = e > m.start[0] ? e : e + 1;
    }
    parts.push_back(Val::str(s.substr(p)));
    if (limit > 0 && (long)parts.size() > limit) parts.resize((size_t)limit);
    return make_array(std::move(parts));
}

Val string_search(Vm& vm, const std::string& s, const Val& re0) {
    Val re = is_regexp(re0) ? re0 : make_regexp(vm, re0.is_undef() ? std::string() : re0.to_str(), "");
    RegexObj* r = get_re(re);
    RxMatch m;
    if (!search_at(r, s, 0, m)) return Val::num(-1);
    set_statics(r, s, m);
    return Val::num((double)m.start[0]);
}

void install_regexp(Vm& vm) {
    Val ctor = make_native([](Vm& vm2, const Val&, std::vector<Val>& a) -> Val {
        Val p = a.empty() ? Val() : a[0];
        if (RegexObj* r = get_re(p)) {
            std::string flags = a.size() > 1 && !a[1].is_undef() ? a[1].to_str() : r->flags;
            return make_regexp(vm2, r->source, flags);
        }
        std::string src = p.is_undef() ? std::string("(?:)") : p.to_str();
        std::string flags = a.size() > 1 && !a[1].is_undef() ? a[1].to_str() : std::string();
        return make_regexp(vm2, src, flags);
    });
    vm.set_global("RegExp", ctor);
    vm.set_regexp_ctor(ctor);
}

Val regexp_static(uint32_t key, bool& found) {
    const std::string& k = atom_name(key);
    found = false;
    if (k.size() == 2 && k[0] == '$' && k[1] >= '1' && k[1] <= '9') {
        found = true;
        return Val::str(statics().groups[k[1] - '0']);
    }
    if (k == "lastMatch" || k == "$&") {
        found = true;
        return Val::str(statics().last_match);
    }
    if (k == "input" || k == "$_") {
        found = true;
        return Val::str(statics().input);
    }
    return Val();
}

}
