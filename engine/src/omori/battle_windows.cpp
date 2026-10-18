#include "battle_windows.h"

#include <algorithm>
#include <cmath>

#include "../core/regex.h"
#include "../js/vm.h"
#include "../rpg/audio_manager.h"
#include "../rpg/bind_util.h"
#include "../rpg/config.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "ui_util.h"

namespace ui {
namespace btl {

namespace {

std::string msg(const char* key) { return system_text("message", key); }
std::string basic(int id) { return system_text("basic", std::to_string(id)); }
std::string num(double n) { return js::number_to_string(n); }

bool in_list(int v, std::initializer_list<int> list) {
    for (int x : list) {
        if (x == v) return true;
    }
    return false;
}

int current_action_skill_id() {
    auto& s = rpg::battle::st();
    if (!s.action) return 0;
    rpg::ItemRef it = s.action->item();
    if (!it.valid() || !it.is_skill()) return 0;
    return it.id;
}

struct LogJsHandle : js::JsObject {
    js::HostClass* js_class() const override { return &rpg::bind::klass("Window_BattleLog").hc; }
};

LogJsHandle& log_js_handle() {
    static LogJsHandle h;
    return h;
}

const Value& item_meta(const rpg::ItemRef& item) { return item.data()["meta"]; }

std::vector<rpg::GameBattler*> battlers_of(const js::Val& v) {
    std::vector<rpg::GameBattler*> out;
    if (!v.is_array()) return out;
    for (auto& x : v.o->items) {
        if (rpg::GameBattler* b = rpg::bind::battler_of(x)) out.push_back(b);
    }
    return out;
}

}

int WindowScrollingTextSource::standard_font_size() { return menu::blue("Window_ScrollingTextSource")["standardFontSize"].as_int(); }

void WindowScrollingTextSource::process_escape_character(const std::string& code, TextState& ts) {
    if (code != "DII") {
        WindowBase::process_escape_character(code, ts);
        return;
    }
    re::Groups g;
    std::string rest = ts.text.substr(ts.index);
    if (!re::search("^\\[(.+)\\]", "", rest, &g)) js::vm().throw_type_error("Cannot read property '0' of null");
    ts.index += g[0].size();
    std::string key = input_key_code(g[1]);
    IconRect up, down;
    key_icon_rects(key, up, down);
    ts.x += 4;
    draw_aligned_key_icon(&c(), key, ts.x, ts.y + 5, up.w, ts.height);
    ts.x += up.w + 4;
}

TextState WindowScrollingTextSource::draw_line(const std::string& text, double x, double y) {
    c().clear();
    TextState ts;
    if (text.empty()) return ts;
    ts.x = x;
    ts.y = y;
    ts.left = x;
    ts.text = convert_escape_characters(text);
    ts.height = calc_text_height(ts, false);
    reset_font_settings();
    while (ts.index < ts.text.size()) process_character(ts);
    return ts;
}

SpriteScrollingText::SpriteScrollingText() {
    x = 14;
    y = 4;
    set_frame(0, 0, 360 - 28, 88 - 4);
    mask_rect = std::make_shared<g2d::Rect>(0, 0, width(), height());
    line_sprites_container = std::make_shared<g2d::Sprite>();
    add_child(line_sprites_container);
    source = WindowBase::make<WindowScrollingTextSource>(0.0, 0.0, width(), height());
    source->y = height();
    source->visible = false;
    add_child(source);
}

void SpriteScrollingText::clear() {
    for (auto& s : list) line_sprites_container->remove_child(s.get());
    list.clear();
}

std::shared_ptr<g2d::Sprite> SpriteScrollingText::add_line(const std::string& text) {
    std::shared_ptr<g2d::Sprite> last = list.empty() ? nullptr : list.back();
    TextState ts = source->draw_line(text, 0, -4);
    int h = (int)std::max(1.0, text.empty() ? 0.0 : ts.height);
    g2d::BitmapPtr b = g2d::Bitmap::create((int)line_width(), h);
    b->blt(source->contents_bitmap().get(), 0, 0, b->width(), b->height(), 0, 0);
    auto sprite = std::make_shared<g2d::Sprite>(b);
    sprite->y = last ? last->y + last->height() : 0;
    sprite->set_opacity(0);
    list.push_back(sprite);
    line_sprites_container->add_child(sprite);
    return sprite;
}

void SpriteScrollingText::update() {
    g2d::Sprite::update();
    update_lines();
}

void SpriteScrollingText::update_lines() {
    if (list.empty()) return;
    std::shared_ptr<g2d::Sprite> first = list.front();
    std::shared_ptr<g2d::Sprite> last = list.back();
    bool needs = (first->y < 0 && first->y >= -first->height()) || last->y + last->height() > height();
    if (needs) {
        first->y = std::max(first->y - 3, -first->height());
        first->set_opacity(first->opacity() - 20);
    }
    if (first->y <= -first->height()) {
        list.erase(list.begin());
        line_sprites_container->remove_child(first.get());
        if (!list.empty()) list.front()->y = 0;
    }
    if (list.empty()) return;
    const double fade = 30;
    if (first->y >= 0 && first->opacity() < 255) first->set_opacity(first->opacity() + fade);
    for (size_t i = 1; i < list.size(); i++) {
        auto& s = list[i];
        auto& prev = list[i - 1];
        s->y = prev->y + prev->height();
        if (s->opacity() < 255 && s->y > 0 && s->y <= height()) s->set_opacity(s->opacity() + fade);
    }
}

void WindowBattleLog::initialize() {
    WindowSelectable::initialize(0, 0, window_width(), window_height());
    set_window_alpha(0);
    lines_.clear();
    methods_.clear();
    wait_count_ = 0;
    wait_mode_.clear();
    base_line_stack_.clear();
    active_chain_skill_input_counter_ = 0;
    active_chain_skill_ = 0;
    waiting_for_input_ = false;
    x = 140;
    y = -4;
    scroll_text_sprite = std::make_shared<SpriteScrollingText>();
    add_child(scroll_text_sprite);
    set_window_alpha(255);
}

void WindowBattleLog::update() {
    WindowChainSkillList* chain = refs().chain;
    if (chain && chain->visible) update_chain_skill_input();
    if (!update_wait()) call_next_method();
    scroll_text_sprite->update();
}

bool WindowBattleLog::update_wait() { return update_wait_count() || update_wait_mode() || update_input_wait(); }

bool WindowBattleLog::update_wait_count() {
    int skill = current_action_skill_id();
    if (rpg::battle::st().phase != "battleEnd") {
        if (skill && in_list(skill, {46, 49, 52, 86, 89, 92, 126, 129, 132, 168, 171, 174}) && wait_count_ > 0) wait_count_ = 0;
    }
    if (wait_count_ > 0) {
        wait_count_ -= 1;
        if (wait_count_ < 0) wait_count_ = 0;
        return true;
    }
    return false;
}

bool WindowBattleLog::update_wait_mode() {
    SpritesetBattle* ss = refs().spriteset;
    if (wait_mode_ == "chainSkillInput") {
        if (active_chain_skill_input_counter_ > 0) return true;
    }
    if (wait_mode_ == "camera") {
        if (rpg::party_in_battle() && rpg::game_screen().camera_duration > 0) return true;
    } else if (wait_mode_ == "zoom") {
        if (rpg::game_screen().is_zooming()) return true;
    }
    if (ss) {
        if (wait_mode_ == "float") {
            if (ss->is_anyone_floating()) return true;
        } else if (wait_mode_ == "jump") {
            if (ss->is_anyone_jumping()) return true;
        } else if (wait_mode_ == "opacity") {
            if (ss->is_anyone_changing_opacity()) return true;
        }
    }
    bool waiting = false;
    if (ss) {
        if (wait_mode_ == "effect") waiting = ss->is_effecting();
        else if (wait_mode_ == "movement") waiting = ss->is_anyone_moving();
        else if (wait_mode_ == "animation") waiting = ss->is_animation_playing();
        else if (wait_mode_ == "popups") waiting = ss->is_popup_playing();
    }
    if (!waiting) wait_mode_.clear();
    return waiting;
}

bool WindowBattleLog::update_input_wait() {
    if (waiting_for_input_ && input::is_triggered(input::OK)) waiting_for_input_ = false;
    return waiting_for_input_;
}

void WindowBattleLog::call_next_method() {
    if (methods_.empty()) return;
    Method m = std::move(methods_.front());
    methods_.pop_front();
    if (m.fn) m.fn();
}

void WindowBattleLog::push_fn(const std::string& name, std::function<void()> fn) { methods_.push_back(Method{name, std::move(fn)}); }

void WindowBattleLog::push(const std::string& method, std::vector<js::Val> args) {
    push_fn(method, [this, method, args]() mutable { call_named(method, args); });
}

void WindowBattleLog::call_named(const std::string& name, std::vector<js::Val>& a) {
    auto arg = [&](size_t i) { return i < a.size() ? a[i] : js::Val(); };
    auto battler = [&](size_t i) { return rpg::bind::battler_of(arg(i)); };
    if (name == "addText") add_text(arg(0).to_str());
    else if (name == "wait") wait();
    else if (name == "waitForInput") waiting_for_input_ = true;
    else if (name == "waitFrames") wait_count_ = arg(0).to_int();
    else if (name == "clear") clear();
    else if (name == "playSE") rpg::audio_manager::play_se(rpg::AudioObj::from_js(arg(0)));
    else if (name == "showGlobalDefeatMessage") {
    } else if (name == "pushBaseLine") push_base_line();
    else if (name == "popBaseLine") pop_base_line();
    else if (name == "waitForNewLine") wait_for_new_line();
    else if (name == "waitForEffect") wait_for_effect();
    else if (name == "waitForMovement") wait_for_movement();
    else if (name == "waitForAnimation") wait_for_animation();
    else if (name == "waitForPopups") wait_for_popups();
    else if (name == "waitForFloat") wait_for_float();
    else if (name == "waitForJump") wait_for_jump();
    else if (name == "waitForOpacity") wait_for_opacity();
    else if (name == "waitForCamera") wait_for_camera();
    else if (name == "waitForZoom") wait_for_zoom();
    else if (name == "setWaitMode") set_wait_mode(arg(0).to_str());
    else if (name == "popupDamage") {
    } else if (name == "startChainSkillInputWait") start_chain_skill_input_wait();
    else if (name == "hideChainSkillList") hide_chain_skill_list();
    else if (name == "addInstantText") add_instant_text(arg(0).to_str());
    else if (name == "performActionStart") {
        if (auto* b = battler(0)) b->perform_action_start(js::host_cast<rpg::GameAction>(arg(1)));
    } else if (name == "performAction") {
        if (auto* b = battler(0)) b->perform_action(js::host_cast<rpg::GameAction>(arg(1)));
    } else if (name == "performActionEnd") {
        if (auto* b = battler(0)) b->perform_action_end();
    } else if (name == "performDamage") {
        if (auto* b = battler(0)) b->perform_damage();
    } else if (name == "performMiss") {
        if (auto* b = battler(0)) b->perform_miss();
    } else if (name == "performRecovery") {
        if (auto* b = battler(0)) b->perform_recovery();
    } else if (name == "performEvasion") {
        if (auto* b = battler(0)) b->perform_evasion();
    } else if (name == "performMagicEvasion") {
        if (auto* b = battler(0)) b->perform_magic_evasion();
    } else if (name == "performCounter") {
        if (auto* b = battler(0)) b->perform_counter();
    } else if (name == "performReflection") {
        if (auto* b = battler(0)) b->perform_reflection();
    } else if (name == "performSubstitute") {
        if (auto* b = battler(0)) b->perform_substitute(battler(1));
    } else if (name == "performCollapse") {
        if (auto* b = battler(0)) b->perform_collapse();
    } else if (name == "showAnimation") {
        show_animation(battler(0), battlers_of(arg(1)), arg(2).to_int());
    } else if (name == "showNormalAnimation") {
        show_normal_animation(battlers_of(arg(0)), arg(1).to_int(), arg(2).truthy());
    } else if (name == "showAttackAnimation") {
        show_attack_animation(battler(0), battlers_of(arg(1)));
    } else {
        rpg::script_error("Window_BattleLog", "Method not found: " + name);
    }
}

void WindowBattleLog::pop_base_line() {
    if (base_line_stack_.empty()) return;
    int base = base_line_stack_.back();
    base_line_stack_.pop_back();
    while ((int)lines_.size() > base) lines_.pop_back();
}

void WindowBattleLog::wait_for_new_line() {
    int base = base_line_stack_.empty() ? 0 : base_line_stack_.back();
    if ((int)lines_.size() > base) wait();
}

std::string WindowBattleLog::last_method() const { return methods_.empty() ? std::string() : methods_.back().name; }

bool WindowBattleLog::is_busy() { return wait_count_ > 0 || !wait_mode_.empty() || !methods_.empty() || waiting_for_input_; }

void WindowBattleLog::clear() {
    lines_.clear();
    base_line_stack_.clear();
    scroll_text_sprite->clear();
}

void WindowBattleLog::add_text(const std::string& text) {
    if (scroll_text_sprite) {
        scroll_text_sprite->add_line(text);
        int speed = rpg::config().battle_log_speed;
        if (speed == 1) {
            push_wait();
        } else if (speed == 2) {
            int skill = current_action_skill_id();
            static const std::initializer_list<int> acs = {46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  59,  60,  61,  86,  87,  88,  89,  90,  91, 92,
                                                           93,  94,  97,  98,  99,  100, 101, 126, 127, 128, 129, 130, 131, 132, 133, 134, 137, 138, 139, 168,
                                                           167, 170, 171, 172, 173, 174, 175, 176, 178, 179, 180};
            const std::string& phase = rpg::battle::st().phase;
            if (skill && in_list(skill, acs)) push_wait();
            else if (phase == "endBattle" || phase == "victory") push_wait();
            else {
                push_wait();
                push_wait();
            }
        }
    }
    lines_.push_back(text);
    wait();
}

void WindowBattleLog::add_instant_text(const std::string& text) {
    if (scroll_text_sprite) {
        auto s = scroll_text_sprite->add_line(text);
        s->set_opacity(255);
    }
}

void WindowBattleLog::push_add_text(const std::string& text) {
    push_fn("addText", [this, text]() { add_text(text); });
}

void WindowBattleLog::push_wait() {
    push_fn("wait", [this]() { wait(); });
}

void WindowBattleLog::push_battler_call(const std::string& name, rpg::GameBattler* b, void (*fn)(rpg::GameBattler*)) {
    rpg::BattlerRef ref = rpg::BattlerRef::of(b);
    push_fn(name, [ref, fn]() {
        if (rpg::GameBattler* x = ref.get()) fn(x);
    });
}

void WindowBattleLog::start_turn() { push_wait(); }

void WindowBattleLog::start_action(rpg::GameBattler* subject, rpg::GameAction* action, const std::vector<rpg::GameBattler*>& targets) {
    (void)targets;
    rpg::BattlerRef ref = rpg::BattlerRef::of(subject);
    rpg::ItemRef item = action ? action->item() : rpg::ItemRef();
    push_fn("processCustomCode", [ref]() {
        if (rpg::GameBattler* s = ref.get()) s->enable_active_chain();
    });
    push_fn("showChainSkillList", [this, ref, item]() { show_chain_skill_list(ref.get(), item); });
    push_fn("processCustomCode", []() {});
    push_fn("clear", [this]() { clear(); });
    push_fn("startChainSkillInputWait", [this]() { start_chain_skill_input_wait(); });
}

void WindowBattleLog::end_action(rpg::GameBattler* subject) {
    (void)subject;
    multi_hit_flag_ = false;
}

void WindowBattleLog::display_action_results(rpg::GameBattler* subject, rpg::GameBattler* target) {
    if (!subject || !target) return;
    auto& st = rpg::battle::st();
    rpg::ItemRef item = st.action ? st.action->item() : rpg::ItemRef();
    if (item.valid() && item_meta(item)["BattleLogType"].truthy()) {
        display_custom_action_text(subject, target, item);
    } else {
        if (yparam_bool("BECOptSpeed")) {
            if (target->result().used) {
                display_critical(target);
                display_damage(target);
                display_affected_status(target);
                display_failure(target);
            }
        } else if (target->result().used) {
            push_fn("pushBaseLine", [this]() { push_base_line(); });
            display_critical(target);
            push_fn("popupDamage", []() {});
            push_fn("popupDamage", []() {});
            display_damage(target);
            display_affected_status(target);
            display_failure(target);
            push_fn("waitForNewLine", [this]() { wait_for_new_line(); });
            push_fn("popBaseLine", [this]() { pop_base_line(); });
        }
        if (target->is_dead()) target->perform_collapse();
    }
    if (subject->is_actor()) {
        WindowChainSkillList* chain = refs().chain;
        if (chain && chain->visible) {
            push_fn("setWaitMode", [this]() { set_wait_mode("chainSkillInput"); });
            push_fn("hideChainSkillList", [this]() { hide_chain_skill_list(); });
        }
        rpg::BattlerRef ref = rpg::BattlerRef::of(subject);
        push_fn("processCustomCode", [ref]() {
            if (rpg::GameBattler* s = ref.get()) s->disable_active_chain();
        });
    }
}

void WindowBattleLog::display_critical(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowCritText")) return;
    if (target->result().critical) push_add_text(target->is_actor() ? msg("criticalToActor") : msg("criticalToEnemy"));
}

void WindowBattleLog::display_damage(rpg::GameBattler* target) {
    if (target->result().missed) {
        display_miss(target);
    } else if (target->result().evaded) {
        display_evasion(target);
    } else {
        display_hp_damage(target);
        display_mp_damage(target);
        display_tp_damage(target);
    }
}

void WindowBattleLog::display_miss(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowMissText")) return;
    std::string fmt;
    if (target->result().physical) {
        fmt = target->is_actor() ? msg("actorNoHit") : msg("enemyNoHit");
        push_battler_call("performMiss", target, [](rpg::GameBattler* b) { b->perform_miss(); });
    } else {
        fmt = msg("actionFailure");
    }
    push_add_text(rpg::js_format(fmt, {target->name()}));
}

void WindowBattleLog::display_evasion(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowEvaText")) return;
    std::string fmt;
    if (target->result().physical) {
        fmt = msg("evasion");
        push_battler_call("performEvasion", target, [](rpg::GameBattler* b) { b->perform_evasion(); });
    } else {
        fmt = msg("magicEvasion");
        push_battler_call("performMagicEvasion", target, [](rpg::GameBattler* b) { b->perform_magic_evasion(); });
    }
    push_add_text(rpg::js_format(fmt, {target->name()}));
}

void WindowBattleLog::display_hp_damage(rpg::GameBattler* target) {
    rpg::GameActionResult& r = target->result();
    if (r.is_hit() && r.hp_damage > 0) {
        if (r.element_strong) {
            push_add_text("...It was a moving attack!");
            push_fn("waitForNewLine", [this]() { wait_for_new_line(); });
        } else if (r.element_weak) {
            push_add_text("...It was a dull attack!");
            push_fn("waitForNewLine", [this]() { wait_for_new_line(); });
        }
    }
    if (!yparam_bool("BECShowHpText")) return;
    if (r.hp_affected) {
        if (r.hp_damage > 0 && !r.drain) push_battler_call("performDamage", target, [](rpg::GameBattler* b) { b->perform_damage(); });
        if (r.hp_damage < 0) push_battler_call("performRecovery", target, [](rpg::GameBattler* b) { b->perform_recovery(); });
        push_add_text(make_hp_damage_text(target));
    }
}

void WindowBattleLog::display_mp_damage(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowMpText")) return;
    if (target->is_alive() && target->result().mp_damage != 0) {
        if (target->result().mp_damage < 0) push_battler_call("performRecovery", target, [](rpg::GameBattler* b) { b->perform_recovery(); });
        push_add_text(make_mp_damage_text(target));
    }
}

void WindowBattleLog::display_tp_damage(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowTpText")) return;
    if (target->is_alive() && target->result().tp_damage != 0) {
        if (target->result().tp_damage < 0) push_battler_call("performRecovery", target, [](rpg::GameBattler* b) { b->perform_recovery(); });
        push_add_text(make_tp_damage_text(target));
    }
}

void WindowBattleLog::display_affected_status(rpg::GameBattler* target) {
    if (!target->result().is_status_affected()) return;
    push_fn("pushBaseLine", [this]() { push_base_line(); });
    display_added_states(target);
    display_removed_states(target);
    display_changed_buffs(target);
    push_fn("waitForNewLine", [this]() { wait_for_new_line(); });
    push_fn("popBaseLine", [this]() { pop_base_line(); });
}

void WindowBattleLog::display_added_states(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowStateText")) return;
    for (int id : target->result().added_states) {
        const Value& state = data::states()[id];
        if (!state.is_obj()) continue;
        std::string m = target->is_actor() ? state["message1"].string() : state["message2"].string();
        if (id == target->death_state_id()) push_battler_call("performCollapse", target, [](rpg::GameBattler* b) { b->perform_collapse(); });
        if (!m.empty()) {
            push_fn("popBaseLine", [this]() { pop_base_line(); });
            push_fn("pushBaseLine", [this]() { push_base_line(); });
            push_add_text(target->name() + m);
            push_fn("waitForEffect", [this]() { wait_for_effect(); });
        }
    }
}

void WindowBattleLog::display_removed_states(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowStateText")) return;
    for (int id : target->result().removed_states) {
        const Value& state = data::states()[id];
        if (!state.is_obj()) continue;
        std::string m = state["message4"].string();
        if (!m.empty()) {
            push_fn("popBaseLine", [this]() { pop_base_line(); });
            push_fn("pushBaseLine", [this]() { push_base_line(); });
            push_add_text(target->name() + m);
        }
    }
}

void WindowBattleLog::display_changed_buffs(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowBuffText")) return;
    rpg::GameActionResult& r = target->result();
    display_buffs(target, r.added_buffs, msg("buffAdd"));
    display_buffs(target, r.added_debuffs, msg("debuffAdd"));
    display_buffs(target, r.removed_buffs, msg("buffRemove"));
}

void WindowBattleLog::display_buffs(rpg::GameBattler* target, const std::vector<int>& buffs, const std::string& fmt) {
    for (int id : buffs) {
        push_fn("popBaseLine", [this]() { pop_base_line(); });
        push_fn("pushBaseLine", [this]() { push_base_line(); });
        push_add_text(rpg::js_format(fmt, {target->name(), system_text("param", std::to_string(id))}));
    }
}

void WindowBattleLog::display_failure(rpg::GameBattler* target) {
    if (!yparam_bool("BECShowFailText")) return;
    if (target->result().is_hit() && !target->result().success) push_add_text(rpg::js_format(msg("actionFailure"), {target->name()}));
}

std::string WindowBattleLog::make_hp_damage_text(rpg::GameBattler* target) {
    rpg::GameActionResult& r = target->result();
    double damage = r.hp_damage;
    bool actor = target->is_actor();
    if (damage > 0 && r.drain) return rpg::js_format(actor ? msg("actorDrain") : msg("enemyDrain"), {target->name(), basic(2), num(damage)});
    if (damage > 0) return rpg::js_format(actor ? msg("actorDamage") : msg("enemyDamage"), {target->name(), num(damage)});
    if (damage < 0) return rpg::js_format(actor ? msg("actorRecovery") : msg("enemyRecovery"), {target->name(), basic(2), num(-damage)});
    return rpg::js_format(actor ? msg("actorNoDamage") : msg("enemyNoDamage"), {target->name()});
}

std::string WindowBattleLog::make_mp_damage_text(rpg::GameBattler* target) {
    rpg::GameActionResult& r = target->result();
    double damage = r.mp_damage;
    bool actor = target->is_actor();
    if (damage > 0 && r.drain) return rpg::js_format(actor ? msg("actorDrain") : msg("enemyDrain"), {target->name(), basic(4), num(damage)});
    if (damage > 0) return rpg::js_format(actor ? msg("actorLoss") : msg("enemyLoss"), {target->name(), basic(4), num(damage)});
    if (damage < 0) return rpg::js_format(actor ? msg("actorRecovery") : msg("enemyRecovery"), {target->name(), basic(4), num(-damage)});
    return "";
}

std::string WindowBattleLog::make_tp_damage_text(rpg::GameBattler* target) {
    rpg::GameActionResult& r = target->result();
    double damage = r.tp_damage;
    bool actor = target->is_actor();
    if (damage > 0) return rpg::js_format(actor ? msg("actorLoss") : msg("enemyLoss"), {target->name(), basic(6), num(damage)});
    if (damage < 0) return rpg::js_format(actor ? msg("actorGain") : msg("enemyGain"), {target->name(), basic(6), num(-damage)});
    return "";
}

void WindowBattleLog::display_counter(rpg::GameBattler* target) {
    if (yparam_bool("BECShowCntText")) add_text(rpg::js_format(msg("counterAttack"), {target->name()}));
    target->perform_counter();
    rpg::GameBattler* sub = rpg::battle::subject();
    std::vector<rpg::GameBattler*> ts;
    if (sub) ts.push_back(sub);
    show_attack_animation(target, ts);
    wait_for_animation();
}

void WindowBattleLog::display_reflection(rpg::GameBattler* target) {
    if (yparam_bool("BECShowRflText")) add_text(rpg::js_format(msg("magicReflection"), {target->name()}));
    target->perform_reflection();
    auto& st = rpg::battle::st();
    int ani = st.action ? st.action->item_data()["animationId"].as_int() : 0;
    rpg::GameBattler* sub = rpg::battle::subject();
    std::vector<rpg::GameBattler*> ts;
    if (sub) ts.push_back(sub);
    show_normal_animation(ts, ani, false);
    wait_for_animation();
}

void WindowBattleLog::display_substitute(rpg::GameBattler* substitute, rpg::GameBattler* target) {
    if (yparam_bool("BECShowSubText")) add_text(rpg::js_format(msg("substitute"), {substitute->name(), target->name()}));
    substitute->perform_substitute(target);
}

void WindowBattleLog::display_auto_affected_status(rpg::GameBattler* target) {
    if (target->result().is_status_affected()) {
        display_affected_status(target);
        push_fn("clear", [this]() { clear(); });
    }
}

void WindowBattleLog::display_current_state(rpg::GameBattler* subject) {
    if (!yparam_bool("BECShowStateText")) return;
    std::string text = subject->most_important_state_text();
    if (!text.empty()) {
        push_add_text(subject->name() + text);
        push_wait();
        push_fn("clear", [this]() { clear(); });
    }
}

void WindowBattleLog::display_regeneration(rpg::GameBattler* subject) {
    (void)subject;
    push_fn("popupDamage", []() {});
}

void WindowBattleLog::display_action(rpg::GameBattler* subject, const rpg::ItemRef& item) {
    if (!item.valid()) return;
    if (item_meta(item)["BattleLogType"].truthy()) return;
    const Value& d = item.data();
    if (yparam_bool("BECFullActText")) {
        size_t count = methods_.size();
        if (item.is_skill()) {
            std::string m1 = d["message1"].string(), m2 = d["message2"].string();
            if (!m1.empty()) push_add_text(subject->name() + rpg::js_format(m1, {d["name"].string()}));
            if (!m2.empty()) push_add_text(rpg::js_format(m2, {d["name"].string()}));
        } else {
            push_add_text(rpg::js_format(msg("useItem"), {subject->name(), d["name"].string()}));
        }
        if (methods_.size() == count) push_wait();
    } else {
        bool counter = rpg::battle::is_countering();
        action_icon_ = counter ? d["counterIcon"].as_int() : d["battleDisplayIcon"].as_int();
        std::string text = counter ? d["counterName"].string() : d["battleDisplayText"].string();
        push_add_text("<SIMPLE>" + text);
        std::string m2 = d["message2"].string();
        if (!m2.empty()) push_add_text("<CENTER>" + rpg::js_format(m2, {text}));
    }
}

std::string WindowBattleLog::make_custom_action_text(rpg::GameBattler* subject, rpg::GameBattler* target, const rpg::ItemRef& item) {
    js::Vm& vm = js::vm();
    js::Val self = log_js_handle().to_js();
    try {
        js::Val fn = vm.get(self, js::atom("makeCustomActionText"));
        if (!fn.is_function()) {
            rpg::script_error("Window_BattleLog.makeCustomActionText", "method is not available in the game data");
            return "";
        }
        std::vector<js::Val> args{subject ? subject->to_js() : js::Val(), target ? target->to_js() : js::Val(), item.to_js()};
        js::Val r = vm.call(fn, self, args);
        return r.to_str();
    } catch (js::Throw& t) {
        rpg::script_error("Window_BattleLog.makeCustomActionText", js::json_stringify(t.value));
        return "";
    }
}

void WindowBattleLog::display_custom_action_text(rpg::GameBattler* subject, rpg::GameBattler* target, const rpg::ItemRef& item) {
    std::string text = make_custom_action_text(subject, target, item);
    bool repeating = item.data()["isRepeatingSkill"].truthy();
    if (!text.empty()) {
        if (multi_hit_flag_ && repeating) return;
        size_t pos = 0;
        while (true) {
            size_t at = text.find("\r\n", pos);
            push_add_text(text.substr(pos, at == std::string::npos ? std::string::npos : at - pos));
            if (at == std::string::npos) break;
            pos = at + 2;
        }
        push_wait();
    }
    if (repeating) multi_hit_flag_ = true;
}

void WindowBattleLog::show_normal_animation(const std::vector<rpg::GameBattler*>& targets, int id, bool mirror) {
    const Value& animation = data::animations()[id];
    if (!animation.is_obj()) return;
    if (animation["position"].as_int() == 3) {
        for (rpg::GameBattler* t : targets) {
            if (t) t->start_animation(id, mirror, 0);
        }
        return;
    }
    double delay = yparam_num("BECAniBaseDel");
    double next = yparam_num("BECAniNextDel");
    for (rpg::GameBattler* t : targets) {
        if (t) t->start_animation(id, mirror, delay);
        delay += next;
    }
}

void WindowBattleLog::show_attack_animation(rpg::GameBattler* subject, const std::vector<rpg::GameBattler*>& targets) {
    if (!subject) return;
    if (subject->is_actor()) {
        rpg::GameActor* a = static_cast<rpg::GameActor*>(subject);
        show_normal_animation(targets, a->attack_animation_id1(), false);
        show_normal_animation(targets, a->attack_animation_id2(), true);
    } else {
        rpg::audio_manager::play_system_sound(9);
    }
}

void WindowBattleLog::show_actor_atk_ani_mirror(rpg::GameBattler* subject, const std::vector<rpg::GameBattler*>& targets) {
    if (!subject) return;
    if (subject->is_actor()) {
        rpg::GameActor* a = static_cast<rpg::GameActor*>(subject);
        show_normal_animation(targets, a->attack_animation_id1(), true);
        show_normal_animation(targets, a->attack_animation_id2(), false);
    } else {
        show_normal_animation(targets, static_cast<rpg::GameEnemy*>(subject)->attack_animation_id1(), true);
    }
}

void WindowBattleLog::show_animation(rpg::GameBattler* subject, const std::vector<rpg::GameBattler*>& targets, int id) {
    if (id < 0) show_attack_animation(subject, targets);
    else show_normal_animation(targets, id, false);
}

void WindowBattleLog::perform_action(rpg::GameBattler* subject, rpg::GameAction* action) {
    if (subject) subject->perform_action(action);
}

void WindowBattleLog::perform_action_end(rpg::GameBattler* subject) {
    if (subject) subject->perform_action_end();
}

void WindowBattleLog::perform_action_start(rpg::GameBattler* subject, rpg::GameAction* action) {
    if (subject) subject->perform_action_start(action);
}

void WindowBattleLog::show_chain_skill_list(rpg::GameBattler* subject, const rpg::ItemRef& skill) {
    if (rpg::game_switches().value(41)) return;
    WindowChainSkillList* chain = refs().chain;
    if (!subject || !subject->is_actor() || !skill.is_skill() || !skill.valid() || !chain) return;
    active_chain_skill_ = 0;
    chain->setup(subject, skill);
    if (!chain->chain_skills.empty()) chain->show();
    if (!chain->chain_skills.empty()) {
        if (refs().status) refs().status->show_actor_chain_skill_bubbles(static_cast<rpg::GameActor*>(subject), chain->chain_skills);
        chain->y = 480;
    }
}

void WindowBattleLog::hide_chain_skill_list() {
    active_chain_skill_ = 0;
    if (refs().chain) refs().chain->hide();
    if (refs().status) refs().status->show_actor_chain_skill_bubbles(nullptr, {});
}

void WindowBattleLog::start_chain_skill_input_wait() {
    active_chain_skill_input_counter_ = (int)rpg::js_number(data::plugin_param("Active Chain Skills", "Minimun Time", "120"));
}

void WindowBattleLog::update_chain_skill_input() {
    WindowChainSkillList* chain = refs().chain;
    if (!chain) return;
    bool had_input = chain->has_chain_input;
    if (active_chain_skill_ == 0) {
        for (auto& entry : chain->chain_skills) {
            if (!input::is_triggered(input::key_of(entry.first))) continue;
            rpg::GameBattler* subject = chain->battler;
            if (!subject) continue;
            rpg::ItemRef skill = rpg::ItemRef::skill(entry.second);
            if (!subject->can_use(skill)) continue;
            if (!subject->is_learned_skill(skill.id)) continue;
            std::string se = data::plugin_param("Active Chain Skills", "Active Skill Sound", "{\"name\": \"Skill2\", \"pan\": 0, \"pitch\": 100, \"volume\": 80}");
            rpg::audio_manager::play_se(rpg::AudioObj::from_js(js::json_parse_text(se)));
            chain->set_chain_input(entry.first);
            active_chain_skill_input_counter_ = 12;
            active_chain_skill_ = skill.id;
            subject->add_active_skill_chain(skill.id);
            break;
        }
    }
    if (active_chain_skill_input_counter_ > 0) active_chain_skill_input_counter_--;
    if (chain->has_chain_input && !had_input && refs().status && chain->battler && chain->battler->is_actor()) {
        refs().status->select_actor_chain_skill_bubble(static_cast<rpg::GameActor*>(chain->battler), chain->chain_input);
    }
}

js::Val WindowBattleLog::js() { return display_js(this); }

bool WindowBattleLog::display_js_get(uint32_t key, js::Val& out) {
    const std::string& n = js::atom_name(key);
    std::weak_ptr<g2d::DisplayObject> w = weak_from_this();
    auto self = [w]() -> WindowBattleLog* {
        auto o = w.lock();
        return o ? static_cast<WindowBattleLog*>(o.get()) : nullptr;
    };
    if (n == "_scrollTextSprite") {
        out = display_js(scroll_text_sprite.get());
    } else if (n == "displayCustomActionText") {
        out = js::make_native([self](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
            WindowBattleLog* l = self();
            if (!l) return js::Val();
            rpg::GameBattler* s = a.size() > 0 ? rpg::bind::battler_of(a[0]) : nullptr;
            rpg::GameBattler* t = a.size() > 1 ? rpg::bind::battler_of(a[1]) : nullptr;
            rpg::ItemRef item = a.size() > 2 ? rpg::ItemRef::from_js(a[2]) : rpg::ItemRef();
            if (s && t && item.valid()) l->display_custom_action_text(s, t, item);
            return js::Val();
        });
    } else if (n == "displayAddedStates" || n == "displayHpDamage" || n == "displayMpDamage") {
        std::string name = n;
        out = js::make_native([self, name](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
            WindowBattleLog* l = self();
            rpg::GameBattler* t = a.empty() ? nullptr : rpg::bind::battler_of(a[0]);
            if (!l || !t) return js::Val();
            if (name == "displayAddedStates") l->display_added_states(t);
            else if (name == "displayHpDamage") l->display_hp_damage(t);
            else l->display_mp_damage(t);
            return js::Val();
        });
    } else if (n == "displayActionResults") {
        out = js::make_native([self](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
            WindowBattleLog* l = self();
            if (l && a.size() > 1) l->display_action_results(rpg::bind::battler_of(a[0]), rpg::bind::battler_of(a[1]));
            return js::Val();
        });
    } else if (n == "push") {
        out = js::make_native([self](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
            WindowBattleLog* l = self();
            if (!l || a.empty()) return js::Val();
            std::vector<js::Val> rest(a.begin() + 1, a.end());
            l->push(a[0].to_str(), rest);
            return js::Val();
        });
    } else if (n == "clear" || n == "addText" || n == "addInstantText" || n == "wait") {
        std::string name = n;
        out = js::make_native([self, name](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
            WindowBattleLog* l = self();
            if (!l) return js::Val();
            if (name == "clear") l->clear();
            else if (name == "addText") l->add_text(a.empty() ? std::string() : a[0].to_str());
            else if (name == "addInstantText") l->add_instant_text(a.empty() ? std::string() : a[0].to_str());
            else l->wait();
            return js::Val();
        });
    } else if (n == "isBusy") {
        out = js::make_native([self](js::Vm&, const js::Val&, std::vector<js::Val>&) {
            WindowBattleLog* l = self();
            return js::Val::boolean(l && l->is_busy());
        });
    } else {
        return false;
    }
    return true;
}

std::vector<std::pair<std::string, int>> chain_skill_list(const rpg::ItemRef& skill) {
    std::vector<std::pair<std::string, int>> out;
    if (!skill.valid()) return out;
    const std::string& note = skill.data()["note"].string();
    if (note.empty()) return out;
    size_t from = 0;
    re::Groups g;
    while (from <= note.size() && re::search("<ChainSkill:(.+),(.+)>", "i", note, &g, from)) {
        std::string input = rpg::trim(g[2]);
        int id = (int)rpg::js_number(g[1]);
        bool replaced = false;
        for (auto& e : out) {
            if (e.first == input) {
                e.second = id;
                replaced = true;
            }
        }
        if (!replaced) out.push_back({input, id});
        size_t next = g.index + std::max<size_t>(g.length, 1);
        if (next <= from) break;
        from = next;
    }
    return out;
}

void WindowChainSkillList::initialize() {
    double dw = std::max(std::round(640 / 2.0), 450.0);
    WindowBase::initialize(-standard_padding(), 0, dw, fitting_height(6));
    set_window_alpha(0);
    WindowBase::hide();
}

void WindowChainSkillList::setup(rpg::GameBattler* b, const rpg::ItemRef& skill) {
    battler = b;
    skill_ = skill;
    chain_skills.clear();
    for (auto& e : chain_skill_list(skill)) {
        if (!b->is_learned_skill(e.second)) continue;
        chain_skills.push_back(e);
    }
    y = 480 - fitting_height(4);
    y -= fitting_height((int)chain_skills.size() + 1);
    enabled_ = true;
    refresh();
}

void WindowChainSkillList::show() {
    activate();
    WindowBase::show();
}

void WindowChainSkillList::set_chain_input(const std::string& value) {
    chain_input = value;
    has_chain_input = true;
    enabled_ = false;
    refresh();
}

void WindowChainSkillList::refresh() {
    if (enabled_) {
        chain_input.clear();
        has_chain_input = false;
    }
    c().clear();
    draw_background_color();
    draw_chain_skill_title();
    if (skill_.valid()) draw_chain_skills();
}

void WindowChainSkillList::draw_background_color() {
    double dh = line_height() * (chain_skills.size() + 1);
    c().gradient_fill_rect(0, 0, c().width(), dh, "rgba(0, 0, 0, 192)", "rgba(0, 0, 0, 0)", false);
    c().paint_opacity = 78;
    c().fill_rect(0, line_height() - 4, c().width(), 2, normal_color());
    c().paint_opacity = 255;
}

void WindowChainSkillList::draw_chain_skill_title() {
    reset_font_settings();
    c().font_size = (int)rpg::js_number(data::plugin_param("Active Chain Skills", "Title Font Size", "20"));
    c().font_italic = true;
    draw_text(data::plugin_param("Active Chain Skills", "Title Text", "Active Chain Skills"), 12, 0, c().width() - 24);
    reset_font_settings();
}

void WindowChainSkillList::draw_chain_skills() {
    double sx = 24, sy = line_height();
    for (auto& e : chain_skill_list(skill_)) {
        const Value& skill = data::skills()[e.second];
        if (!battler->is_learned_skill(skill["id"].as_int())) continue;
        std::string text = input_text_settings(e.first, skill);
        text += rpg::js_format("\\i[%1]%2", {num(skill["iconIndex"].num()), skill["name"].string()});
        draw_text_ex(text, sx, sy);
        sy += line_height();
    }
}

std::string WindowChainSkillList::input_text_settings(const std::string& input, const Value& skill) {
    bool active = has_chain_input && input == chain_input;
    static const struct {
        const char* input;
        const char* label;
    } names[] = {{"up", "(\xE2\x86\x91) Up Skill"}, {"down", "(\xE2\x86\x93) Down Skill"}, {"left", "(\xE2\x86\x90) Left Skill"}, {"right", "(\xE2\x86\x92) Right Skill"}};
    for (auto& n : names) {
        if (input != n.input) continue;
        std::string base = n.label;
        if (enabled_ && battler->can_use(rpg::ItemRef::skill(skill["id"].as_int()))) return data::plugin_param("Active Chain Skills", (base + " ON").c_str());
        if (!enabled_ && active) return data::plugin_param("Active Chain Skills", (base + " ACT").c_str());
        return data::plugin_param("Active Chain Skills", (base + " OFF").c_str());
    }
    return "";
}

void WindowBattleStatus::initialize() {
    WindowSelectable::initialize(30, 0, 1, 1);
    set_window_alpha(0);
}

int WindowBattleStatus::max_items() { return (int)rpg::game_party().battle_members().size(); }

void WindowBattleStatus::refresh() {
    if (!face_windows) return;
    for (auto& w : *face_windows) w->refresh();
}

void WindowBattleStatus::update() {
    WindowSelectable::update();
    down_arrow_visible = false;
    up_arrow_visible = false;
}

void WindowBattleStatus::update_cursor() {
    WindowSelectable::update_cursor();
    update_selected_face_windows();
}

void WindowBattleStatus::update_selected_face_windows() {
    if (!face_windows) return;
    for (size_t i = 0; i < face_windows->size(); i++) (*face_windows)[i]->selected = (int)i == index_;
}

int WindowBattleStatus::find_next_index_in_direction(const std::string& d, int i) {
    rpg::GameParty& p = rpg::game_party();
    auto has = [&](int s) { return p.member_at_status_index(s) != nullptr; };
    if (d == "up") {
        if (i == 2) {
            if (has(0)) return 0;
            if (has(1)) return 1;
        } else if (i == 3) {
            if (has(1)) return 1;
            if (has(0)) return 0;
        }
    } else if (d == "down") {
        if (i == 0) {
            if (has(2)) return 2;
            if (has(3)) return 3;
        } else if (i == 1) {
            if (has(3)) return 3;
            if (has(2)) return 2;
        }
    } else if (d == "left") {
        if (i == 1) {
            if (has(0)) return 0;
            if (has(2)) return 2;
        } else if (i == 3) {
            if (has(2)) return 2;
            if (has(0)) return 0;
        }
    } else if (d == "right") {
        if (i == 0) {
            if (has(1)) return 1;
            if (has(3)) return 3;
        } else if (i == 2) {
            if (has(3)) return 3;
            if (has(1)) return 1;
        }
    }
    return i;
}

void WindowBattleStatus::update_index_from_direction(const std::string& direction) {
    int i = index();
    if (i >= 0) {
        index_ = find_next_index_in_direction(direction, i);
        update_cursor();
        update_selected_face_windows();
    }
}

void WindowBattleStatus::update_status_requests() {
    if (rpg::battle::st().victory_phase) return;
    for (rpg::GameActor* a : rpg::game_party().battle_members()) {
        if (a && a->is_status_refresh_requested()) a->complete_status_refresh_request();
    }
}

void WindowBattleStatus::show_actor_chain_skill_bubbles(rpg::GameActor* actor, const std::vector<std::pair<std::string, int>>& list) {
    if (!face_windows) return;
    for (auto& w : *face_windows) {
        if (w->actor() == actor) {
            w->show_acs_bubbles(15);
            w->setup_acs_bubbles(list);
        } else {
            w->hide_acs_bubbles(15);
        }
    }
}

void WindowBattleStatus::select_actor_chain_skill_bubble(rpg::GameActor* actor, const std::string& direction) {
    if (!face_windows) return;
    for (auto& w : *face_windows) {
        if (w->actor() == actor) {
            w->select_acs_bubble(direction);
            break;
        }
    }
}

bool WindowBattleStatus::display_js_get(uint32_t key, js::Val& out) {
    const std::string& n = js::atom_name(key);
    if (n != "refresh" && n != "select" && n != "deselect") return false;
    std::weak_ptr<g2d::DisplayObject> w = weak_from_this();
    out = js::make_native([w, n](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
        auto o = w.lock();
        if (!o) return js::Val();
        WindowBattleStatus* s = static_cast<WindowBattleStatus*>(o.get());
        if (n == "refresh") s->refresh();
        else if (n == "select") s->select(a.empty() ? -1 : a[0].to_int());
        else s->deselect();
        return js::Val();
    });
    return true;
}

void WindowBattleActor::initialize(double nx, double ny) {
    WindowBattleStatus::initialize();
    x = nx;
    y = ny;
    set_openness(255);
    hide();
}

void WindowBattleActor::show() {
    select(0);
    WindowBattleStatus::show();
}

void WindowBattleActor::hide() {
    WindowBattleStatus::hide();
    rpg::game_party().select(nullptr);
}

void WindowBattleActor::select(int i) {
    WindowBattleStatus::select(i);
    rpg::game_party().select(actor());
}

rpg::GameActor* WindowBattleActor::actor() {
    auto m = rpg::game_party().members();
    int i = index();
    return i >= 0 && i < (int)m.size() ? m[(size_t)i] : nullptr;
}

void WindowBattleActor::auto_select() {
    auto action = rpg::battle::inputting_action();
    if (!action) return;
    input_lock = false;
    select_dead_ = false;
    set_cursor_all(false);
    if (action->is_for_user()) {
        if (rpg::battle::actor()) select(rpg::battle::actor()->index());
        input_lock = true;
    } else if (action->is_for_all()) {
        input_lock = true;
        set_cursor_all(true);
    } else if (action->is_for_dead_friend()) {
        select_dead_ = true;
        auto_select_first_dead_actor();
    }
    update_cursor();
}

void WindowBattleActor::update_cursor() {
    if (cursor_all()) {
        double h = max_rows() * item_height();
        set_cursor_rect(0, 0, c().width(), h);
        set_top_row(0);
    } else if (is_cursor_visible()) {
        ItemRect r = item_rect(index());
        set_cursor_rect(r.x, r.y, r.width, r.height);
    } else {
        set_cursor_rect(0, 0, 0, 0);
    }
}

void WindowBattleActor::auto_select_first_dead_actor() {
    auto m = rpg::game_party().members();
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i] && m[i]->is_dead()) {
            select((int)i);
            return;
        }
    }
}

