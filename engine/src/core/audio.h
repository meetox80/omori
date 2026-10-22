#pragma once
#include <string>

namespace audio {

struct Track {
    std::string name;
    int volume = 90;
    int pitch = 100;
    int pan = 0;
    double pos = 0;
};

void init();
void shutdown();
void update();

void set_volumes(int bgm, int bgs, int me, int se);

void play_bgm(const Track& t, double pos = 0);
void stop_bgm();
void fadeout_bgm(double seconds);
void fadein_bgm(double seconds);
bool bgm_playing();
double bgm_position();
Track current_bgm();
Track save_bgm();
void replay_bgm(const Track& t);

void play_bgs(const Track& t, double pos = 0);
void stop_bgs();
void fadeout_bgs(double seconds);
Track current_bgs();

void play_me(const Track& t);
void stop_me();
void fadeout_me(double seconds);
bool me_playing();

void play_se(const Track& t);
void stop_se();
bool se_playing();

void loop_current_me();
void play_current_bgm_once();
bool take_bgm_once_finished();
void fade_bgm_to(double volume, double seconds);
void fade_bgs_to(double volume, double seconds);

void play_danger_bgs(const Track& t, double pos = 0);
void stop_danger_bgs();
void fadeout_danger_bgs(double seconds);
Track current_danger_bgs();

constexpr int EXTRA_BUFFERS = 4;
void play_extra(int index, const std::string& folder, const Track& t, double pos, bool loop);
void stop_extra(int index);
void fadeout_extra(int index, double seconds);
void clear_extra();

constexpr int MOVIE_VOICES = 3;
bool play_movie_audio(int slot, const std::string& rel, double pos);
void stop_movie_audio(int slot);
bool movie_audio_active(int slot);
double movie_audio_position(int slot);


void stop_all();
void on_resume();
void release_memory();
void set_se_budget(size_t bytes);
void set_low_memory_handler(void (*fn)());
double take_mix_stats(double& max_us);
void write_dump(const std::string& path);

}
