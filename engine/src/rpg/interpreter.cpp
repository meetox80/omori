#include "interpreter.h"
#include "battle.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "../../../shared/jsops.h"
#include "../core/audio.h"
#include "../core/image.h"
#include "../core/regex.h"
#include "../js/compile.h"
#include "../js/dataview.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "../platform/platform.h"
#include "audio_manager.h"
#include "battler.h"
#include "data.h"
#include "game.h"
#include "hooks.h"
#include "input.h"
#include "params.h"
#include "party.h"
#include "enemy.h"
#include "scene.h"
#include "node.h"

namespace rpg {

namespace {

js::Vm& V() { return js::vm(); }

int random_int(double n) { return (int)std::floor(n * rng::next()); }

std::unordered_map<const Value*, int>& fc_retargeted() {
    static std::unordered_map<const Value*, int> m;
    return m;
}

struct ListOverride {
    std::shared_ptr<Document> doc;
};

std::unordered_map<const Value*, ListOverride>& list_overrides() {
    static std::unordered_map<const Value*, ListOverride> m;
    return m;
}

const Value& P(const Value* p, size_t i) { return p ? (*p)[i] : Value::null_value(); }

int Pi(const Value* p, size_t i) { return P(p, i).as_int(); }

double Pn(const Value* p, size_t i) { return P(p, i).num(); }

std::string Ps(const Value* p, size_t i) { return P(p, i).string(); }

js::Val Pv(const Value* p, size_t i) { return js::value_to_js(P(p, i)); }

void tone_of(const Value& v, double out[4]) {
    for (int i = 0; i < 4; i++) out[i] = v[i].num();
}

int wait_fps_max() {
    static int v = [] {
        double n = js_number(data::plugin_param("WaitFPS", "maxWaitTime", "0"));
        return std::isnan(n) ? 0 : (int)n;
    }();
    return v;
}

bool contains_str(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

}

std::vector<std::string> split_spaces(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t p = s.find(' ', start);
        if (p == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

GameInterpreter::GameInterpreter(int depth) : depth_(depth) {
    if (depth_ >= 100) script_error("INTERPRETER", "Common event calls exceeded the limit");
    clear();
    mfps_wait = wait_fps_max();
}

GameInterpreter::~GameInterpreter() = default;

void GameInterpreter::clear() {
    map_id_ = 0;
    event_id_ = 0;
    list_ = nullptr;
    keep_.reset();
    index_ = 0;
    wait_count = 0;
    wait_mode.clear();
    comments.clear();
    character_ = CharRef();
    child.reset();
    requested_image_count = 0;
    wait_conditions.clear();
    mfps_wait = wait_fps_max();
}

static std::string localized_picture(const std::string& name) {
    const std::string def = "_en";
    const std::string cur = "_" + data::language();
    size_t p = name.find(def);
    if (p == std::string::npos) return name;
    return name.substr(0, p) + cur + name.substr(p + def.size());
}

static void request_image(const char* folder, const std::string& name) {
    if (!name.empty()) images::prefetch(std::string("img/") + folder, name);
}

static void request_list_images(const Value* list, std::vector<int>& common_list) {
    if (!list) return;
    for (size_t i = 0; i < list->size(); i++) {
        const Value& command = (*list)[i];
        const Value& p = command["parameters"];
        switch (command["code"].as_int()) {
        case 101: request_image("faces", p[0].string()); break;
        case 117: {
            int id = p[0].as_int();
            const Value& ce = data::common_events()[id];
            if (ce.is_obj() && std::find(common_list.begin(), common_list.end(), id) == common_list.end()) {
                common_list.push_back(id);
                request_list_images(&ce["list"], common_list);
            }
            break;
        }
        case 129:
            if (p[1].as_int() == 0) {
                if (GameActor* a = game_actors().actor(p[0].as_int())) request_image("characters", a->character_name());
            }
            break;
        case 205: {
            const Value& route = p[1]["list"];
            for (size_t k = 0; k < route.size(); k++) {
                if (route[k]["code"].as_int() == ROUTE_CHANGE_IMAGE) request_image("characters", route[k]["parameters"][0].string());
            }
            break;
        }
        case 212:
        case 337:
            if (p[1].as_int()) {
                const Value& anim = data::animations()[p[1].as_int()];
                request_image("animations", anim["animation1Name"].string());
                request_image("animations", anim["animation2Name"].string());
            }
            break;
        case 216:
            if (p[0].as_int() == 0) {
                for (GameFollower* f : game_player().followers().all()) request_image("characters", f->character_name());
            }
            break;
        case 231: request_image("pictures", localized_picture(p[1].string())); break;
        case 282: {
            const Value& names = data::tilesets()[p[0].as_int()]["tilesetNames"];
            for (size_t k = 0; k < names.size(); k++) request_image("tilesets", names[k].string());
            break;
        }
        case 283:
            if (party_in_battle()) {
                request_image("battlebacks1", p[0].string());
                request_image("battlebacks2", p[1].string());
            }
            break;
        case 284:
            if (!party_in_battle()) request_image("parallaxes", p[0].string());
            break;
        case 322:
            request_image("characters", p[1].string());
            request_image("faces", p[3].string());
            request_image("sv_actors", p[5].string());
            break;
        case 323: request_image("characters", p[1].string()); break;
        case 336: {
            const Value& enemy = data::enemies()[p[1].as_int()];
            request_image(data::system()["optSideView"].truthy() ? "sv_enemies" : "enemies", enemy["battlerName"].string());
            break;
        }
        }
    }
}

static int request_images(const Value* list) {
    if (!list || !list->size()) return 0;
    const Value& first = (*list)[0];
    if (first["code"].as_int() == 108 && first["parameters"][0].string().find("<IgnoreEventImageLoading>") != std::string::npos) return 0;
    int count = 0;
    for (size_t i = 0; i < list->size(); i++) {
        const Value& c = (*list)[i];
        if (c["code"].as_int() != 356) continue;
        std::vector<std::string> args = split_spaces(c["parameters"][0].string());
        if (args.empty() || args[0] != "ShowMessage") continue;
        const Value& d = data::lang_entry(args.size() > 1 ? args[1] : "");
        if (d["faceset"].string() != "") {
            request_image("faces", d["faceset"].string());
            count++;
        }
        const Value& extra = d["extraFaces"];
        for (size_t k = 0; k < extra.size(); k++) request_image("faces", extra[k]["faceset"].string());
        count += (int)extra.size();
    }
    std::vector<int> common_list;
    request_list_images(list, common_list);
    return count;
}

void GameInterpreter::setup(const Value* list, int event_id, std::shared_ptr<const void> keep) {
    if (!party_in_battle() && depth_ == 0) {
        GameEvent* ev = event_id ? game_map().event(event_id) : nullptr;
        if (ev && ev->trigger != 4) {
            game_temp().expando.set(js::atom("_canSkipCutscene"), js::Val::boolean(false));
            game_temp().expando.set(js::atom("_skippingCutscene"), js::Val::boolean(false));
            scenes::reset_skip_cutscene_duration();
        }
    }
    clear();
    map_id_ = game_map().map_id();
    event_id_ = event_id;
    auto ov = list_overrides().find(list);
    if (ov != list_overrides().end()) {
        keep_ = ov->second.doc;
        list_ = &ov->second.doc->root();
    } else {
        list_ = list;
        keep_ = std::move(keep);
    }
    requested_image_count = request_images(list_);
}

bool GameInterpreter::is_on_current_map() const { return map_id_ == game_map().map_id(); }

bool GameInterpreter::setup_reserved_common_event() {
    if (game_temp().is_common_event_reserved()) {
        if (game_temp().force_action_queue_list.truthy()) {
            auto doc = js::list_doc(game_temp().force_action_queue_list, "forceActionQueue");
            setup(&doc->root(), 0, doc);
        } else {
            setup(game_temp().reserved_common_event_list(), 0, data::keep());
        }
        game_temp().clear_common_event();
        return true;
    }
    return false;
}

static std::vector<void (*)(GameInterpreter&)>& interpreter_update_hooks() {
    static std::vector<void (*)(GameInterpreter&)> hooks;
    return hooks;
}

bool add_interpreter_update_hook(void (*fn)(GameInterpreter&)) {
    interpreter_update_hooks().push_back(fn);
    return true;
}

void GameInterpreter::update() {
    while (is_running()) {
        if (update_child() || update_wait()) break;
        if (scene_manager::is_scene_changing()) break;
        if (!execute_command()) break;
        if (check_freeze()) break;
    }
    for (auto fn : interpreter_update_hooks()) fn(*this);
}

bool GameInterpreter::update_child() {
    if (child) {
        child->update();
        if (child->is_running()) return true;
        child.reset();
    }
    return false;
}

bool GameInterpreter::update_wait() { return update_wait_count() || update_wait_mode(); }

bool GameInterpreter::update_wait_count() {
    if (wait_count > 0) {
        wait_count--;
        return true;
    }
    return false;
}

bool GameInterpreter::base_update_wait_mode() {
    bool waiting = false;
    GameCharacter* ch = character_.get();
    if (wait_mode == "message") waiting = game_message().is_busy();
    else if (wait_mode == "transfer") waiting = game_player().is_transferring();
    else if (wait_mode == "scroll") waiting = game_map().is_scrolling();
    else if (wait_mode == "route") waiting = ch && ch->is_move_route_forcing();
    else if (wait_mode == "animation") waiting = ch && ch->is_animation_playing();
    else if (wait_mode == "balloon") waiting = ch && ch->is_balloon_playing();
    else if (wait_mode == "gather") waiting = game_player().are_followers_gathering();
    else if (wait_mode == "action") waiting = battle::is_action_forced();
    else if (wait_mode == "video") waiting = scenes::is_movie_playing();
    else if (wait_mode == "image") waiting = false;
    if (!waiting) wait_mode.clear();
    return waiting;
}

bool GameInterpreter::update_wait_mode() {
    if (wait_mode == "fps") {
        mfps_wait--;
        bool waiting = !scene_manager::meet_fps_condition();
        if (!waiting || mfps_wait <= 0) {
            wait_mode.clear();
            mfps_wait = wait_fps_max();
        }
        return waiting;
    }
    if (wait_mode == "se") {
        if (!audio::se_playing()) {
            wait_mode.clear();
        } else {
            return true;
        }
    } else if (wait_mode == "me") {
        if (audio::me_playing()) return true;
        wait_mode.clear();
    }
    bool waiting;
    if (wait_mode == "QTE") {
        waiting = game_switches().value((int)js_number(data::plugin_param("Omori Map QTE", "Active QTE Switch ID", "0")));
    } else if (wait_mode == "nameInput") {
        waiting = scenes::is_input_window_active();
    } else if (wait_mode == "pictureAnimation") {
        GamePicture* pic = game_screen().picture(last_animating_picture);
        if (pic && pic->has_animation()) {
            waiting = true;
        } else {
            last_animating_picture = 0;
            waiting = base_update_wait_mode();
        }
    } else {
        waiting = base_update_wait_mode();
    }
    for (size_t i = 0; i < wait_conditions.size(); i++) {
        bool ok = false;
        js::Val r = run_script(wait_conditions[i].program.get(), wait_conditions[i].source, "WAIT FOR CONDITION ERROR", &ok);
        if (!r.truthy()) {
            waiting = true;
            if (wait_mode.empty()) wait_mode = "condition";
            break;
        }
        wait_conditions.pop_back();
        i--;
    }
    return waiting;
}

bool GameInterpreter::execute_command() {
    const Value* cmd = current_command();
    if (fc::follower_control && cmd) {
        int code = (*cmd)["code"].as_int();
        if (code == 205 || code == 212 || code == 213) {
            const Value& p0 = (*cmd)["parameters"][0];
            if (p0.is_num() && p0.num() < 0 && !fc_retargeted().count(cmd)) fc_retargeted()[cmd] = fc::follower_control_id;
        }
    }
    if (cmd) {
        params_ = (*cmd).find("parameters");
        indent_ = (*cmd)["indent"].as_int();
        if (!command((*cmd)["code"].as_int())) return false;
        index_++;
    } else {
        terminate();
    }
    return true;
}

bool GameInterpreter::check_freeze() {
    if (frame_count_ != frame_count()) {
        frame_count_ = frame_count();
        freeze_checker = 0;
    }
    return freeze_checker++ >= 100000;
}

void GameInterpreter::terminate() {
    if (!party_in_battle() && depth_ == 0) {
        GameEvent* ev = event_id_ ? game_map().event(event_id_) : nullptr;
        if (ev && ev->trigger != 4) {
            game_temp().expando.set(js::atom("_canSkipCutscene"), js::Val());
            game_temp().expando.set(js::atom("_skippingCutscene"), js::Val());
            scenes::reset_skip_cutscene_duration();
        }
    }
    list_ = nullptr;
    keep_.reset();
    comments.clear();
}

void GameInterpreter::skip_branch() {
    while (index_ + 1 < list_size() && command_at(index_ + 1)["indent"].as_int() > indent_) index_++;
}

const Value& GameInterpreter::command_at(int i) const {
    if (!list_ || i < 0 || i >= (int)list_->size()) return Value::null_value();
    return (*list_)[(size_t)i];
}

const Value* GameInterpreter::current_command() const {
    if (!list_ || index_ < 0 || index_ >= (int)list_->size()) return nullptr;
    return &(*list_)[(size_t)index_];
}

int GameInterpreter::next_event_code() const {
    const Value& c = command_at(index_ + 1);
    return c.is_obj() ? c["code"].as_int() : 0;
}

GameCharacter* GameInterpreter::character_id(int param) {
    if (party_in_battle()) return nullptr;
    if (param < 0) return &game_player();
    if (is_on_current_map()) return game_map().event(param > 0 ? param : event_id_);
    return nullptr;
}

GameCharacter* GameInterpreter::character(const Value& param) {
    const Value* cmd = current_command();
    if (cmd && &param == &(*cmd)["parameters"][0]) {
        auto it = fc_retargeted().find(cmd);
        if (it != fc_retargeted().end()) return game_player().followers_.follower(it->second);
    }
    if (param.is_str()) {
        re::Groups g;
        if (re::search("fcF:(\\d+)", "", param.string(), &g)) return game_player().followers_.follower(g.num(1));
        return character_id((int)js_number(param.string()));
    }
    return character_id(param.as_int());
}

js::Val GameInterpreter::operate_value(int operation, int operand_type, double operand) {
    js::Val value = operand_type == 0 ? js::Val::num(operand) : game_variables().value((int)operand);
    if (operation == 0) return value;
    return js::Val::num(-value.to_num());
}

void GameInterpreter::change_hp(GameBattler* target, double value, bool allow_death) {
    if (target->is_alive()) {
        if (!allow_death && target->hp() <= -value) value = 1 - target->hp();
        target->gain_hp(value);
        if (target->is_dead()) target->perform_collapse();
    }
}

void GameInterpreter::setup_child(const Value* list, int event_id, std::shared_ptr<const void> keep) {
    child.reset(new GameInterpreter(depth_ + 1));
    child->setup(list, event_id, std::move(keep));
}

void GameInterpreter::jump_to(int index) {
    int last = index_;
    int start = std::min(index, last), end = std::max(index, last);
    int indent = indent_;
    for (int i = start; i <= end; i++) {
        int ni = command_at(i)["indent"].as_int();
        if (ni != indent) {
            branch[indent] = js::Val::null();
            indent = ni;
        }
    }
    index_ = index;
}

void GameInterpreter::operate_variable(int id, int op, const js::Val& value) {
    js::Val old = game_variables().value(id);
    switch (op) {
    case 0: game_variables().set_value(id, value); break;
    case 1: game_variables().set_value(id, V().binop(JB_ADD, old, value)); break;
    case 2: game_variables().set_value(id, V().binop(JB_SUB, old, value)); break;
    case 3: game_variables().set_value(id, V().binop(JB_MUL, old, value)); break;
    case 4: game_variables().set_value(id, V().binop(JB_DIV, old, value)); break;
    case 5: game_variables().set_value(id, V().binop(JB_MOD, old, value)); break;
    }
}

void GameInterpreter::operate_self_variable(int id, int op, const js::Val& value) {
    std::string key = std::to_string(map_id_) + "," + std::to_string(event_id_) + "," + std::to_string(id);
    js::Val old = game_self_variables().value(key);
    switch (op) {
    case 0: game_self_variables().set_value(key, value); break;
    case 1: game_self_variables().set_value(key, V().binop(JB_ADD, old, value)); break;
    case 2: game_self_variables().set_value(key, V().binop(JB_SUB, old, value)); break;
    case 3: game_self_variables().set_value(key, V().binop(JB_MUL, old, value)); break;
    case 4: game_self_variables().set_value(key, V().binop(JB_DIV, old, value)); break;
    case 5: game_self_variables().set_value(key, V().binop(JB_MOD, old, value)); break;
    }
}

js::Val GameInterpreter::self_variable_value(const js::Val& id) const {
    std::string key = std::to_string(map_id_) + "," + std::to_string(event_id_) + "," + id.to_str();
    return game_self_variables().value(key);
}

js::Val GameInterpreter::game_data_operand(int type, int p1, int p2) {
    switch (type) {
    case 0: return js::Val::num(game_party().num_items(ItemRef::item(p1)));
    case 1: return js::Val::num(game_party().num_items(ItemRef::weapon(p1)));
    case 2: return js::Val::num(game_party().num_items(ItemRef::armor(p1)));
    case 3: {
        GameActor* a = game_actors().actor(p1);
        if (a) {
            switch (p2) {
            case 0: return js::Val::num(a->level);
            case 1: return js::Val::num(a->current_exp());
            case 2: return js::Val::num(a->hp());
            case 3: return js::Val::num(a->mp());
            default:
                if (p2 >= 4 && p2 <= 11) return js::Val::num(a->param(p2 - 4));
            }
        }
        break;
    }
    case 4: {
        auto m = game_troop().unit_members();
        GameBattler* e = p1 >= 0 && p1 < (int)m.size() ? m[(size_t)p1] : nullptr;
        if (e) {
            switch (p2) {
            case 0: return js::Val::num(e->hp());
            case 1: return js::Val::num(e->mp());
            default:
                if (p2 >= 2 && p2 <= 9) return js::Val::num(e->param(p2 - 2));
            }
        }
        break;
    }
    case 5: {
        GameCharacter* c = character_id(p1);
        if (c) {
            switch (p2) {
            case 0: return js::Val::num(c->x_);
            case 1: return js::Val::num(c->y_);
            case 2: return js::Val::num(c->direction());
            case 3: return js::Val::num(c->screen_x());
            case 4: return js::Val::num(c->screen_y());
            }
        }
        break;
    }
    case 6: {
        auto m = game_party().members();
        GameActor* a = p1 >= 0 && p1 < (int)m.size() ? m[(size_t)p1] : nullptr;
        return js::Val::num(a ? a->actor_id() : 0);
    }
    case 7:
        switch (p1) {
        case 0: return js::Val::num(game_map().map_id());
        case 1: return js::Val::num(game_party().size());
        case 2: return js::Val::num(game_party().gold());
        case 3: return js::Val::num(game_party().steps());
        case 4: return js::Val::num(game_system().playtime());
        case 5: return js::Val::num(game_timer().seconds());
        case 6: return js::Val::num(game_system().save_count);
        case 7: return js::Val::num(game_system().battle_count);
        case 8: return js::Val::num(game_system().win_count);
        case 9: return js::Val::num(game_system().escape_count);
        }
        break;
    }
    return js::Val::num(0);
}

js::Val GameInterpreter::run_script(const Value* program, const std::string& source, const char* where, bool* ok) {
    std::shared_ptr<const Value> compiled;
    if (!program || !program->is_arr()) {
        compiled = js::compile_cached(source);
        program = compiled.get();
    }
    if (!program) {
        if (ok) *ok = false;
        script_error(where, "could not compile: " + source);
        return js::Val();
    }
    js::ScopePtr scope = std::make_shared<js::Scope>();
    scope->function_scope = true;
    js::Val result;
    bool good = V().run(*program, to_js(), scope, &result, keep_);
    if (!good) script_error(where, V().last_error);
    if (ok) *ok = good;
    return result;
}

bool GameInterpreter::command(int code) {
    switch (code) {
    case 101: return command101();
    case 102: return command102();
    case 402: return command402();
    case 403: return command403();
    case 103: return command103();
    case 104: return command104();
    case 105: return command105();
    case 108: return command108();
    case 111: return command111();
    case 411: return command411();
    case 112: return true;
    case 413: return command413();
    case 113: return command113();
    case 115: return command115();
    case 117: return command117();
    case 118: return command118();
    case 119: return command119();
    case 121: return command121();
    case 122: return command122();
    case 123: return command123();
    case 124: return command124();
    case 125: return command125();
    case 126: return command126();
    case 127: return command127();
    case 128: return command128();
    case 129: return command129();
    case 132: return command132();
    case 133: return command133();
    case 134: return command134();
    case 135: return command135();
    case 136: return command136();
    case 137: return command137();
    case 138: return command138();
    case 139: return command139();
    case 140: return command140();
    case 201: return command201();
    case 202: return command202();
    case 203: return command203();
    case 204: return command204();
    case 205: return command205();
    case 206: return command206();
    case 211: return command211();
    case 212: return command212();
    case 213: return command213();
    case 214: return command214();
    case 216: return command216();
    case 217: return command217();
    case 221: return command221();
    case 222: return command222();
    case 223: return command223();
    case 224: return command224();
    case 225: return command225();
    case 230: return command230();
    case 231: return command231();
    case 232: return command232();
    case 233: return command233();
    case 234: return command234();
    case 235: return command235();
    case 236: return command236();
    case 241: return command241();
    case 242: return command242();
    case 243: return command243();
    case 244: return command244();
    case 245: return command245();
    case 246: return command246();
    case 249: return command249();
    case 250: return command250();
    case 251: return command251();
    case 261: return command261();
    case 281: return command281();
    case 282: return command282();
    case 283: return command283();
    case 284: return command284();
    case 285: return command285();
    case 301: return command301();
    case 601: return command601();
    case 602: return command602();
    case 603: return command603();
    case 302: return command302();
    case 303: return command303();
    case 311: return command311();
    case 312: return command312();
    case 326: return command326();
    case 313: return command313();
    case 314: return command314();
    case 315: return command315();
    case 316: return command316();
    case 317: return command317();
    case 318: return command318();
    case 319: return command319();
    case 320: return command320();
    case 321: return command321();
    case 322: return command322();
    case 323: return command323();
    case 324: return command324();
    case 325: return command325();
    case 331: return command331();
    case 332: return command332();
    case 342: return command342();
    case 333: return command333();
    case 334: return command334();
    case 335: return command335();
    case 336: return command336();
    case 337: return command337();
    case 339: return command339();
    case 340: return command340();
    case 351: return command351();
    case 352: return command352();
    case 353: return command353();
    case 354: return command354();
    case 355: return command355();
    case 356: return command356();
    case 357: return command357();
    default: return true;
    }
}

bool GameInterpreter::is_continue_message_string() const {
    if (next_event_code() == 101 && game_system().message_rows() > 4) return true;
    return next_event_code() == 401;
}

bool GameInterpreter::command101() {
    if (!game_message().is_busy()) {
        game_message().set_face_image(Ps(params_, 0), Pi(params_, 1));
        game_message().background = Pi(params_, 2);
        game_message().position_type = Pi(params_, 3);
        while (is_continue_message_string()) {
            index_++;
            if (command_at(index_)["code"].as_int() == 401) game_message().add_text(command_at(index_)["parameters"][0].string());
            if ((int)game_message().texts.size() >= game_system().message_rows()) break;
        }
        switch (next_event_code()) {
        case 102:
            index_++;
            setup_choices(command_at(index_)["parameters"]);
            break;
        case 103:
            index_++;
            setup_num_input(command_at(index_)["parameters"]);
            break;
        case 104:
            index_++;
            setup_item_choice(command_at(index_)["parameters"]);
            break;
        }
        index_++;
        set_wait_mode("message");
    }
    return false;
}

bool GameInterpreter::command102() {
    if (!game_message().is_busy()) {
        setup_choices(*params_);
        index_++;
        set_wait_mode("message");
    }
    return false;
}

static std::shared_ptr<Document> extended_choices_list(const Value* list, int current, int indent, bool& changed) {
    js::Val l = js::value_to_js(*list);
    js::Vm& vm = V();
    auto cmd = [&](int i) { return i >= 0 && i < (int)l.o->items.size() ? l.o->items[(size_t)i] : js::Val(); };
    auto field = [&](const js::Val& o, const char* k) { return vm.get(o, js::atom(k)); };
    auto param = [&](const js::Val& c, int k) { return vm.get(field(c, "parameters"), js::Val::num(k)); };
    auto set_param = [&](const js::Val& c, int k, const js::Val& v) { vm.put(field(c, "parameters"), js::Val::num(k), v); };
    int index = current + 1;
    double total = 0;
    while (index < (int)l.o->items.size()) {
        js::Val c = cmd(index);
        if (field(c, "indent").to_num() == indent) {
            int code = field(c, "code").to_int();
            if (code == 404 && field(cmd(index + 1), "code").to_int() != 102) {
                break;
            } else if (code == 102) {
                js::Val cur = cmd(current);
                if (param(c, 2).to_num() >= 0) set_param(cur, 2, js::Val::num(param(c, 2).to_num() + total));
                if (param(c, 1).to_num() >= 0) set_param(cur, 1, js::Val::num(param(c, 1).to_num() + total));
                else if (param(c, 1).to_num() == -2) set_param(cur, 1, param(c, 1));
                js::Val extra = param(c, 0);
                js::Val dest = param(cur, 0);
                if (extra.is_array() && dest.is_array()) {
                    for (auto& ch : extra.o->items) dest.o->items.push_back(ch);
                }
                auto& items = l.o->items;
                if (index - 1 >= 0 && index - 1 < (int)items.size()) items.erase(items.begin() + (index - 1), items.begin() + std::min((int)items.size(), index + 1));
                index -= 2;
                changed = true;
            } else if (code == 402) {
                if (param(c, 0).to_num() != total) changed = true;
                set_param(c, 0, js::Val::num(total));
                total += 1;
            }
        }
        index += 1;
    }
    if (!changed) return nullptr;
    return js::list_doc(l, "choices");
}

void GameInterpreter::setup_choices(const Value& params0) {
    const Value* params = &params0;
    if (list_ && current_command() && &params0 == &(*current_command())["parameters"]) {
        bool changed = false;
        auto doc = extended_choices_list(list_, index_, indent_, changed);
        if (doc) {
            list_overrides()[list_] = ListOverride{doc};
            keep_ = doc;
            list_ = &doc->root();
            params = &(*current_command())["parameters"];
        }
    }
    std::vector<std::string> choices;
    const Value& cv = (*params)[0];
    for (size_t i = 0; i < cv.size(); i++) choices.push_back(cv[i].string());
    std::string first = choices.empty() ? std::string() : choices[0];
    js::Val jukebox = game_system().expando.find(js::atom("_jukeboxList")) ? *game_system().expando.find(js::atom("_jukeboxList")) : js::Val();
    bool jukebox_hit = first == "JukeboxList";
    if (!jukebox_hit && jukebox.is_array()) {
        for (auto& j : jukebox.o->items) jukebox_hit = jukebox_hit || js::loose_equals(j, js::Val::str(first));
    }
    if (jukebox_hit) {
        choices.clear();
        bool has_nevermind = false;
        if (jukebox.is_array()) {
            for (auto& j : jukebox.o->items) {
                choices.push_back(j.to_str());
                if (j.is_array() && j.o->items.size() == 2 && j.o->items[0].to_str() == "farawaytown_extras_pizzaminigame.message_17") has_nevermind = true;
            }
        }
        (void)has_nevermind;
        choices.push_back("farawaytown_extras_pizzaminigame.message_17,farawaytown_extras_pizzaminigame.message_17");
    }
    js::Val* washing = game_system().expando.find(js::atom("_washingDishes"));
    if (washing && washing->truthy()) {
        js::Val* dishes = game_system().expando.find(js::atom("_dishList"));
        choices.clear();
        if (dishes && dishes->is_array()) {
            for (auto& d : dishes->o->items) choices.push_back(d.to_str());
        }
    }
    int cancel_type = (*params)[1].as_int();
    int default_type = params->size() > 2 ? (*params)[2].as_int() : 0;
    int position_type = params->size() > 3 ? (*params)[3].as_int() : 2;
    int background = params->size() > 4 ? (*params)[4].as_int() : 0;
    if (game_switches().value(804)) {
        if (first == "Hardware Shelf") {
            int shelf_num = (int)game_variables().number(812);
            js::Val* shelves = game_system().expando.find(js::atom("shelves"));
            std::vector<std::string> products;
            if (shelves && shelves->is_array()) {
                js::Val shelf = V().get(*shelves, js::Val::num(shelf_num));
                if (shelf.is_array()) {
                    for (auto& v : shelf.o->items) {
                        double n = v.to_num();
                        if (n == -1) continue;
                        const char* key = n == 0 ? "message_3" : n == 10 ? "message_4" : n == 3 ? "message_5" : n == 4 ? "message_6" : n == 6 ? "message_7" : n == 7 ? "message_8" : nullptr;
                        if (key) products.push_back(data::lang_text(std::string("farawaytown_extras_hardwareminigame.") + key));
                    }
                }
            }
            products.push_back(data::lang_text("farawaytown_extras_pizzaminigame.message_17"));
            choices = products;
        }
        game_message().set_choices(choices, default_type, cancel_type);
        game_message().choice_background = background;
        game_message().choice_position_type = position_type;
        int indent = indent_;
        game_message().choice_callback = [this, indent](int n) { branch[indent] = js::Val::num(n); };
        return;
    }
    setup_choices_list(choices, cancel_type, default_type, position_type, background);
}

void GameInterpreter::setup_choices_list(std::vector<std::string> choices, int cancel_type, int default_type, int position_type, int background) {
    if (cancel_type >= (int)choices.size()) cancel_type = -2;
    game_message().set_choices(choices, default_type, cancel_type);
    game_message().choice_background = background;
    game_message().choice_position_type = position_type;
    int indent = indent_;
    game_message().choice_callback = [this, indent](int n) { branch[indent] = js::Val::num(n); };
}

bool GameInterpreter::command402() {
    if (!js::strict_equals(branch[indent_], Pv(params_, 0))) skip_branch();
    return true;
}

bool GameInterpreter::command403() {
    const js::Val& b = branch[indent_];
    if (b.is_num() && b.n >= 0) skip_branch();
    else if (!b.is_num() && !b.is_undef() && b.to_num() >= 0) skip_branch();
    return true;
}

bool GameInterpreter::command103() {
    if (!game_message().is_busy()) {
        setup_num_input(*params_);
        index_++;
        set_wait_mode("message");
    }
    return false;
}

void GameInterpreter::setup_num_input(const Value& params) { game_message().set_number_input(params[0].as_int(), params[1].as_int()); }

bool GameInterpreter::command104() {
    if (!game_message().is_busy()) {
        setup_item_choice(*params_);
        index_++;
        set_wait_mode("message");
    }
    return false;
}

void GameInterpreter::setup_item_choice(const Value& params) {
    int t = params[1].as_int();
    game_message().set_item_choice(params[0].as_int(), t ? t : 2);
}

bool GameInterpreter::command105() {
    if (!game_message().is_busy()) {
        game_message().set_scroll(Pi(params_, 0), P(params_, 1).truthy());
        while (next_event_code() == 405) {
            index_++;
            game_message().add(command_at(index_)["parameters"][0].string());
        }
        index_++;
        set_wait_mode("message");
    }
    return false;
}

bool GameInterpreter::command108() {
    comments.clear();
    comments.push_back(Ps(params_, 0));
    while (next_event_code() == 408) {
        index_++;
        comments.push_back(command_at(index_)["parameters"][0].string());
    }
    return true;
}

static js::Val compare(int op, const js::Val& a, const js::Val& b) {
    js::Vm& vm = V();
    switch (op) {
    case 0: return vm.binop(JB_SEQ, a, b);
    case 1: return vm.binop(JB_GE, a, b);
    case 2: return vm.binop(JB_LE, a, b);
    case 3: return vm.binop(JB_GT, a, b);
    case 4: return vm.binop(JB_LT, a, b);
    case 5: return vm.binop(JB_SNE, a, b);
    }
    return js::Val::boolean(false);
}

bool GameInterpreter::command111() {
    bool result = false;
    switch (Pi(params_, 0)) {
    case 0:
        result = Pi(params_, 2) == 0 ? game_switches().value(Pi(params_, 1)) : !game_switches().value(Pi(params_, 1));
        break;
    case 1: {
        js::Val v1 = game_variables().value(Pi(params_, 1));
        js::Val v2 = Pi(params_, 2) == 0 ? Pv(params_, 3) : game_variables().value(Pi(params_, 3));
        result = compare(Pi(params_, 4), v1, v2).truthy();
        break;
    }
    case 2:
        if (event_id_ > 0) {
            bool v = game_self_switches().value(map_id_, event_id_, Ps(params_, 1));
            result = Pi(params_, 2) == 0 ? v : !v;
        }
        break;
    case 3:
        if (game_timer().is_working()) {
            if (Pi(params_, 2) == 0) result = game_timer().seconds() >= Pn(params_, 1);
            else result = game_timer().seconds() <= Pn(params_, 1);
        }
        break;
    case 4: {
        GameActor* actor = game_actors().actor(Pi(params_, 1));
        if (actor) {
            const Value& n = P(params_, 3);
            switch (Pi(params_, 2)) {
            case 0: {
                auto m = game_party().members();
                result = std::find(m.begin(), m.end(), actor) != m.end();
                break;
            }
            case 1: result = n.is_str() && actor->name() == n.string(); break;
            case 2: result = actor->class_id == n.as_int() && data::classes()[n.as_int()].is_obj(); break;
            case 3: result = actor->has_skill(n.as_int()); break;
            case 4: result = actor->has_weapon(ItemRef::weapon(n.as_int())); break;
            case 5: result = actor->has_armor(ItemRef::armor(n.as_int())); break;
            case 6: result = actor->is_state_affected(n.as_int()); break;
            }
        }
        break;
    }
    case 5: {
        auto m = game_troop().unit_members();
        int idx = Pi(params_, 1);
        GameBattler* enemy = idx >= 0 && idx < (int)m.size() ? m[(size_t)idx] : nullptr;
        if (enemy) {
            if (Pi(params_, 2) == 0) result = enemy->is_alive();
            else if (Pi(params_, 2) == 1) result = enemy->is_state_affected(Pi(params_, 3));
        }
        break;
    }
    case 6: {
        GameCharacter* c = character(P(params_, 1));
        if (c) result = c->direction() == Pi(params_, 2);
        break;
    }
    case 7:
        switch (Pi(params_, 2)) {
        case 0: result = game_party().gold() >= Pn(params_, 1); break;
        case 1: result = game_party().gold() <= Pn(params_, 1); break;
        case 2: result = game_party().gold() < Pn(params_, 1); break;
        }
        break;
    case 8: result = game_party().has_item(ItemRef::item(Pi(params_, 1)), false); break;
    case 9: result = game_party().has_item(ItemRef::weapon(Pi(params_, 1)), P(params_, 2).truthy()); break;
    case 10: result = game_party().has_item(ItemRef::armor(Pi(params_, 1)), P(params_, 2).truthy()); break;
    case 11: result = input::is_pressed(input::key_of(Ps(params_, 1))); break;
    case 12: {
        bool ok = false;
        js::Val r = run_script(&P(params_, 2), Ps(params_, 1), "CONDITIONAL BRANCH SCRIPT ERROR", &ok);
        result = ok && r.truthy();
        break;
    }
    case 13: {
        GameVehicle* v = game_map().vehicle(Ps(params_, 1));
        result = game_player().vehicle() == v;
        if (!v && P(params_, 1).is_num()) {
            int i = Pi(params_, 1);
            GameVehicle* vv = i == 0 ? game_map().boat() : i == 1 ? game_map().ship() : i == 2 ? game_map().airship() : nullptr;
            result = game_player().vehicle() == vv;
        }
        break;
    }
    case 14: {
        std::string base = std::to_string(map_id_) + "," + std::to_string(event_id_) + ",";
        js::Val v1 = game_self_variables().value(base + Pv(params_, 1).to_str());
        js::Val v2;
        if (Pi(params_, 2) == 0) v2 = Pv(params_, 3);
        else if (Pi(params_, 2) == 1) v2 = game_self_variables().value(base + Pv(params_, 3).to_str());
        else v2 = game_variables().value(Pi(params_, 3));
        result = compare(Pi(params_, 4), v1, v2).truthy();
        break;
    }
    }
    branch[indent_] = js::Val::boolean(result);
    if (!result) skip_branch();
    return true;
}

bool GameInterpreter::command411() {
    const js::Val& b = branch[indent_];
    if (!(b.is_bool() && !b.b)) skip_branch();
    return true;
}

bool GameInterpreter::command413() {
    do {
        index_--;
    } while (index_ > 0 && command_at(index_)["indent"].as_int() != indent_);
    return true;
}

bool GameInterpreter::command113() {
    int depth = 0;
    while (index_ < list_size() - 1) {
        index_++;
        int code = command_at(index_)["code"].as_int();
        if (code == 112) depth++;
        if (code == 413) {
            if (depth > 0) depth--;
            else break;
        }
    }
    return true;
}

bool GameInterpreter::command115() {
    index_ = list_size();
    return true;
}

bool GameInterpreter::command117() {
    const Value& ce = data::common_events()[Pi(params_, 0)];
    if (ce.is_obj()) {
        int eid = is_on_current_map() ? event_id_ : 0;
        setup_child(&ce["list"], eid, data::keep());
    }
    return true;
}

bool GameInterpreter::command118() {
    std::string label = Ps(params_, 0);
    if (re::test("<Enable Skip>", label)) {
        game_temp().expando.set(js::atom("_canSkipCutscene"), js::Val::boolean(true));
        game_temp().expando.set(js::atom("_skippingCutscene"), js::Val::boolean(false));
    } else if (re::test("<Disable Skip>", label) || re::test("<Skip Target>", label)) {
        game_temp().expando.set(js::atom("_canSkipCutscene"), js::Val::boolean(false));
        game_temp().expando.set(js::atom("_skippingCutscene"), js::Val::boolean(false));
    }
    return true;
}

bool GameInterpreter::command119() {
    std::string label = Ps(params_, 0);
    for (int i = 0; i < list_size(); i++) {
        const Value& c = command_at(i);
        if (c["code"].as_int() == 118 && c["parameters"][0].string() == label) {
            jump_to(i);
            return false;
        }
    }
    return true;
}

bool GameInterpreter::command121() {
    for (int i = Pi(params_, 0); i <= Pi(params_, 1); i++) game_switches().set_value(i, Pi(params_, 2) == 0);
    return true;
}

bool GameInterpreter::command122() {
    js::Val value = js::Val::num(0);
    switch (Pi(params_, 3)) {
    case 0: value = Pv(params_, 4); break;
    case 1: value = game_variables().value(Pi(params_, 4)); break;
    case 2: {
        double range = Pn(params_, 5) - Pn(params_, 4) + 1;
        for (int i = Pi(params_, 0); i <= Pi(params_, 1); i++) operate_variable(i, Pi(params_, 2), js::Val::num(Pn(params_, 4) + random_int(range)));
        return true;
    }
    case 3: value = game_data_operand(Pi(params_, 4), Pi(params_, 5), Pi(params_, 6)); break;
    case 4: {
        bool ok = false;
        js::Val r = run_script(&P(params_, 5), Ps(params_, 4), "CONTROL VARIABLE SCRIPT ERROR", &ok);
        value = ok ? r : js::Val::num(0);
        break;
    }
    }
    for (int i = Pi(params_, 0); i <= Pi(params_, 1); i++) operate_variable(i, Pi(params_, 2), value);
    return true;
}

bool GameInterpreter::command357() {
    js::Val value = js::Val::num(0);
    switch (Pi(params_, 3)) {
    case 0: value = Pv(params_, 4); break;
    case 1: value = game_variables().value(Pi(params_, 4)); break;
    case 2: {
        double range = Pn(params_, 5) - Pn(params_, 4) + 1;
        for (int i = Pi(params_, 0); i <= Pi(params_, 1); i++) operate_self_variable(i, Pi(params_, 2), js::Val::num(Pn(params_, 4) + random_int(range)));
        return true;
    }
    case 3: value = game_data_operand(Pi(params_, 4), Pi(params_, 5), Pi(params_, 6)); break;
    case 4: {
        bool ok = false;
        value = run_script(&P(params_, 5), Ps(params_, 4), "CONTROL SELF VARIABLE SCRIPT ERROR", &ok);
        break;
    }
    case 5: value = self_variable_value(Pv(params_, 4)); break;
    }
    for (int i = Pi(params_, 0); i <= Pi(params_, 1); i++) operate_self_variable(i, Pi(params_, 2), value);
    return true;
}

bool GameInterpreter::command123() {
    if (event_id_ > 0) game_self_switches().set_value(map_id_, event_id_, Ps(params_, 0), Pi(params_, 1) == 0);
    return true;
}

bool GameInterpreter::command124() {
    if (Pi(params_, 0) == 0) game_timer().start(Pi(params_, 1) * 60);
    else game_timer().stop();
    return true;
}

bool GameInterpreter::command125() {
    game_party().gain_gold(operate_value(Pi(params_, 0), Pi(params_, 1), Pn(params_, 2)).to_num());
    return true;
}

bool GameInterpreter::command126() {
    game_party().gain_item(ItemRef::item(Pi(params_, 0)), operate_value(Pi(params_, 1), Pi(params_, 2), Pn(params_, 3)).to_num());
    return true;
}

bool GameInterpreter::command127() {
    game_party().gain_item(ItemRef::weapon(Pi(params_, 0)), operate_value(Pi(params_, 1), Pi(params_, 2), Pn(params_, 3)).to_num(), P(params_, 4).truthy());
    return true;
}

bool GameInterpreter::command128() {
    game_party().gain_item(ItemRef::armor(Pi(params_, 0)), operate_value(Pi(params_, 1), Pi(params_, 2), Pn(params_, 3)).to_num(), P(params_, 4).truthy());
    return true;
}

bool GameInterpreter::command129() {
    GameActor* actor = game_actors().actor(Pi(params_, 0));
    if (actor) {
        if (Pi(params_, 1) == 0) {
            if (P(params_, 2).truthy()) actor->setup(Pi(params_, 0));
            game_party().add_actor(Pi(params_, 0));
        } else {
            game_party().remove_actor(Pi(params_, 0));
        }
    }
    return true;
}

bool GameInterpreter::command132() {
    game_system().battle_bgm_ = AudioObj::from(P(params_, 0));
    return true;
}

bool GameInterpreter::command133() {
    game_system().victory_me_ = AudioObj::from(P(params_, 0));
    return true;
}

bool GameInterpreter::command134() {
    game_system().save_enabled = Pi(params_, 0) != 0;
    return true;
}

bool GameInterpreter::command135() {
    game_system().menu_enabled = Pi(params_, 0) != 0;
    return true;
}

bool GameInterpreter::command136() {
    game_system().encounter_enabled = Pi(params_, 0) != 0;
    game_player().make_encounter_count();
    return true;
}

bool GameInterpreter::command137() {
    game_system().formation_enabled = Pi(params_, 0) != 0;
    return true;
}

bool GameInterpreter::command138() {
    const Value& t = P(params_, 0);
    game_system().has_window_tone = true;
    for (int i = 0; i < 4; i++) game_system().window_tone_[i] = t[i].num();
    return true;
}

bool GameInterpreter::command139() {
    game_system().defeat_me_ = AudioObj::from(P(params_, 0));
    return true;
}

static GameVehicle* vehicle_of(const Value& v) {
    if (v.is_str()) return game_map().vehicle(v.string());
    int i = v.as_int();
    return i == 0 ? game_map().boat() : i == 1 ? game_map().ship() : i == 2 ? game_map().airship() : nullptr;
}

bool GameInterpreter::command140() {
    GameVehicle* v = vehicle_of(P(params_, 0));
    if (v) v->bgm = AudioObj::from(P(params_, 1));
    return true;
}

bool GameInterpreter::command201() {
    if (!party_in_battle() && !game_message().is_busy()) {
        double map_id, x, y;
        if (Pi(params_, 0) == 0) {
            map_id = Pn(params_, 1);
            x = Pn(params_, 2);
            y = Pn(params_, 3);
        } else {
            map_id = game_variables().number(Pi(params_, 1));
            x = game_variables().number(Pi(params_, 2));
            y = game_variables().number(Pi(params_, 3));
        }
        game_player().reserve_transfer((int)map_id, x, y, Pi(params_, 4), Pi(params_, 5));
        set_wait_mode("transfer");
        index_++;
    }
    return false;
}

bool GameInterpreter::command202() {
    double map_id, x, y;
    if (Pi(params_, 1) == 0) {
        map_id = Pn(params_, 2);
        x = Pn(params_, 3);
        y = Pn(params_, 4);
    } else {
        map_id = game_variables().number(Pi(params_, 2));
        x = game_variables().number(Pi(params_, 3));
        y = game_variables().number(Pi(params_, 4));
    }
    GameVehicle* v = vehicle_of(P(params_, 0));
    if (v) v->set_location((int)map_id, x, y);
    return true;
}

bool GameInterpreter::command203() {
    game_temp().bypass_load_location = true;
    GameCharacter* c = character(P(params_, 0));
    if (c) {
        if (Pi(params_, 1) == 0) {
            c->locate(Pn(params_, 2), Pn(params_, 3));
        } else if (Pi(params_, 1) == 1) {
            c->locate(game_variables().number(Pi(params_, 2)), game_variables().number(Pi(params_, 3)));
        } else {
            GameCharacter* c2 = character(P(params_, 2));
            if (c2) c->swap(*c2);
        }
        if (Pi(params_, 4) > 0) c->set_direction(Pi(params_, 4));
    }
    game_temp().bypass_load_location = false;
    return true;
}

bool GameInterpreter::command204() {
    if (!party_in_battle()) {
        if (game_map().is_scrolling()) {
            set_wait_mode("scroll");
            return false;
        }
        game_map().start_scroll(Pi(params_, 0), Pn(params_, 1), Pn(params_, 2));
    }
    return true;
}

bool GameInterpreter::command205() {
    game_map().refresh_if_needed();
    GameCharacter* c = character(P(params_, 0));
    character_ = CharRef::of(c);
    if (c) {
        const Value& route = P(params_, 1);
        c->force_move_route(route_shared(route, keep_));
        if (route["wait"].truthy()) set_wait_mode("route");
    }
    return true;
}

bool GameInterpreter::command206() {
    game_player().get_on_off_vehicle();
    return true;
}

bool GameInterpreter::command211() {
    game_player().set_transparent(Pi(params_, 0) == 0);
    return true;
}

bool GameInterpreter::command212() {
    GameCharacter* c = character(P(params_, 0));
    character_ = CharRef::of(c);
    if (c) {
        c->request_animation(Pn(params_, 1));
        if (P(params_, 2).truthy()) set_wait_mode("animation");
    }
    return true;
}

bool GameInterpreter::command213() {
    GameCharacter* c = character(P(params_, 0));
    character_ = CharRef::of(c);
    if (c) {
        c->request_balloon(Pi(params_, 1));
        if (P(params_, 2).truthy()) set_wait_mode("balloon");
    }
    return true;
}

bool GameInterpreter::command214() {
    if (is_on_current_map() && event_id_ > 0) game_map().erase_event(event_id_);
    return true;
}

bool GameInterpreter::command216() {
    if (Pi(params_, 0) == 0) game_player().show_followers();
    else game_player().hide_followers();
    game_player().refresh();
    return true;
}

bool GameInterpreter::command217() {
    if (!party_in_battle()) {
        game_player().gather_followers();
        set_wait_mode("gather");
    }
    return true;
}

bool GameInterpreter::command221() {
    if (!game_message().is_busy()) {
        game_screen().start_fade_out(fade_speed());
        wait(fade_speed());
        index_++;
    }
    return false;
}

bool GameInterpreter::command222() {
    if (!game_message().is_busy()) {
        game_screen().start_fade_in(fade_speed());
        wait(fade_speed());
        index_++;
    }
    return false;
}

bool GameInterpreter::command223() {
    double t[4];
    tone_of(P(params_, 0), t);
    game_screen().start_tint(t, Pi(params_, 1));
    if (P(params_, 2).truthy()) wait(Pi(params_, 1));
    return true;
}

bool GameInterpreter::command224() {
    double c[4];
    tone_of(P(params_, 0), c);
    game_screen().start_flash(c, Pi(params_, 1));
    if (P(params_, 2).truthy()) wait(Pi(params_, 1));
    return true;
}

bool GameInterpreter::command225() {
    game_screen().start_shake(Pn(params_, 0), Pn(params_, 1), Pi(params_, 2));
    if (P(params_, 3).truthy()) wait(Pi(params_, 2));
    return true;
}

bool GameInterpreter::command230() {
    wait(Pi(params_, 0));
    return true;
}


bool GameInterpreter::command231() {
    double x, y;
    if (Pi(params_, 3) == 0) {
        x = Pn(params_, 4);
        y = Pn(params_, 5);
    } else {
        x = game_variables().number(Pi(params_, 4));
        y = game_variables().number(Pi(params_, 5));
    }
    game_screen().show_picture(Pi(params_, 0), localized_picture(Ps(params_, 1)), Pi(params_, 2), x, y, Pn(params_, 6), Pn(params_, 7), Pn(params_, 8),
                               Pi(params_, 9));
    return true;
}

bool GameInterpreter::command232() {
    double x, y;
    if (Pi(params_, 3) == 0) {
        x = Pn(params_, 4);
        y = Pn(params_, 5);
    } else {
        x = game_variables().number(Pi(params_, 4));
        y = game_variables().number(Pi(params_, 5));
    }
    game_screen().move_picture(Pi(params_, 0), Pi(params_, 2), x, y, Pn(params_, 6), Pn(params_, 7), Pn(params_, 8), Pi(params_, 9), Pi(params_, 10));
    if (P(params_, 11).truthy()) wait(Pi(params_, 10));
    return true;
}

bool GameInterpreter::command233() {
    game_screen().rotate_picture(Pi(params_, 0), Pn(params_, 1));
    return true;
}

bool GameInterpreter::command234() {
    double t[4];
    tone_of(P(params_, 1), t);
    game_screen().tint_picture(Pi(params_, 0), t, Pi(params_, 2));
    if (P(params_, 3).truthy()) wait(Pi(params_, 2));
    return true;
}

bool GameInterpreter::command235() {
    game_screen().erase_picture(Pi(params_, 0));
    return true;
}

bool GameInterpreter::command236() {
    if (!party_in_battle()) {
        game_screen().change_weather(Ps(params_, 0), Pn(params_, 1), Pi(params_, 2));
        if (P(params_, 3).truthy()) wait(Pi(params_, 2));
    }
    return true;
}

bool GameInterpreter::command241() {
    audio_manager::play_bgm(AudioObj::from(P(params_, 0)));
    if (Pn(params_, 1) > 0) audio_manager::fade_in_bgm(Pn(params_, 1));
    return true;
}

bool GameInterpreter::command242() {
    audio_manager::fade_out_bgm(Pn(params_, 0));
    return true;
}

bool GameInterpreter::command243() {
    game_system().save_bgm();
    return true;
}

bool GameInterpreter::command244() {
    game_system().replay_bgm();
    return true;
}

bool GameInterpreter::command245() {
    audio_manager::play_bgs(AudioObj::from(P(params_, 0)));
    return true;
}

bool GameInterpreter::command246() {
    audio_manager::fade_out_bgs(Pn(params_, 0));
    return true;
}

bool GameInterpreter::command249() {
    audio_manager::play_me(AudioObj::from(P(params_, 0)));
    return true;
}

bool GameInterpreter::command250() {
    audio_manager::play_se(AudioObj::from(P(params_, 0)));
    return true;
}

bool GameInterpreter::command251() {
    audio_manager::stop_se();
    return true;
}

bool GameInterpreter::command261() {
    if (!game_message().is_busy()) {
        std::string name = Ps(params_, 0);
        if (!name.empty()) {
            scenes::play_movie(name);
            set_wait_mode("video");
        }
        index_++;
    }
    return false;
}

bool GameInterpreter::command281() {
    if (Pi(params_, 0) == 0) game_map().enable_name_display();
    else game_map().disable_name_display();
    return true;
}

bool GameInterpreter::command282() {
    game_map().change_tileset(Pi(params_, 0));
    return true;
}

bool GameInterpreter::command283() {
    game_map().change_battleback(Ps(params_, 0), Ps(params_, 1));
    return true;
}

bool GameInterpreter::command284() {
    game_map().change_parallax(Ps(params_, 0), P(params_, 1).truthy(), P(params_, 2).truthy(), Pn(params_, 3), Pn(params_, 4));
    return true;
}

bool GameInterpreter::command285() {
    double x, y;
    if (Pi(params_, 2) == 0) {
        x = Pn(params_, 3);
        y = Pn(params_, 4);
    } else {
        x = game_variables().number(Pi(params_, 3));
        y = game_variables().number(Pi(params_, 4));
    }
    double value;
    switch (Pi(params_, 1)) {
    case 0: value = game_map().terrain_tag(x, y); break;
    case 1: value = game_map().event_id_xy(x, y); break;
    case 2:
    case 3:
    case 4:
    case 5: value = game_map().tile_id(x, y, Pi(params_, 1) - 2); break;
    default: value = game_map().region_id(x, y); break;
    }
    game_variables().set_value(Pi(params_, 0), value < 0 && Pi(params_, 1) >= 6 ? js::Val() : js::Val::num(value));
    return true;
}

bool GameInterpreter::command301() {
    if (!party_in_battle()) {
        int troop_id;
        if (Pi(params_, 0) == 0) troop_id = Pi(params_, 1);
        else if (Pi(params_, 0) == 1) troop_id = (int)game_variables().number(Pi(params_, 1));
        else troop_id = game_player().make_encounter_troop_id();
        if (data::troops()[troop_id].is_obj()) {
            battle::setup(troop_id, P(params_, 2).truthy(), P(params_, 3).truthy());
            int indent = indent_;
            battle::set_event_callback([this, indent](int n) { branch[indent] = js::Val::num(n); });
            game_player().make_encounter_count();
            scenes::push_battle();
        }
    }
    return true;
}

bool GameInterpreter::command601() {
    if (!js::strict_equals(branch[indent_], js::Val::num(0))) skip_branch();
    return true;
}

bool GameInterpreter::command602() {
    if (!js::strict_equals(branch[indent_], js::Val::num(1))) skip_branch();
    return true;
}

bool GameInterpreter::command603() {
    if (!js::strict_equals(branch[indent_], js::Val::num(2))) skip_branch();
    return true;
}

bool GameInterpreter::command302() {
    if (!party_in_battle()) {
        Node* goods = node_arr();
        node_push(goods, js::value_to_node(*params_));
        while (next_event_code() == 605) {
            index_++;
            node_push(goods, js::value_to_node(command_at(index_)["parameters"]));
        }
        auto doc = js::program_doc(goods);
        node_free(goods);
        js::Val shop = js::make_object();
        js::Val* existing = game_temp().expando.find(js::atom("_shopData"));
        if (existing && existing->is_obj()) shop = *existing;
        else game_temp().expando.set(js::atom("_shopData"), shop);
        V().put(shop, js::atom("goods"), js::value_to_js(doc->root()));
        scenes::snap_for_shop();
        scenes::push_shop(doc->root(), doc, P(params_, 4).truthy());
    }
    return true;
}

bool GameInterpreter::command303() {
    if (!party_in_battle()) {
        if (data::actors()[Pi(params_, 0)].is_obj()) scenes::push_name_input(Pi(params_, 0), Pi(params_, 1));
    }
    return true;
}

template <class F>
static void iterate_actor_id(int param, F fn) {
    if (param == 0) {
        for (GameActor* a : game_party().members()) fn(a);
    } else {
        GameActor* a = game_actors().actor(param);
        if (a) fn(a);
    }
}

template <class F>
static void iterate_actor_ex(int p1, int p2, F fn) {
    if (p1 == 0) iterate_actor_id(p2, fn);
    else iterate_actor_id((int)game_variables().number(p2), fn);
}

template <class F>
static void iterate_enemy_index(int param, F fn) {
    auto m = game_troop().unit_members();
    if (param < 0) {
        for (GameBattler* e : m) fn(e);
    } else if (param < (int)m.size() && m[(size_t)param]) {
        fn(m[(size_t)param]);
    }
}

bool GameInterpreter::command311() {
    double value = operate_value(Pi(params_, 2), Pi(params_, 3), Pn(params_, 4)).to_num();
    bool allow = P(params_, 5).truthy();
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) { change_hp(a, value, allow); });
    return true;
}

bool GameInterpreter::command312() {
    double value = operate_value(Pi(params_, 2), Pi(params_, 3), Pn(params_, 4)).to_num();
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) { a->gain_mp(value); });
    return true;
}