void WindowBattleActor::update() {
    WindowBattleStatus::update();
    if (active && input::is_triggered(input::SHIFT)) {
        if (group_target_swap) group_target_swap();
    }
}

void WindowBattleEnemy::initialize(double nx, double ny) {
    enemies_.clear();
    double w = 640 - 192;
    double h = fitting_height(4);
    WindowSelectable::initialize(nx, ny, w, h);
    refresh();
    hide();
}

rpg::GameEnemy* WindowBattleEnemy::enemy() {
    int i = index();
    return i >= 0 && i < (int)enemies_.size() ? enemies_[(size_t)i] : nullptr;
}

int WindowBattleEnemy::enemy_index() {
    rpg::GameEnemy* e = enemy();
    return e ? e->index() : -1;
}

void WindowBattleEnemy::draw_item(int index) {
    if (index < 0 || index >= (int)enemies_.size()) return;
    reset_text_color();
    ItemRect r = item_rect_for_text(index);
    draw_text(enemies_[(size_t)index]->name(), r.x, r.y, r.width);
}

void WindowBattleEnemy::show() {
    refresh();
    select(0);
    WindowSelectable::show();
}

void WindowBattleEnemy::hide() {
    WindowSelectable::hide();
    rpg::game_troop().select(nullptr);
}

