#include "save.h"
#include "lang_names.h"
#include "params.h"
#include "timers.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <new>
#include <unordered_map>

#include "../core/bytes.h"
#include "../core/fs.h"
#include "../js/bind.h"
#include "../js/compile.h"
#include "../js/vm.h"
#include "../platform/platform.h"
#include "action.h"
#include "battle.h"
#include "data.h"
#include "enemy.h"
#include "filter.h"
#include "game.h"
#include "interpreter.h"
#include "party.h"
#include "scene.h"

namespace rpg {
namespace save {

struct Ctx {
    std::unordered_map<js::Obj*, int> ids;
    std::vector<js::Val> objs;
    std::shared_ptr<Document> doc;
    std::vector<std::function<void()>> fixups;
};

static void put_varint(std::vector<uint8_t>& b, uint64_t n) {
    for (;;) {
        uint8_t v = n & 0x7F;
        n >>= 7;
        if (!n) {
            b.push_back(v);
            return;
        }
        b.push_back(v | 0x80);
    }
}

class Writer {
public:
    void null() { body_.push_back(0); }
    void boolean(bool v) { body_.push_back(v ? 2 : 1); }

    void num(double v) {
        if (v == std::floor(v) && v >= -2147483648.0 && v < 2147483648.0) {
            int64_t i = (int64_t)v;
            uint64_t z = (uint64_t)i << 1;
            body_.push_back(3);
            put_varint(body_, i < 0 ? ~z : z);
            return;
        }
        uint64_t bits;
        std::memcpy(&bits, &v, 8);
        body_.push_back(4);
        for (int k = 0; k < 8; k++) body_.push_back((uint8_t)(bits >> (8 * k)));
    }

    void str(const char* s, size_t n) {
        body_.push_back(5);
        put_varint(body_, intern(s, n));
    }

    void cstr(const char* s) { str(s, std::strlen(s)); }

    void bytes(const uint8_t* p, size_t n) {
        body_.push_back(8);
        put_varint(body_, n);
        body_.insert(body_.end(), p, p + n);
    }

    void key(const char* k) { put_varint(body_, intern(k, std::strlen(k))); }

    void arr(size_t n) {
        body_.push_back(6);
        put_varint(body_, n);
    }

    size_t open_obj() {
        body_.push_back(7);
        size_t at = body_.size();
        body_.insert(body_.end(), COUNT_BYTES, 0);
        return at;
    }

    void close_obj(size_t at, size_t n) {
        for (int k = 0; k < COUNT_BYTES; k++) body_[at + k] = (uint8_t)(((n >> (7 * k)) & 0x7F) | (k + 1 < COUNT_BYTES ? 0x80 : 0));
    }

    std::vector<uint8_t> finish() {
        std::vector<uint8_t> table;
        put_varint(table, offs_.size());
        for (size_t i = 0; i < offs_.size(); i++) {
            put_varint(table, lens_[i]);
            table.insert(table.end(), pool_.begin() + offs_[i], pool_.begin() + offs_[i] + lens_[i]);
            table.push_back(0);
        }
        std::vector<uint8_t> out;
        out.reserve(12 + table.size() + body_.size());
        out.insert(out.end(), {'V', 'B', 'I', 'N', 2, 0, 0, 0});
        for (int k = 0; k < 4; k++) out.push_back((uint8_t)(table.size() >> (8 * k)));
        out.insert(out.end(), table.begin(), table.end());
        out.insert(out.end(), body_.begin(), body_.end());
        std::vector<uint8_t>().swap(body_);
        return out;
    }

private:
    static constexpr int COUNT_BYTES = 4;

    static uint32_t hash(const char* s, size_t n) {
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
        return h;
    }

    uint32_t intern(const char* s, size_t n) {
        if ((offs_.size() + 1) * 2 > slots_.size()) rehash();
        size_t mask = slots_.size() - 1, at = hash(s, n) & mask;
        while (uint32_t id = slots_[at]) {
            if (lens_[id - 1] == n && !std::memcmp(pool_.data() + offs_[id - 1], s, n)) return id - 1;
            at = (at + 1) & mask;
        }
        offs_.push_back((uint32_t)pool_.size());
        lens_.push_back((uint32_t)n);
        pool_.insert(pool_.end(), s, s + n);
        slots_[at] = (uint32_t)offs_.size();
        return (uint32_t)offs_.size() - 1;
    }

    void rehash() {
        std::vector<uint32_t> slots(slots_.empty() ? 256 : slots_.size() * 2, 0);
        size_t mask = slots.size() - 1;
        for (size_t i = 0; i < offs_.size(); i++) {
            size_t at = hash(pool_.data() + offs_[i], lens_[i]) & mask;
            while (slots[at]) at = (at + 1) & mask;
            slots[at] = (uint32_t)i + 1;
        }
        slots_.swap(slots);
    }