bool GameInterpreter::command326() {
    double value = operate_value(Pi(params_, 2), Pi(params_, 3), Pn(params_, 4)).to_num();
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) { a->gain_tp(value); });
    return true;
}

bool GameInterpreter::command313() {
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) {
        bool already = a->is_dead();
        if (Pi(params_, 2) == 0) a->add_state(Pi(params_, 3));
        else a->remove_state(Pi(params_, 3));
        if (a->is_dead() && !already) a->perform_collapse();
        a->clear_result();
    });
    return true;
}

bool GameInterpreter::command314() {
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) { a->recover_all(); });
    return true;
}

bool GameInterpreter::command315() {
    double value = operate_value(Pi(params_, 2), Pi(params_, 3), Pn(params_, 4)).to_num();
    bool show = P(params_, 5).truthy();
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) { a->change_exp(a->current_exp() + value, show); });
    return true;
}

bool GameInterpreter::command316() {
    double value = operate_value(Pi(params_, 2), Pi(params_, 3), Pn(params_, 4)).to_num();
    bool show = P(params_, 5).truthy();
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) { a->change_level((int)(a->level + value), show); });
    return true;
}

bool GameInterpreter::command317() {
    double value = operate_value(Pi(params_, 3), Pi(params_, 4), Pn(params_, 5)).to_num();
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) { a->add_param(Pi(params_, 2), value); });
    return true;
}