void WindowBattleEnemy::refresh() {
    enemies_.clear();
    for (rpg::GameBattler* b : rpg::game_troop().alive_members()) enemies_.push_back(static_cast<rpg::GameEnemy*>(b));
    std::stable_sort(enemies_.begin(), enemies_.end(), [](rpg::GameEnemy* a, rpg::GameEnemy* b) {
        if (a->sprite_pos_x() == b->sprite_pos_x()) return a->sprite_pos_y() < b->sprite_pos_y();
        return a->sprite_pos_x() < b->sprite_pos_x();
    });
    WindowSelectable::refresh();
}

void WindowBattleEnemy::select(int i) {
    WindowSelectable::select(i);
    rpg::game_troop().select(enemy());
}

void WindowBattleEnemy::auto_select() {
    std::string v = yparam_str("BECEnemyAutoSel");
    int i = (v == "0" || v.empty()) ? 0 : max_items() - 1;
    select(i);
}

void WindowBattleEnemy::update() {
    WindowSelectable::update();
    if (active && input::is_triggered(input::SHIFT)) {
        if (group_target_swap) group_target_swap();
    }
}

void WindowActorCommand::initialize() {
    custom_cursor_x_offset_ = 0;
    command_max_cols_ = 2;
    WindowCommand::initialize(0, 0);
    create_command_sprites();
    set_window_alpha(0);
    deactivate();
    actor_ = nullptr;
    set_openness(0);
}

