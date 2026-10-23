#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../gfx2d/bitmap.h"
#include "../js/value.h"

namespace ui {

const Value& yparam(const char* key);
double yparam_num(const char* key, double def = 0);
bool yparam_bool(const char* key);
std::string yparam_str(const char* key);

const Value& lang_data(const std::string& code);
std::shared_ptr<const void> lang_keep(const std::string& code);
std::string lang_message_text(const std::string& code);
const Value& system_lang();
std::string database_text(const std::string& key);
std::string system_text(const std::string& type, const std::string& name);
std::string plugin_text(const std::string& type, const std::string& name);
std::string replace_database_codes(std::string text);

g2d::BitmapPtr load_system(const std::string& name);
g2d::BitmapPtr load_picture(const std::string& name);
g2d::BitmapPtr load_face(const std::string& name);
g2d::BitmapPtr load_character(const std::string& name);
g2d::BitmapPtr load_atlas(const std::string& name);
g2d::BitmapPtr load_parallax(const std::string& name);
g2d::BitmapPtr load_battleback1(const std::string& name);
g2d::BitmapPtr load_battleback2(const std::string& name);
g2d::BitmapPtr load_enemy(const std::string& name);
g2d::BitmapPtr load_sv_enemy(const std::string& name);
g2d::BitmapPtr load_animation(const std::string& name);
g2d::BitmapPtr load_title1(const std::string& name);
g2d::BitmapPtr load_title2(const std::string& name);
g2d::BitmapPtr load_folder(const std::string& folder, const std::string& name);
bool is_big_character(const std::string& name);
bool is_object_character(const std::string& name);

void play_cursor();
void play_ok();
void play_cancel();
void play_buzzer();
void play_equip();
void play_save();
void play_load();
void play_shop();
void play_se(const std::string& name, double volume = 90, double pitch = 100, double pan = 0);

std::string to_js_string(const js::Val& v);
std::string number_text(double n);
std::string group_digits(double n);
int current_world();

std::string input_key_code(const std::string& input);
struct IconRect {
    double x = 0, y = 0, w = 24, h = 24;
};
void key_icon_rects(const std::string& key, IconRect& up, IconRect& down, const std::string& type = "", bool physical = false);
void draw_aligned_key_icon(g2d::Bitmap* dst, const std::string& key, double x, double y, double width, double height, const std::string& align = "center",
                           int side = 0);
void draw_key_icon(g2d::Bitmap* dst, const std::string& key, double x, double y, int side = 0);
void draw_button_icon(g2d::Bitmap* dst, const std::string& key, double x, double y);
g2d::BitmapPtr input_icon_bitmap();

}