bool GameInterpreter::command318() {
    iterate_actor_ex(Pi(params_, 0), Pi(params_, 1), [&](GameActor* a) {
        if (Pi(params_, 2) == 0) a->learn_skill(Pi(params_, 3));
        else a->forget_skill(Pi(params_, 3));
    });
    return true;
}

bool GameInterpreter::command319() {
    GameActor* a = game_actors().actor(Pi(params_, 0));
    if (!a) return true;
    std::vector<int> slots = a->equip_slots();
    auto it = std::find(slots.begin(), slots.end(), Pi(params_, 1));
    int index = (it == slots.end() ? -1 : (int)(it - slots.begin())) + 1;
    a->change_equip_by_id(index, Pi(params_, 2));
    return true;
}

bool GameInterpreter::command320() {
    GameActor* a = game_actors().actor(Pi(params_, 0));
    if (a) a->set_name(Ps(params_, 1));
    return true;
}

bool GameInterpreter::command321() {
    GameActor* a = game_actors().actor(Pi(params_, 0));
    if (a && data::classes()[Pi(params_, 1)].is_obj()) a->change_class(Pi(params_, 1), P(params_, 2).truthy());
    return true;
}

bool GameInterpreter::command322() {
    GameActor* a = game_actors().actor(Pi(params_, 0));
    if (a) {
        a->set_character_image(Ps(params_, 1), Pi(params_, 2));
        a->set_face_image(Ps(params_, 3), Pi(params_, 4));
        a->set_battler_image(Ps(params_, 5));
    }
    game_player().refresh();
    return true;
}

