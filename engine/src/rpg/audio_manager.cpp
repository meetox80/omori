#include "audio_manager.h"

#include "../core/audio.h"
#include "data.h"
#include "game.h"

#include <vector>

namespace rpg {
namespace audio_manager {

static audio::Track track(const AudioObj& a) {
    audio::Track t;
    t.name = a.name;
    t.volume = (int)a.volume;
    t.pitch = (int)a.pitch;
    t.pan = (int)a.pan;
    t.pos = a.pos;
    return t;
}

static AudioObj from_track(const audio::Track& t, bool valid) {
    AudioObj a;
    a.valid = valid;
    a.name = t.name;
    a.volume = t.volume;
    a.pitch = t.pitch;
    a.pan = t.pan;
    a.pos = t.pos;
    return a;
}

void play_bgm(const AudioObj& a, double pos) {
    if (!a.valid) return;
    audio::play_bgm(track(a), pos);
}

void replay_bgm(const AudioObj& a) {
    if (!a.valid) return;
    audio::replay_bgm(track(a));
}

void stop_bgm() { audio::stop_bgm(); }
void fade_out_bgm(double s) { audio::fadeout_bgm(s); }
void fade_in_bgm(double s) { audio::fadein_bgm(s); }
AudioObj save_bgm() { return from_track(audio::save_bgm(), true); }
AudioObj current_bgm() { return from_track(audio::current_bgm(), true); }

void play_bgs(const AudioObj& a, double pos) {
    if (!a.valid) return;
    audio::play_bgs(track(a), pos);
}

void replay_bgs(const AudioObj& a) { play_bgs(a, a.pos); }
void stop_bgs() { audio::stop_bgs(); }
void fade_out_bgs(double s) { audio::fadeout_bgs(s); }

AudioObj save_bgs() {
    audio::Track t = audio::current_bgs();
    if (t.name.empty()) t.volume = 0;
    return from_track(t, true);
}

AudioObj current_bgs() { return from_track(audio::current_bgs(), true); }

void play_me(const AudioObj& a) {
    if (!a.valid) return;
    audio::play_me(track(a));
}

void stop_me() { audio::stop_me(); }
void fade_out_me(double s) { audio::fadeout_me(s); }

void play_se(const AudioObj& a) {
    if (!a.valid || a.name.empty()) return;
    audio::play_se(track(a));
}

void stop_se() { audio::stop_se(); }
void play_static_se(const AudioObj& a) { play_se(a); }
void stop_all() { audio::stop_all(); }

static std::vector<int> g_frame_system_sounds;
static int g_bgm_once_switch = 0;

void play_system_sound(int n) {
    for (int k : g_frame_system_sounds) {
        if (k == n) return;
    }
    g_frame_system_sounds.push_back(n);
    play_static_se(AudioObj::from(data::system()["sounds"][n]));
}

void clear_unique_check_se() { g_frame_system_sounds.clear(); }

void loop_current_me() { audio::loop_current_me(); }

void play_current_bgm_once(int switch_id) {
    g_bgm_once_switch = switch_id;
    audio::play_current_bgm_once();
}

void fade_bgm_to(double volume, double frames) { audio::fade_bgm_to(volume, frames / 60.0); }
void fade_bgs_to(double volume, double frames) { audio::fade_bgs_to(volume, frames / 60.0); }

void play_danger_bgs(const AudioObj& a, double pos) { audio::play_danger_bgs(track(a), pos); }
void stop_danger_bgs() { audio::stop_danger_bgs(); }
void fade_out_danger_bgs(double seconds) { audio::fadeout_danger_bgs(seconds); }

AudioObj current_danger_bgs() {
    audio::Track t = audio::current_danger_bgs();
    return from_track(t, !t.name.empty());
}

void play_to_extra_buffer(int index, const std::string& type, const AudioObj& a, double pos, bool loop) { audio::play_extra(index, type, track(a), pos, loop); }
void stop_extra_buffer(int index) { audio::stop_extra(index); }
void fadeout_extra_buffer(int index, double seconds) { audio::fadeout_extra(index, seconds); }
void clear_extra_buffers() { audio::clear_extra(); }
bool are_all_se_completed() { return !audio::se_playing(); }

void update() {
    audio::update();
    if (audio::take_bgm_once_finished() && g_bgm_once_switch > 0 && game_objects_exist()) game_switches().set_value(g_bgm_once_switch, true);
}

}
}
