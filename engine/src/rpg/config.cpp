#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../core/audio.h"
#include "../core/fs.h"
#include "data.h"

namespace rpg {

static Config g_config;

Config& config() { return g_config; }

void config_apply_volumes() { audio::set_volumes(g_config.bgm_volume, g_config.bgs_volume, g_config.me_volume, g_config.se_volume); }

struct Field {
    const char* name;
    int Config::*i;
    bool Config::*b;
};

static const Field FIELDS[] = {{"alwaysDash", nullptr, &Config::always_dash},
                               {"commandRemember", nullptr, &Config::command_remember},
                               {"bgmVolume", &Config::bgm_volume, nullptr},
                               {"bgsVolume", &Config::bgs_volume, nullptr},
                               {"meVolume", &Config::me_volume, nullptr},
                               {"seVolume", &Config::se_volume, nullptr},
                               {"battleCamera", nullptr, &Config::battle_camera},
                               {"characterStrafe", nullptr, &Config::character_strafe},
                               {"characterTurning", nullptr, &Config::character_turning},
                               {"battleAnimations", nullptr, &Config::battle_animations},
                               {"battleAnimationSpeed", &Config::battle_animation_speed, nullptr},
                               {"battleLogSpeed", &Config::battle_log_speed, nullptr},
                               {"menuAnimations", nullptr, &Config::menu_animations},
                               {"gamepadTips", nullptr, &Config::gamepad_tips},
                               {"textSkip", nullptr, &Config::text_skip},
                               {"screenEffectsEnabled", nullptr, &Config::screen_effects_enabled},
                               {"screenEffectsIntensity", &Config::screen_effects_intensity, nullptr},
                               {"TKMFilterEnabledAll", nullptr, &Config::filters_enabled},
                               {nullptr, nullptr, nullptr}};

void config_load() {
    g_config = Config();
    g_config.filters_enabled = data::plugin_param("FilterController", "enabledAll-DefaultValue", "true") == "true";
    std::string a = data::plugin_param("Aries001_AnimationScreenEffects", "Default Option", "true");
    g_config.screen_effects_enabled = a == "true";
    g_config.screen_effects_intensity = std::atoi(data::plugin_param("Aries001_AnimationScreenEffects", "Default Power", "100").c_str());
    std::vector<uint8_t> raw;
    if (fs::read_file(fs::save_path("config.txt"), raw)) {
        std::string text(raw.begin(), raw.end());
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            for (int i = 0; FIELDS[i].name; i++) {
                if (k != FIELDS[i].name) continue;
                if (FIELDS[i].i) g_config.*(FIELDS[i].i) = std::atoi(v.c_str());
                else g_config.*(FIELDS[i].b) = v == "1" || v == "true";
            }
        }
    }
    config_apply_volumes();
}

void config_save() {
    std::string out;
    for (int i = 0; FIELDS[i].name; i++) {
        out += FIELDS[i].name;
        out += "=";
        if (FIELDS[i].i) out += std::to_string(g_config.*(FIELDS[i].i));
        else out += (g_config.*(FIELDS[i].b)) ? "1" : "0";
        out += "\n";
    }
    fs::write_file(fs::save_path("config.txt"), out.data(), out.size());
}

}