bool GameInterpreter::command323() {
    GameVehicle* v = vehicle_of(P(params_, 0));
    if (v) v->set_image(Ps(params_, 1), Pi(params_, 2));
    return true;
}

bool GameInterpreter::command324() {
    GameActor* a = game_actors().actor(Pi(params_, 0));
    if (a) a->nickname_ = Ps(params_, 1);
    return true;
}

bool GameInterpreter::command325() {
    GameActor* a = game_actors().actor(Pi(params_, 0));
    if (a) a->profile_ = Ps(params_, 1);
    return true;
}

bool GameInterpreter::command331() {
    double value = operate_value(Pi(params_, 1), Pi(params_, 2), Pn(params_, 3)).to_num();
    bool allow = P(params_, 4).truthy();
    iterate_enemy_index(Pi(params_, 0), [&](GameBattler* e) { change_hp(e, value, allow); });
    return true;
}

bool GameInterpreter::command332() {
    double value = operate_value(Pi(params_, 1), Pi(params_, 2), Pn(params_, 3)).to_num();
    iterate_enemy_index(Pi(params_, 0), [&](GameBattler* e) { e->gain_mp(value); });
    return true;
}

bool GameInterpreter::command342() {
    double value = operate_value(Pi(params_, 1), Pi(params_, 2), Pn(params_, 3)).to_num();
    iterate_enemy_index(Pi(params_, 0), [&](GameBattler* e) { e->gain_tp(value); });
    return true;
}