void WindowActorCommand::make_command_list() {
    if (actor_ && actor_->has_custom_battle_commands()) {
        make_custom_actor_command_list();
    } else if (actor_) {
        add_attack_command();
        add_skill_commands();
        add_guard_command();
        add_item_command();
    }
    int world = rpg::game_variables().value(22).to_int();
    if (world == 3 || world == 4) {
        if (list_.size() > 2) list_.erase(list_.begin() + 2, list_.end());
    }
}

void WindowActorCommand::make_custom_actor_command_list() {
    const js::Val& list = actor_->battle_commands_list;
    if (!list.is_array()) return;
    std::vector<js::Val> items = list.o->items;
    for (auto& obj : items) add_actor_custom_command(obj);
}

void WindowActorCommand::add_actor_custom_command(const js::Val& obj) {
    js::Vm& vm = js::vm();
    std::string type = rpg::to_lower(vm.get(obj, js::atom("type")).to_str());
    int id = vm.get(obj, js::atom("id")).to_int();
    if (type == "attack") add_attack_command();
    else if (type == "skilllist") add_skill_commands();
    else if (type == "defend") add_guard_command();
    else if (type == "items") add_item_command();
    else if (type == "skilltypelist") {
        add_command(data::system()["skillTypes"][id].string(), "skill", true, js::Val::num(id));
    } else if (type == "skill") {
        rpg::ItemRef skill = rpg::ItemRef::skill(id);
        if (actor_->can_show_item_battle_command(skill)) {
            const Value* cn = skill.data()["meta"].find("CommandName");
            std::string name = cn && cn->truthy() ? data::js_string(*cn) : skill.data()["name"].string();
            add_command(rpg::trim(name), "actionSkill", actor_->can_use(skill), js::Val::num(id));
        }
    } else if (type == "item") {
        rpg::ItemRef item = rpg::ItemRef::item(id);
        if (actor_->can_show_item_battle_command(item)) {
            const Value* cn = item.data()["meta"].find("CommandName");
            std::string name = cn && cn->truthy() ? data::js_string(*cn) : item.data()["name"].string();
            add_command(rpg::trim(name), "actionItem", actor_->can_use(item), js::Val::num(id));
        }
    } else {
        add_command("ERROR", "ERROR", false);
    }
}

