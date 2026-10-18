#include "timers.h"

#include <vector>

#include "../platform/platform.h"

namespace rpg {
namespace timers {

namespace {

struct Timer {
    int id = 0;
    double due = 0;
    std::function<void()> fn;
};

std::vector<Timer>& list() {
    static std::vector<Timer> l;
    return l;
}

int g_next_id = 1;

}

int set_timeout(double ms, std::function<void()> fn) {
    Timer t;
    t.id = g_next_id++;
    t.due = plat::time_seconds() + (ms > 0 ? ms : 0) / 1000.0;
    t.fn = std::move(fn);
    list().push_back(std::move(t));
    return list().back().id;
}

void clear_timeout(int id) {
    auto& l = list();
    for (size_t i = 0; i < l.size(); i++) {
        if (l[i].id == id) {
            l.erase(l.begin() + (long)i);
            return;
        }
    }
}

void run() {
    double now = plat::time_seconds();
    std::vector<Timer> due;
    auto& l = list();
    for (size_t i = 0; i < l.size();) {
        if (l[i].due <= now) {
            due.push_back(std::move(l[i]));
            l.erase(l.begin() + (long)i);
        } else {
            i++;
        }
    }
    for (Timer& t : due) {
        if (t.fn) t.fn();
    }
}

void clear_all() { list().clear(); }

}
}