bool GameInterpreter::command333() {
    iterate_enemy_index(Pi(params_, 0), [&](GameBattler* e) {
        bool already = e->is_dead();
        if (Pi(params_, 1) == 0) e->add_state(Pi(params_, 2));
        else e->remove_state(Pi(params_, 2));
        if (e->is_dead() && !already) e->perform_collapse();
        e->clear_result();
    });
    return true;
}

bool GameInterpreter::command334() {
    iterate_enemy_index(Pi(params_, 0), [&](GameBattler* e) { e->recover_all(); });
    return true;
}

bool GameInterpreter::command335() {
    iterate_enemy_index(Pi(params_, 0), [&](GameBattler* e) {
        e->appear();
        game_troop().make_unique_names();
    });
    return true;
}

bool GameInterpreter::command336() {
    iterate_enemy_index(Pi(params_, 0), [&](GameBattler* e) {
        if (GameEnemy* en = dynamic_cast<GameEnemy*>(e)) en->transform(Pi(params_, 1));
        game_troop().make_unique_names();
    });
    return true;
}

bool GameInterpreter::command337() {
    int target = P(params_, 2).truthy() ? -1 : Pi(params_, 0);
    iterate_enemy_index(target, [&](GameBattler* e) {
        if (e->is_alive()) e->start_animation(Pi(params_, 1), false, 0);
    });
    return true;
}

bool GameInterpreter::command339() {
    if (!party_in_battle()) return true;
    auto fn = [&](GameBattler* b) {
        if (!b->is_death_state_affected()) {
            b->force_action(Pi(params_, 2), Pi(params_, 3));
            battle::force_action(b);
            set_wait_mode("action");
        }
    };
    if (Pi(params_, 0) == 0) iterate_enemy_index(Pi(params_, 1), fn);
    else iterate_actor_id(Pi(params_, 1), fn);
    return true;
}

bool GameInterpreter::command340() {
    battle::abort();
    return true;
}

bool GameInterpreter::command351() {
    if (!party_in_battle()) scenes::push_menu();
    return true;
}

bool GameInterpreter::command352() {
    if (!party_in_battle()) scenes::push_save();
    return true;
}

bool GameInterpreter::command353() {
    scenes::goto_gameover();
    return true;
}

bool GameInterpreter::command354() {
    scenes::goto_title();
    return true;
}

bool GameInterpreter::command355() {
    const Value* cmd = current_command();
    std::string source = (*cmd)["parameters"][0].string() + "\n";
    const Value& compiled = (*cmd)["parameters"][1];
    while (next_event_code() == 655) {
        index_++;
        source += command_at(index_)["parameters"][0].string() + "\n";
    }
    run_script(compiled.is_arr() ? &compiled : nullptr, source, "SCRIPT CALL ERROR", nullptr);
    return true;
}

bool GameInterpreter::command356() {
    std::vector<std::string> args = split_spaces(Ps(params_, 0));
    std::string command = args.front();
    args.erase(args.begin());
    if (command.size() && to_lower(trim(command)) == "wait" && !args.empty() && to_lower(trim(args[0])) == "for" && args.size() > 1 &&
        to_lower(trim(args[1])) == "condition") {
        std::string condition;
        for (size_t i = 2; i < args.size(); i++) condition += args[i] + " ";
        const Value& compiled = P(params_, 1);
        WaitCondition wc;
        wc.source = condition;
        if (compiled.is_arr()) wc.program = std::shared_ptr<const Value>(keep_, &compiled);
        else wc.program = js::compile_cached(condition);
        wait_conditions.push_back(wc);
        return true;
    }
    plugin_command(command, args);
    return true;
}

