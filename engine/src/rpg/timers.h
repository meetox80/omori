#pragma once
#include <functional>

namespace rpg {
namespace timers {

int set_timeout(double ms, std::function<void()> fn);
void clear_timeout(int id);
void run();
void clear_all();

}
}