    std::vector<uint8_t> body_;
    std::vector<char> pool_;
    std::vector<uint32_t> offs_, lens_, slots_;
};

namespace {

enum JsTag { TAG_UNDEF = 0, TAG_ARRAY = 1, TAG_OBJECT = 2, TAG_REF = 3, TAG_HOST = 4 };

bool callable(const js::Val& v) { return v.t == js::T::Obj && (v.o->kind == js::Kind::Function || v.o->kind == js::Kind::Native); }

bool host(Writer& w, const char* kind, double a = 0, const std::string* s = nullptr) {
    w.arr(3);
    w.num(TAG_HOST);
    w.cstr(kind);
    if (s) w.cstr(s->c_str());
    else w.num(a);
    return true;
}

bool encode_host(Writer& w, js::Obj* o) {
    js::Val v = js::Val::obj(o);
    auto same = [&](const js::Val& x) { return x.t == js::T::Obj && x.o == o; };
    if (same(game_system_js())) return host(w, "system");
    if (same(game_screen_js())) return host(w, "screen");
    if (same(game_timer_js())) return host(w, "timer");
    if (same(game_temp_js())) return host(w, "temp");
    if (same(game_message_js())) return host(w, "message");
    if (same(switches_proxy())) return host(w, "switches");
    if (same(variables_proxy())) return host(w, "variables");
    js::JsObject* jo = js::as_object(v);
    if (!jo) return false;
    if (jo == &game_party()) return host(w, "party");
    if (jo == &game_troop()) return host(w, "troop");
    if (jo == &game_map()) return host(w, "map");
    if (jo == &game_player()) return host(w, "player");
    if (jo == &game_actors()) return host(w, "actors");
    if (jo == &game_player().followers_) return host(w, "followers");
    if (auto* a = dynamic_cast<GameActor*>(jo)) return host(w, "actor", a->actor_id());
    if (auto* e = dynamic_cast<GameEnemy*>(jo)) return host(w, "enemy", e->index());
    if (auto* ev = dynamic_cast<GameEvent*>(jo)) {
        if (game_map().event(ev->event_id()) == ev) return host(w, "event", ev->event_id());
        return false;
    }
    if (auto* f = dynamic_cast<GameFollower*>(jo)) {
        auto& d = game_player().followers_.data;
        for (size_t i = 0; i < d.size(); i++) {
            if (d[i].get() == f) return host(w, "follower", (double)i);
        }
        return false;
    }
    if (auto* fog = dynamic_cast<GameMapFog*>(jo)) {
        for (auto& kv : game_map().map_fogs) {
            if (kv.second.get() == fog) return host(w, "fog", 0, &kv.first);
        }
        return false;
    }
    return false;
}

js::Val decode_host(const Value& v) {
    std::string kind = v[1].string();
    const Value& a = v[2];
    if (kind == "system") return game_system_js();
    if (kind == "screen") return game_screen_js();
    if (kind == "timer") return game_timer_js();
    if (kind == "temp") return game_temp_js();
    if (kind == "message") return game_message_js();
    if (kind == "switches") return switches_proxy();
    if (kind == "variables") return variables_proxy();
    if (kind == "party") return game_party().to_js();
    if (kind == "troop") return game_troop().to_js();
    if (kind == "map") return game_map().to_js();
    if (kind == "player") return game_player().to_js();
    if (kind == "actors") return game_actors().to_js();
    if (kind == "followers") return game_player().followers_.to_js();
    if (kind == "actor") {
        GameActor* ac = game_actors().actor(a.as_int());
        return ac ? ac->to_js() : js::Val::null();
    }
    if (kind == "enemy") {
        auto m = game_troop().members();
        int i = a.as_int();
        return i >= 0 && i < (int)m.size() ? m[(size_t)i]->to_js() : js::Val::null();
    }
    if (kind == "event") {
        GameEvent* e = game_map().event(a.as_int());
        return e ? e->to_js() : js::Val::null();
    }
    if (kind == "follower") {
        GameFollower* f = game_player().followers_.follower(a.as_int());
        return f ? f->to_js() : js::Val::null();
    }
    if (kind == "fog") {
        auto it = game_map().map_fogs.find(a.string());
        return it != game_map().map_fogs.end() ? it->second->to_js() : js::Val::null();
    }
    return js::Val::null();
}

void encode_val(Ctx* ctx, Writer& w, const js::Val& v) {
    switch (v.t) {
        case js::T::Undef:
            w.arr(1);
            w.num(TAG_UNDEF);
            return;
        case js::T::Null: w.null(); return;
        case js::T::Bool: w.boolean(v.b); return;
        case js::T::Num: w.num(v.n); return;
        case js::T::Str: w.str(v.str_ref().data(), v.str_ref().size()); return;
        case js::T::Obj: break;
    }
    js::Obj* o = v.o;
    if (callable(v)) {
        w.arr(1);
        w.num(TAG_UNDEF);
        return;
    }
    if (o->kind == js::Kind::Host) {
        if (!encode_host(w, o)) w.null();
        return;
    }
    auto it = ctx->ids.find(o);
    if (it != ctx->ids.end()) {
        w.arr(2);
        w.num(TAG_REF);
        w.num(it->second);
        return;
    }
    int id = (int)ctx->ids.size();
    ctx->ids[o] = id;
    if (o->kind == js::Kind::Array) {
        w.arr(2 + o->items.size());
        w.num(TAG_ARRAY);
        w.num(id);
        for (auto& item : o->items) encode_val(ctx, w, item);
        return;
    }
    std::vector<uint32_t> keys;
    js::ordered_keys(o, keys);
    std::vector<std::pair<uint32_t, const js::Val*>> fields;
    fields.reserve(keys.size());
    for (uint32_t k : keys) {
        const js::Val* pv = o->find(k);
        if (pv && !callable(*pv)) fields.emplace_back(k, pv);
    }
    w.arr(2 + fields.size() * 2);
    w.num(TAG_OBJECT);
    w.num(id);
    for (auto& f : fields) {
        w.cstr(js::atom_name(f.first).c_str());
        encode_val(ctx, w, *f.second);
    }
}

void write_value(Writer& w, const Value& v) {
    switch (v.t) {
        case Value::Null: w.null(); return;
        case Value::Bool: w.boolean(v.b); return;
        case Value::Int:
        case Value::Num: w.num(v.num()); return;
        case Value::Str: w.str(v.str(), v.size()); return;
        case Value::Bytes: w.bytes(v.bytes, v.size()); return;
        case Value::Arr:
            w.arr(v.size());
            for (size_t i = 0; i < v.size(); i++) write_value(w, v[i]);
            return;
        case Value::Obj: {
            size_t at = w.open_obj(), n = 0;
            for (const Member& m : v.members()) {
                w.key(m.key);
                write_value(w, m.v);
                n++;
            }
            w.close_obj(at, n);
            return;
        }
    }
}

js::Val decode_val(Ctx* ctx, const Value& v) {
    switch (v.t) {
        case Value::Null: return js::Val::null();
        case Value::Bool: return js::Val::boolean(v.b);
        case Value::Int: return js::Val::num(v.i);
        case Value::Num: return js::Val::num(v.dbl());
        case Value::Str: return js::Val::str(std::string(v.s, v.n));
        case Value::Arr: break;
        default: return js::Val();
    }
    int tag = v[0].as_int();
    if (tag == TAG_UNDEF) return js::Val();
    if (tag == TAG_REF) {
        int id = v[1].as_int();
        return id >= 0 && id < (int)ctx->objs.size() ? ctx->objs[(size_t)id] : js::Val();
    }
    if (tag == TAG_HOST) return decode_host(v);
    int id = v[1].as_int();
    if ((int)ctx->objs.size() <= id) ctx->objs.resize((size_t)id + 1);
    if (tag == TAG_ARRAY) {
        js::Val arr = js::make_array();
        ctx->objs[(size_t)id] = arr;
        for (size_t i = 2; i < v.size(); i++) arr.o->items.push_back(decode_val(ctx, v[i]));
        return arr;
    }
    js::Val obj = js::make_object();
    ctx->objs[(size_t)id] = obj;
    for (size_t i = 2; i + 1 < v.size(); i += 2) obj.o->set(js::atom(v[i].string()), decode_val(ctx, v[i + 1]));
    return obj;
}

const Value* field(const Value* in, const char* k) { return in ? in->find(k) : nullptr; }

}

Ar::Ar(Writer* out, Ctx* ctx) : out_(out), ctx_(ctx) {}
Ar::Ar(const Value* in, Ctx* ctx) : in_(in), ctx_(ctx) {}

bool Ar::has(const char* k) const { return reading() ? field(in_, k) != nullptr : true; }

Writer& Ar::put(const char* k) {
    written_++;
    out_->key(k);
    return *out_;
}

void Ar::io(const char* k, int& v) {
    if (!reading()) {
        put(k).num(v);
        return;
    }
    if (const Value* f = field(in_, k)) v = f->as_int(v);
}

void Ar::io(const char* k, double& v) {
    if (!reading()) {
        put(k).num(v);
        return;
    }
    if (const Value* f = field(in_, k)) v = f->num(v);
}

void Ar::io(const char* k, bool& v) {
    if (!reading()) {
        put(k).boolean(v);
        return;
    }
    if (const Value* f = field(in_, k)) v = f->truthy();
}

void Ar::io(const char* k, uint64_t& v) {
    double d = (double)v;
    io(k, d);
    v = (uint64_t)d;
}

void Ar::io(const char* k, std::string& v) {
    if (!reading()) {
        put(k).str(v.data(), v.size());
        return;
    }
    if (const Value* f = field(in_, k)) v = f->string();
}

void Ar::io(const char* k, js::Val& v) {
    if (!reading()) {
        encode_val(ctx_, put(k), v);
        return;
    }
    if (const Value* f = field(in_, k)) v = decode_val(ctx_, *f);
    else v = js::Val();
}

void Ar::io(const char* k, js::Expando& v) {
    if (!reading()) {
        size_t n = 0;
        for (auto& p : v.props) n += !callable(p.v);
        Writer& w = put(k);
        w.arr(n * 2);
        for (auto& p : v.props) {
            if (callable(p.v)) continue;
            w.cstr(js::atom_name(p.key).c_str());
            encode_val(ctx_, w, p.v);
        }
        return;
    }
    v.clear();
    const Value* f = field(in_, k);
    if (!f) return;
    for (size_t i = 0; i + 1 < f->size(); i += 2) v.set(js::atom((*f)[i].string()), decode_val(ctx_, (*f)[i + 1]));
}

template <class T, class W, class R>
static void io_vec(Ar& ar, const char* k, std::vector<T>& v, W w, R r) {
    if (!ar.reading()) {
        Writer& out = ar.put(k);
        out.arr(v.size());
        for (auto& x : v) w(out, x);
        return;
    }
    const Value* f = field(ar.in(), k);
    if (!f) return;
    v.clear();
    for (size_t i = 0; i < f->size(); i++) v.push_back(r((*f)[i]));
}

void Ar::io(const char* k, std::vector<int>& v) {
    io_vec(*this, k, v, [](Writer& w, int x) { w.num(x); }, [](const Value& x) { return x.as_int(); });
}

void Ar::io(const char* k, std::vector<double>& v) {
    io_vec(*this, k, v, [](Writer& w, double x) { w.num(x); }, [](const Value& x) { return x.num(); });
}

void Ar::io(const char* k, std::vector<bool>& v) {
    if (!reading()) {
        Writer& w = put(k);
        w.arr(v.size());
        for (bool x : v) w.boolean(x);
        return;
    }
    const Value* f = field(in_, k);
    if (!f) return;
    v.clear();
    for (size_t i = 0; i < f->size(); i++) v.push_back((*f)[i].truthy());
}

void Ar::io(const char* k, std::vector<uint8_t>& v) {
    if (!reading()) {
        put(k).bytes(v.data(), v.size());
        return;
    }
    const Value* f = field(in_, k);
    if (!f) return;
    if (f->t == Value::Bytes) v.assign(f->bytes, f->bytes + f->n);
}

void Ar::io(const char* k, std::vector<std::string>& v) {
    io_vec(*this, k, v, [](Writer& w, const std::string& x) { w.str(x.data(), x.size()); }, [](const Value& x) { return x.string(); });
}

template <class K, class V, class WK, class WV, class RK, class RV>
static void io_map(Ar& ar, const char* k, std::map<K, V>& m, WK wk, WV wv, RK rk, RV rv) {
    if (!ar.reading()) {
        Writer& w = ar.put(k);
        w.arr(m.size() * 2);
        for (auto& kv : m) {
            wk(w, kv.first);
            wv(w, kv.second);
        }
        return;
    }
    const Value* f = field(ar.in(), k);
    if (!f) return;
    m.clear();
    for (size_t i = 0; i + 1 < f->size(); i += 2) m[rk((*f)[i])] = rv((*f)[i + 1]);
}

static void num_out(Writer& w, double d) { w.num(d); }
static void bool_out(Writer& w, bool b) { w.boolean(b); }
static void str_out(Writer& w, const std::string& s) { w.str(s.data(), s.size()); }
static int int_of(const Value& v) { return v.as_int(); }
static double num_of(const Value& v) { return v.num(); }
static std::string str_of(const Value& v) { return v.string(); }

void Ar::io(const char* k, std::map<int, double>& v) { io_map(*this, k, v, num_out, num_out, int_of, num_of); }
void Ar::io(const char* k, std::map<int, int>& v) { io_map(*this, k, v, num_out, num_out, int_of, int_of); }

void Ar::io(const char* k, std::map<int, bool>& v) {
    io_map(*this, k, v, num_out, bool_out, int_of, [](const Value& x) { return x.truthy(); });
}

void Ar::io(const char* k, std::map<int, js::Val>& v) {
    Ctx* c = ctx_;
    io_map(*this, k, v, num_out, [c](Writer& w, const js::Val& x) { encode_val(c, w, x); }, int_of, [c](const Value& x) { return decode_val(c, x); });
}

void Ar::io(const char* k, std::map<std::string, int>& v) { io_map(*this, k, v, str_out, num_out, str_of, int_of); }

void Ar::io(const char* k, std::map<std::string, bool>& v) {
    io_map(*this, k, v, str_out, bool_out, str_of, [](const Value& x) { return x.truthy(); });
}

void Ar::io(const char* k, std::map<std::string, js::Val>& v) {
    Ctx* c = ctx_;
    io_map(*this, k, v, str_out, [c](Writer& w, const js::Val& x) { encode_val(c, w, x); }, str_of, [c](const Value& x) { return decode_val(c, x); });
}

void Ar::io(const char* k, std::map<int, std::vector<int>>& v) {
    io_map(
        *this, k, v, num_out,
        [](Writer& w, const std::vector<int>& x) {
            w.arr(x.size());
            for (int i : x) w.num(i);
        },
        int_of,
        [](const Value& x) {
            std::vector<int> out;
            for (size_t i = 0; i < x.size(); i++) out.push_back(x[i].as_int());
            return out;
        });
}

void Ar::io(const char* k, std::map<int, std::pair<int, int>>& v) {
    io_map(
        *this, k, v, num_out,
        [](Writer& w, const std::pair<int, int>& x) {
            w.arr(2);
            w.num(x.first);
            w.num(x.second);
        },
        int_of, [](const Value& x) { return std::make_pair(x[0].as_int(), x[1].as_int()); });
}

void Ar::io(const char* k, double* arr, size_t n) {
    if (!reading()) {
        Writer& w = put(k);
        w.arr(n);
        for (size_t i = 0; i < n; i++) w.num(arr[i]);
        return;
    }
    const Value* f = field(in_, k);
    if (!f) return;
    for (size_t i = 0; i < n && i < f->size(); i++) arr[i] = (*f)[i].num();
}

void Ar::io(const char* k, bool* arr, size_t n) {
    if (!reading()) {
        Writer& w = put(k);
        w.arr(n);
        for (size_t i = 0; i < n; i++) w.boolean(arr[i]);
        return;
    }
    const Value* f = field(in_, k);
    if (!f) return;
    for (size_t i = 0; i < n && i < f->size(); i++) arr[i] = (*f)[i].truthy();
}

void Ar::obj(const char* k, const std::function<void(Ar&)>& f) {
    if (!reading()) {
        Writer& w = put(k);
        size_t at = w.open_obj();
        Ar sub(out_, ctx_);
        f(sub);
        w.close_obj(at, sub.written_);
        return;
    }
    const Value* v = field(in_, k);
    if (!v || !v->is_obj()) return;
    Ar sub(v, ctx_);
    f(sub);
}

size_t Ar::list(const char* k, size_t write_count, const std::function<void(Ar&, size_t)>& f) {
    if (!reading()) {
        Writer& w = put(k);
        w.arr(write_count);
        for (size_t i = 0; i < write_count; i++) {
            size_t at = w.open_obj();
            Ar sub(out_, ctx_);
            f(sub, i);
            w.close_obj(at, sub.written_);
        }
        return write_count;
    }
    const Value* v = field(in_, k);
    if (!v || !v->is_arr()) return 0;
    for (size_t i = 0; i < v->size(); i++) {
        Ar sub(&(*v)[i], ctx_);
        f(sub, i);
    }
    return v->size();
}

const Value* Ar::value(const char* k, const Value* v) {
    if (!reading()) {
        Writer& w = put(k);
        if (v) write_value(w, *v);
        else w.null();
        return v;
    }
    return field(in_, k);
}

std::shared_ptr<const void> keep_of(Ar& ar) { return ar.ctx()->doc; }

void defer(Ar& ar, std::function<void()> fn) { ar.ctx()->fixups.push_back(std::move(fn)); }

namespace {

void audio_io(Ar& ar, const char* k, AudioObj& a) {
    ar.obj(k, [&](Ar& s) {
        s.io("name", a.name);
        s.io("volume", a.volume);
        s.io("pitch", a.pitch);
        s.io("pan", a.pan);
        s.io("pos", a.pos);
        s.io("valid", a.valid);
    });
}

void item_io(Ar& ar, const char* k, GameItem& it) {
    ar.obj(k, [&](Ar& s) {
        s.io("class", it.data_class);
        s.io("id", it.item_id);
    });
}

void charref_io(Ar& ar, const char* k, CharRef& r) {
    ar.obj(k, [&](Ar& s) {
        s.io_enum("kind", r.kind);
        s.io("id", r.id);
    });
}

void battler_ref_io(Ar& ar, const char* k, BattlerRef& r) {
    ar.obj(k, [&](Ar& s) {
        s.io("actor", r.actor_id);
        s.io("enemy", r.enemy_index);
    });
}

void route_io(Ar& ar, const char* k, RoutePtr& r) {
    if (!ar.reading()) {
        if (!r) return;
        ar.obj(k, [&](Ar& s) {
            s.io("repeat", r->repeat);
            s.io("skippable", r->skippable);
            s.io("wait", r->wait);
            s.list("list", r->list.size(), [&](Ar& c, size_t i) {
                RouteCmd& cmd = r->list[i];
                c.io("code", cmd.code);
                c.value("params", cmd.params);
                c.io("num", cmd.num);
                c.io("synthetic", cmd.synthetic);
            });
        });
        return;
    }
    if (!ar.has(k)) {
        r = nullptr;
        return;
    }
    auto route = std::make_shared<MoveRoute>();
    route->keep = keep_of(ar);
    ar.obj(k, [&](Ar& s) {
        s.io("repeat", route->repeat);
        s.io("skippable", route->skippable);
        s.io("wait", route->wait);
        s.list("list", 0, [&](Ar& c, size_t) {
            RouteCmd cmd;
            c.io("code", cmd.code);
            const Value* p = c.value("params", nullptr);
            cmd.params = p && !p->is_null() ? p : nullptr;
            c.io("num", cmd.num);
            c.io("synthetic", cmd.synthetic);
            route->list.push_back(cmd);
        });
    });
    r = route;
}

void result_io(Ar& ar, GameActionResult& r) {
    ar.io("used", r.used);
    ar.io("missed", r.missed);
    ar.io("evaded", r.evaded);
    ar.io("physical", r.physical);
    ar.io("drain", r.drain);
    ar.io("critical", r.critical);
    ar.io("success", r.success);
    ar.io("hpAffected", r.hp_affected);
    ar.io("hpDamage", r.hp_damage);
    ar.io("mpDamage", r.mp_damage);
    ar.io("tpDamage", r.tp_damage);
    ar.io("addedStates", r.added_states);
    ar.io("removedStates", r.removed_states);
    ar.io("addedBuffs", r.added_buffs);
    ar.io("addedDebuffs", r.added_debuffs);
    ar.io("removedBuffs", r.removed_buffs);
    ar.io("elementStrong", r.element_strong);
    ar.io("elementWeak", r.element_weak);
    ar.io("x", r.expando);
}

void action_io(Ar& ar, GameAction& a) {
    ar.io("subjectActor", a.subject_actor_id);
    ar.io("subjectEnemy", a.subject_enemy_index);
    ar.io("forcing", a.forcing);
    item_io(ar, "item", a.item_);
    ar.io("targetIndex", a.target_index);
    ar.io("counter", a.is_counter_);
    ar.io("forceMiss", a.is_force_miss);
    ar.io("targetGroup", a.target_group);
    ar.io("hasReflection", a.has_reflection_target);
    battler_ref_io(ar, "reflection", a.reflection_target);
    ar.io("snatch", a.snatch_target_index);
    ar.io("activeChain", a.active_chain_skill);
    ar.io("x", a.expando);
}

void actions_io(Ar& ar, const char* k, std::vector<std::shared_ptr<GameAction>>& v, GameBattler* owner) {
    if (ar.reading()) {
        if (!ar.has(k)) return;
        v.clear();
    }
    ar.list(k, v.size(), [&](Ar& s, size_t i) {
        if (ar.reading()) {
            auto a = std::make_shared<GameAction>(owner);
            action_io(s, *a);
            v.push_back(a);
        } else {
            action_io(s, *v[i]);
        }
    });
}

void anims_io(Ar& ar, const char* k, std::vector<GameBattler::Anim>& v) {
    if (ar.reading()) {
        if (!ar.has(k)) return;
        v.clear();
    }
    ar.list(k, v.size(), [&](Ar& s, size_t i) {
        GameBattler::Anim a;
        if (!ar.reading()) a = v[i];
        s.io("id", a.id);
        s.io("mirror", a.mirror);
        s.io("delay", a.delay);
        if (ar.reading()) v.push_back(a);
    });
}

void battler_base_io(Ar& ar, GameBattlerBase& b) {
    ar.io("hp", b.hp_);
    ar.io("mp", b.mp_);
    ar.io("tp", b.tp_);
    ar.io("hidden", b.hidden);
    ar.io("paramPlus", b.param_plus_, 8);
    ar.io("states", b.states_);
    ar.io("stateTurns", b.state_turns);
    ar.io("buffs", b.buffs_, 8);
    ar.io("buffTurns", b.buff_turns, 8);
    ar.io("stateCounter", b.state_counter);
    ar.io("stateOrigin", b.state_origin);
    ar.io("statusRefreshRequested", b.status_refresh_requested);
    ar.io("immortalState", b.immortal_state);
    ar.io("horrorFilters", b.horror_filters);
    ar.io("activeChain", b.active_chain_enabled);
    ar.io("x", b.expando);
    if (ar.reading()) {
        b.has_dmg_capped = false;
        b.has_maximum_damage = false;
        b.has_maximum_healing = false;
        b.has_cache_buff_rate = false;
        b.cache_buff_rate.clear();
        b.has_passive_raw = false;
        b.passive_raw.clear();
        b.check_passive.clear();
        b.check_aura.clear();
        b.check_aura_state_condition.clear();
    }
}

void battler_io(Ar& ar, GameBattler& b) {
    battler_base_io(ar, b);
    actions_io(ar, "actions", b.actions, &b);
    ar.io("speed", b.speed_);
    if (ar.reading() && !b.result_) b.result_.reset(new GameActionResult());
    if (b.result_) ar.obj("result", [&](Ar& s) { result_io(s, *b.result_); });
    ar.io("actionState", b.action_state);
    ar.io("lastTargetIndex", b.last_target_index);
    anims_io(ar, "animations", b.animations);
    if (ar.reading()) b.damage_popup.clear();
    ar.list("damagePopup", b.damage_popup.size(), [&](Ar& s, size_t i) {
        if (ar.reading()) {
            b.damage_popup.emplace_back();
            result_io(s, b.damage_popup.back());
        } else {
            result_io(s, b.damage_popup[i]);
        }
    });
    ar.io("effectType", b.effect_type);
    ar.io("motionType", b.motion_type);
    ar.io("weaponImageId", b.weapon_image_id);
    ar.io("motionRefresh", b.motion_refresh);
    ar.io("selected", b.selected);
    ar.io("freeStateTurn", b.free_state_turn);
    ar.io("selfTurnCount", b.self_turn_count);
    ar.io("counters", b.counters);
    ar.io("flinched", b.flinched);
    ar.io("noEffectMessage", b.no_effect_message);
    ar.io("noStateMessage", b.no_state_message);
    ar.io("counterSkills", b.counter_skills_);
    ar.io("hasCounterSkills", b.has_counter_skills);
    actions_io(ar, "preCounterActions", b.original_pre_counter_actions, &b);
    ar.io("hasPreCounterActions", b.has_original_pre_counter_actions);
    ar.io("collapseSoundPlayed", b.collapse_sound_played);
    anims_io(ar, "animationQueue", b.animation_queue);
    if (ar.reading()) {
        b.has_group_defeat_cache = false;
        b.has_counter_total_cache = false;
    }
}

void movement_graphic_io(Ar& ar, const char* k, MovementGraphic& g) {
    ar.obj(k, [&](Ar& s) {
        s.io("name", g.name);
        s.io("index", g.index);
        s.io("set", g.set);
    });
}

void movement_graphics_io(Ar& ar, const char* k, MovementGraphics& g) {
    ar.obj(k, [&](Ar& s) {
        movement_graphic_io(s, "idle", g.idle);
        movement_graphic_io(s, "walking", g.walking);
        movement_graphic_io(s, "running", g.running);
        movement_graphic_io(s, "climbing", g.climbing);
    });
}

void actor_io(Ar& ar, GameActor& a) {
    battler_io(ar, a);
    ar.io("actorId", a.actor_id_);
    ar.io("name", a.name_);
    ar.io("nickname", a.nickname_);
    ar.io("profile", a.profile_);
    ar.io("classId", a.class_id);
    ar.io("level", a.level);
    ar.io("characterName", a.character_name_);
    ar.io("characterIndex", a.character_index_);
    ar.io("faceName", a.face_name_);
    ar.io("faceIndex", a.face_index_);
    ar.io("battlerName", a.battler_name_);
    ar.io("exp", a.exp_);
    ar.io("skills", a.skills_);
    if (ar.reading()) {
        if (ar.has("equips")) a.equips_.clear();
    }
    ar.list("equips", a.equips_.size(), [&](Ar& s, size_t i) {
        if (ar.reading()) {
            GameItem it;
            s.io("class", it.data_class);
            s.io("id", it.item_id);
            a.equips_.push_back(it);
        } else {
            s.io("class", a.equips_[i].data_class);
            s.io("id", a.equips_[i].item_id);
        }
    });
    ar.io("actionInputIndex", a.action_input_index);
    item_io(ar, "lastMenuSkill", a.last_menu_skill);
    item_io(ar, "lastBattleSkill", a.last_battle_skill);
    ar.io("lastCommandSymbol", a.last_command_symbol);
    ar.io("stateSteps", a.state_steps);
    movement_graphics_io(ar, "movementGraphics", a.character_movement_graphics);
    ar.io("equippedSkills", a.equipped_skills);
    ar.io("lockedEquipmentSlots", a.locked_equipment_slots);
    ar.io("disabledBattleCommands", a.disabled_battle_commands);
    ar.io("farawayActorId", a.faraway_actor_id);
    ar.io("useVictoryFace", a.use_victory_face);
    ar.io("battleFaceName", a.battle_face_name);
    ar.io("lockedEquippedSkills", a.locked_equipped_skills);
    ar.io("lockedSkills", a.locked_skills);
    ar.io("battleCommandsList", a.battle_commands_list);
    ar.io("playedCollapseSound", a.played_collapse_sound);
    ar.io("levelUpData", a.level_up_data);
    if (ar.reading()) {
        a.has_anchor_x = false;
        a.has_anchor_y = false;
        a.disable_dashing_cache = -1;
        if (!a.battle_commands_list.is_array()) a.init_battle_commands_list();
    }
}

void enemy_io(Ar& ar, GameEnemy& e) {
    battler_io(ar, e);
    ar.io("enemyId", e.enemy_id_);
    ar.io("letter", e.letter);
    ar.io("plural", e.plural);
    ar.io("screenX", e.screen_x_);
    ar.io("screenY", e.screen_y_);
    ar.io("hasStealables", e.has_stealables);
    if (ar.reading() && ar.has("stealables")) e.stealables.clear();
    ar.list("stealables", e.stealables.size(), [&](Ar& s, size_t i) {
        StealableItem it;
        if (!ar.reading()) it = e.stealables[i];
        s.io("type", it.type);
        s.io("id", it.id);
        s.io("rate", it.rate);
        s.io("stolen", it.is_stolen);
        s.io("drop", it.is_drop);
        if (ar.reading()) e.stealables.push_back(it);
    });
    ar.io("troopId", e.troop_id);
    ar.io("troopMemberId", e.troop_member_id);
    ar.io("hasTroopMemberId", e.has_troop_member_id);
    ar.io("stateTransforming", e.is_state_transforming);
    ar.io("hasDeathTurn", e.has_death_turn);
    ar.io("deathTurn", e.death_turn_);
    ar.io("struckCreated", e.struck_created);
    ar.io("struckSkills", e.struck_skills);
    ar.io("struckStype", e.struck_stype);
    ar.io("struckItems", e.struck_items);
    ar.io("struckStates", e.struck_states);
    ar.io("struckElements", e.struck_elements);
    ar.io("lastStruckId", e.last_struck_id);
    ar.io("lastStruckSkill", e.last_struck_skill);
    ar.io("lastStruckActor", e.last_struck_actor);
    ar.io("hasSvWeapon", e.has_sv_weapon);
    ar.io("svWeaponImageId", e.sv_weapon_image_id);
    ar.io("svAttackMotion", e.sv_attack_motion);
    ar.io("hasSvAttackAnimation", e.has_sv_attack_animation);
    ar.io("svAttackAnimationId", e.sv_attack_animation_id);
    ar.io("hasSvBattlerName", e.has_sv_battler_name);
    ar.io("svBattlerName", e.sv_battler_name);
    ar.io("svIdleMotion", e.sv_idle_motion);
}

void interpreter_ptr_io(Ar& ar, const char* k, std::unique_ptr<GameInterpreter>& p, int depth) {
    if (!ar.reading()) {
        if (p) ar.obj(k, [&](Ar& s) { interpreter_io(s, *p); });
        return;
    }
    if (!ar.has(k)) {
        p.reset();
        return;
    }
    if (!p) p.reset(new GameInterpreter(depth));
    ar.obj(k, [&](Ar& s) { interpreter_io(s, *p); });
}

void character_base_io(Ar& ar, GameCharacterBase& c) {
    ar.io("x", c.x_);
    ar.io("y", c.y_);
    ar.io("realX", c.real_x);
    ar.io("realY", c.real_y);
    ar.io("moveSpeed", c.move_speed_);
    ar.io("moveFrequency", c.move_frequency_);
    ar.io("opacity", c.opacity_);
    ar.io("blendMode", c.blend_mode_);
    ar.io("direction", c.direction_);
    ar.io("pattern", c.pattern_);
    ar.io("priorityType", c.priority_type);
    ar.io("tileId", c.tile_id_);
    ar.io("characterName", c.character_name_);
    ar.io("characterIndex", c.character_index_);
    ar.io("isObjectCharacter", c.is_object_character_);
    ar.io("walkAnime", c.walk_anime);
    ar.io("stepAnime", c.step_anime);
    ar.io("directionFix", c.direction_fix);
    ar.io("through", c.through);
    ar.io("transparent", c.transparent);
    ar.io("bushDepth", c.bush_depth_);
    ar.io("animationId", c.animation_id_);
    ar.io("balloonId", c.balloon_id_);
    ar.io("animationPlaying", c.animation_playing);
    ar.io("balloonPlaying", c.balloon_playing);
    ar.io("animationCount", c.animation_count);
    ar.io("stopCount", c.stop_count);
    ar.io("jumpCount", c.jump_count);
    ar.io("jumpPeak", c.jump_peak);
    ar.io("movementSuccess", c.movement_success);
    ar.io("reflections", c.reflections);
    ar.io("isEmf", c.is_emf);
    ar.obj("emf", [&](Ar& s) {
        s.io("frameNum", c.emf.frame_num);
        s.io("idleFrame", c.emf.idle_frame);
        s.io("pattern", c.emf.pattern);
    });
    ar.io("hasCustomFrameX", c.has_custom_frame_x);
    ar.io("hasCustomFrameY", c.has_custom_frame_y);
    ar.io("hasCustomFrameW", c.has_custom_frame_w);
    ar.io("hasCustomFrameH", c.has_custom_frame_h);
    ar.io("customFrameX", c.custom_frame_x);
    ar.io("customFrameY", c.custom_frame_y);
    ar.io("customFrameW", c.custom_frame_w);
    ar.io("customFrameH", c.custom_frame_h);
    ar.io("hasSpriteOffsets", c.has_sprite_offsets);
    ar.io("spriteOffsetX", c.sprite_offset_x);
    ar.io("spriteOffsetY", c.sprite_offset_y);
    charref_io(ar, "target", c.target);
    ar.io("hasTargetXY", c.has_target_xy);
    ar.io("targetX", c.target_x);
    ar.io("targetY", c.target_y);
    ar.io("singleFrame", c.single_frame);
    ar.io("hasBobble", c.has_bobble);
    ar.obj("bobble", [&](Ar& s) {
        s.io("base", c.bobble.base);
        s.io("speed", c.bobble.speed);
        s.io("amplitude", c.bobble.amplitude);
    });
    ar.io("hasInputIcon", c.has_input_icon);
    ar.obj("inputIcon", [&](Ar& s) {
        s.io("input", c.input_icon.input);
        s.io("key", c.input_icon.key);
        s.io("gamepad", c.input_icon.gamepad);
    });
    ar.io("spinCount", c.spin_count);
    ar.io("spinDelay", c.spin_delay);
    ar.io("spinDelayCount", c.spin_delay_count);
    ar.io("hasShadow", c.has_shadow);
    ar.obj("shadow", [&](Ar& s) {
        s.io("opacity", c.shadow.opacity);
        s.io("angle", c.shadow.angle);
        s.io("x", c.shadow.x);
        s.io("y", c.shadow.y);
        s.io("scaleY", c.shadow.scale_y);
    });
    if (!ar.reading()) {
        if (c.custom_frame_animation) {
            CustomFrameAnimation& cf = *c.custom_frame_animation;
            ar.obj("customFrameAnimation", [&](Ar& s) {
                s.io("index", cf.index);
                s.io("count", cf.count);
                s.io("repeat", cf.repeat);
                s.list("list", cf.list.size(), [&](Ar& e, size_t i) {
                    FrameAnimStep& st = cf.list[i];
                    e.io("name", st.name);
                    e.io("index", st.index);
                    e.io("direction", st.direction);
                    e.io("frame", st.frame);
                    e.io("duration", st.duration);
                    audio_io(e, "se", st.se);
                });
            });
        }
    } else if (ar.has("customFrameAnimation")) {
        c.custom_frame_animation.reset(new CustomFrameAnimation());
        CustomFrameAnimation& cf = *c.custom_frame_animation;
        ar.obj("customFrameAnimation", [&](Ar& s) {
            s.io("index", cf.index);
            s.io("count", cf.count);
            s.io("repeat", cf.repeat);
            s.list("list", 0, [&](Ar& e, size_t) {
                FrameAnimStep st;
                e.io("name", st.name);
                e.io("index", st.index);
                e.io("direction", st.direction);
                e.io("frame", st.frame);
                e.io("duration", st.duration);
                audio_io(e, "se", st.se);
                cf.list.push_back(st);
            });
        });
    } else {
        c.custom_frame_animation.reset();
    }
    ar.io("disableFootprints", c.disable_footprints);
    ar.io("footprintX", c.footprint_x);
    ar.io("footprintY", c.footprint_y);
    ar.io("lastDirection", c.last_direction);
    ar.io("horrorFilters", c.horror_filters);
    ar.io("stepBehaviorSet", c.step_behavior_set);
    ar.io("stepAnimationBehavior", c.step_animation_behavior);
    ar.io("hasHitbox", c.has_hitbox);
    ar.io("hitboxUp", c.hitbox_up);
    ar.io("hitboxLeft", c.hitbox_left);
    ar.io("hitboxRight", c.hitbox_right);
    ar.io("hitboxDown", c.hitbox_down);
    ar.io("patternMoveRouteLocked", c.pattern_move_route_locked);
    ar.io("gstop", c.gstop);
    ar.io("alwaysUpdateMovementChecked", c.always_update_movement_checked);
    ar.io("alwaysUpdateMovement", c.always_update_movement);
    ar.io("footCanPlay", c.foot_can_play);
    ar.io("eventconImg", c.eventcon_img);
    ar.io("eventconIndex", c.eventcon_index);
    ar.io("eventconPlaying", c.eventcon_playing);
    ar.io("hasEventcon", c.has_eventcon);
    ar.io("eventconIndexDefined", c.eventcon_index_defined);
    ar.obj("tremble", [&](Ar& s) {
        s.io("active", c.tremble.active);
        s.io("hasPower", c.tremble.has_power);
        s.io("power", c.tremble.power);
        s.io("speed", c.tremble.speed);
        s.io("hasStop", c.tremble.has_stop);
        s.io("stopCycle", c.tremble.stop_cycle);
    });
    if (ar.reading() && c.tremble.active) {
        bool has_power = c.tremble.has_power;
        double power = c.tremble.power, speed = c.tremble.speed, cycle = c.tremble.has_stop ? c.tremble.stop_cycle : 0;
        c.tremble = TrembleInfo();
        if (has_power && power != 0) c.start_tremble(power, speed, cycle);
    }
    ar.io("x_", c.expando);
    if (ar.reading()) c.sprite_height_cache = -1;
}

void character_io(Ar& ar, GameCharacter& c) {
    character_base_io(ar, c);
    ar.io("moveRouteForcing", c.move_route_forcing);
    route_io(ar, "moveRoute", c.move_route);
    ar.io("moveRouteIndex", c.move_route_index);
    route_io(ar, "originalMoveRoute", c.original_move_route);
    ar.io("originalMoveRouteIndex", c.original_move_route_index);
    ar.io("waitCount", c.wait_count);
    ar.io("detecting", c.detecting);
    ar.io("hasSearchXY", c.has_search_xy);
    ar.io("searchX", c.search_x);
    ar.io("searchY", c.search_y);
    ar.io("detectPause", c.detect_pause);
    ar.io("detectPauseTime", c.detect_pause_time);
    ar.io("origMoveFrequency", c.orig_move_frequency);
    ar.io("origMoveSpeed", c.orig_move_speed);
    ar.io("origX", c.orig_x);
    ar.io("origY", c.orig_y);
    ar.io("origDirection", c.orig_direction);
    ar.io("hasOrigDirection", c.has_orig_direction);
    ar.io("detectBalloon", c.detect_balloon);
    ar.io("searchTurnIndex", c.search_turn_index);
    ar.io("blockLos", c.block_los);
    ar.io("dTargetX", c.d_target_x);
    ar.io("dTargetY", c.d_target_y);
    ar.io("repeats", c.repeats);
    ar.io("repeatStartIndex", c.repeat_start_index);
}

void follower_io(Ar& ar, GameFollower& f) {
    character_io(ar, f);
    ar.io("memberIndex", f.member_index);
    ar.io("fcEventId", f.fc_event_id);
    ar.io("fcEventLock", f.fc_event_lock);
    ar.io("fcPrelockDirection", f.fc_prelock_direction);
    ar.io("fcRetainAttrib", f.fc_retain_attrib);
    ar.io("hasMovementGraphics", f.has_character_movement_graphics);
    if (!ar.reading()) {
        if (f.character_movement_graphics) movement_graphics_io(ar, "movementGraphics", *f.character_movement_graphics);
        if (f.graphics_data) {
            ar.obj("graphicsData", [&](Ar& s) {
                s.io("id", f.graphics_data->id);
                s.io("name", f.graphics_data->name);
                s.io("index", f.graphics_data->index);
            });
        }
        return;
    }
    if (ar.has("movementGraphics")) {
        f.character_movement_graphics.reset(new MovementGraphics());
        movement_graphics_io(ar, "movementGraphics", *f.character_movement_graphics);
    } else {
        f.character_movement_graphics.reset();
    }
    if (ar.has("graphicsData")) {
        f.graphics_data.reset(new ExtraGraphics());
        ar.obj("graphicsData", [&](Ar& s) {
            s.io("id", f.graphics_data->id);
            s.io("name", f.graphics_data->name);
            s.io("index", f.graphics_data->index);
        });
    } else {
        f.graphics_data.reset();
    }
}

void vehicle_io(Ar& ar, GameVehicle& v) {
    character_io(ar, v);
    ar.io("type", v.type);
    ar.io("mapId", v.map_id);
    ar.io("altitude", v.altitude);
    ar.io("driving", v.driving);
    audio_io(ar, "bgm", v.bgm);
}

void player_io(Ar& ar, GamePlayer& p) {
    character_io(ar, p);
    ar.io("vehicleType", p.vehicle_type);
    ar.io("vehicleGettingOn", p.vehicle_getting_on);
    ar.io("vehicleGettingOff", p.vehicle_getting_off);
    ar.io("dashing", p.dashing);
    ar.io("needsMapReload", p.needs_map_reload);
    ar.io("transferring", p.transferring);
    ar.io("newMapId", p.new_map_id);
    ar.io("newX", p.new_x);
    ar.io("newY", p.new_y);
    ar.io("newDirection", p.new_direction);
    ar.io("fadeType", p.fade_type);
    ar.io("encounterCount", p.encounter_count);
    ar.io("blockedMovementInputs", p.blocked_movement_inputs);
    ar.obj("ambience", [&](Ar& s) {
        s.io("name", p.ambience.name);
        s.io("x", p.ambience.x);
        s.io("y", p.ambience.y);
        s.io("opacity", p.ambience.opacity);
        s.io("useCustomPosition", p.ambience.use_custom_position);
    });
    ar.io("footstepDisabled", p.footstep_disabled);
    ar.io("footstepSound", p.footstep_sound);
    GameFollowers& fl = p.followers_;
    ar.obj("followers", [&](Ar& s) {
        s.io("visible", fl.visible);
        s.io("gathering", fl.gathering);
        s.io("x", fl.expando);
        if (s.reading()) {
            if (!s.has("data")) return;
            fl.data.clear();
        }
        s.list("data", fl.data.size(), [&](Ar& e, size_t i) {
            if (s.reading()) {
                int mi = 0;
                e.io("memberIndex", mi);
                fl.data.emplace_back(new GameFollower(mi));
                follower_io(e, *fl.data.back());
            } else {
                follower_io(e, *fl.data[i]);
            }
        });
    });
}

void spawn_data_io(Ar& ar, SpawnData& d) {
    ar.io("baseMapId", d.base_map_id);
    ar.io("spawnId", d.spawn_id);
    ar.io("mapId", d.map_id);
    ar.io("eventId", d.event_id);
    ar.io("x", d.x);
    ar.io("y", d.y);
    ar.io("preserved", d.preserved);
}

void event_io(Ar& ar, GameEvent& e) {
    character_io(ar, e);
    ar.io("mapId", e.map_id);
    ar.io("eventId", e.event_id_);
    ar.io("moveType", e.move_type);
    ar.io("trigger", e.trigger);
    ar.io("triggerNull", e.trigger_null);
    ar.io("starting", e.starting);
    ar.io("erased", e.erased);
    ar.io("pageIndex", e.page_index);
    ar.io("originalPattern", e.original_pattern);
    ar.io("originalDirection", e.original_direction);
    ar.io("prelockDirection", e.prelock_direction);
    ar.io("locked", e.locked);
    ar.io("fcFollowerId", e.fc_follower_id);
    ar.io("copiedEvent", e.copied_event);
    ar.io("copiedMapId", e.copied_map_id);
    ar.io("copiedEventId", e.copied_event_id);
    ar.io("allTouchTrigger", e.all_touch_trigger);
    ar.io("morphed", e.morphed);
    ar.io("morphMapId", e.morph_map_id);
    ar.io("morphEventId", e.morph_event_id);
    ar.io("spawned", e.spawned);
    ar.io("spawnId", e.spawn_id);
    ar.io("spawnMapId", e.spawn_map_id);
    ar.io("spawnEventId", e.spawn_event_id);
    ar.io("spawnPreserved", e.spawn_preserved);
    ar.obj("spawnData", [&](Ar& s) { spawn_data_io(s, e.spawn_data); });
    ar.io("initialAutoTriggerBypass", e.initial_auto_trigger_bypass);
    ar.io("activationDist", e.activation_dist);
    ar.io("activationType", e.activation_type);
    ar.io("alertBalloon", e.alert_balloon);
    ar.io("alertCommonEvent", e.alert_common_event);
    ar.io("alertLock", e.alert_lock);
    ar.io("alertPlayer", e.alert_player);
    ar.io("alertSound", e.alert_sound);
    ar.io("alertSoundVol", e.alert_sound_vol);
    ar.io("alertSoundPitch", e.alert_sound_pitch);
    ar.io("alertSoundPan", e.alert_sound_pan);
    ar.io("alertTimer", e.alert_timer);
    ar.io("chasePlayer", e.chase_player);
    ar.io("chaseRange", e.chase_range);
    ar.io("chaseSpeed", e.chase_speed);
    ar.io("defaultSpeed", e.default_speed);
    ar.io("fleePlayer", e.flee_player);
    ar.io("fleeRange", e.flee_range);
    ar.io("fleeSpeed", e.flee_speed);
    ar.io("seePlayer", e.see_player);
    ar.io("sightLock", e.sight_lock);
    ar.io("returnAfter", e.return_after);
    ar.io("returnWait", e.return_wait);
    ar.io("returnPhase", e.return_phase);
    ar.io("returnFrames", e.return_frames);
    ar.io("staggerCount", e.stagger_count);
    ar.io("startLocationX", e.start_location_x);
    ar.io("startLocationY", e.start_location_y);
    ar.io("startLocationDir", e.start_location_dir);
    if (!ar.reading()) {
        interpreter_ptr_io(ar, "interpreter", e.interpreter, 0);
        return;
    }
    if (ar.has("interpreter")) {
        const Value* iv = ar.in()->find("interpreter");
        Ctx* ctx = ar.ctx();
        GameEvent* ep = &e;
        defer(ar, [ep, iv, ctx]() {
            if (!ep->interpreter) ep->interpreter.reset(new GameInterpreter());
            Ar sub(iv, ctx);
            interpreter_io(sub, *ep->interpreter);
        });
    } else {
        e.interpreter.reset();
    }
}

std::shared_ptr<GameEvent> make_spawned(Ar& ar) {
    SpawnData sd;
    ar.obj("spawnData", [&](Ar& s) { spawn_data_io(s, sd); });
    auto ev = std::make_shared<GameEvent>(sd.map_id, sd.event_id, &sd);
    event_io(ar, *ev);
    return ev;
}

void easing_io(Ar& ar, const char* k, EasingSet& e) {
    ar.obj(k, [&](Ar& s) {
        s.io("general", e.general);
        s.io("x", e.x);
        s.io("y", e.y);
        s.io("scaleX", e.scale_x);
        s.io("scaleY", e.scale_y);
        s.io("opacity", e.opacity);
        s.io("red", e.red);
        s.io("green", e.green);
        s.io("blue", e.blue);
        s.io("gray", e.gray);
        s.io("any", e.any);
    });
}

void frame_rect_io(Ar& ar, FrameRect& r) {
    ar.io("x", r.x);
    ar.io("y", r.y);
    ar.io("w", r.w);
    ar.io("h", r.h);
}

void picture_io(Ar& ar, GamePicture& p) {
    ar.io("name", p.name);
    ar.io("origin", p.origin);
    ar.io("x", p.x);
    ar.io("y", p.y);
    ar.io("scaleX", p.scale_x);
    ar.io("scaleY", p.scale_y);
    ar.io("opacity", p.opacity);
    ar.io("blendMode", p.blend_mode);
    ar.io("targetX", p.target_x);
    ar.io("targetY", p.target_y);
    ar.io("targetScaleX", p.target_scale_x);
    ar.io("targetScaleY", p.target_scale_y);
    ar.io("targetOpacity", p.target_opacity);
    ar.io("duration", p.duration);
    ar.io("hasTone", p.has_tone);
    ar.io("tone", p.tone, 4);
    ar.io("toneTarget", p.tone_target, 4);
    ar.io("toneDuration", p.tone_duration);
    ar.io("angle", p.angle_);
    ar.io("rotationSpeed", p.rotation_speed);
    ar.io("maxFrame", p.max_frame);
    ar.io("frame", p.frame);
    ar.io("maxAngle", p.max_angle);
    ar.io("pinnedToMap", p.pinned_to_map);
    ar.io("followShake", p.follow_shake);
    ar.io("useParallax", p.use_parallax);
    ar.obj("parallaxRect", [&](Ar& s) { frame_rect_io(s, p.parallax_rect); });
    if (!ar.reading()) {
        if (p.frame_data) {
            FrameData& fd = *p.frame_data;
            ar.obj("frameData", [&](Ar& s) {
                s.list("frames", fd.frames.size(), [&](Ar& e, size_t i) { frame_rect_io(e, fd.frames[i]); });
                s.io("frameIndex", fd.frame_index);
                s.io("refresh", fd.refresh);
                s.io("hasAnim", fd.has_anim);
                s.list("animFrames", fd.anim.frames.size(), [&](Ar& e, size_t i) {
                    e.io("frame", fd.anim.frames[i].frame);
                    e.io("delay", fd.anim.frames[i].delay);
                    e.io("maxDelay", fd.anim.frames[i].max_delay);
                });
                s.io("animLoops", fd.anim.loops);
                s.io("animIndex", fd.anim.index);
            });
        }
    } else if (ar.has("frameData")) {
        p.frame_data.reset(new FrameData());
        FrameData& fd = *p.frame_data;
        ar.obj("frameData", [&](Ar& s) {
            s.list("frames", 0, [&](Ar& e, size_t) {
                FrameRect r;
                frame_rect_io(e, r);
                fd.frames.push_back(r);
            });
            s.io("frameIndex", fd.frame_index);
            s.io("refresh", fd.refresh);
            s.io("hasAnim", fd.has_anim);
            s.list("animFrames", 0, [&](Ar& e, size_t) {
                FrameAnim::F f;
                e.io("frame", f.frame);
                e.io("delay", f.delay);
                e.io("maxDelay", f.max_delay);
                fd.anim.frames.push_back(f);
            });
            s.io("animLoops", fd.anim.loops);
            s.io("animIndex", fd.anim.index);
        });
    } else {
        p.frame_data.reset();
    }
    ar.io("moveCurves", p.move_curves);
    easing_io(ar, "moveEase", p.move_ease);
    ar.io("moveFrom", p.move_from, 5);
    ar.io("moveActive", p.move_active, 5);
    ar.io("moveTo", p.move_to, 5);
    ar.io("moveTotal", p.move_total);
    ar.io("toneCurves", p.tone_curves);
    easing_io(ar, "toneEase", p.tone_ease);
    ar.io("toneFrom", p.tone_from, 4);
    ar.io("toneTo", p.tone_to, 4);
    ar.io("toneActive", p.tone_active, 4);
    ar.io("toneTotal", p.tone_total);
    ar.io("horrorFilters", p.horror_filters);
    ar.io("x_", p.expando);
}

void screen_io(Ar& ar, GameScreen& s) {
    ar.io("brightness", s.brightness);
    ar.io("fadeOutDuration", s.fade_out_duration);
    ar.io("fadeInDuration", s.fade_in_duration);
    ar.io("tone", s.tone, 4);
    ar.io("toneTarget", s.tone_target, 4);
    ar.io("toneDuration", s.tone_duration);
    ar.io("flashColor", s.flash_color, 4);
    ar.io("flashDuration", s.flash_duration);
    ar.io("flashWait", s.flash_wait);
    ar.io("shakePower", s.shake_power);
    ar.io("shakeSpeed", s.shake_speed);
    ar.io("shakeDuration", s.shake_duration);
    ar.io("shakeDirection", s.shake_direction);
    ar.io("shake", s.shake);
    ar.io("shakeVectorX", s.shake_vector_x);
    ar.io("shakeVectorY", s.shake_vector_y);
    ar.io("zoomX", s.zoom_x);
    ar.io("zoomY", s.zoom_y);
    ar.io("zoomScale", s.zoom_scale);
    ar.io("zoomScaleTarget", s.zoom_scale_target);
    ar.io("zoomDuration", s.zoom_duration);
    ar.io("cameraDuration", s.camera_duration);
    ar.io("weatherType", s.weather_type);
    ar.io("weatherPower", s.weather_power);
    ar.io("weatherPowerTarget", s.weather_power_target);
    ar.io("weatherDuration", s.weather_duration);
    ar.io("weatherPowerTargetText", s.weather_power_target_text);
    ar.io("stormThunderCount", s.storm_thunder_count);
    if (ar.reading() && ar.has("pictures")) s.pictures.clear();
    ar.list("pictures", s.pictures.size(), [&](Ar& e, size_t i) {
        if (ar.reading()) {
            bool present = false;
            e.io("present", present);
            if (!present) {
                s.pictures.emplace_back();
                return;
            }
            s.pictures.emplace_back(new GamePicture());
            picture_io(e, *s.pictures.back());
        } else {
            bool present = (bool)s.pictures[i];
            e.io("present", present);
            if (present) picture_io(e, *s.pictures[i]);
        }
    });
    ar.io("blurStrength", s.blur_strength);
    ar.io("blurDelta", s.blur_delta);
    ar.io("blurDuration", s.blur_duration);
    ar.io("zblurStrength", s.zblur_strength);
    ar.io("zblurDelta", s.zblur_delta);
    ar.io("zblurNradius", s.zblur_nradius);
    ar.io("zblurOriginX", s.zblur_origin_x);
    ar.io("zblurOriginY", s.zblur_origin_y);
    ar.io("zblurDuration", s.zblur_duration);
    ar.io("glowPower", s.glow_power);
    ar.io("glowDelta", s.glow_delta);
    ar.io("glowSize", s.glow_size);
    ar.io("glowThreshold", s.glow_threshold);
    ar.io("glowDuration", s.glow_duration);
    ar.io("waveAmp", s.wave_amp);
    ar.io("waveDelta", s.wave_delta);
    ar.io("waveLength", s.wave_length);
    ar.io("waveOriginX", s.wave_origin_x);
    ar.io("waveOriginY", s.wave_origin_y);
    ar.io("waveDuration", s.wave_duration);
    ar.io("waveInitialDuration", s.wave_initial_duration);
    ar.io("distPower", s.dist_power);
    ar.io("distDensity", s.dist_density);
    ar.io("distOffset", s.dist_offset);
    ar.io("distDelta", s.dist_delta, 2);
    ar.io("distDuration", s.dist_duration);
    ar.io("distInitialDuration", s.dist_initial_duration);
    ar.io("horrorFilters", s.horror_filters);
    ar.io("x_", s.expando);
}

void system_io(Ar& ar, GameSystem& s) {
    ar.io("saveEnabled", s.save_enabled);
    ar.io("menuEnabled", s.menu_enabled);
    ar.io("encounterEnabled", s.encounter_enabled);
    ar.io("formationEnabled", s.formation_enabled);
    ar.io("battleCount", s.battle_count);
    ar.io("winCount", s.win_count);
    ar.io("escapeCount", s.escape_count);
    ar.io("saveCount", s.save_count);
    ar.io("versionId", s.version_id);
    ar.io("framesOnSave", s.frames_on_save);
    audio_io(ar, "bgmOnSave", s.bgm_on_save);
    audio_io(ar, "bgsOnSave", s.bgs_on_save);
    ar.io("hasWindowTone", s.has_window_tone);
    ar.io("windowTone", s.window_tone_, 4);
    audio_io(ar, "battleBgm", s.battle_bgm_);
    audio_io(ar, "victoryMe", s.victory_me_);
    audio_io(ar, "defeatMe", s.defeat_me_);
    audio_io(ar, "savedBgm", s.saved_bgm);
    audio_io(ar, "walkingBgm", s.walking_bgm);
    ar.io("battleSystem", s.battle_system);
    ar.io("actSeqBypassDamageCap", s.act_seq_bypass_damage_cap);
    ar.io("hasActSeqDamageCap", s.has_act_seq_damage_cap);
    ar.io("actSeqDamageCap", s.act_seq_damage_cap);
    ar.io("damageRate", s.damage_rate);
    ar.io("flatDamage", s.flat_damage);
    ar.io("healRate", s.heal_rate);
    ar.io("flatHeal", s.flat_heal);
    ar.io("defaultDamageCap", s.default_damage_cap);
    ar.io("hasNewDamageCap", s.has_new_damage_cap);
    ar.io("hasNewHealingCap", s.has_new_healing_cap);
    ar.io("newDamageCap", s.new_damage_cap);
    ar.io("newHealingCap", s.new_healing_cap);
    ar.io("critMult", s.crit_mult);
    ar.io("flatCrit", s.flat_crit);
    ar.io("forceCritical", s.force_critical);
    ar.io("forceNoCritical", s.force_no_critical);
    ar.io("showEventMiniLabel", s.show_event_mini_label);
    ar.io("disableDashing", s.disable_dashing);
    ar.io("footstepsEnabled", s.footsteps_enabled);
    if (ar.reading() && ar.has("savedEventLocations")) s.saved_event_locations.clear();
    std::vector<std::string> loc_keys;
    for (auto& kv : s.saved_event_locations) loc_keys.push_back(kv.first);
    ar.list("savedEventLocations", loc_keys.size(), [&](Ar& e, size_t i) {
        std::string key;
        SavedLocation l;
        if (!ar.reading()) {
            key = loc_keys[i];
            l = s.saved_event_locations[key];
        }
        e.io("key", key);
        e.io("x", l.x);
        e.io("y", l.y);
        e.io("d", l.d);
        if (ar.reading()) s.saved_event_locations[key] = l;
    });
    ar.io("wordWrap", s.word_wrap_);
    ar.io("fastForward", s.fast_forward);
    ar.io("msgFontName", s.msg_font_name);
    ar.io("msgFontSize", s.msg_font_size);
    ar.io("msgFontOutline", s.msg_font_outline);
    ar.io("messageRowsExpr", s.message_rows_expr);
    ar.io("messageWidthExpr", s.message_width_expr);
    ar.io("msgSoundEnable", s.msg_sound_enable);
    ar.io("msgSoundName", s.msg_sound_name);
    ar.io("msgSoundVol", s.msg_sound_vol);
    ar.io("msgSoundPitch", s.msg_sound_pitch);
    ar.io("msgSoundPitchVar", s.msg_sound_pitch_var);
    ar.io("msgSoundPan", s.msg_sound_pan);
    ar.io("msgSoundPanVar", s.msg_sound_pan_var);
    ar.io("msgSoundInterval", s.msg_sound_interval);
    ar.io("msgChoiceMax", s.msg_choice_max);
    ar.io("hideChoices", s.hide_choices);
    ar.io("disableChoices", s.disable_choices);
    ar.io("choiceShowInitialized", s.choice_show_initialized);
    ar.io("choiceEnableInitialized", s.choice_enable_initialized);
    ar.io("msgPositionX", s.msg_position_x);
    ar.io("msgPositionY", s.msg_position_y);
    ar.io("msgAnchorX", s.msg_anchor_x);
    ar.io("msgAnchorY", s.msg_anchor_y);
    ar.io("originalMagicalSeed", s.original_magical_seed);
    ar.io("magicalSeed", s.magical_seed);
    ar.io("playtimeMs", s.playtime_ms);
    if (ar.reading() && ar.has("preservedMorphs")) s.preserved_morphs.clear();
    std::vector<std::string> morph_keys;
    for (auto& kv : s.preserved_morphs) morph_keys.push_back(kv.first);
    ar.list("preservedMorphs", morph_keys.size(), [&](Ar& e, size_t i) {
        std::string key;
        MorphTarget m;
        if (!ar.reading()) {
            key = morph_keys[i];
            m = s.preserved_morphs[key];
        }
        e.io("key", key);
        e.io("mapId", m.map_id);
        e.io("eventId", m.event_id);
        if (ar.reading()) s.preserved_morphs[key] = m;
    });
    if (ar.reading() && ar.has("spawnedEvents")) s.map_spawned_events.clear();
    std::vector<int> spawn_maps;
    for (auto& kv : s.map_spawned_events) spawn_maps.push_back(kv.first);
    ar.list("spawnedEvents", spawn_maps.size(), [&](Ar& e, size_t i) {
        int map_id = ar.reading() ? 0 : spawn_maps[i];
        e.io("mapId", map_id);
        auto& vec = s.map_spawned_events[map_id];
        if (ar.reading()) vec.clear();
        e.list("events", vec.size(), [&](Ar& ev, size_t j) {
            if (ar.reading()) {
                bool present = false;
                ev.io("present", present);
                vec.push_back(present ? make_spawned(ev) : nullptr);
            } else {
                bool present = (bool)vec[j];
                ev.io("present", present);
                if (present) event_io(ev, *vec[j]);
            }
        });
    });
    ar.io("hasGachaBook", s.has_gacha_book);
    ar.io("hasGacha", s.has_gacha);
    for (int t = 0; t < 3; t++) {
        std::string kb = "gachaBook" + std::to_string(t);
        std::string kg = "gacha" + std::to_string(t);
        ar.io(kb.c_str(), s.gacha_book[t]);
        ar.io(kg.c_str(), s.gacha[t]);
    }
    ar.io("x_", s.expando);
}

void temp_io(Ar& ar, GameTemp& t) {
    ar.io("commonEventId", t.common_event_id);
    ar.io("destinationX", t.destination_x);
    ar.io("destinationY", t.destination_y);
    ar.io("destinationValid", t.destination_valid);
    ar.io("stopMapEvents", t.stop_map_events);
    ar.io("stopMapPlayer", t.stop_map_player);
    ar.io("moveAllowPlayerCollision", t.move_allow_player_collision);
    ar.io("loadLocationDirection", t.load_location_direction);
    ar.io("bypassLoadLocation", t.bypass_load_location);
    ar.io("deathStateClear", t.death_state_clear);
    ar.io("recoverAllClear", t.recover_all_clear);
    ar.io("customLeaveEffectEval", t.custom_leave_effect_eval);
    ar.io("isGatheringAuraData", t.is_gathering_aura_data);
    ar.io("checkAllAliveMembers", t.check_all_alive_members);
    ar.io("taunt", t.taunt);
    ar.io("tauntMode", t.taunt_mode);
    ar.io("forceActionQueue", t.force_action_queue_list);
    ar.io("x_", t.expando);
    if (ar.reading()) t.taunt_action = nullptr;
}

void timer_io(Ar& ar, GameTimer& t) {
    ar.io("frames", t.frames);
    ar.io("working", t.working);
    ar.io("paused", t.paused);
    ar.io("direction", t.direction);
    ar.io("x_", t.expando);
}

void message_io(Ar& ar, GameMessage& m) {
    ar.io("texts", m.texts);
    ar.io("choices", m.choices);
    ar.io("faceName", m.face_name);
    ar.io("faceIndex", m.face_index);
    ar.io("background", m.background);
    ar.io("positionType", m.position_type);
    ar.io("choiceDefaultType", m.choice_default_type);
    ar.io("choiceCancelType", m.choice_cancel_type);
    ar.io("choiceBackground", m.choice_background);
    ar.io("choicePositionType", m.choice_position_type);
    ar.io("numInputVariableId", m.num_input_variable_id);
    ar.io("numInputMaxDigits", m.num_input_max_digits);
    ar.io("itemChoiceVariableId", m.item_choice_variable_id);
    ar.io("itemChoiceItypeId", m.item_choice_itype_id);
    ar.io("scrollMode", m.scroll_mode);
    ar.io("scrollSpeed", m.scroll_speed);
    ar.io("scrollNoFast", m.scroll_no_fast);
    if (ar.reading() && ar.has("extraFaces")) m.extra_faces.clear();
    ar.list("extraFaces", m.extra_faces.size(), [&](Ar& e, size_t i) {
        ExtraFace f;
        if (!ar.reading()) f = m.extra_faces[i];
        e.io("faceName", f.face_name);
        e.io("faceIndex", f.face_index);
        e.io("color", f.color);
        e.io("hasColor", f.has_color);
        if (ar.reading()) m.extra_faces.push_back(f);
    });
    ar.io("faceBackgroundColor", m.face_background_color);
    ar.io("hasFaceBackgroundColor", m.has_face_background_color);
    ar.io("choiceLabels", m.choice_labels);
    ar.io("x_", m.expando);
    if (ar.reading()) m.choice_callback = nullptr;
}

void switches_io(Ar& ar) { ar.io("switches", game_switches().raw()); }

void variables_io(Ar& ar) {
    auto& raw = game_variables().raw();
    if (!ar.reading()) {
        std::map<int, js::Val> m;
        for (size_t i = 0; i < raw.size(); i++) {
            if (!raw[i].is_undef()) m[(int)i] = raw[i];
        }
        ar.io("variables", m);
        return;
    }
    std::map<int, js::Val> m;
    ar.io("variables", m);
    raw.clear();
    for (auto& kv : m) {
        if (kv.first < 0) continue;
        if ((int)raw.size() <= kv.first) raw.resize((size_t)kv.first + 1);
        raw[(size_t)kv.first] = kv.second;
    }
}

void self_switches_io(Ar& ar) { ar.io("selfSwitches", game_self_switches().raw()); }
void self_variables_io(Ar& ar) { ar.io("selfVariables", game_self_variables().raw()); }

void event_data_io(Ar& ar) {
    auto& raw = game_event_data().raw();
    if (ar.reading() && ar.has("eventData")) raw.clear();
    std::vector<std::string> cats;
    for (auto& kv : raw) cats.push_back(kv.first);
    ar.list("eventData", cats.size(), [&](Ar& e, size_t i) {
        std::string cat = ar.reading() ? std::string() : cats[i];
        e.io("category", cat);
        e.io("values", raw[cat]);
    });
}

void actors_io(Ar& ar) {
    GameActors& actors = game_actors();
    std::vector<int> ids;
    for (auto& kv : actors.data) {
        if (kv.second) ids.push_back(kv.first);
    }
    if (ar.reading()) {
        if (!ar.has("actors")) return;
        std::vector<int> seen;
        ar.list("actors", 0, [&](Ar& e, size_t) {
            int id = 0;
            e.io("actorId", id);
            auto& slot = actors.data[id];
            if (!slot) slot.reset(new GameActor(id));
            actor_io(e, *slot);
            seen.push_back(id);
        });
        ar.io("actorsX", actors.expando);
        for (auto it = actors.data.begin(); it != actors.data.end();) {
            if (std::find(seen.begin(), seen.end(), it->first) == seen.end()) it = actors.data.erase(it);
            else ++it;
        }
        return;
    }
    ar.list("actors", ids.size(), [&](Ar& e, size_t i) { actor_io(e, *actors.data[ids[i]]); });
    ar.io("actorsX", actors.expando);
}

void counts_io(Ar& ar, const char* k, ItemCounts& c) { ar.io(k, c); }

void party_io(Ar& ar) {
    GameParty& p = game_party();
    ar.obj("party", [&](Ar& s) {
        s.io("inBattle", p.in_battle_);
        s.io("gold", p.gold_);
        s.io("steps", p.steps_);
        item_io(s, "lastItem", p.last_item);
        s.io("menuActorId", p.menu_actor_id);
        s.io("targetActorId", p.target_actor_id);
        s.io("actors", p.actors);
        counts_io(s, "items", p.items_);
        counts_io(s, "weapons", p.weapons_);
        counts_io(s, "armors", p.armors_);
        s.io("activeWorld", p.active_world);
        s.io("scannedEnemyList", p.scanned_enemy_list);
        s.io("blackLetters", p.black_letters);
        s.io("blackLetterIndex", p.black_letter_index);
        s.obj("lastItemData", [&](Ar& e) {
            e.io("id", p.last_item_data.id);
            e.io("type", p.last_item_data.type);
            e.io("amount", p.last_item_data.amount);
        });
        s.io("stressEnergyCount", p.stress_energy_count);
        if (s.reading() && s.has("worldItems")) p.world_items.clear();
        std::vector<int> worlds;
        for (auto& kv : p.world_items) worlds.push_back(kv.first);
        s.list("worldItems", worlds.size(), [&](Ar& e, size_t i) {
            int w = s.reading() ? 0 : worlds[i];
            e.io("world", w);
            WorldItems& wi = p.world_items[w];
            counts_io(e, "items", wi.items);
            counts_io(e, "weapons", wi.weapons);
            counts_io(e, "armors", wi.armors);
        });
        s.io("lastWorldItemIndex", p.last_world_item_index);
        s.io("questList", p.quest_list);
        s.io("questStandByMessage", p.quest_stand_by_message);
        s.io("albumPictures", p.album_pictures);
        s.io("albumPicturePositions", p.album_picture_positions);
        s.io("defeatedEnemies", p.defeated_enemies);
        s.io("x_", p.expando);
    });
}

void troop_io(Ar& ar) {
    GameTroop& t = game_troop();
    ar.obj("troop", [&](Ar& s) {
        s.io("inBattle", t.in_battle_);
        s.io("troopId", t.troop_id);
        s.io("eventFlags", t.event_flags);
        auto enemies_io = [&](const char* k, std::vector<std::shared_ptr<GameEnemy>>& v) {
            if (s.reading()) {
                if (!s.has(k)) return;
                v.clear();
            }
            s.list(k, v.size(), [&](Ar& e, size_t i) {
                if (s.reading()) {
                    int id = 0;
                    double x = 0, y = 0;
                    e.io("enemyId", id);
                    e.io("screenX", x);
                    e.io("screenY", y);
                    auto en = std::make_shared<GameEnemy>(id, x, y);
                    enemy_io(e, *en);
                    v.push_back(en);
                } else {
                    enemy_io(e, *v[i]);
                }
            });
        };
        enemies_io("enemies", t.enemies);
        enemies_io("newEnemies", t.new_enemies);
        s.io("turnCount", t.turn_count_);
        s.io("namesCount", t.names_count);
        s.io("ignorePages", t.ignore_pages);
        s.io("aiKnownElementRates", t.ai_known_element_rates);
        s.io("x_", t.expando);
        if (!s.reading()) {
            interpreter_ptr_io(s, "interpreter", t.interpreter, 0);
        } else if (s.has("interpreter")) {
            const Value* iv = s.in()->find("interpreter");
            Ctx* ctx = s.ctx();
            defer(s, [&t, iv, ctx]() {
                if (!t.interpreter) t.interpreter.reset(new GameInterpreter());
                Ar sub(iv, ctx);
                interpreter_io(sub, *t.interpreter);
            });
        }
    });
}

void fog_io(Ar& ar, GameMapFog& f) {
    ar.io("id", f.id);
    ar.io("name", f.name);
    ar.io("opacity", f.opacity);
    ar.io("blendMode", f.blend_mode);
    ar.io("scaleX", f.scale_x);
    ar.io("scaleY", f.scale_y);
    ar.io("moveX", f.move_x);
    ar.io("moveY", f.move_y);
    ar.io("width", f.width);
    ar.io("height", f.height);
    ar.io("mapBind", f.map_bind);
    ar.io("visible", f.visible);
    ar.io("deactivateOnInvisible", f.deactivate_on_invisible);
    ar.io("active", f.active);
    ar.io("priority", f.priority);
    ar.io("originX", f.origin_x);
    ar.io("originY", f.origin_y);
    ar.io("movePX", f.move_px);
    ar.io("movePY", f.move_py);
    ar.io("originJs", f.origin_js);
    ar.io("moveJs", f.move_js);
    ar.io("x_", f.expando);
}

void filter_io(Ar& ar, FilterController& f) {
    ar.io("eraseFlag", f.erase_flag);
    ar.io("eraseAfterMoveFlag", f.erase_after_move_flag);
    if (ar.reading() && ar.has("moveQueue")) f.move_queue.clear();
    ar.list("moveQueue", f.move_queue.size(), [&](Ar& e, size_t i) {
        FilterController::Queued q;
        if (!ar.reading()) q = f.move_queue[i];
        e.io("target", q.target);
        e.io("has", q.has);
        e.io("duration", q.duration);
        if (ar.reading()) f.move_queue.push_back(q);
    });
    ar.io("time", f.time);
    ar.io("addiTime", f.addi_time);
    ar.io("currentParams", f.current_params);
    ar.io("targetParams", f.target_params);
    ar.io("enabled", f.enabled);
    ar.io("duration", f.duration);
}

void footprint_io(Ar& ar, GameFootprint& f) {
    ar.io("x", f.x);
    ar.io("y", f.y);
    ar.io("rows", f.rows);
    ar.io("columns", f.columns);
    ar.io("graphicsName", f.graphics_name);
    ar.io("index", f.index);
    ar.io("frame", f.frame);
    ar.io("direction", f.direction);
    ar.io("lastDirection", f.last_direction);
    ar.io("duration", f.duration);
    ar.io("opacity", f.opacity);
    ar.io("fadeSpeed", f.fade_speed);
    ar.io("removed", f.removed);
}

void map_io(Ar& ar) {
    GameMap& m = game_map();
    ar.obj("map", [&](Ar& s) {
        int map_id = m.map_id_;
        s.io("mapId", map_id);
        if (s.reading()) m.restore_begin(map_id);
        s.io("tilesetId", m.tileset_id);
        if (!s.reading()) {
            s.list("events", m.events_.size(), [&](Ar& e, size_t i) {
                std::shared_ptr<GameEvent>& ev = m.events_[i];
                int kind = 0;
                int spawn_index = -1;
                if (ev) {
                    auto it = std::find(m.spawned_events.begin(), m.spawned_events.end(), ev);
                    if (it != m.spawned_events.end()) {
                        kind = 2;
                        spawn_index = (int)(it - m.spawned_events.begin());
                    } else {
                        kind = 1;
                    }
                }
                e.io("kind", kind);
                if (kind == 2) e.io("spawnIndex", spawn_index);
                if (kind == 1) event_io(e, *ev);
            });
        } else {
            m.spawned_events = game_system().get_map_spawned_event_data(map_id);
            std::vector<std::pair<std::shared_ptr<GameEvent>, const Value*>> pending;
            m.events_.clear();
            Ctx* ctx = s.ctx();
            s.list("events", 0, [&](Ar& e, size_t i) {
                int kind = 0;
                e.io("kind", kind);
                if (m.events_.size() <= i) m.events_.resize(i + 1);
                if (kind == 1) {
                    int eid = (int)i;
                    auto ev = std::make_shared<GameEvent>(map_id, eid);
                    m.events_[i] = ev;
                    pending.push_back({ev, e.in()});
                } else if (kind == 2) {
                    int si = -1;
                    e.io("spawnIndex", si);
                    if (si >= 0 && si < (int)m.spawned_events.size()) m.events_[i] = m.spawned_events[(size_t)si];
                }
            });
            for (auto& p : pending) {
                Ar sub(p.second, ctx);
                event_io(sub, *p.first);
            }
        }
        if (s.reading() && s.has("commonEvents")) {
            s.list("commonEvents", 0, [&](Ar& e, size_t) {
                int id = 0;
                e.io("id", id);
                for (auto& ce : m.common_events) {
                    if (ce->common_event_id != id) continue;
                    const Value* iv = e.in()->find("interpreter");
                    if (!iv) break;
                    GameCommonEvent* cep = ce.get();
                    Ctx* ctx = e.ctx();
                    defer(e, [cep, iv, ctx]() {
                        if (!cep->interpreter) cep->interpreter.reset(new GameInterpreter());
                        Ar sub(iv, ctx);
                        interpreter_io(sub, *cep->interpreter);
                    });
                    break;
                }
            });
        } else if (!s.reading()) {
            s.list("commonEvents", m.common_events.size(), [&](Ar& e, size_t i) {
                e.io("id", m.common_events[i]->common_event_id);
                interpreter_ptr_io(e, "interpreter", m.common_events[i]->interpreter, 0);
            });
        }
        s.list("vehicles", m.vehicles_.size(), [&](Ar& e, size_t i) {
            if (i < m.vehicles_.size()) vehicle_io(e, *m.vehicles_[i]);
        });
        s.io("displayX", m.display_x_);
        s.io("displayY", m.display_y_);
        s.io("nameDisplay", m.name_display);
        s.io("scrollDirection", m.scroll_direction);
        s.io("scrollRest", m.scroll_rest);
        s.io("scrollSpeed", m.scroll_speed);
        s.io("parallaxName", m.parallax_name_);
        s.io("parallaxZero", m.parallax_zero);
        s.io("parallaxLoopX", m.parallax_loop_x);
        s.io("parallaxLoopY", m.parallax_loop_y);
        s.io("parallaxSx", m.parallax_sx);
        s.io("parallaxSy", m.parallax_sy);
        s.io("parallaxX", m.parallax_x);
        s.io("parallaxY", m.parallax_y);
        s.io("hasBattleback1", m.has_battleback1);
        s.io("hasBattleback2", m.has_battleback2);
        s.io("battleback1Name", m.battleback1_name_);
        s.io("battleback2Name", m.battleback2_name_);
        s.io("needsRefresh", m.needs_refresh);
        s.io("regionMoveDirections", m.region_move_directions);
        if (s.reading() && s.has("fogs")) m.map_fogs.clear();
        std::vector<std::string> fog_keys;
        for (auto& kv : m.map_fogs) fog_keys.push_back(kv.first);
        s.list("fogs", fog_keys.size(), [&](Ar& e, size_t i) {
            std::string key = s.reading() ? std::string() : fog_keys[i];
            e.io("key", key);
            if (s.reading()) {
                auto fog = std::make_shared<GameMapFog>();
                fog_io(e, *fog);
                m.map_fogs[key] = fog;
            } else {
                fog_io(e, *m.map_fogs[key]);
            }
        });
        if (s.reading() && s.has("filters")) {
            m.filter_con.clear();
            m.filter_con_order.clear();
        }
        s.list("filters", m.filter_con_order.size(), [&](Ar& e, size_t i) {
            std::string key, type, id;
            int target_obj = 0, char_map_id = 0;
            bool char_is_number = false, has_char_map = false;
            double character = 0;
            FilterController* f = nullptr;
            if (!s.reading()) {
                key = m.filter_con_order[i];
                f = m.filter_con[key].get();
                type = f->filter_type;
                id = f->id;
                target_obj = f->target_obj;
                char_is_number = f->char_is_number;
                character = f->character;
                has_char_map = f->has_char_map;
                char_map_id = f->char_map_id;
            }
            e.io("key", key);
            e.io("type", type);
            e.io("id", id);
            e.io("targetObj", target_obj);
            e.io("charIsNumber", char_is_number);
            e.io("character", character);
            e.io("hasCharMap", has_char_map);
            e.io("charMapId", char_map_id);
            if (s.reading()) {
                auto fc = std::make_unique<FilterController>(type, id, target_obj, char_is_number, character, has_char_map, char_map_id);
                f = fc.get();
                m.filter_con[key] = std::move(fc);
                m.filter_con_order.push_back(key);
            }
            if (f) filter_io(e, *f);
        });
        if (s.reading() && s.has("footprints")) m.footprints_.clear();
        s.list("footprints", m.footprints_.size(), [&](Ar& e, size_t i) {
            if (s.reading()) {
                auto fp = std::make_shared<GameFootprint>();
                footprint_io(e, *fp);
                m.footprints_.push_back(fp);
            } else {
                footprint_io(e, *m.footprints_[i]);
            }
        });
        s.io("recordedMorphedEvents", m.recorded_morphed_events);
        s.io("recordedMorphedEventData", m.recorded_morphed_event_data);
        s.io("hasScrollTarget", m.has_scroll_target);
        s.io("scrollStartX", m.scroll_start_x);
        s.io("scrollStartY", m.scroll_start_y);
        s.io("scrollTargetX", m.scroll_target_x);
        s.io("scrollTargetY", m.scroll_target_y);
        s.io("scrollDuration", m.scroll_duration);
        s.io("scrollingTime", m.scrolling_time);
        s.io("hasOverrideDisplayName", m.has_override_display_name);
        s.io("overrideDisplayName", m.override_display_name);
        charref_io(s, "camTarget", m.cam_target);
        s.io("camNorm", m.cam_norm);
        s.io("camPoint", m.cam_point);
        s.io("camPx", m.cam_px);
        s.io("camPy", m.cam_py);
        s.io("camSpeed", m.cam_speed);
        s.io("zoomX", m.zoom_x);
        s.io("zoomY", m.zoom_y);
        s.io("hasSavedCam", m.has_saved_cam);
        s.io("savedCamArgs", m.saved_cam_args);
        s.io("x_", m.expando);
        if (!s.reading()) {
            interpreter_ptr_io(s, "interpreter", m.interpreter, 0);
        } else if (s.has("interpreter")) {
            const Value* iv = s.in()->find("interpreter");
            Ctx* ctx = s.ctx();
            defer(s, [&m, iv, ctx]() {
                if (!m.interpreter) m.interpreter.reset(new GameInterpreter());
                Ar sub(iv, ctx);
                interpreter_io(sub, *m.interpreter);
            });
        }
        if (s.reading()) defer(s, [&m]() { m.restore_end(); });
    });
}

enum ListSource { LIST_NONE = 0, LIST_COMMON = 1, LIST_EVENT = 2, LIST_TROOP = 3, LIST_EMBED = 4 };

void list_io(Ar& ar, GameInterpreter& in) {
    if (!ar.reading()) {
        int src = LIST_NONE;
        int a = 0, b = 0;
        const Value* list = in.list_;
        if (list) {
            const Value& ces = data::common_events();
            for (size_t i = 0; i < ces.size() && src == LIST_NONE; i++) {
                if (ces[i].is_obj() && &ces[i]["list"] == list) {
                    src = LIST_COMMON;
                    a = (int)i;
                }
            }
            if (src == LIST_NONE) {
                for (GameEvent* e : game_map().events()) {
                    const Value& pages = e->event()["pages"];
                    for (size_t p = 0; p < pages.size(); p++) {
                        if (&pages[p]["list"] == list) {
                            src = LIST_EVENT;
                            a = e->event_id();
                            b = (int)p;
                            break;
                        }
                    }
                    if (src != LIST_NONE) break;
                }
            }
            if (src == LIST_NONE && game_troop().troop_id > 0) {
                const Value& pages = game_troop().troop()["pages"];
                for (size_t p = 0; p < pages.size(); p++) {
                    if (&pages[p]["list"] == list) {
                        src = LIST_TROOP;
                        a = game_troop().troop_id;
                        b = (int)p;
                        break;
                    }
                }
            }
            if (src == LIST_NONE) src = LIST_EMBED;
        }
        ar.io("listSource", src);
        ar.io("listA", a);
        ar.io("listB", b);
        if (src == LIST_EMBED) ar.value("list", list);
        int params_index = -1;
        if (list && in.params_) {
            for (size_t i = 0; i < list->size(); i++) {
                if (&(*list)[i]["parameters"] == in.params_) {
                    params_index = (int)i;
                    break;
                }
            }
        }
        ar.io("paramsIndex", params_index);
        return;
    }
    int src = LIST_NONE, a = 0, b = 0, params_index = -1;
    ar.io("listSource", src);
    ar.io("listA", a);
    ar.io("listB", b);
    ar.io("paramsIndex", params_index);
    in.list_ = nullptr;
    in.keep_.reset();
    if (src == LIST_COMMON) {
        const Value& ce = data::common_events()[a];
        if (ce.is_obj()) {
            in.list_ = &ce["list"];
            in.keep_ = data::keep();
        }
    } else if (src == LIST_EVENT) {
        GameEvent* e = game_map().event(a);
        if (e) {
            const Value& pages = e->event()["pages"];
            if (b >= 0 && b < (int)pages.size()) {
                in.list_ = &pages[b]["list"];
                in.keep_ = e->event_keep();
            }
        }
    } else if (src == LIST_TROOP) {
        const Value& troop = data::troops()[a];
        if (troop.is_obj() && b >= 0 && b < (int)troop["pages"].size()) {
            in.list_ = &troop["pages"][b]["list"];
            in.keep_ = data::keep();
        }
    } else if (src == LIST_EMBED) {
        const Value* l = ar.value("list", nullptr);
        if (l && l->is_arr()) {
            in.list_ = l;
            in.keep_ = keep_of(ar);
        }
    }
    in.params_ = nullptr;
    if (in.list_ && params_index >= 0 && params_index < (int)in.list_->size()) in.params_ = &(*in.list_)[params_index]["parameters"];
}

void apply_fixups(Ctx& ctx) {
    for (size_t i = 0; i < ctx.fixups.size(); i++) {
        auto fn = ctx.fixups[i];
        fn();
    }
    ctx.fixups.clear();
}

std::vector<uint8_t> serialize(const std::function<void(Ar&)>& f) {
    Ctx ctx;
    Writer w;
    size_t at = w.open_obj();
    Ar ar(&w, &ctx);
    f(ar);
    w.close_obj(at, ar.written());
    return w.finish();
}

voidpf z_alloc(voidpf, uInt items, uInt size) {
    try {
        return ::operator new((size_t)items * size);
    } catch (const std::bad_alloc&) {
        return Z_NULL;
    }
}

void z_free(voidpf, voidpf p) { ::operator delete(p); }

std::vector<uint8_t> pack(const std::vector<uint8_t>& raw) {
    uLong cap = compressBound((uLong)raw.size());
    std::vector<uint8_t> out(12 + cap);
    std::memcpy(out.data(), "OMSV", 4);
    uint32_t version = 1;
    uint32_t size = (uint32_t)raw.size();
    bytes::put_le32(out.data() + 4, version);
    bytes::put_le32(out.data() + 8, size);
    z_stream z{};
    z.zalloc = z_alloc;
    z.zfree = z_free;
    if (deflateInit(&z, 6) != Z_OK) return {};
    z.next_in = const_cast<Bytef*>(raw.data());
    z.avail_in = (uInt)raw.size();
    z.next_out = out.data() + 12;
    z.avail_out = (uInt)cap;
    int r = deflate(&z, Z_FINISH);
    uLong len = z.total_out;
    deflateEnd(&z);
    if (r != Z_STREAM_END) return {};
    out.resize(12 + len);
    return out;
}

bool unpack(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    if (in.size() < 12 || std::memcmp(in.data(), "OMSV", 4) != 0) return false;
    uint32_t size = bytes::le32(in.data() + 8);
    out.resize(size);
    uLongf dst = size;
    if (uncompress(out.data(), &dst, in.data() + 12, (uLong)(in.size() - 12)) != Z_OK || dst != size) return false;
    return true;
}

std::shared_ptr<Document> parse(std::vector<uint8_t>&& raw) {
    auto doc = std::make_shared<Document>();
    if (!doc->load_memory(std::move(raw))) return nullptr;
    return doc;
}

std::string slot_file(int slot) { return fs::save_path("file" + std::to_string(slot) + ".omsav"); }
std::string global_file() { return fs::save_path("global.omsav"); }

int g_last_accessed = 1;

struct GlobalInfo {
    std::map<int, SlotInfo> slots;
};

bool load_global(GlobalInfo& gi) {
    std::vector<uint8_t> file, raw;
    if (!fs::read_file(global_file(), file) || !unpack(file, raw)) return false;
    auto doc = parse(std::move(raw));
    if (!doc) return false;
    Ctx ctx;
    ctx.doc = doc;
    Ar ar(&doc->root(), &ctx);
    ar.list("slots", 0, [&](Ar& e, size_t) {
        int id = 0;
        SlotInfo si;
        si.exists = true;
        e.io("id", id);
        e.io("actorName", si.actor_name);
        e.io("actorLevel", si.actor_level);
        e.io("faceName", si.face_name);
        e.io("faceIndex", si.face_index);
        e.io("chapter", si.chapter);
        e.io("location", si.location);
        e.io("playtime", si.playtime);
        e.io("timestamp", si.timestamp);
        e.io("worldIndex", si.world_index);
        gi.slots[id] = si;
    });
    return true;
}

bool save_global(GlobalInfo& gi) {
    std::vector<int> ids;
    for (auto& kv : gi.slots) ids.push_back(kv.first);
    std::vector<uint8_t> packed = pack(serialize([&](Ar& ar) {
        ar.list("slots", ids.size(), [&](Ar& e, size_t i) {
            int id = ids[i];
            SlotInfo& si = gi.slots[id];
            e.io("id", id);
            e.io("actorName", si.actor_name);
            e.io("actorLevel", si.actor_level);
            e.io("faceName", si.face_name);
            e.io("faceIndex", si.face_index);
            e.io("chapter", si.chapter);
            e.io("location", si.location);
            e.io("playtime", si.playtime);
            e.io("timestamp", si.timestamp);
            e.io("worldIndex", si.world_index);
        });
    }));
    return !packed.empty() && fs::write_file(global_file(), packed.data(), packed.size());
}

SlotInfo make_slot_info() {
    SlotInfo si;
    si.exists = true;
    GameActor* leader = game_party().leader();
    if (leader) {
        si.actor_name = leader->name();
        si.actor_level = leader->level;
        si.face_name = leader->face_save_load();
        si.face_index = leader->face_save_load_index();
    }
    si.chapter = game_variables().value(23);
    si.location = game_map().display_name();
    si.playtime = game_system().playtime_text();
    si.timestamp = (double)std::time(nullptr) * 1000.0;
    si.world_index = game_variables().value(22);
    return si;
}

void write_contents(Ar& ar, bool full) {
    switches_io(ar);
    variables_io(ar);
    self_switches_io(ar);
    self_variables_io(ar);
    actors_io(ar);
    party_io(ar);
    ar.obj("system", [](Ar& s) { system_io(s, game_system()); });
    ar.obj("timer", [](Ar& s) { timer_io(s, game_timer()); });
    if (full) {
        event_data_io(ar);
        ar.obj("screen", [](Ar& s) { screen_io(s, game_screen()); });
        map_io(ar);
        ar.obj("player", [](Ar& s) { player_io(s, game_player()); });
    } else {
        ar.obj("temp", [](Ar& s) { temp_io(s, game_temp()); });
        ar.obj("message", [](Ar& s) { message_io(s, game_message()); });
        troop_io(ar);
    }
}

}

void interpreter_io(Ar& ar, GameInterpreter& in) {
    ar.io("depth", in.depth_);
    ar.io("mapId", in.map_id_);
    ar.io("eventId", in.event_id_);
    list_io(ar, in);
    ar.io("index", in.index_);
    ar.io("waitCount", in.wait_count);
    ar.io("waitMode", in.wait_mode);
    ar.io("comments", in.comments);
    charref_io(ar, "character", in.character_);
    ar.io("branch", in.branch);
    ar.io("indent", in.indent_);
    ar.io("frameCount", in.frame_count_);
    ar.io("freezeChecker", in.freeze_checker);
    ar.io("requestedImageCount", in.requested_image_count);
    std::vector<std::string> conds;
    for (auto& w : in.wait_conditions) conds.push_back(w.source);
    ar.io("waitConditions", conds);
    if (ar.reading()) {
        in.wait_conditions.clear();
        for (auto& c : conds) {
            GameInterpreter::WaitCondition w;
            w.source = c;
            w.program = js::compile_cached(c);
            in.wait_conditions.push_back(w);
        }
    }
    ar.io("mfpsWait", in.mfps_wait);
    ar.io("languageChoices", in.language_choices);
    ar.io("languageChoiceLabels", in.language_choice_labels);
    ar.io("lastAnimatingPicture", in.last_animating_picture);
    ar.io("callEventRunning", in.call_event_running);
    ar.io("callEventEventId", in.call_event_event_id);
    ar.io("callEventPageId", in.call_event_page_id);
    ar.io("callEventMapId", in.call_event_map_id);
    ar.io("x_", in.expando);
    if (!ar.reading()) {
        if (in.child) ar.obj("child", [&](Ar& s) { interpreter_io(s, *in.child); });
        return;
    }
    if (ar.has("child")) {
        if (!in.child) in.child.reset(new GameInterpreter(in.depth_ + 1));
        ar.obj("child", [&](Ar& s) { interpreter_io(s, *in.child); });
    } else {
        in.child.reset();
    }
}

std::vector<uint8_t> snapshot_for_retry() {
    try {
        return pack(serialize([](Ar& ar) { write_contents(ar, false); }));
    } catch (const std::bad_alloc&) {
        plat::log("battle retry snapshot skipped: out of memory");
        return {};
    }
}

void restore_retry_snapshot(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> raw;
    if (data.empty() || !unpack(data, raw)) return;
    auto doc = parse(std::move(raw));
    if (!doc) return;
    Ctx ctx;
    ctx.doc = doc;
    Ar ar(&doc->root(), &ctx);
    write_contents(ar, false);
    apply_fixups(ctx);
}

bool save_game(int slot) {
    std::vector<uint8_t> packed;
    try {
        packed = pack(serialize([](Ar& ar) { write_contents(ar, true); }));
    } catch (const std::bad_alloc&) {
        plat::log("save %d failed: out of memory", slot);
        return false;
    }
    if (packed.empty()) return false;
    std::string path = slot_file(slot);
    std::string backup = path + ".bak";
    std::vector<uint8_t> old;
    bool had_old = fs::read_file(path, old);
    if (had_old) fs::write_file(backup, old.data(), old.size());
    if (!fs::write_file(path, packed.data(), packed.size())) {
        if (had_old) fs::write_file(path, old.data(), old.size());
        return false;
    }
    if (had_old) fs::remove_file(backup);
    g_last_accessed = slot;
    GlobalInfo gi;
    load_global(gi);
    gi.slots[slot] = make_slot_info();
    save_global(gi);
    return true;
}

bool load_game(int slot) {
    std::vector<uint8_t> file, raw;
    if (!fs::read_file(slot_file(slot), file) || !unpack(file, raw)) return false;
    auto doc = parse(std::move(raw));
    if (!doc) return false;
    create_game_objects();
    Ctx ctx;
    ctx.doc = doc;
    Ar ar(&doc->root(), &ctx);
    write_contents(ar, true);
    apply_fixups(ctx);
    g_last_accessed = slot;
    game_system().on_after_load();
    after_load_hook();
    {
        game_system().init_message_font_settings();
        const Value& actors = data::actors();
        for (size_t id = 1; id < actors.size(); id++) {
            if (!actors[id].is_obj()) continue;
            GameActor* a = game_actors().actor((int)id);
            if (!a) continue;
            std::string name = a->name();
            std::string converted = convert_name_to_language("actor", name, data::language());
            if (converted != name) a->set_name(converted);
        }
    }
    int ce = params().uce_load_game;
    if (ce > 0) timers::set_timeout(2000, [ce]() { game_temp().reserve_common_event(ce); });
    return true;
}

bool exists(int slot) { return fs::exists(slot_file(slot)); }

bool remove(int slot) {
    bool ok = fs::remove_file(slot_file(slot));
    GlobalInfo gi;
    if (load_global(gi) && gi.slots.erase(slot)) save_global(gi);
    return ok;
}

bool info(int slot, SlotInfo& out) {
    if (!exists(slot)) return false;
    GlobalInfo gi;
    if (!load_global(gi)) return false;
    auto it = gi.slots.find(slot);
    if (it == gi.slots.end()) return false;
    out = it->second;
    out.actor_name = convert_name_to_language("actor", out.actor_name, data::language());
    if (!is_text_in_current_language(out.actor_name)) out.actor_name = data::lang_entry("XX_BLUE.Omori_Name_Input")["defaultname"].string();
    if (out.chapter.is_str()) out.chapter = js::Val::str(convert_name_to_language("chapter", out.chapter.to_str(), data::language()));
    out.location = convert_name_to_language("location", out.location, data::language());
    return true;
}

bool any_exists() {
    for (int i = 1; i <= kMaxSlots; i++) {
        if (exists(i)) return true;
    }
    return false;
}

int latest_slot() {
    GlobalInfo gi;
    load_global(gi);
    int best = 1;
    double stamp = 0;
    for (auto& kv : gi.slots) {
        if (!exists(kv.first) || kv.first == kCrashSlot) continue;
        if (kv.second.timestamp > stamp) {
            stamp = kv.second.timestamp;
            best = kv.first;
        }
    }
    return best;
}

int last_accessed() { return g_last_accessed; }
void set_last_accessed(int slot) { g_last_accessed = slot; }

void select_for_new_game() {
    GlobalInfo gi;
    load_global(gi);
    g_last_accessed = 1;
    int count = 0;
    for (auto& kv : gi.slots) {
        if (kv.first != kCrashSlot && kv.first <= kMaxSlots) count = std::max(count, kv.first);
    }
    if (count < kMaxSlots) {
        g_last_accessed = count + 1;
        return;
    }
    double stamp = 1e300;
    for (int i = 1; i <= kMaxSlots; i++) {
        auto it = gi.slots.find(i);
        if (it == gi.slots.end()) {
            g_last_accessed = i;
            return;
        }
        if (it->second.timestamp < stamp) {
            stamp = it->second.timestamp;
            g_last_accessed = i;
        }
    }
}

bool write_text(const std::string& name, const std::string& text) { return fs::write_file(fs::save_path(name), text.data(), text.size()); }

bool read_text(const std::string& name, std::string& out) {
    std::vector<uint8_t> raw;
    if (!fs::read_file(fs::save_path(name), raw)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}

}
}