std::string GameInterpreter::args_to_string(const std::vector<std::string>& args) const {
    std::string s;
    for (auto& a : args) s += a + " ";
    return trim(s);
}

static double parse_int_arg(const std::vector<std::string>& args, size_t i) { return i < args.size() ? js_parse_int(args[i]) : NAN; }

static std::string arg_at(const std::vector<std::string>& args, size_t i) { return i < args.size() ? args[i] : std::string(); }

void GameInterpreter::plugin_command(const std::string& command, std::vector<std::string> args) {
    if (to_lower(command) == "steamworks") {
        if (args.empty()) return;
        std::string action = to_lower(args[0]);
        if (action == "activateachievement" || action == "giveachivement" || action == "grantachievement") {
            if (args.size() < 2) return;
            std::string name;
            for (size_t i = 1; i < args.size(); i++) name += args[i];
            game_system().unlock_achievement(name);
        }
        return;
    }
    if (to_upper(command) == "WAITFPS") {
        wait_count = 1;
        set_wait_mode("fps");
    }
    std::string lc = to_lower(trim(command));
    if (lc == "wait" && !args.empty()) {
        std::string a0 = to_lower(trim(args[0]));
        if (a0 == "for" && args.size() > 1) {
            set_wait_mode(to_lower(trim(args[1])));
            if (wait_mode == "se") wait((int)js_parse_int(data::plugin_param("SRD_WaitOptions", "SE Wait Buffer", "")));
        } else {
            re::Groups g;
            if (re::search("(\\d+)", "i", args[0], &g)) {
                int value = (int)js_parse_int(g[1]);
                std::string unit = to_lower(trim(arg_at(args, 1)));
                if (unit == "frames") wait(value);
                else if (unit == "seconds") wait(value * 60);
                else if (unit == "minutes") wait(value * 3600);
            }
        }
    } else if (lc == "cancel" && !args.empty() && to_lower(trim(args[0])) == "wait") {
        set_wait_mode("");
        wait(0);
    }
    if (command == "RotatePictureEx") {
        int pid = (int)js_number(arg_at(args, 0));
        GamePicture* pic = game_screen().picture(pid);
        double angle = js_number(arg_at(args, 1));
        int frame = (int)js_number(arg_at(args, 2));
        bool needs_wait = js_number(arg_at(args, 3)) != 0;
        if (angle != 0 && frame > 0 && pic) {
            pic->set_rotation_with_frame(angle, frame);
            if (needs_wait) game_map().interpreter->wait(frame);
        }
    }
    if (re::search("HorrorEffect", "i", command)) plugin_command_horror_effects(args);
    std::string uc = to_upper(command);
    if (uc == "CREATEFILTER") {
        std::string id = arg_at(args, 0);
        std::string type = arg_at(args, 1);
        int target = (int)js_parse_int(arg_at(args, 2));
        if (std::isnan((double)target) || js_parse_int(arg_at(args, 2)) != js_parse_int(arg_at(args, 2))) target = 0;
        if (target == 4000) target += event_id_;
        int ch = 0;
        bool has_ch = args.size() > 3;
        if (has_ch) {
            if (args[3] == "screen") {
                ch = 0;
            } else {
                double cv = js_number(args[3]);
                ch = std::isnan(cv) ? 0 : (int)cv;
                if (ch == 0) {
                    if (!party_in_battle()) {
                        ch = event_id_;
                    } else {
                        GameBattler* sub = battle::subject();
                        int index = 0;
                        if (sub) {
                            auto tm = game_troop().unit_members();
                            auto it = std::find(tm.begin(), tm.end(), sub);
                            if (it != tm.end()) {
                                index = -((int)(it - tm.begin()) + 1);
                            } else {
                                auto pm = game_party().unit_members();
                                auto it2 = std::find(pm.begin(), pm.end(), sub);
                                index = it2 == pm.end() ? 0 : (int)(it2 - pm.begin()) + 1;
                            }
                        }
                        ch = index;
                    }
                }
            }
        }
        game_map().create_filter(id, type, target, ch, !has_ch);
    } else if (uc == "SETFILTER") {
        game_map().set_filter(arg_at(args, 0), filter_param_process(std::vector<std::string>(args.begin() + std::min<size_t>(1, args.size()), args.end())));
    } else if (uc == "ENABLEFILTER") {
        game_map().enable_filter(arg_at(args, 0), arg_at(args, 1) == "true");
    } else if (uc == "MOVEFILTER" || uc == "MOVEFILTERQ") {
        double d = js_number(args.empty() ? "" : args.back());
        int dur = (std::isnan(d) || d == 0) ? 1 : (int)d;
        std::vector<std::string> mid;
        for (size_t i = 1; i + 1 < args.size(); i++) mid.push_back(args[i]);
        if (uc == "MOVEFILTER") game_map().move_filter(arg_at(args, 0), filter_param_process(mid), dur);
        else game_map().move_filter_queue(arg_at(args, 0), filter_param_process(mid), dur);
    } else if (uc == "ERASEFILTER") {
        game_map().erase_filter(arg_at(args, 0));
    } else if (uc == "ERASEFILTERAFTERMOVE") {
        game_map().erase_filter_after_move(arg_at(args, 0));
    } else if (uc == "SETFILTERSPEED") {
        double v = js_number(arg_at(args, 1));
        game_map().set_filter_addi_time(arg_at(args, 0), std::isnan(v) ? 0 : v);
    }
    if (command == "GachaBook") {
        std::string a = arg_at(args, 0);
        if (a == "open") scenes::push_gacha_book();
        else if (a == "add") game_system().add_to_gacha_book(arg_at(args, 1), (int)js_number(arg_at(args, 2)));
        else if (a == "remove") game_system().remove_from_gacha_book(arg_at(args, 1), (int)js_number(arg_at(args, 2)));
        else if (a == "complete") game_system().complete_gacha_book();
        else if (a == "clear") game_system().clear_gacha_book();
    }
    if (command == "Gacha") {
        std::string a = arg_at(args, 0);
        if (a == "open") scenes::push_gacha();
        else if (a == "add") game_system().add_to_gacha(arg_at(args, 1), (int)js_number(arg_at(args, 2)));
        else if (a == "remove") game_system().remove_from_gacha(arg_at(args, 1), (int)js_number(arg_at(args, 2)));
        else if (a == "clear") game_system().clear_gacha();
    }
    if (command == "SlotMachine") {
        std::string a = arg_at(args, 0);
        if (a == "open") scenes::push_slot_machine();
        else if (a == "expectation") slot_machine::expectation = js_number(arg_at(args, 1));
        else if (a == "scale") {
            std::string s = arg_at(args, 1);
            slot_machine::scale = s == "1" ? 10 : s == "2" ? 100 : 1;
        }
    }
    if (command == "ChangeEventcon") {
        if (args.size() == 4) {
            int map = (int)js_parse_int(args[0]);
            int ev = (int)js_parse_int(args[1]);
            std::string img = args[2];
            int index = (int)js_parse_int(args[3]);
            if (!game_event_data().has_category("eventcon")) game_event_data().add_category("eventcon");
            game_event_data().set_value_ext("eventcon", map, ev, js::make_array({js::Val::str(img), js::Val::num(index)}));
            if (map == game_map().map_id()) {
                GameEvent* e = game_map().event(ev);
                if (e) {
                    e->remove_eventcon();
                    e->setup_eventcon(img, index);
                }
            }
        }
    } else if (command == "RemoveEventcon") {
        if (args.size() == 2) {
            int map = (int)js_parse_int(args[0]);
            int ev = (int)js_parse_int(args[1]);
            if (!game_event_data().has_category("eventcon")) game_event_data().add_category("eventcon");
            game_event_data().set_value_ext("eventcon", map, ev, js::Val());
            if (map == game_map().map_id()) {
                GameEvent* e = game_map().event(ev);
                if (e) e->remove_eventcon();
            }
        }
    }
    {
        if (command == "EnableLetterSound") game_system().msg_sound_enable = true;
        else if (command == "DisableLetterSound") game_system().msg_sound_enable = false;
        else if (command == "LetterSoundName") change_letter_sound_name(args);
        else if (command == "LetterSoundVolume") game_system().msg_sound_vol = parse_int_arg(args, 0);
        else if (command == "LetterSoundPitch") game_system().msg_sound_pitch = parse_int_arg(args, 0);
        else if (command == "LetterSoundPitchVariance") game_system().msg_sound_pitch_var = parse_int_arg(args, 0);
        else if (command == "LetterSoundPan") game_system().msg_sound_pan = parse_int_arg(args, 0);
        else if (command == "LetterSoundPanVariance") game_system().msg_sound_pan_var = parse_int_arg(args, 0);
        else if (command == "LetterSoundInterval") game_system().msg_sound_interval = (int)parse_int_arg(args, 0);
        else if (command == "LetterSoundReset") game_system().init_message_sounds();
        else if (command == "ChoiceRowMax") game_system().msg_choice_max = (int)parse_int_arg(args, 0);
        else if (command == "HideChoice") game_system().hide_choice((int)parse_int_arg(args, 0));
        else if (command == "ShowChoice") game_system().show_choice((int)parse_int_arg(args, 0));
        else if (command == "ClearHiddenChoices") game_system().hide_choices.clear();
        else if (command == "DisableChoice") game_system().disable_choice((int)parse_int_arg(args, 0));
        else if (command == "EnableChoice") game_system().enable_choice((int)parse_int_arg(args, 0));
        else if (command == "ClearDisabledChoices") game_system().disable_choices.clear();
        else if (command == "ClearChoiceSettings") game_system().clear_choice_settings();
        else if (command == "MessagePositionX") game_system().msg_position_x = js::Val::num(parse_int_arg(args, 0));
        else if (command == "MessagePositionY") game_system().msg_position_y = js::Val::num(parse_int_arg(args, 0));
        else if (command == "MessagePositionXAuto") game_system().msg_position_x = js::Val::str("auto");
        else if (command == "MessagePositionYAuto") game_system().msg_position_y = js::Val::str("auto");
        else if (command == "MessageAnchorX") set_message_anchor(arg_at(args, 0), 'x');
        else if (command == "MessageAnchorY") set_message_anchor(arg_at(args, 0), 'y');
        else if (command == "MessagePositionReset") {
            game_system().init_message_position();
            game_system().message_rows_expr.clear();
            game_system().message_width_expr.clear();
        }
    }
    if (command == "MessageRows") game_system().message_rows_expr = arg_at(args, 0);
    if (command == "MessageWidth") game_system().message_width_expr = arg_at(args, 0);
    if (command == "EnableWordWrap") game_system().word_wrap_ = true;
    if (command == "DisableWordWrap") game_system().word_wrap_ = false;
    if (command == "EnableFastForward") game_system().fast_forward = true;
    if (command == "DisableFastForward") game_system().fast_forward = false;
    if (command == "ShowMessage") command_show_language_message(arg_at(args, 0));
    else if (command == "ChangeLanguage") data::set_language(arg_at(args, 0));
    else if (command == "AddChoice") add_language_choice(arg_at(args, 0), arg_at(args, 1));
    else if (command == "ShowChoices") command_show_language_choices(arg_at(args, 0));
    if (to_upper(command) == "SMARTPATH" && !args.empty()) {
        bool ok = false;
        js::Val who = run_script(nullptr, args[0], "SMARTPATH", &ok);
        GameCharacter* subject = character_id(who.to_int());
        if (subject && args.size() > 1) {
            if (to_upper(args[1]) == "CANCEL") {
                subject->clear_target();
            } else if (args.size() > 2) {
                js::Val x = run_script(nullptr, args[1], "SMARTPATH", &ok);
                js::Val y = run_script(nullptr, args[2], "SMARTPATH", &ok);
                subject->set_target(CharRef(), x.to_num(), y.to_num(), true);
            } else {
                js::Val t = run_script(nullptr, args[1], "SMARTPATH", &ok);
                subject->set_target(CharRef::of(character_id(t.to_int())), 0, 0, false);
            }
        }
    }
    if (re::search("EVENTTIMER", "i", command)) {
        if (const Value* ast = yanfly_ast("TimerCode")) {
            js::ScopePtr scope = std::make_shared<js::Scope>();
            scope->function_scope = true;
            scope->declare(js::atom("data"), js::Val::str(args_to_string(args)));
            js::Val r;
            if (!V().run(*ast, to_js(), scope, &r, data::keep())) script_error("EVENT TIMER CONTROL EFFECT CODE ERROR", V().last_error);
        }
    }
    if (command == "CAM") game_map().cam_control(args);
    if (command == "StopEventMovement") game_temp().stop_map_events = true;
    if (command == "AllowEventMovement") game_temp().stop_map_events = false;
    if (command == "StopPlayerMovement") game_temp().stop_map_player = true;
    if (command == "AllowPlayerMovement") game_temp().stop_map_player = false;
    if (command == "SmartJump") game_player().smart_jump((int)parse_int_arg(args, 0));
    if (command == "ResetAllEventLocations") game_map().reset_all_event_locations();
    if (command == "EnableFootsteps") game_system().footsteps_enabled = true;
    else if (command == "DisableFootsteps") game_system().footsteps_enabled = false;
    if (command == "EnableDashing") game_system().disable_dashing = false;
    else if (command == "DisableDashing") game_system().disable_dashing = true;
    else if (command == "ToggleDashing") game_system().disable_dashing = !game_system().disable_dashing;
    if (command == "CallEvent") call_event(args_to_string(args));
    if (command == "HideMiniLabel") game_system().show_event_mini_label = false;
    if (command == "ShowMiniLabel") game_system().show_event_mini_label = true;
    if (command == "RefreshMiniLabel" && !party_in_battle()) scenes::refresh_mini_labels();
    if (command == "exaFC.linkEvent") {
        GameFollower* f = game_player().followers_.follower((int)js_number(arg_at(args, 1)));
        if (f) f->fc_event_id = arg_at(args, 0) == "true" ? (int)js_number(arg_at(args, 2)) : -1;
    }
    if (command == "exaFC.followerInstruct") {
        if (arg_at(args, 0) == "true") {
            fc::follower_control_id = (int)js_number(arg_at(args, 1));
            fc::follower_control = true;
        } else {
            fc::follower_control_id = -1;
            fc::follower_control = false;
        }
    }
    if (command == "exaFC.moveType") {
        GameFollower* f = game_player().followers_.follower((int)js_number(arg_at(args, 1)));
        if (f) {
            std::string t = arg_at(args, 0);
            if (t == "fixed") f->move_type = 0;
            else if (t == "random") f->move_type = 1;
            else if (t == "approach") f->move_type = 2;
            else if (t == "custom") {
                if (next_event_code() == 205) {
                    f->set_move_route(route_from(command_at(index_ + 1)["parameters"][1], keep_));
                    f->move_type = 3;
                    index_ += 2;
                } else {
                    f->move_type = 0;
                }
            } else if (t == "clear") {
                f->move_type = 0;
                f->set_move_route(nullptr);
            }
            if (!arg_at(args, 2).empty()) f->set_move_speed(js_number(args[2]));
            if (!arg_at(args, 3).empty()) f->set_move_frequency(js_number(args[3]));
        }
    }
    if (command == "exaFC.lockProperties") {
        GameFollower* f = game_player().followers_.follower((int)js_number(arg_at(args, 1)));
        if (f) f->fc_retain_attrib = arg_at(args, 0) == "true";
    }
    if (command == "exaFC.followerStop") fc::follower_stop = arg_at(args, 0) == "true";
    if (command == "exaFC.followerCollision") {
        bool toggle = arg_at(args, 0) == "true";
        fc::follower_collision = toggle;
        for (GameFollower* f : game_player().followers_.all()) f->set_through(!toggle);
    }
    if (command == "exaFC.clearAll") {
        fc::follower_control_id = -1;
        fc::follower_control = false;
        fc::follower_stop = false;
        fc::follower_collision = false;
        for (GameFollower* f : game_player().followers_.all()) {
            f->fc_event_id = -1;
            f->move_type = 0;
            f->fc_event_lock = false;
            f->fc_prelock_direction = 0;
            f->fc_retain_attrib = false;
            f->set_move_route(nullptr);
            f->set_through(true);
            f->update();
        }
    }
    if (party_in_battle()) battle::alter_battleback(Ps(params_, 0));
    std::string lower = to_lower(command);
    if (lower == "add_enemy") {
        game_troop().add_enemy_reinforcement((int)std::floor(js_number(arg_at(args, 3))), (int)std::floor(js_number(arg_at(args, 0))));
    } else if (lower == "add_enemy_troop") {
        game_troop().add_troop_reinforcements((int)std::floor(js_number(arg_at(args, 0))));
    } else if (lower == "remove_enemy") {
        game_troop().remove_enemy_reinforcement((int)std::floor(js_number(arg_at(args, 3))), (int)std::floor(js_number(arg_at(args, 0))));
    } else if (lower == "remove_enemy_troop") {
        game_troop().remove_troop_reinforcements((int)std::floor(js_number(arg_at(args, 0))));
    }
    if (command == "SetDamageCap") game_system().set_new_damage_cap(js_parse_int(arg_at(args, 0)), true);
    if (command == "SetHealingCap") game_system().set_new_damage_cap(js_parse_int(arg_at(args, 0)), false);
    if (command == "EnableDamageCap") game_system().default_damage_cap = true;
    if (command == "DisableDamageCap") game_system().default_damage_cap = false;
    if (command == "setBattleSys" && !party_in_battle()) game_system().battle_system = arg_at(args, 0);
    if (uc == "ENABLETEXTFF") tddp_ff::text_ff_disabled = false;
    if (uc == "DISABLETEXTFF") tddp_ff::text_ff_disabled = true;
    if (uc == "DISABLENEXTTEXTFF") tddp_ff::next_text_ff_disabled = true;
    if (command == "GainGold") game_party().gain_gold(js_parse_int(arg_at(args, 0)));
    if (command == "LoseGold") game_party().lose_gold(js_parse_int(arg_at(args, 0)));
    if (command == "AriesWeather" || command == "AriesSetWeatherImage" || command == "AriesToggleStormThunder") weather_plugin_command(command, args);
}

