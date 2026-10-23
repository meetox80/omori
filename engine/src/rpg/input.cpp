#include "input.h"

#include <cstring>

#include "../platform/platform.h"
#include "config.h"

namespace input {

int key_repeat_wait = 24;
int key_repeat_interval = 6;

static bool g_current[KEY_COUNT];
static bool g_previous[KEY_COUNT];
static int g_latest = NONE;
static int g_pressed_time = 0;
static int g_dir4 = 0;
static int g_dir8 = 0;
static char g_axis = 0;

static const char* NAMES[KEY_COUNT] = {"ok", "escape", "shift", "control", "tag", "tab", "pageup", "pagedown", "up", "down", "left", "right"};

static const int ORDER[KEY_COUNT] = {TAB, OK, SHIFT, CONTROL, TAG, ESCAPE, LEFT, UP, RIGHT, DOWN, PAGEUP, PAGEDOWN};

int key_of(const std::string& name) {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (name == NAMES[i]) return i;
    }
    if (name == "cancel") return CANCEL;
    if (name == "menu") return MENU;
    return NONE;
}

const char* name_of(int k) {
    if (k >= 0 && k < KEY_COUNT) return NAMES[k];
    if (k == CANCEL) return "cancel";
    if (k == MENU) return "menu";
    return "";
}

void clear() {
    std::memset(g_current, 0, sizeof(g_current));
    std::memset(g_previous, 0, sizeof(g_previous));
    g_latest = NONE;
    g_pressed_time = 0;
    g_dir4 = g_dir8 = 0;
    g_axis = 0;
}

static int sign_x() { return (g_current[RIGHT] ? 1 : 0) - (g_current[LEFT] ? 1 : 0); }
static int sign_y() { return (g_current[DOWN] ? 1 : 0) - (g_current[UP] ? 1 : 0); }

static int numpad(int x, int y) { return 5 - y * 3 + x; }

void update() {
    uint32_t b = plat::buttons();
    if (rpg::confirm_on_circle()) {
        uint32_t face = b & (BTN_OK | BTN_CANCEL);
        b ^= face;
        if (face & BTN_OK) b |= BTN_CANCEL;
        if (face & BTN_CANCEL) b |= BTN_OK;
    }
    g_current[OK] = (b & BTN_OK) != 0;
    g_current[ESCAPE] = (b & (BTN_CANCEL | BTN_START)) != 0;
    g_current[SHIFT] = (b & BTN_DASH) != 0;
    g_current[TAG] = (b & BTN_MENU) != 0;
    g_current[TAB] = (b & BTN_SELECT) != 0;
    g_current[PAGEUP] = (b & BTN_PAGEUP) != 0;
    g_current[PAGEDOWN] = (b & BTN_PAGEDOWN) != 0;
    g_current[CONTROL] = false;
    g_current[UP] = (b & BTN_UP) != 0;
    g_current[DOWN] = (b & BTN_DOWN) != 0;
    g_current[LEFT] = (b & BTN_LEFT) != 0;
    g_current[RIGHT] = (b & BTN_RIGHT) != 0;
    if (g_latest != NONE && g_current[g_latest]) g_pressed_time++;
    else g_latest = NONE;
    for (int i = 0; i < KEY_COUNT; i++) {
        int k = ORDER[i];
        if (g_current[k] && !g_previous[k]) {
            g_latest = k;
            g_pressed_time = 0;
        }
        g_previous[k] = g_current[k];
    }
    int x = sign_x(), y = sign_y();
    g_dir8 = numpad(x, y);
    if (x != 0 && y != 0) {
        if (g_axis == 'x') y = 0;
        else x = 0;
    } else if (x != 0) {
        g_axis = 'y';
    } else if (y != 0) {
        g_axis = 'x';
    }
    g_dir4 = numpad(x, y);
    if (g_dir4 == 5) g_dir4 = 0;
    if (g_dir8 == 5) g_dir8 = 0;
}

static bool escape_compatible(int k) { return k == CANCEL || k == MENU; }

bool is_pressed(int k) {
    if (escape_compatible(k)) return g_current[ESCAPE];
    return k >= 0 && k < KEY_COUNT && g_current[k];
}

bool is_triggered(int k) {
    if (escape_compatible(k)) return is_triggered(ESCAPE);
    return g_latest == k && g_pressed_time == 0;
}

bool is_repeated(int k) {
    if (escape_compatible(k)) return is_repeated(ESCAPE);
    return g_latest == k && (g_pressed_time == 0 || (g_pressed_time >= key_repeat_wait && g_pressed_time % key_repeat_interval == 0));
}

bool is_long_pressed(int k) {
    if (escape_compatible(k)) return is_long_pressed(ESCAPE);
    return g_latest == k && g_pressed_time >= key_repeat_wait;
}

int dir4() { return g_dir4; }
int dir8() { return g_dir8; }
int pressed_time() { return g_pressed_time; }

bool any_triggered() { return g_latest != NONE && g_pressed_time == 0; }

}
