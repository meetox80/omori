#pragma once
#include <cstdint>

namespace rpg {
namespace perf {

bool enabled();
void set_enabled(bool on);
void add(int section, uint64_t us);
const char* name(int section);

enum Section { MapEvents = 0, Player, Screen, Interpreter, Spriteset, Windows, Filters, Tilemap, Weather, Fog, Footprints, Load, Audio, Images, Timers, Count };

class Scope {
public:
    explicit Scope(int section);
    ~Scope();

private:
    int section_;
    uint64_t start_;
};

}
}
