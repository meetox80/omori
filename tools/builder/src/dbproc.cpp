#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "dbproc.h"
#include "js.h"
#include "jsops.h"
#include "util.h"
}

#include "core/value.h"
#include "js/compile.h"
#include "js/value.h"
#include "js/vm.h"

namespace plat {
void log(const char* fmt, ...) {
    if (!getenv("OMORI_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
double time_seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

namespace fs {
struct DataFile {};
bool read_data(const std::string&, std::vector<uint8_t>&) { return false; }
DataFile* open_data(const std::string&) { return nullptr; }
size_t data_read(DataFile*, void*, size_t) { return 0; }
int data_seek(DataFile*, int64_t, int) { return -1; }
int64_t data_tell(DataFile*) { return 0; }
void data_close(DataFile*) {}
}

namespace {

const char* PRELUDE = R"JS(
var window = this;
var DataManager = {};
DataManager._databaseFiles = [];
DataManager.checkError = function() {};
DataManager.isDatabaseLoaded = function() { this.checkError(); return true; };
DataManager.isSkill = function(item) { return !!item && $dataSkills.contains(item); };
DataManager.isItem = function(item) { return !!item && $dataItems.contains(item); };
DataManager.isWeapon = function(item) { return !!item && $dataWeapons.contains(item); };
DataManager.isArmor = function(item) { return !!item && $dataArmors.contains(item); };
DataManager.isBattleTest = function() { return false; };
DataManager.isEventTest = function() { return false; };
var PluginManager = {
    _parameters: {},
    parameters: function(name) { return this._parameters[name.toLowerCase()] || {}; },
    setParameters: function(name, parameters) { this._parameters[name.toLowerCase()] = parameters; }
};
var Utils = {
    RPGMAKER_NAME: 'MV',
    RPGMAKER_VERSION: '1.6.1',
    isOptionValid: function(name) { return false; },
    isNwjs: function() { return true; },
    isMobileDevice: function() { return false; },
    isMobileSafari: function() { return false; },
    isAndroidChrome: function() { return false; },
    canReadGameFiles: function() { return true; },
    rgbToCssColor: function(r, g, b) { return 'rgb(' + Math.round(r) + ',' + Math.round(g) + ',' + Math.round(b) + ')'; }
};
var JsonEx = {
    makeDeepCopy: function(object) { return JSON.parse(JSON.stringify(object)); },
    stringify: function(object) { return JSON.stringify(object); },
    parse: function(json) { return JSON.parse(json); }
};
var $dataActors = null;
var $dataClasses = null;
var $dataSkills = null;
var $dataItems = null;
var $dataWeapons = null;
var $dataArmors = null;
var $dataEnemies = null;
var $dataTroops = null;
var $dataStates = null;
var $dataAnimations = null;
var $dataTilesets = null;
var $dataCommonEvents = null;
var $dataSystem = null;
var $dataMapInfos = null;
var $dataMap = null;
)JS";

const char* DB_KEYS[][2] = {{"actors", "$dataActors"},     {"classes", "$dataClasses"},       {"skills", "$dataSkills"},
                            {"items", "$dataItems"},       {"weapons", "$dataWeapons"},       {"armors", "$dataArmors"},
                            {"enemies", "$dataEnemies"},   {"troops", "$dataTroops"},         {"states", "$dataStates"},
                            {"animations", "$dataAnimations"}, {"tilesets", "$dataTilesets"}, {"commonEvents", "$dataCommonEvents"},
                            {"system", "$dataSystem"},     {"mapInfos", "$dataMapInfos"},     {nullptr, nullptr}};

js::Val to_val(const Node* n) {
    if (!n) return js::Val();
    switch (n->type) {
    case N_NULL: return js::Val::null();
    case N_FALSE: return js::Val::boolean(false);
    case N_TRUE: return js::Val::boolean(true);
    case N_NUM: return js::Val::num(n->num);
    case N_STR: return js::Val::str(std::string(n->str, n->len));
    case N_ARR: {
        std::vector<js::Val> items;
        items.reserve(n->count);
        for (size_t i = 0; i < n->count; i++) items.push_back(to_val(n->items[i]));
        return js::make_array(std::move(items));
    }
    case N_OBJ: {
        js::Val o = js::make_object();
        for (size_t i = 0; i < n->count; i++) o.o->set(js::atom(n->keys[i]), to_val(n->items[i]));
        return o;
    }
    default: return js::Val::null();
    }
}

struct Exporter {
    std::set<const js::Obj*> stack;
    std::vector<std::string> warnings;
    std::string path;

    Node* run(const js::Val& v, bool in_array) {
        switch (v.t) {
        case js::T::Undef: return in_array ? node_null() : nullptr;
        case js::T::Null: return node_null();
        case js::T::Bool: return node_bool(v.b);
        case js::T::Num: return std::isfinite(v.n) ? node_num(v.n) : node_null();
        case js::T::Str: return node_strn(v.s->data.data(), v.s->data.size());
        case js::T::Obj: break;
        }
        js::Obj* o = v.o;
        if (o->kind == js::Kind::Function || o->kind == js::Kind::Native) return in_array ? node_null() : nullptr;
        if (o->kind == js::Kind::Host) {
            if (o->cls && o->cls->call) return in_array ? node_null() : nullptr;
            warnings.push_back(path);
            return node_null();
        }
        if (stack.count(o)) {
            warnings.push_back(path + " (cycle)");
            return node_null();
        }
        stack.insert(o);
        Node* out;
        if (o->kind == js::Kind::Array) {
            out = node_arr();
            for (size_t i = 0; i < o->items.size(); i++) {
                std::string saved = path;
                path += "[" + std::to_string(i) + "]";
                node_push(out, run(o->items[i], true));
                path = saved;
            }
        } else {
            out = node_obj();
            std::vector<uint32_t> keys;
            js::ordered_keys(o, keys);
            for (uint32_t k : keys) {
                const js::Val* pv = o->find(k);
                if (!pv) continue;
                std::string saved = path;
                path += "." + js::atom_name(k);
                Node* c = run(*pv, false);
                path = saved;
                if (c) node_set(out, js::atom_name(k).c_str(), c);
            }
        }
        stack.erase(o);
        return out;
    }
};

js::HostClass& stub_class();

js::Val stub() {
    static js::Val s = [] {
        js::Obj* o = js::obj_new(js::Kind::Host);
        o->cls = &stub_class();
        o->ptr = nullptr;
        return js::Val::obj(o);
    }();
    return s;
}

js::HostClass& stub_class() {
    static js::HostClass hc = [] {
        js::HostClass c;
        c.name = "Stub";
        c.get = [](js::Vm&, js::Obj*, uint32_t, js::Val& out) {
            out = stub();
            return true;
        };
        c.set = [](js::Vm&, js::Obj*, uint32_t, const js::Val&) { return true; };
        c.keys = [](js::Obj*, std::vector<uint32_t>&) {};
        c.call = [](js::Vm&, const js::Val&, std::vector<js::Val>&) { return stub(); };
        return c;
    }();
    return hc;
}

struct Program {
    std::shared_ptr<Document> doc;
};

using js::compile_doc;
using js::program_doc;

struct Runner {
    js::Vm& vm = js::vm();
    js::ScopePtr scope = std::make_shared<js::Scope>();
    std::vector<std::shared_ptr<Document>> docs;
    int failed_statements = 0;

    Runner() {
        scope->function_scope = true;
        vm.set_resolver([](uint32_t, js::Val& out) {
            out = stub();
            return true;
        });
        vm.set_eval_compiler([this](const std::string& code, std::string& err) -> std::shared_ptr<const ::Value> {
            auto doc = compile_doc(code, err);
            if (!doc) return nullptr;
            docs.push_back(doc);
            return std::shared_ptr<const ::Value>(doc, &doc->root());
        });
    }

    bool exec(const std::string& src, std::string& err) {
        auto doc = compile_doc(src, err);
        if (!doc) return false;
        docs.push_back(doc);
        try {
            vm.run_in_scope(doc->root(), stub(), scope, doc);
        } catch (js::Throw& t) {
            err = js::json_stringify(t.value);
            return false;
        }
        return true;
    }

    std::string ranges_json;

    void exec_statements(const std::string& name, const std::string& src) {
        char e[256];
        Node* ranges = node_arr();
        Node* ast = js_compile_ranges(src.data(), src.size(), e, sizeof(e), ranges);
        if (ast && getenv("OMORI_DUMP_DB_IN")) {
            if (!ranges_json.empty()) ranges_json += ",";
            ranges_json += "\"" + name + "\":[";
            for (size_t i = 0; i < ranges->count; i++) {
                if (i) ranges_json += ",";
                ranges_json += "[" + std::to_string((long long)ranges->items[i]->items[0]->num) + "," + std::to_string((long long)ranges->items[i]->items[1]->num) + "]";
            }
            ranges_json += "]";
        }
        node_free(ranges);
        if (!ast) {
            log_msg("  note: plugin %s uses syntax the data step skips (%s)", name.c_str(), e);
            return;
        }
        Node* body = ast->items[1];
        for (size_t i = 0; i < body->count; i++) {
            Node* prog = node_arr();
            node_push(prog, node_num(JS_PROGRAM));
            Node* list = node_arr();
            node_push(list, body->items[i]);
            node_push(prog, list);
            auto doc = program_doc(prog);
            list->count = 0;
            node_free(prog);
            if (!doc) continue;
            docs.push_back(doc);
            try {
                vm.run_in_scope(doc->root(), stub(), scope, doc);
            } catch (js::Throw&) {
                failed_statements++;
            }
        }
        node_free(ast);
    }

    js::Val global(const char* name) {
        if (js::Val* v = scope->find(js::atom(name))) return *v;
        if (js::Val* g = vm.global(js::atom(name))) return *g;
        return js::Val();
    }

    void set(const char* name, const js::Val& v) {
        if (js::Val* s = scope->find(js::atom(name))) *s = v;
        else scope->declare(js::atom(name), v);
    }
};

struct JsExport {
    const char* cls;
    const char* method;
};

const JsExport JS_EXPORTS[] = {{"Game_Interpreter", "force*"},
                               {"Game_Interpreter", "initSingingMiniGame"},
                               {"Game_Interpreter", "wakeUpSingingMoles"},
                               {"Game_Interpreter", "updateSingingMiniGame"},
                               {"Game_Interpreter", "refreshSingingEventGraphics"},
                               {"Game_Interpreter", "updateSingingEventBGM"},
                               {"Game_Interpreter", "initializeHardwareOrganizing"},
                               {"Game_Interpreter", "placeTool"},
                               {"Game_Interpreter", "replaceTool"},
                               {"Game_Interpreter", "returnTool"},
                               {"Game_Interpreter", "getToolName"},
                               {"Game_Interpreter", "checkHardwareOrganization"},
                               {"Game_Interpreter", "initPizzaDelivery"},
                               {"Game_Interpreter", "checkImages"},
                               {"Game_Interpreter", "houseHints"},
                               {"Game_Interpreter", "generateHouse"},
                               {"Game_Interpreter", "correctHouse"},
                               {"Game_Interpreter", "checkedHouseAlready"},
                               {"Game_Interpreter", "checkingCurrentHouse"},
                               {"Game_Interpreter", "keyItemCull"},
                               {"Game_System", "randomizeArray"},
                               {"Game_System", "playerHasCD"},
                               {"Game_System", "addJukeboxItem"},
                               {"Game_System", "shuffleDishList"},
                               {"Game_System", "shuffleSortingObjects"},
                               {"Game_System", "calculateSortingPercentage"},
                               {"Game_System", "distributeItems"},
                               {"Window_BattleLog", "makeCustomActionText"},
                               {nullptr, nullptr}};

bool export_wanted(const char* cls, const char* method) {
    for (int i = 0; JS_EXPORTS[i].cls; i++) {
        if (strcmp(JS_EXPORTS[i].cls, cls)) continue;
        const char* m = JS_EXPORTS[i].method;
        size_t n = strlen(m);
        if (n && m[n - 1] == '*') {
            if (!strncmp(m, method, n - 1)) return true;
        } else if (!strcmp(m, method)) {
            return true;
        }
    }
    return false;
}

bool is_op(const Node* n, int op) { return n && n->type == N_ARR && n->count > 0 && node_numv(n->items[0], -1) == op; }

void collect_js_methods(const std::string& src, Node* out) {
    char e[256];
    Node* ast = js_compile(src.data(), src.size(), e, sizeof(e));
    if (!ast) return;
    Node* body = ast->count > 1 ? ast->items[1] : nullptr;
    for (size_t i = 0; body && i < body->count; i++) {
        Node* st = body->items[i];
        if (!is_op(st, JS_EXPR) || st->count < 2) continue;
        Node* as = st->items[1];
        if (!is_op(as, JS_ASSIGN) || as->count < 4 || node_numv(as->items[1], -1) != 0) continue;
        Node* target = as->items[2];
        Node* fn = as->items[3];
        if (!is_op(target, JS_GET) || !is_op(fn, JS_FUNC) || target->count < 3) continue;
        Node* proto = target->items[1];
        const char* method = node_strv(target->items[2]);
        if (!method || !is_op(proto, JS_GET) || proto->count < 3) continue;
        const char* pname = node_strv(proto->items[2]);
        Node* cls = proto->items[1];
        if (!pname || strcmp(pname, "prototype") || !is_op(cls, JS_NAME) || cls->count < 2) continue;
        const char* cname = node_strv(cls->items[1]);
        if (!cname || !export_wanted(cname, method)) continue;
        Node* bucket = node_get(out, cname);
        if (!bucket) {
            bucket = node_obj();
            node_set(out, cname, bucket);
        }
        Node* prog = node_arr();
        node_push(prog, node_num(JS_PROGRAM));
        Node* list = node_arr();
        Node* ex = node_arr();
        node_push(ex, node_num(JS_EXPR));
        node_push(ex, node_clone(fn));
        node_push(list, ex);
        node_push(prog, list);
        node_set(bucket, method, prog);
    }
    node_free(ast);
}

std::string read_plugin(Game* g, const char* name) {
    char* path = str_fmt("js/plugins/%s.js", name);
    Buf b = {0};
    int ok = game_read(g, path, &b);
    free(path);
    std::string s;
    if (ok) s.assign((const char*)b.data, b.len);
    buf_free(&b);
    return s;
}

}

extern "C" int dbproc_run(Game* g, Node* root, Node* plugin_list, char** err) {
    *err = nullptr;
    Runner r;
    std::string e;
    if (!r.exec(PRELUDE, e)) {
        *err = str_fmt("data step prelude failed: %s", e.c_str());
        return 0;
    }
    js::Val plugins = to_val(plugin_list);
    r.set("$plugins", plugins);
    js::Val pm = r.global("PluginManager");
    if (plugins.is_array()) {
        for (auto& p : plugins.o->items) {
            if (!p.is_obj()) continue;
            js::Val st = r.vm.get(p, js::atom("status"));
            if (!st.truthy()) continue;
            js::Val nm = r.vm.get(p, js::atom("name"));
            std::vector<js::Val> args{nm, r.vm.get(p, js::atom("parameters"))};
            r.vm.call_method(pm, js::atom("setParameters"), args);
        }
    }
    Node* order = node_get(root, "pluginOrder");
    for (size_t i = 0; order && i < order->count; i++) {
        const char* name = node_strv(order->items[i]);
        if (!name) continue;
        std::string src = read_plugin(g, name);
        if (src.empty()) continue;
        r.exec_statements(name, src);
    }
    for (int i = 0; DB_KEYS[i][0]; i++) {
        Node* n = node_get(root, DB_KEYS[i][0]);
        if (n) r.set(DB_KEYS[i][1], to_val(n));
    }
    if (const char* dump = getenv("OMORI_DUMP_DB_IN")) {
        std::string all = "{";
        for (int i = 0; DB_KEYS[i][0]; i++) {
            if (i) all += ",";
            all += "\"" + std::string(DB_KEYS[i][0]) + "\":" + js::json_stringify(r.global(DB_KEYS[i][1]));
        }
        all += ",\"$plugins\":" + js::json_stringify(plugins) + ",\"$ranges\":{" + r.ranges_json + "}}";
        file_write(dump, all.data(), all.size());
    }
    js::Val dm = r.global("DataManager");
    try {
        std::vector<js::Val> none;
        js::Val loaded = r.vm.call_method(dm, js::atom("isDatabaseLoaded"), none);
        if (!loaded.truthy()) {
            *err = str_fmt("data step: DataManager.isDatabaseLoaded returned false");
            return 0;
        }
    } catch (js::Throw& t) {
        *err = str_fmt("data step failed: %s", js::json_stringify(t.value).c_str());
        return 0;
    }
    if (const char* dump = getenv("OMORI_DUMP_DB")) {
        std::string all = "{";
        for (int i = 0; DB_KEYS[i][0]; i++) {
            if (i) all += ",";
            all += "\"" + std::string(DB_KEYS[i][0]) + "\":" + js::json_stringify(r.global(DB_KEYS[i][1]));
        }
        all += ",\"Yanfly\":" + js::json_stringify(r.global("Yanfly")) + "}";
        file_write(dump, all.data(), all.size());
    }
    Exporter ex;
    for (int i = 0; DB_KEYS[i][0]; i++) {
        js::Val v = r.global(DB_KEYS[i][1]);
        if (!v.is_obj()) continue;
        ex.path = DB_KEYS[i][1];
        Node* n = ex.run(v, false);
        if (n) node_set(root, DB_KEYS[i][0], n);
    }
    js::Val yanfly = r.global("Yanfly");
    if (yanfly.is_obj()) {
        ex.path = "Yanfly";
        Node* y = node_obj();
        static const char* keys[] = {"Param",       "SkillIdRef", "StateIdRef", "ItemIdRef",   "WeaponIdRef", "ArmorIdRef", "ElementIdRef", "STypeIdRef",
                                     "EnemyIdRef",  "Icon",       "MsgMacro",   "MsgMacroRef", "Lunatic",     "EventSpawn", nullptr};
        for (int k = 0; keys[k]; k++) {
            const js::Val* v = yanfly.o->find(js::atom(keys[k]));
            if (!v) continue;
            ex.path = std::string("Yanfly.") + keys[k];
            Node* c = ex.run(*v, false);
            if (c) node_set(y, keys[k], c);
        }
        node_set(root, "yanfly", y);
    }
    if (!ex.warnings.empty()) {
        for (size_t i = 0; i < ex.warnings.size() && i < 20; i++) log_msg("  note: data step left a host value at %s", ex.warnings[i].c_str());
    }
    Node* methods = node_obj();
    for (size_t i = 0; order && i < order->count; i++) {
        const char* name = node_strv(order->items[i]);
        if (!name) continue;
        collect_js_methods(read_plugin(g, name), methods);
    }
    node_set(root, "jsMethods", methods);
    {
        std::string src = read_plugin(g, "GTP_OmoriFixes");
        size_t at = src.find("Print_Something_Txt");
        if (at != std::string::npos) at = src.find("writeFileSync(", at);
        size_t q1 = at == std::string::npos ? at : src.find('`', at);
        size_t q2 = q1 == std::string::npos ? q1 : src.find('`', q1 + 1);
        size_t q3 = q2 == std::string::npos ? q2 : src.find('`', q2 + 1);
        size_t q4 = q3 == std::string::npos ? q3 : src.find('`', q3 + 1);
        if (q4 != std::string::npos) node_set(root, "somethingTxt", node_strn(src.data() + q3 + 1, q4 - q3 - 1));
    }
    if (getenv("OMORI_VERBOSE")) log_msg("  data step: %d plugin statements skipped", r.failed_statements);
    return 1;
}
