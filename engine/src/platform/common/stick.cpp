#include "../platform.h"

namespace plat {

static bool g_stick = false;

void set_stick_enabled(bool on) { g_stick = on; }

bool stick_enabled() { return g_stick; }

}
