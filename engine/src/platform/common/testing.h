#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../platform.h"

namespace testing {

void parse_args(plat::Options& opt, const std::vector<std::string>& args);
std::vector<std::string> read_args_file(const std::string& path);
void setup(const plat::Options& opt);
uint32_t buttons(uint64_t frame);
bool touch(uint64_t frame, int& x, int& y);
bool exit_due(uint64_t frame);
bool warp_due(uint64_t frame, int& map, int& x, int& y, int& d);
bool shot_due(uint64_t frame);
bool reset_due(uint64_t frame);
bool battle_due(uint64_t frame, int& troop);
bool eval_due(uint64_t frame, std::string& src);
std::string shot_path(const plat::Options& opt, uint64_t frame, const char* ext);
bool write_bmp(const std::string& path, const uint32_t* px, int w, int h, int stride);

}