void WindowActorCommand::add_attack_command() { add_command(system_text("command", "2"), "attack", actor_->can_attack()); }

void WindowActorCommand::add_skill_commands() {
    std::vector<int> types = actor_->added_skill_types();
    std::sort(types.begin(), types.end());
    for (int t : types) add_command(data::system()["skillTypes"][t].string(), "skill", true, js::Val::num(t));
}

void WindowActorCommand::add_guard_command() { add_command(system_text("command", "3"), "guard", actor_->can_guard()); }
void WindowActorCommand::add_item_command() { add_command(system_text("command", "4"), "item"); }

bool WindowActorCommand::is_current_item_enabled() {
    if (actor_ && actor_->is_battle_command_disabled(index())) return false;
    return WindowCommand::is_current_item_enabled();
}

void WindowActorCommand::create_command_sprites() {
    std::string name = "BattleCommands_DreamWorld";
    custom_cursor_x_offset_ = 12;
    command_max_cols_ = 2;
    switch (rpg::game_variables().value(22).to_int()) {
    case 1:
        name = "BattleCommands_DreamWorld";
        command_max_cols_ = 2;
        break;
    case 2: name = "BattleCommands_Faraway"; break;
    case 3:
    case 4:
    case 5:
        name = "BattleCommands_BlackSpace";
        custom_cursor_x_offset_ = 90;
        command_max_cols_ = 1;
        break;
    }
    command_sprites.clear();
    g2d::BitmapPtr b = load_system(name);
    double sw = b->width() / (double)command_max_cols_;
    double sh = b->height() / 2.0;
    for (int i = 0; i < 4; i++) {
        auto s = std::make_shared<g2d::Sprite>(b);
        ItemRect r = item_rect(i);
        double sx = (i % command_max_cols_) * sw;
        double sy = std::floor(i / command_max_cols_) * sh;
        s->set_frame(sx, sy, sw, sh);
        s->x = r.x;
        s->y = r.y;
        command_sprites.push_back(s);
        add_child_to_back(s);
    }
}

