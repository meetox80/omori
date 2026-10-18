#pragma once
#include "state.h"

namespace rpg {
namespace audio_manager {

void play_bgm(const AudioObj& a, double pos = 0);
void replay_bgm(const AudioObj& a);
void stop_bgm();
void fade_out_bgm(double seconds);
void fade_in_bgm(double seconds);
AudioObj save_bgm();
AudioObj current_bgm();
void play_bgs(const AudioObj& a, double pos = 0);
void replay_bgs(const AudioObj& a);
void stop_bgs();
void fade_out_bgs(double seconds);
AudioObj save_bgs();
AudioObj current_bgs();
void play_me(const AudioObj& a);
void stop_me();
void fade_out_me(double seconds);
void play_se(const AudioObj& a);
void stop_se();
void play_static_se(const AudioObj& a);
void stop_all();
void play_system_sound(int n);
void loop_current_me();
void play_current_bgm_once(int switch_id);
void fade_bgm_to(double volume, double frames);
void fade_bgs_to(double volume, double frames);
void play_danger_bgs(const AudioObj& a, double pos = 0);
void stop_danger_bgs();
void fade_out_danger_bgs(double seconds);
AudioObj current_danger_bgs();
void play_to_extra_buffer(int index, const std::string& type, const AudioObj& a, double pos, bool loop);
void stop_extra_buffer(int index);
void fadeout_extra_buffer(int index, double seconds);
void clear_extra_buffers();
bool are_all_se_completed();
void clear_unique_check_se();
void update();

}
}
