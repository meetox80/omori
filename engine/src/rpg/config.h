#pragma once
#include <string>

namespace rpg {

struct Config {
    bool always_dash = false;
    bool command_remember = false;
    int bgm_volume = 100;
    int bgs_volume = 100;
    int me_volume = 100;
    int se_volume = 100;
    bool battle_camera = true;
    bool character_strafe = true;
    bool character_turning = true;
    bool battle_animations = true;
    int battle_animation_speed = 0;
    int battle_log_speed = 1;
    bool menu_animations = true;
    bool gamepad_tips = false;
    bool text_skip = false;
    bool screen_effects_enabled = true;
    int screen_effects_intensity = 100;
    bool filters_enabled = true;
};

Config& config();
void config_load();
void config_save();
void config_apply_volumes();

}