void GameInterpreter::change_letter_sound_name(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        game_system().msg_sound_name = args[0];
        return;
    }
    std::string text;
    for (auto& a : args) text += " " + a;
    game_system().msg_sound_name = text;
}

void GameInterpreter::set_message_anchor(const std::string& s, char type) {
    if (type == 'x') {
        if (re::test("center", s)) game_system().msg_anchor_x = 0.5;
        else if (re::test("right", s)) game_system().msg_anchor_x = 1;
        else game_system().msg_anchor_x = 0;
    } else {
        if (re::test("center", s)) game_system().msg_anchor_y = 0.5;
        else if (re::test("bottom", s)) game_system().msg_anchor_y = 1;
        else game_system().msg_anchor_y = 0;
    }
}

bool GameInterpreter::command_show_language_message(const std::string& code) {
    if (!game_message().is_busy()) {
        game_message().show_language_message(code);
        switch (next_event_code()) {
        case 102:
            index_++;
            setup_choices(command_at(index_)["parameters"]);
            break;
        case 103:
            index_++;
            setup_num_input(command_at(index_)["parameters"]);
            break;
        case 104:
            index_++;
            setup_item_choice(command_at(index_)["parameters"]);
            break;
        case 356: {
            std::vector<std::string> parts = split_spaces(command_at(index_ + 1)["parameters"][0].string());
            if (!parts.empty() && parts[0] == "ShowChoices") {
                index_++;
                double cancel = parts.size() > 1 ? js_number(parts[1]) : NAN;
                setup_language_choices(std::isnan(cancel) ? -1000000 : (int)cancel);
            }
            break;
        }
        }
        set_wait_mode("message");
    }
    return false;
}

void GameInterpreter::add_language_choice(const std::string& code, const std::string& label) {
    language_choices.push_back(data::lang_text(code));
    language_choice_labels.push_back(label);
}

bool GameInterpreter::command_show_language_choices(const std::string& cancel_type) {
    if (!game_message().is_busy()) {
        double c = js_parse_int(cancel_type);
        setup_language_choices(std::isnan(c) ? -1000000 : (int)c);
        index_++;
        set_wait_mode("message");
    }
    return false;
}

bool GameInterpreter::command_language_jump_to(const std::string& label) {
    for (int i = 0; i < list_size(); i++) {
        const Value& c = command_at(i);
        if (c["code"].as_int() == 118 && c["parameters"][0].string() == label) {
            jump_to(i);
            return false;
        }
    }
    return true;
}

void GameInterpreter::setup_language_choices(int cancel) {
    std::vector<std::string> choices = language_choices;
    int cancel_type = cancel;
    if (cancel_type >= (int)choices.size()) cancel_type = -2;
    game_message().set_choices(choices, 0, cancel_type);
    game_message().choice_background = 0;
    game_message().choice_position_type = 2;
    game_message().choice_labels = language_choice_labels;
    int indent = indent_;
    game_message().choice_callback = [this, indent](int n) {
        if (n >= 0) {
            std::string label = n < (int)game_message().choice_labels.size() ? game_message().choice_labels[(size_t)n] : std::string();
            command_language_jump_to(label);
        } else {
            branch[indent] = js::Val::num(n);
        }
    };
    language_choices.clear();
    language_choice_labels.clear();
}

void GameInterpreter::call_event(const std::string& line) {
    if (call_event_running) {
        process_call_event();
        return;
    }
    int event_id, page_id, map_id;
    re::Groups g;
    if (re::search("(\\d+),[ ](.*)", "i", line, &g)) {
        event_id = (int)js_parse_int(g[1]);
        std::string rest = g[2];
        re::Groups g2;
        page_id = re::search("PAGE[ ](\\d+)", "i", rest, &g2) ? (int)js_parse_int(g2[1]) : 1;
        re::Groups g3;
        map_id = re::search("MAP[ ](\\d+)", "i", rest, &g3) ? (int)js_parse_int(g3[1]) : game_map().map_id();
    } else {
        double e = js_parse_int(line);
        if (std::isnan(e) || e == 0) return;
        event_id = (int)e;
        page_id = 1;
        map_id = game_map().map_id();
    }
    call_event_map_id = map_id;
    call_event_event_id = event_id;
    call_event_page_id = page_id;
    call_event_running = true;
    process_call_event();
}

void GameInterpreter::process_call_event() { insert_call_event_data(call_event_event_id, call_event_page_id); }

void GameInterpreter::insert_call_event_data(int event_id, int page_id) {
    call_event_running = false;
    std::shared_ptr<const void> keep;
    const Value* ev = preloaded_event(call_event_map_id, event_id, &keep);
    if (!ev || !ev->is_obj()) return;
    const Value& page = (*ev)["pages"][page_id - 1];
    if (!page.is_obj()) return;
    setup_child(&page["list"], event_id_, keep);
}

void GameInterpreter::plugin_command_horror_effects(std::vector<std::string> args) {
    if (args.empty()) return;
    std::string text = args.front();
    args.erase(args.begin());
    auto shift = [&]() {
        if (args.empty()) return std::string();
        std::string s = args.front();
        args.erase(args.begin());
        return s;
    };
    js::Val target;
    if (re::test("(?:Screen|Map)", text)) target = game_screen_js();
    else if (re::test("(?:System|Battle)", text)) target = game_system_js();
    else if (re::test("Player", text)) target = game_player().to_js();
    else if (re::test("Follower", text)) {
        double i = js_parse_int(shift());
        if (!std::isnan(i) && i != 0) {
            GameFollower* f = game_player().followers_.follower((int)i - 1);
            if (f) target = f->to_js();
        }
    } else if (re::test("Event", text)) {
        double id = js_parse_int(shift());
        if (id == 0) id = event_id_;
        if (!std::isnan(id) && id != 0) {
            GameEvent* e = game_map().event((int)id);
            if (e) target = e->to_js();
        }
    } else if (re::test("Picture", text)) {
        double id = js_parse_int(shift());
        if (!std::isnan(id) && id != 0) target = picture_js((int)id);
    } else if (re::test("Actor", text)) {
        double id = js_parse_int(shift());
        if (!std::isnan(id) && id != 0) {
            GameActor* a = game_actors().actor((int)id);
            if (a) target = a->to_js();
        }
    } else if (re::test("Party", text)) {
        double i = js_parse_int(shift());
        if (!std::isnan(i) && i != 0) {
            auto m = game_party().members();
            if (i - 1 >= 0 && i - 1 < m.size()) target = m[(size_t)(i - 1)]->to_js();
        }
    } else if (re::test("Enemy", text)) {
        double i = js_parse_int(shift());
        if (!std::isnan(i) && i != 0) {
            auto m = game_troop().unit_members();
            if (i - 1 >= 0 && i - 1 < m.size()) target = m[(size_t)(i - 1)]->to_js();
        }
    }
    if (!target.truthy()) return;
    js::Vm& vm = V();
    auto call = [&](const char* m, std::vector<js::Val> a) { vm.call_method(target, js::atom(m), a); };
    std::string cmd = shift();
    int type;
    if (re::test("Create", cmd)) type = 1;
    else if (re::test("Remove", cmd)) type = 2;
    else if (re::test("Change", cmd)) type = 3;
    else if (re::test("Clear", cmd)) {
        call("removeHorrorEffect", {js::Val::str("noise")});
        call("removeHorrorEffect", {js::Val::str("glitch")});
        call("removeHorrorEffect", {js::Val::str("tv")});
        call("clearHorrorEffects", {});
        return;
    } else {
        return;
    }
    std::string effect = shift();
    if (!re::test("(?:Noise|Glitch|TV)", effect)) return;
    std::string kind = to_lower(effect);
    if (type == 1) {
        call("createHorrorEffect", {js::Val::str(kind)});
        return;
    }
    if (type == 2) {
        call("removeHorrorEffect", {js::Val::str(kind)});
        return;
    }
    std::string key = shift();
    js::Val value = js::Val::num(0);
    auto eval_word = [&](const std::string& w) {
        bool ok = false;
        return run_script(nullptr, to_lower(w), "HORROR EFFECT", &ok);
    };
    if (kind == "noise") {
        if (re::test("rate", key)) {
            key = "noise";
            value = js::Val::num(js_parse_float(shift()));
        } else if (re::test("animated", key)) {
            key = "animated";
            value = eval_word(shift());
        } else {
            return;
        }
    } else if (kind == "glitch") {
        if (re::test("max", key)) {
            key = "sliceMax";
            value = js::Val::num(js_parse_int(shift()));
        } else if (re::test("min", key)) {
            key = "sliceMin";
            value = js::Val::num(js_parse_int(shift()));
        } else if (re::test("slice", key)) {
            key = "slices";
            value = js::Val::num(js_parse_int(shift()));
            call("setHorrorEffectToValue", {js::Val::str(kind), js::Val::str("sliceMax"), value});
            call("setHorrorEffectToValue", {js::Val::str(kind), js::Val::str("sliceMin"), value});
        } else if (re::test("offset", key)) {
            key = "offset";
            value = js::Val::num(js_parse_int(shift()));
        } else if (re::test("animated", key)) {
            key = "animated";
            value = eval_word(shift());
            if (value.is_bool() && !value.b) call("setHorrorEffectToValue", {js::Val::str(kind), js::Val::str("refreshRequest"), js::Val::boolean(true)});
        } else if (re::test("frequency", key)) {
            key = "aniFrequency";
            value = js::Val::num(js_parse_int(shift()));
        } else if (re::test("strength", key)) {
            key = "aniStrength";
            value = js::Val::num(js_parse_int(shift()));
        } else if (re::test("refresh", key)) {
            key = "refreshRequest";
            value = js::Val::boolean(true);
        } else {
            return;
        }
    } else if (kind == "tv") {
        if (re::test("thick", key)) {
            key = "lineWidth";
            value = js::Val::num(js_parse_int(shift()));
        } else if (re::test("corner", key)) {
            key = "vignetting";
            value = js::Val::num(js_parse_float(shift()));
        } else if (re::test("animated", key)) {
            key = "animated";
            value = eval_word(shift());
        } else if (re::test("speed", key)) {
            key = "aniSpeed";
            value = js::Val::num(js_parse_float(shift()));
        } else {
            return;
        }
    }
    call("setHorrorEffectToValue", {js::Val::str(kind), js::Val::str(key), value});
}