void WindowActorCommand::setup(rpg::GameActor* actor) {
    actor_ = actor;
    clear_command_list();
    make_command_list();
    refresh();
    select_last();
    activate();
    open();
}

void WindowActorCommand::process_ok() {
    if (actor_) {
        if (rpg::config().command_remember) actor_->set_last_command_symbol(current_symbol());
        else actor_->set_last_command_symbol("");
    }
    WindowCommand::process_ok();
}

void WindowActorCommand::select_last() {
    select(0);
    if (actor_ && rpg::config().command_remember) {
        std::string symbol = actor_->last_command_symbol;
        select_symbol(symbol);
        if (symbol == "skill") {
            rpg::ItemRef skill = actor_->last_battle_skill.object();
            if (skill.valid()) select_ext(js::Val::num(skill.data()["stypeId"].num()));
        }
    }
}

SpriteBattleBSRedHand::SpriteBattleBSRedHand(int side) : side_(side) {
    g2d::BitmapPtr b = load_system("bsRedHands");
    double w = b->width() / 3.0, h = b->height() / 2.0;
    set_bitmap(b);
    set_frame(0, side * h, w, h);
    play_se("SE_red_hands", 90, 100, 0);
    move_speed_ = side == 0 ? -4 : 4;
    phase_ = 0;
    frame_ = random_int(4);
    count_ = 0;
    delay_ = 7;
    set_opacity(0);
}

