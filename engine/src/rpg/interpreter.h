#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/bind.h"
#include "character.h"

namespace rpg {

class GameBattler;

class GameInterpreter : public js::JsObject {
public:
    explicit GameInterpreter(int depth = 0);
    ~GameInterpreter() override;

    int depth_ = 0;
    int map_id_ = 0;
    int event_id_ = 0;
    const Value* list_ = nullptr;
    std::shared_ptr<const void> keep_;
    int index_ = 0;
    int wait_count = 0;
    std::string wait_mode;
    std::vector<std::string> comments;
    CharRef character_;
    std::unique_ptr<GameInterpreter> child;
    std::map<int, js::Val> branch;
    const Value* params_ = nullptr;
    int indent_ = 0;
    uint64_t frame_count_ = 0;
    int freeze_checker = 0;
    int requested_image_count = 0;
    struct WaitCondition {
        std::string source;
        std::shared_ptr<const Value> program;
    };
    std::vector<WaitCondition> wait_conditions;
    int mfps_wait = 0;
    std::vector<std::string> language_choices;
    std::vector<std::string> language_choice_labels;
    int last_animating_picture = 0;
    bool call_event_running = false;
    int call_event_event_id = 0;
    int call_event_page_id = 0;
    int call_event_map_id = 0;

    void clear();
    void setup(const Value* list, int event_id, std::shared_ptr<const void> keep);
    int event_id() const { return event_id_; }
    bool is_on_current_map() const;
    bool setup_reserved_common_event();
    bool is_running() const { return list_ != nullptr; }
    void update();
    bool update_child();
    bool update_wait();
    bool update_wait_count();
    bool update_wait_mode();
    bool base_update_wait_mode();
    void set_wait_mode(const std::string& m) { wait_mode = m; }
    void wait(int duration) { wait_count = duration; }
    int fade_speed() const { return 24; }
    bool execute_command();
    bool check_freeze();
    void terminate();
    void skip_branch();
    const Value* current_command() const;
    int next_event_code() const;
    const Value* list() const { return list_; }
    int list_size() const { return list_ ? (int)list_->size() : 0; }
    const Value& command_at(int i) const;
    GameCharacter* character(const Value& param);
    GameCharacter* character_id(int id);
    js::Val operate_value(int operation, int operand_type, double operand);
    void change_hp(GameBattler* target, double value, bool allow_death);
    void setup_child(const Value* list, int event_id, std::shared_ptr<const void> keep);
    void jump_to(int index);
    void operate_variable(int id, int op, const js::Val& value);
    void operate_self_variable(int id, int op, const js::Val& value);
    js::Val game_data_operand(int type, int p1, int p2);
    bool eval_condition(const Value& p, int compiled_index, const char* where, js::Val* out);
    js::Val run_script(const Value* program, const std::string& source, const char* where, bool* ok);
    js::Val self_variable_value(const js::Val& id) const;

    bool command(int code);
    bool command101();
    bool command102();
    void setup_choices(const Value& params);
    void setup_choices_list(std::vector<std::string> choices, int cancel_type, int default_type, int position_type, int background);
    bool command402();
    bool command403();
    bool command103();
    bool command104();
    bool command105();
    bool command108();
    bool command111();
    bool command411();
    bool command413();
    bool command113();
    bool command115();
    bool command117();
    bool command118();
    bool command119();
    bool command121();
    bool command122();
    bool command123();
    bool command124();
    bool command125();
    bool command126();
    bool command127();
    bool command128();
    bool command129();
    bool command132();
    bool command133();
    bool command134();
    bool command135();
    bool command136();
    bool command137();
    bool command138();
    bool command139();
    bool command140();
    bool command201();
    bool command202();
    bool command203();
    bool command204();
    bool command205();
    bool command206();
    bool command211();
    bool command212();
    bool command213();
    bool command214();
    bool command216();
    bool command217();
    bool command221();
    bool command222();
    bool command223();
    bool command224();
    bool command225();
    bool command230();
    bool command231();
    bool command232();
    bool command233();
    bool command234();
    bool command235();
    bool command236();
    bool command241();
    bool command242();
    bool command243();
    bool command244();
    bool command245();
    bool command246();
    bool command249();
    bool command250();
    bool command251();
    bool command261();
    bool command281();
    bool command282();
    bool command283();
    bool command284();
    bool command285();
    bool command301();
    bool command601();
    bool command602();
    bool command603();
    bool command302();
    bool command303();
    bool command311();
    bool command312();
    bool command326();
    bool command313();
    bool command314();
    bool command315();
    bool command316();
    bool command317();
    bool command318();
    bool command319();
    bool command320();
    bool command321();
    bool command322();
    bool command323();
    bool command324();
    bool command325();
    bool command331();
    bool command332();
    bool command342();
    bool command333();
    bool command334();
    bool command335();
    bool command336();
    bool command337();
    bool command339();
    bool command340();
    bool command351();
    bool command352();
    bool command353();
    bool command354();
    bool command355();
    bool command356();
    bool command357();

    void plugin_command(const std::string& command, std::vector<std::string> args);
    bool command_show_language_message(const std::string& code);
    void add_language_choice(const std::string& code, const std::string& label);
    bool command_show_language_choices(const std::string& cancel_type);
    bool command_language_jump_to(const std::string& label);
    void setup_language_choices(int cancel);
    bool is_continue_message_string() const;
    void setup_num_input(const Value& params);
    void setup_item_choice(const Value& params);
    std::string args_to_string(const std::vector<std::string>& args) const;
    void call_event(const std::string& line);
    void process_call_event();
    void insert_call_event_data(int event_id, int page_id);
    void plugin_command_horror_effects(std::vector<std::string> args);
    void set_message_anchor(const std::string& s, char type);
    void change_letter_sound_name(const std::vector<std::string>& args);
    void set_picture_animation(int picture_id, const js::Val& frames, double delay, double loops, bool wait);
    void setup_picture_custom_frames(int picture_id, double width, double height, int hframes, int vframes);
    void remove_picture_custom_frames(int picture_id);
    void set_picture_frame_index(int picture_id, int index);
    void wait_for_bitmap(bool bitmap_ok);
    void process_skip_cutscene();
    GameInterpreter* latest_interpreter();
    void stop_skip_cutscene(GameInterpreter* in, int target_line, bool found_target);
    void set_event_icon(int event_id, const std::string& img, int index);
    void add_black_letter(const std::vector<std::string>& letters);
    void set_picture_as_parallax(double width, double height, double xs, double ys);
    void change_parallax_picture_speed(int picture_id, const js::Val& xs, const js::Val& ys);
    void setup_shop(const std::string& name, int type);
    void call_save_menu(bool save, bool load);
    void call_album_menu(int item_id, int interface_mode);
    void call_black_letter_menu(bool forced);
    void call_quest_menu();
    void show_name_input_windows(const std::string& name, int max, bool wait);
    void start_smash_qte(const js::Val& segments, const js::Val& time, const js::Val& width);
    void start_stop_the_arrow_qte(const js::Val& direction, const js::Val& delay, const js::Val& x, const js::Val& y);
    void start_hit_the_mark_qte(const js::Val& speed, const js::Val& index, const js::Val& x, const js::Val& y);
    void show_pizza_note(const js::Val& text);
    void setup_ambience(const std::string& name, double x, double y, bool custom);

    js::HostClass* js_class() const override;
};

std::vector<std::string> split_spaces(const std::string& s);
bool add_interpreter_update_hook(void (*fn)(GameInterpreter&));

void retire_interpreter(std::unique_ptr<GameInterpreter> in);
void flush_retired_interpreters();

}
