#pragma once
#include <memory>
#include <string>

#include "../core/value.h"

namespace data {

bool load();
const Value& root();
const Value& system();
const Value& actors();
const Value& classes();
const Value& skills();
const Value& items();
const Value& weapons();
const Value& armors();
const Value& enemies();
const Value& troops();
const Value& states();
const Value& animations();
const Value& common_events();
const Value& map_infos();
const Value& tilesets();
std::shared_ptr<const void> keep();
const Value& plugin(const char* name);
std::string plugin_param(const char* plugin, const char* key, const char* def = "");
const Value& quests();
const Value& text_colors();
std::shared_ptr<Document> lang_file(const std::string& file);
const Value& lang_entry(const std::string& code);
std::string lang_text(const std::string& code);
const std::string& language();
void set_language(const std::string& lang);
std::string js_string(const Value& v);

}