void SpriteBattleBSRedHand::update() {
    g2d::Sprite::update();
    if (phase_ == 0) {
        set_opacity(opacity() + 30);
        x += move_speed_;
        if (opacity() >= 255) phase_ = 1;
    } else if (phase_ == 1) {
        bool mv = side_ == 0 ? x > 170 : x < 130;
        if (mv) x += move_speed_;
        else phase_ = 2;
    } else {
        set_opacity(opacity() - 10);
        if (opacity() <= 0 && parent) {
            parent->remove_child(this);
            return;
        }
    }
    static const int frames[] = {0, 1, 2, 1};
    count_--;
    if (count_ <= 0) {
        frame_ = (frame_ + 1) % 4;
        set_frame(frames[frame_] * width(), frame().y, width(), height());
        count_ = delay_;
    }
}

void WindowPartyCommand::initialize() {
    WindowCommand::initialize(0, 0);
    set_window_alpha(0);
    create_command_sprites();
    escape_block_container = std::make_shared<g2d::Sprite>();
    add_child(escape_block_container);
    deactivate();
}

void WindowPartyCommand::play_ok_sound() {
    if (current_world() == 5) {
        play_se("SE_play", 90, 100, 0);
        return;
    }
    WindowCommand::play_ok_sound();
}

ItemRect WindowPartyCommand::item_rect(int index) {
    ItemRect r = WindowCommand::item_rect(index);
    if (rpg::game_variables().value(22).to_int() == 5) {
        r.x += 95;
        r.y += 20;
    } else {
        static const double off[2] = {60, 80};
        r.x += index >= 0 && index < 2 ? off[index] : std::nan("");
    }
    return r;
}

void WindowPartyCommand::make_command_list() {
    add_command(system_text("command", "0"), "fight");
    if (rpg::game_variables().value(22).to_int() != 5) add_command(system_text("command", "1"), "escape", rpg::battle::can_escape());
}