void GameInterpreter::setup_picture_custom_frames(int picture_id, double width, double height, int hframes, int vframes) {
    double fw = width / hframes, fh = height / vframes;
    int total = hframes * vframes;
    std::vector<FrameRect> rects;
    for (int i = 0; i < total; i++) rects.push_back(FrameRect{(i % hframes) * fw, std::floor((double)i / hframes) * fh, fw, fh});
    GamePicture* pic = game_screen().picture(picture_id);
    if (pic) pic->setup_custom_frame(rects);
}

void GameInterpreter::remove_picture_custom_frames(int picture_id) {
    GamePicture* pic = game_screen().picture(picture_id);
    if (pic) pic->clear_custom_frame();
}

void GameInterpreter::set_picture_frame_index(int picture_id, int index) {
    GamePicture* pic = game_screen().picture(picture_id);
    if (!pic) return;
    pic->set_custom_frame_index(index);
    scenes::update_picture_custom_frame(picture_id);
}

void GameInterpreter::set_picture_animation(int picture_id, const js::Val& frames, double delay, double loops, bool wait_anim) {
    GamePicture* pic = game_screen().picture(picture_id);
    if (!pic) return;
    FrameAnim anim;
    anim.loops = loops;
    anim.index = 0;
    if (frames.is_array()) {
        for (auto& f : frames.o->items) {
            FrameAnim::F fr;
            if (f.is_array()) {
                fr.frame = V().get(f, js::Val::num(0)).to_int();
                fr.delay = fr.max_delay = V().get(f, js::Val::num(1)).to_int();
            } else {
                fr.frame = f.to_int();
                fr.delay = fr.max_delay = (int)delay;
            }
            anim.frames.push_back(fr);
        }
    }
    pic->set_custom_frame_animation(anim);
    scenes::update_picture_custom_frame(picture_id);
    if (wait_anim && !std::isinf(loops)) {
        set_wait_mode("pictureAnimation");
        last_animating_picture = picture_id;
    }
}

void GameInterpreter::wait_for_bitmap(bool bitmap_ok) {
    if (!bitmap_ok) {
        wait(10);
        for (int i = index_; i >= 0; i--) {
            if (command_at(i)["code"].as_int() == 355) {
                index_ = i - 1;
                break;
            }
        }
    }
}

GameInterpreter* GameInterpreter::latest_interpreter() {
    GameInterpreter* in = this;
    while (in->child && in->child->list_) in = in->child.get();
    return in;
}

void GameInterpreter::process_skip_cutscene() {
    GameInterpreter* in = latest_interpreter();
    if (in->list_) {
        for (int i = in->index_; i < in->list_size(); i++) {
            const Value& c = in->command_at(i);
            if (c["code"].as_int() == 118 && re::test("<Skip Target>", c["parameters"][0].string())) {
                in->stop_skip_cutscene(in, i, true);
                return;
            }
        }
    }
    if (in == this) {
        in->stop_skip_cutscene(in, list_size() - 1, false);
    } else {
        in->terminate();
        process_skip_cutscene();
    }
}

void GameInterpreter::stop_skip_cutscene(GameInterpreter* in, int target_line, bool found_target) {
    in->wait_mode.clear();
    in->wait(in->fade_speed());
    game_screen().start_fade_out(in->fade_speed());
    if (in == this) {
        game_temp().expando.set(js::atom("_canSkipCutscene"), js::Val::boolean(false));
        game_temp().expando.set(js::atom("_skippingCutscene"), js::Val::boolean(true));
    }
    scenes::reset_skip_cutscene_duration();
    in->jump_to(target_line);
    if (!found_target) game_screen().start_fade_in(in->fade_speed());
}

void GameInterpreter::set_event_icon(int event_id, const std::string& img, int index) {
    int map = game_map().map_id();
    GameEvent* e = game_map().event(event_id);
    if (!game_event_data().has_category("eventcon")) game_event_data().add_category("eventcon");
    game_event_data().set_value_ext("eventcon", map, event_id, js::make_array({js::Val::str(img), js::Val::num(index)}));
    if (!e) return;
    e->remove_eventcon();
    e->setup_eventcon(img, index);
    e->refresh();
}

void GameInterpreter::add_black_letter(const std::vector<std::string>& letters0) {
    std::vector<std::string> letters;
    for (auto& l : letters0) letters.push_back(to_upper(l));
    std::vector<int> items;
    const Value& all = data::items();
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].is_obj() && all[i]["meta"].find("Blackletter")) items.push_back((int)i);
    }
    for (auto& letter : letters) {
        for (int id : items) {
            if (to_upper(trim(actor_meta(data::items()[id], "Blackletter"))) == letter) {
                ItemRef r = ItemRef::item(id);
                if (!game_party().has_item(r, false)) {
                    game_party().gain_item(r, 1);
                    game_party().black_letters.push_back(id);
                }
                break;
            }
        }
    }
    for (int id : items) {
        std::string letter = trim(actor_meta(data::items()[id], "Blackletter"));
        if (contains_str(letters, letter)) {
            ItemRef r = ItemRef::item(id);
            if (!game_party().has_item(r, false)) {
                game_party().gain_item(r, 1);
                game_party().black_letters.push_back(id);
            }
        }
    }
}

void GameInterpreter::set_picture_as_parallax(double width, double height, double xs, double ys) {
    if (next_event_code() == 231) {
        index_++;
        const Value* cmd = current_command();
        params_ = (*cmd).find("parameters");
        indent_ = (*cmd)["indent"].as_int();
        command231();
        GamePicture* pic = game_screen().picture(Pi(params_, 0));
        if (pic) pic->setup_parallax(xs, ys, width, height);
    }
}

void GameInterpreter::change_parallax_picture_speed(int picture_id, const js::Val& xs, const js::Val& ys) {
    GamePicture* pic = game_screen().picture(picture_id);
    if (!pic) return;
    if (!xs.is_nullish()) pic->parallax_rect.x = xs.to_num();
    if (!ys.is_nullish()) pic->parallax_rect.y = ys.to_num();
}

void GameInterpreter::setup_shop(const std::string& name, int type) {
    game_temp().clear_shop_data();
    js::Val data = *game_temp().expando.find(js::atom("_shopData"));
    std::shared_ptr<Document> system_doc = data::lang_file("System");
    const Value& shop_menu = system_doc ? system_doc->root()["plugins"]["itemShopMenu"] : Value::null_value();
    const Value& text = shop_menu["defaultText"];
    const Value& keeper = shop_menu["shopKeepers"][to_lower(name).c_str()];
    auto put = [&](const char* k, const js::Val& v) { V().put(data, js::atom(k), v); };
    put("shopType", js::Val::num(type));
    put("name", js::Val::str(keeper["shopName"].truthy() ? keeper["shopName"].string() : text["shopName"].string()));
    const Value* mb = keeper.find("showMailboxShopkeeper");
    put("showMailboxShopkeeper", mb ? js::value_to_js(*mb) : js::Val::boolean(false));
    std::string header;
    if (type == 0) header = keeper["buyHeader"].truthy() ? keeper["buyHeader"].string() : text["buyHeader"].string();
    else header = keeper["sellHeader"].truthy() ? keeper["sellHeader"].string() : text["sellHeader"].string();
    put("transactionHeader", js::Val::str(header));
    js::Val texts = js::make_object();
    static const char* keys[] = {"maxItemMessage", "notEnoughMoneyMessage", "onItemListBuyOkMessage", "onItemListSellOkMessage",
                                 "itemBuyingPromptMessage", "itemSellingPromptMessage", "itemBuyingConfirmationMessage",
                                 "itemSellingConfirmationMessage", "itemBuyingCancelMessage", "itemSellingCancelMessage"};
    for (const char* k : keys) V().put(texts, js::atom(k), js::Val::str("Prologue_WHITESPACE.message_3"));
    const Value& kt = keeper["texts"];
    for (const Member* m = kt.begin_members(); m != kt.end_members(); ++m) V().put(texts, js::atom(m->key), js::value_to_js(m->v));
    put("texts", texts);
}

void GameInterpreter::call_save_menu(bool save, bool load) { scenes::push_omori_file(save, load); }

void GameInterpreter::call_album_menu(int item_id, int interface_mode) { scenes::push_photo_album(item_id, interface_mode); }

void GameInterpreter::call_black_letter_menu(bool forced) { scenes::push_black_letter_menu(forced); }

void GameInterpreter::call_quest_menu() { scenes::push_quest_menu(); }

void GameInterpreter::show_name_input_windows(const std::string& name, int max, bool wait_input) {
    scenes::show_name_input_windows(name, max);
    if (wait_input) set_wait_mode("nameInput");
}

static int qte_switch() { return (int)js_number(data::plugin_param("Omori Map QTE", "Active QTE Switch ID", "0")); }

void GameInterpreter::start_smash_qte(const js::Val& segments, const js::Val& time, const js::Val& width) {
    game_switches().set_value(qte_switch(), true);
    scenes::start_qte_smash(segments, time, width);
    set_wait_mode("QTE");
}

void GameInterpreter::start_stop_the_arrow_qte(const js::Val& direction, const js::Val& delay, const js::Val& x, const js::Val& y) {
    game_switches().set_value(qte_switch(), true);
    scenes::start_qte_stop_the_arrow(direction, delay, x, y);
    set_wait_mode("QTE");
}

void GameInterpreter::start_hit_the_mark_qte(const js::Val& speed, const js::Val& index, const js::Val& x, const js::Val& y) {
    game_switches().set_value(qte_switch(), true);
    int idx = index.is_undef() ? random_int(12) : index.to_int();
    scenes::start_qte_hit_the_mark(speed, idx, x, y);
    set_wait_mode("QTE");
}

void GameInterpreter::show_pizza_note(const js::Val& text0) {
    AudioObj se;
    se.valid = true;
    se.name = "GEN_mess_paper";
    se.volume = 100;
    se.pitch = 100;
    audio_manager::play_se(se);
    js::Val text = text0;
    if (js::strict_equals(text, js::Val::num(0))) text = prop("_pizzaHouse1");
    if (js::strict_equals(text, js::Val::num(1))) text = prop("_pizzaHouse2");
    if (js::strict_equals(text, js::Val::num(2))) text = prop("_pizzaHouse3");
    scenes::show_pizza_note(text.to_str());
}

void GameInterpreter::setup_ambience(const std::string& name, double x, double y, bool custom) {
    AmbienceData& d = game_player().ambience;
    d.name = name;
    d.x = x;
    (void)y;
    d.y = custom ? 1 : 0;
    d.use_custom_position = custom;
    scenes::setup_ambience_bitmaps(d.name);
}

static std::vector<std::unique_ptr<GameInterpreter>>& retired_interpreters() {
    static std::vector<std::unique_ptr<GameInterpreter>> v;
    return v;
}

void retire_interpreter(std::unique_ptr<GameInterpreter> in) {
    if (in) retired_interpreters().push_back(std::move(in));
}

void flush_retired_interpreters() { retired_interpreters().clear(); }

}
