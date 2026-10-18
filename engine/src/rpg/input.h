#pragma once
#include <cstdint>
#include <string>

namespace input {

enum Key : uint8_t {
    OK = 0,
    ESCAPE,
    SHIFT,
    CONTROL,
    TAG,
    TAB,
    PAGEUP,
    PAGEDOWN,
    UP,
    DOWN,
    LEFT,
    RIGHT,
    KEY_COUNT,
    CANCEL = 200,
    MENU = 201,
    NONE = 255
};

void update();
void clear();
bool is_pressed(int k);
bool is_triggered(int k);
bool is_repeated(int k);
bool is_long_pressed(int k);
int dir4();
int dir8();
int key_of(const std::string& name);
const char* name_of(int k);
int pressed_time();
bool any_triggered();
extern int key_repeat_wait;
extern int key_repeat_interval;

}