void WindowPartyCommand::create_command_sprites() {
    std::string name = "party_command";
    int size = 2;
    switch (rpg::game_variables().value(22).to_int()) {
    case 1: name = "PartyCommands_DreamWorld"; break;
    case 2: name = "PartyCommands_Faraway"; break;
    case 3:
    case 4: name = "PartyCommands_BlackSpace"; break;
    case 5:
        name = "PartyCommands_FinalBattle";
        size = 1;
        break;
    }
    if (rpg::game_variables().value(1220).to_num() >= 5 && rpg::game_troop().troop_id == 891) {
        name = "PartyCommands_FinalBattle";
        size = 1;
    }
    command_sprites.clear();
    g2d::BitmapPtr b = load_system(name);
    if (size > 1) {
        double h = b->height() / (double)size;
        for (int i = 0; i < size; i++) {
            auto s = std::make_shared<g2d::Sprite>(b);
            s->set_frame(0, i * h, 360, h);
            s->y = i * (h + 3);
            command_sprites.push_back(s);
            add_child_to_back(s);
        }
    } else {
        auto s = std::make_shared<g2d::Sprite>(b);
        s->set_frame(0, 0, 360, b->height());
        s->y = 0;
        add_child_to_back(s);
    }
}

void WindowPartyCommand::process_ok() {
    int world = rpg::game_variables().value(22).to_int();
    if (current_symbol() == "escape" && (world == 4 || world == 5)) {
        if (escape_block_container->children.empty()) start_escape_block_effect();
        return;
    }
    escape_block_container->remove_children();
    WindowCommand::process_ok();
}

void WindowPartyCommand::start_escape_block_effect() {
    static const double spacing[3] = {20, 0, 30};
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < 3; i++) {
            auto sprite = std::make_shared<SpriteBattleBSRedHand>(s);
            sprite->x = s == 0 ? (width() - 60) + spacing[i] : -0 - spacing[i];
            sprite->y = (item_rect(1).y - 25) + i * 13;
            escape_block_container->add_child(sprite);
        }
    }
}

void WindowPartyCommand::setup() {
    clear_command_list();
    make_command_list();
    refresh();
    select(0);
    activate();
    open();
}

void WindowItemListBack::initialize(double w, double h) {
    WindowBase::initialize(0, -30, w, h);
    c().fill_rect(0, 28, c().width(), 2, "rgba(255, 255, 255, 2)");
}

void WindowItemListBack::set_item(const rpg::ItemRef& item, rpg::GameActor* actor) {
    c().clear_rect(0, 0, c().width(), 28);
    const Value& loc = menu::blue("Omori_Battle_System");
    if (item.valid() && item.is_item()) {
        c().draw_text(rpg::js_format(loc["hold"].string(), {num(rpg::game_party().battle_num_items(item))}), 6, 2, 100, 20, "left");
    }
    if (item.valid() && item.is_skill()) {
        c().draw_text(loc["cost"].string(), 6, 2, 100, 20, "left");
        if (actor) c().draw_text(num(actor->skill_mp_cost(item)), 0, 2, 95, 20, "right");
        draw_mp_icon(100, 6);
    }
}

void WindowBattleSkill::initialize(double nx, double ny, double w, double h) {
    WindowSelectable::initialize(nx, ny + 30, w, 90);
    actor_ = nullptr;
    stype_id_ = 0;
    data_.clear();
    back_window_ = WindowBase::make<WindowItemListBack>(w, h);
    add_child_to_back(back_window_);
    set_window_alpha(0);
    double ww = width(), wh = height(), q = 12;
    down_arrow_sprite()->move(ww - q, (wh - q) + 6);
    up_arrow_sprite()->move(ww - q, q + 1);
    hide();
}

int WindowBattleSkill::max_cols() { return (int)yparam_num("SCCWindowCol", 2); }

rpg::ItemRef WindowBattleSkill::item() {
    int i = index();
    return i >= 0 && i < (int)data_.size() ? data_[(size_t)i] : rpg::ItemRef();
}

bool WindowBattleSkill::is_current_item_enabled() { return is_enabled(item()); }

bool WindowBattleSkill::is_enabled(const rpg::ItemRef& item) { return actor_ && item.valid() && actor_->can_use(item); }

void WindowBattleSkill::make_item_list() {
    data_.clear();
    if (!actor_) return;
    for (const rpg::ItemRef& s : actor_->skills()) {
        if (includes(s)) data_.push_back(s);
    }
}

void WindowBattleSkill::select_last() {
    if (!rpg::config().command_remember) {
        select(0);
        return;
    }
    rpg::ItemRef last = actor_ ? (rpg::party_in_battle() ? actor_->last_battle_skill.object() : actor_->last_menu_skill.object()) : rpg::ItemRef();
    int idx = -1;
    for (size_t i = 0; i < data_.size(); i++) {
        if (last.valid() && data_[i] == last) {
            idx = (int)i;
            break;
        }
    }
    select(idx >= 0 ? idx : 0);
}

void WindowBattleSkill::set_actor(rpg::GameActor* actor) {
    if (actor_ != actor) {
        actor_ = actor;
        refresh();
        reset_scroll();
    }
}

void WindowBattleSkill::set_stype_id(int id) {
    if (stype_id_ != id) {
        stype_id_ = id;
        refresh();
        reset_scroll();
    }
}

ItemRect WindowBattleSkill::item_rect(int index) {
    ItemRect r = WindowSelectable::item_rect(index);
    r.x += 36;
    return r;
}

void WindowBattleSkill::draw_item(int index) {
    if (index < 0 || index >= (int)data_.size()) return;
    rpg::ItemRef item = data_[(size_t)index];
    c().font_size = 24;
    if (!item.valid()) return;
    ItemRect r = item_rect(index);
    r.width -= text_padding();
    r.width -= 40;
    change_paint_opacity(is_enabled(item));
    const Value* sn = item.data()["meta"].find("ShortName");
    std::string name = sn && sn->truthy() ? rpg::trim(data::js_string(*sn)) : item.data()["name"].string();
    c().draw_text(name, r.x + 5, r.y, r.width, r.height, "left");
    change_paint_opacity(true);
}

void WindowBattleSkill::update_help() {
    set_help_window_item(item());
    if (back_window_) back_window_->set_item(item(), actor_);
}

void WindowBattleSkill::refresh() {
    make_item_list();
    create_contents();
    draw_all_items();
}

void WindowBattleSkill::show() {
    select_last();
    show_help_window();
    WindowSelectable::show();
}

void WindowBattleSkill::hide() {
    hide_help_window();
    WindowSelectable::hide();
}

void WindowBattleItem::initialize(double nx, double ny, double w, double h) {
    menu::WindowItemList::initialize(nx, ny + 30, w, 90);
    back_window_ = WindowBase::make<WindowItemListBack>(w, h);
    add_child_to_back(back_window_);
    set_window_alpha(0);
    up_arrow_sprite()->x = width() - 12;
    down_arrow_sprite()->x = up_arrow_sprite()->x;
    down_arrow_sprite()->y = height() - 33;
    hide();
}

bool WindowBattleItem::includes(const rpg::ItemRef& item) {
    if (!item.valid() || !item.is_item()) return false;
    if (!rpg::game_party().can_use(item)) return false;
    if (rpg::game_party().battle_num_items(item) <= 0) return false;
    if (category_ == "toys") return rpg::is_toy_item(item.data());
    if (category_ == "snacks") return rpg::is_consumable_item(item.data());
    return false;
}

ItemRect WindowBattleItem::item_rect(int index) {
    ItemRect r = WindowSelectable::item_rect(index);
    r.x += 36;
    return r;
}

void WindowBattleItem::draw_item(int index) {
    if (index < 0 || index >= (int)data_.size()) return;
    rpg::ItemRef item = data_[(size_t)index];
    c().font_size = 24;
    if (!item.valid()) return;
    ItemRect r = item_rect(index);
    r.width -= text_padding();
    r.width -= 40;
    change_paint_opacity(is_enabled(item));
    const Value* sn = item.data()["meta"].find("ShortName");
    std::string name = sn && sn->truthy() ? rpg::trim(data::js_string(*sn)) : item.data()["name"].string();
    c().draw_text(name, r.x, r.y, r.width, r.height, "left");
    change_paint_opacity(true);
}

void WindowBattleItem::update_help() {
    menu::WindowItemList::update_help();
    if (back_window_) back_window_->set_item(item(), nullptr);
}

void WindowBattleItem::cursor_down(bool wrap) {
    (void)wrap;
    int i = index(), mi = max_items(), mc = max_cols();
    if (i < mi - mc) select((i + mc) % mi);
    else if (i < mi && mi > 2 && i == mi - 2) select(mi - 1);
}

void WindowBattleItem::show() {
    select_last();
    show_help_window();
    menu::WindowItemList::show();
}

void WindowBattleItem::hide() {
    hide_help_window();
    menu::WindowItemList::hide();
}

}
}
