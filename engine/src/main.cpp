#include "core/audio.h"
#include "core/fs.h"
#include "core/image.h"
#include "gfx2d/bitmap.h"
#include "gfx2d/text.h"
#include "js/random.h"
#include "omori/scenes.h"
#include "platform/platform.h"
#include "rpg/data.h"
#include "rpg/game.h"
#include "rpg/scene.h"

#include <algorithm>
#include <ctime>

int main(int argc, char** argv) {
    if (!plat::init(argc, argv)) return 1;
    if (plat::options().input_script.empty()) rng::seed(((uint64_t)std::time(nullptr) << 20) ^ plat::time_us());
    audio::init();
    plat::set_low_memory_handler([] {
        images::release_memory();
        audio::release_memory();
        g2d::release_decoded_sources();
        g2d::release_font_atlases();
    });
#if defined(PSP)
    images::set_budget(16u << 20);
#elif defined(__vita__)
    images::set_budget(96u << 20);
#elif defined(__wii__)
    images::set_budget(28u << 20);
#elif defined(__WIIU__)
    images::set_budget(256u << 20);
#elif defined(__PPU__) || defined(XENON)
    images::set_budget(96u << 20);
#elif defined(__PS2__)
    images::set_budget(std::clamp<size_t>(plat::heap_limit() > (22u << 20) ? plat::heap_limit() - (22u << 20) : 0, 2u << 20, 8u << 20));
#elif defined(__3DS__)
    images::set_budget(plat::heap_limit() > (96u << 20) ? (32u << 20) : (16u << 20));
#else
    images::set_budget(32u << 20);
#endif
    if (!data::load()) {
        plat::log("data missing");
        plat::shutdown();
        return 1;
    }
    rpg::install_bindings();
    rpg::scene_manager::run(ui::boot_scene());
    if (plat::options().dump_audio > 0) audio::write_dump(fs::save_path("audio_dump.raw"));
    plat::shutdown();
    return 0;
}
