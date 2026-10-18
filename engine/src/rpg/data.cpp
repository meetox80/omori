#include "data.h"

#include <cstdio>
#include <cstdlib>
#include <list>
#include <unordered_map>

#include "../core/fs.h"
#include "../platform/platform.h"

namespace data {

static std::shared_ptr<Document> g_doc_ptr = std::make_shared<Document>();
static Document& g_doc = *g_doc_ptr;
static std::string g_language = "en";
static std::unordered_map<std::string, std::shared_ptr<Document>> g_lang;
static std::list<std::string> g_lang_lru;

bool load() {
    if (!g_doc.load_data("game.bin")) {
        plat::log("failed to load game.bin");
        return false;
    }
    return true;
}

const Value& root() { return g_doc.root(); }
const Value& system() { return root()["system"]; }
const Value& actors() { return root()["actors"]; }
const Value& classes() { return root()["classes"]; }
const Value& skills() { return root()["skills"]; }
const Value& items() { return root()["items"]; }
const Value& weapons() { return root()["weapons"]; }
const Value& armors() { return root()["armors"]; }
const Value& enemies() { return root()["enemies"]; }
const Value& troops() { return root()["troops"]; }
const Value& states() { return root()["states"]; }
const Value& animations() { return root()["animations"]; }
const Value& common_events() { return root()["commonEvents"]; }
const Value& map_infos() { return root()["mapInfos"]; }
const Value& tilesets() { return root()["tilesets"]; }
std::shared_ptr<const void> keep() { return g_doc_ptr; }
const Value& quests() { return root()["quests"]; }
const Value& text_colors() { return root()["textColors"]; }
const Value& plugin(const char* name) { return root()["plugins"][name]; }

std::string plugin_param(const char* plugin_name, const char* key, const char* def) {
    const Value* v = plugin(plugin_name).find(key);
    return v && v->is_str() ? v->string() : std::string(def);
}

const std::string& language() { return g_language; }

void set_language(const std::string& lang) {
    if (lang == g_language) return;
    g_language = lang;
    g_lang.clear();
    g_lang_lru.clear();
}

std::shared_ptr<Document> lang_file(const std::string& file) {
    auto it = g_lang.find(file);
    if (it != g_lang.end()) {
        g_lang_lru.remove(file);
        g_lang_lru.push_front(file);
        return it->second;
    }
    auto doc = std::make_shared<Document>();
    if (!doc->load_data("lang/" + g_language + "/" + file + ".bin")) {
        plat::log("missing language file %s", file.c_str());
        doc.reset();
    }
    g_lang[file] = doc;
    g_lang_lru.push_front(file);
    while (g_lang_lru.size() > 24) {
        g_lang.erase(g_lang_lru.back());
        g_lang_lru.pop_back();
    }
    return doc;
}

const Value& lang_entry(const std::string& code) {
    size_t dot = code.find('.');
    if (dot == std::string::npos) return Value::null_value();
    std::shared_ptr<Document> d = lang_file(code.substr(0, dot));
    if (!d) return Value::null_value();
    return d->root()[code.substr(dot + 1).c_str()];
}

std::string js_string(const Value& v) {
    switch (v.t) {
    case Value::Null: return "null";
    case Value::Bool: return v.b ? "true" : "false";
    case Value::Int: return std::to_string(v.i);
    case Value::Num: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v.dbl());
        for (int prec = 1; prec <= 17; prec++) {
            std::snprintf(buf, sizeof(buf), "%.*g", prec, v.dbl());
            if (std::strtod(buf, nullptr) == v.dbl()) break;
        }
        return buf;
    }
    case Value::Str: return v.string();
    case Value::Arr: {
        std::string out;
        for (size_t i = 0; i < v.size(); i++) {
            if (i) out += ",";
            if (!v[i].is_null()) out += js_string(v[i]);
        }
        return out;
    }
    default: return "[object Object]";
    }
}

std::string lang_text(const std::string& code) {
    const Value& e = lang_entry(code);
    if (e.is_str()) return e.string();
    return e["text"].string();
}

}
