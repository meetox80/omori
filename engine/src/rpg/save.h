#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/value.h"

namespace rpg {

class GameInterpreter;

namespace save {

struct Ctx;
class Writer;

class Ar {
public:
    Ar(Writer* out, Ctx* ctx);
    Ar(const Value* in, Ctx* ctx);
    bool reading() const { return in_ != nullptr; }
    Ctx* ctx() const { return ctx_; }

    void io(const char* k, int& v);
    void io(const char* k, double& v);
    void io(const char* k, bool& v);
    void io(const char* k, uint64_t& v);
    void io(const char* k, std::string& v);
    void io(const char* k, js::Val& v);
    void io(const char* k, js::Expando& v);
    void io(const char* k, std::vector<int>& v);
    void io(const char* k, std::vector<double>& v);
    void io(const char* k, std::vector<bool>& v);
    void io(const char* k, std::vector<uint8_t>& v);
    void io(const char* k, std::vector<std::string>& v);
    void io(const char* k, std::map<int, double>& v);
    void io(const char* k, std::map<int, int>& v);
    void io(const char* k, std::map<int, bool>& v);
    void io(const char* k, std::map<int, js::Val>& v);
    void io(const char* k, std::map<std::string, int>& v);
    void io(const char* k, std::map<std::string, bool>& v);
    void io(const char* k, std::map<std::string, js::Val>& v);
    void io(const char* k, std::map<int, std::vector<int>>& v);
    void io(const char* k, std::map<int, std::pair<int, int>>& v);
    void io(const char* k, double* arr, size_t n);
    void io(const char* k, bool* arr, size_t n);

    template <class E>
    void io_enum(const char* k, E& v) {
        int i = (int)v;
        io(k, i);
        v = (E)i;
    }

    bool has(const char* k) const;
    void obj(const char* k, const std::function<void(Ar&)>& f);
    size_t list(const char* k, size_t write_count, const std::function<void(Ar&, size_t)>& f);
    const Value* value(const char* k, const Value* v);
    const Value* in() const { return in_; }
    Writer& put(const char* k);
    size_t written() const { return written_; }

private:
    Writer* out_ = nullptr;
    const Value* in_ = nullptr;
    Ctx* ctx_ = nullptr;
    size_t written_ = 0;
};

struct SlotInfo {
    bool exists = false;
    std::string actor_name;
    int actor_level = 0;
    std::string face_name;
    int face_index = 0;
    js::Val chapter;
    std::string location;
    std::string playtime;
    double timestamp = 0;
    js::Val world_index;
};

constexpr int kMaxSlots = 6;
constexpr int kCrashSlot = 44;

std::vector<uint8_t> snapshot_for_retry();
void restore_retry_snapshot(const std::vector<uint8_t>& data);

bool save_game(int slot);
bool load_game(int slot);
bool exists(int slot);
bool remove(int slot);
bool info(int slot, SlotInfo& out);
bool any_exists();
int latest_slot();
int last_accessed();
void set_last_accessed(int slot);
void select_for_new_game();

bool write_text(const std::string& name, const std::string& text);
bool read_text(const std::string& name, std::string& out);

std::shared_ptr<const void> keep_of(Ar& ar);
void defer(Ar& ar, std::function<void()> fn);
void interpreter_io(Ar& ar, GameInterpreter& in);

}

}
