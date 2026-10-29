#include <algorithm>
#include <cmath>

#include "../core/image.h"
#include "../js/vm.h"
#include "../rpg/audio_manager.h"
#include "../rpg/bind_util.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "battle_windows.h"
#include "message.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace btl {

namespace {

std::string general_text(const char* key) { return menu::lang_pinned("XX_GENERAL", key)["text"].string(); }

bool troop_in(std::initializer_list<int> list) {
    int id = rpg::game_troop().troop_id;
    for (int t : list) {
        if (t == id) return true;
    }
    return false;
}

}

SceneBattle::SceneBattle() {
    using_tone_filter_ = false;
    for (double& v : main_status_tone_) v = 0;
    tone_filter_ = std::make_shared<ToneOverlay>();
    main_status_tone_filter_ = std::make_shared<ToneOverlay>();
    main_status_tone_filter_->tone = main_status_tone_;
}

SceneBattle::~SceneBattle() {
    if (refs().scene == this) refs() = Refs();
}

void SceneBattle::create() {
    auto& st = rpg::battle::st();
    refs() = Refs();
    refs().scene = this;
    refs().scene_node = this;
    if (st.battle_retried) {
        st.waiting_for_retry_video = st.is_final_battle;
        retry_fade_in_anim_ = 20;
        current_retry_fade_in_anim_ = retry_fade_in_anim_;
        rpg::game_screen().start_fade_out(1);
        rpg::Scene::create();
        create_display_objects();
        stress_bar->set_opacity(0);
        help_window->set_window_alpha(255);
        log_window->set_window_alpha(255);
        log_window->set_visible(true);
        party_command_window->set_window_alpha(0);
        actor_window->set_window_alpha(255);
        face_windows_container->set_opacity(255);
        if (auto* w = dynamic_cast<g2d::Window*>(scroll_text_layer_.get())) w->set_window_opacity(255);
    } else {
        rpg::Scene::create();
        create_display_objects();
    }
}

void SceneBattle::create_display_objects() {
    if (profile::BACKGROUND_DECODE) images::prefetch("img/system", "Damage");
    spriteset = std::make_shared<SpritesetBattle>();
    spriteset->scene = this;
    refs().spriteset = spriteset.get();
    spriteset->initialize();
    add_child(spriteset);
    tone_filter_->tone = spriteset->tone;
    create_window_layer();
    create_all_windows();
    rpg::battle::register_scene(log_window.get(), spriteset.get(), this);
    if (spriteset->timer_sprite) add_child(spriteset->timer_sprite);
    create_battle_intro_container();
    add_tone_filter_compatibility();
}

void SceneBattle::create_all_windows() {
    create_log_window();
    create_status_window();
    create_party_command_window();
    create_actor_command_window();
    create_help_window();
    create_skill_window();
    create_item_window();
    create_actor_window();
    create_enemy_window();
    size_t before = window_layer->children.size();
    message_window = create_message_window(*this);
    size_t middle = window_layer->children.size();
    scroll_text_window = create_scroll_text_window(*this);
    size_t after = window_layer->children.size();
    message_layer_children_.clear();
    for (size_t i = before; i < after && i < window_layer->children.size(); i++) message_layer_children_.push_back(window_layer->children[i].get());
    scroll_text_layer_ = middle < after && middle < window_layer->children.size() ? window_layer->children[middle] : nullptr;
    create_chain_skill_window();
    party_command_window->x = 140;
    party_command_window->y = 480;
    actor_command_window->x = 140;
    actor_command_window->y = 480;
    create_stress_bar();
    low_hp_overlay = std::make_shared<SpriteBattleLowHpOverlay>();
    add_child(low_hp_overlay);
}

void SceneBattle::create_log_window() {
    log_window = WindowBase::make<WindowBattleLog>();
    refs().log = log_window.get();
    add_window(log_window);
}

void SceneBattle::create_status_window() {
    status_window = WindowBase::make<WindowBattleStatus>();
    refs().status = status_window.get();
    refs().status_base = status_window.get();
    add_window(status_window);
    create_face_windows();
}

void SceneBattle::create_face_windows() {
    face_windows_container = std::make_shared<g2d::Sprite>();
    add_child(face_windows_container);
    layers.container = std::make_shared<g2d::Sprite>();
    face_windows_container->add_child(layers.container);
    std::shared_ptr<g2d::Sprite>* list[5] = {&layers.behind, &layers.status_back, &layers.face, &layers.polaroid, &layers.front};
    for (auto* l : list) {
        *l = std::make_shared<g2d::Sprite>();
        layers.container->add_child(*l);
    }
    face_windows.clear();
    for (int i = 0; i < 4; i++) {
        double fx = 14 + ((i % 2) * ((640 - 28) - 114));
        double fy = 5 + (std::floor(i / 2) * ((480 - 16) - 164));
        auto w = std::shared_ptr<WindowOmoriBattleActorStatus>(new WindowOmoriBattleActorStatus());
        w->initialize(i, &layers, fx, fy);
        w->x = !w->actor() ? (i % 2 ? 640 : -w->window_width()) : fx;
        w->y = fy;
        face_windows.push_back(w);
        face_windows_container->add_child(w);
    }
    for (int i = 0; i < 5; i++) {
        auto s = std::make_shared<SpriteBattleFaceAnimation>();
        s->effect_target = s.get();
        layers.animation[i] = s;
        (*list[i])->add_child(s);
    }
    status_window->face_windows = &face_windows;
}

void SceneBattle::create_party_command_window() {
    party_command_window = std::shared_ptr<WindowPartyCommand>(new WindowPartyCommand());
    party_command_window->initialize();
    party_command_window->set_handler("fight", [this]() { command_fight(); });
    party_command_window->set_handler("escape", [this]() { command_escape(); });
    party_command_window->deselect();
    add_window(party_command_window);
}

void SceneBattle::create_actor_command_window() {
    actor_command_window = std::shared_ptr<WindowActorCommand>(new WindowActorCommand());
    actor_command_window->initialize();
    actor_command_window->set_handler("attack", [this]() { command_attack(); });
    actor_command_window->set_handler("skill", [this]() { command_skill(); });
    actor_command_window->set_handler("guard", [this]() { command_item("snacks"); });
    actor_command_window->set_handler("item", [this]() { command_item("toys"); });
    actor_command_window->set_handler("cancel", [this]() { select_previous_command(); });
    actor_command_window->set_handler("actionSkill", [this]() { command_actor_command_action(); });
    actor_command_window->set_handler("actionItem", [this]() { command_actor_command_action(); });
    add_window(actor_command_window);
}

void SceneBattle::create_help_window() {
    help_window = std::shared_ptr<menu::WindowOmoMenuHelp>(new menu::WindowOmoMenuHelp());
    help_window->initialize(360.0, 93.0);
    help_window->x = 140;
    help_window->y = -4;
    help_window->icon_rate = 0.75;
    add_child(help_window);
}

void SceneBattle::create_skill_window() {
    skill_window = WindowBase::make<WindowBattleSkill>(140.0, 480.0 + 30, 360.0, 100.0);
    skill_window->set_help_window(help_window.get());
    skill_window->set_handler("ok", [this]() { on_skill_ok(); });
    skill_window->set_handler("cancel", [this]() { on_skill_cancel(); });
    add_window(skill_window);
}

void SceneBattle::create_item_window() {
    item_window = WindowBase::make<WindowBattleItem>(140.0, 480.0 + 30, 360.0, 100.0);
    item_window->set_help_window(help_window.get());
    item_window->set_handler("ok", [this]() { on_item_ok(); });
    item_window->set_handler("cancel", [this]() { on_item_cancel(); });
    add_window(item_window);
}

void SceneBattle::create_actor_window() {
    actor_window = std::shared_ptr<WindowBattleActor>(new WindowBattleActor());
    actor_window->initialize(0.0, status_window->y);
    actor_window->set_handler("ok", [this]() { on_actor_ok(); });
    actor_window->set_handler("cancel", [this]() { on_actor_cancel(); });
    add_window(actor_window);
    actor_window->x = 640 - actor_window->width();
    actor_window->face_windows = &face_windows;
    actor_window->visible = false;
    actor_window->group_target_swap = [this]() { group_target_swap(0); };
}

void SceneBattle::create_enemy_window() {
    enemy_window = std::shared_ptr<WindowBattleEnemy>(new WindowBattleEnemy());
    enemy_window->initialize(0.0, status_window->y);
    enemy_window->x = 640 - enemy_window->width();
    enemy_window->set_handler("ok", [this]() { on_enemy_ok(); });
    enemy_window->set_handler("cancel", [this]() { on_enemy_cancel(); });
    add_window(enemy_window);
    enemy_window->group_target_swap = [this]() { group_target_swap(1); };
    enemy_window->y = 480;
}

void SceneBattle::create_chain_skill_window() {
    chain_window = std::shared_ptr<WindowChainSkillList>(new WindowChainSkillList());
    chain_window->initialize();
    add_child(chain_window);
    refs().chain = chain_window.get();
}

void SceneBattle::create_stress_bar() {
    stress_bar = std::make_shared<SpriteStressBar>();
    stress_bar->x = 140;
    stress_bar->y = 480 - 56;
    stress_bar->visible = !rpg::game_switches().value(41);
    int world = rpg::game_variables().value(22).to_int();
    if (world == 2 || world == 5 || world == 6) stress_bar->visible = false;
    add_child_at(stress_bar, 2);
}

void SceneBattle::create_battle_intro_container() {
    const js::Val& intro = rpg::battle::st().battle_intro;
    battle_intro_container = std::make_shared<SpriteBattleIntroContainer>(intro.truthy() ? intro.to_str() : std::string());
    add_child(battle_intro_container);
}

void SceneBattle::add_tone_filter_compatibility() {
    for (auto& w : window_layer->children) {
        if (std::find(message_layer_children_.begin(), message_layer_children_.end(), w.get()) != message_layer_children_.end()) continue;
        w->filters.push_back(tone_filter_);
    }
    face_windows_container->filters.push_back(tone_filter_);
    set_filters(stress_bar.get(), {tone_filter_});
    set_filters(help_window.get(), {tone_filter_});
    set_filters(log_window->scroll_text_sprite.get(), {tone_filter_});
}

void SceneBattle::start() {
    if (needs_gp_white()) {
        create_gp_white();
        default_start();
    } else {
        if (!rpg::game_troop().is_white_transition_troop()) fade_in(fade_speed(), rpg::game_switches().value(369));
        default_start();
    }
    if (!rpg::game_troop().wait_for_emotion() && !rpg::game_troop().are_summon_troops()) set_start_tint();
    help_window->hide();
}

void SceneBattle::default_start() {
    rpg::Scene::start();
    rpg::battle::play_battle_bgm();
    rpg::battle::start_battle();
}

bool SceneBattle::needs_gp_white() {
    const js::Val& intro = rpg::battle::st().battle_intro;
    if (intro.truthy() && intro.to_str() == "finalbattle") return true;
    return rpg::game_troop().is_white_transition_troop();
}

void SceneBattle::create_gp_white() {
    gp_white = std::make_shared<g2d::ScreenSprite>();
    gp_white->set_white();
    add_child(gp_white);
}

void SceneBattle::update_gp_white() {
    if (rpg::battle::is_battle_intro_active() || rpg::game_troop().troop_id == 891) return;
    if (!gp_white) return;
    if (gp_white->alpha > 0) gp_white->alpha -= 0.05;
    if (gp_white->alpha <= 0) {
        remove_child(gp_white.get());
        gp_white = nullptr;
    }
}

void SceneBattle::set_start_tint() {
    if (rpg::game_switches().value(1615)) {
        double t[4] = {-187, -204, -102, 170};
        rpg::game_screen().start_tint(t, 8);
    } else if (rpg::game_troop().troop_id == 444) {
        double t[4] = {-255, -255, -255, 255};
        rpg::game_screen().start_tint(t, 0);
    } else if (!rpg::game_switches().value(466)) {
        double t[4] = {0, 0, 0, 0};
        rpg::game_screen().start_tint(t, 8);
    }
}

void SceneBattle::fade_in(int duration, bool white) {
    rpg::Scene::start_fade_in(duration, white);
    if (fade_sprite) add_child(fade_sprite);
}

void SceneBattle::fade_out(int duration, bool white) {
    rpg::Scene::start_fade_out(duration, white);
    if (fade_sprite) add_child(fade_sprite);
}

void SceneBattle::start_fade_out(int duration, bool white) { fade_out(duration, white); }
void SceneBattle::start_fade_in(int duration, bool white) { fade_in(duration, white); }

void SceneBattle::raise_fade_sprite() {
    if (spriteset && spriteset->fade_sprite) add_child(spriteset->fade_sprite);
}

void SceneBattle::refresh_status() {
    if (status_window) status_window->refresh();
}

void SceneBattle::set_window_layer_hidden(bool hidden) {
    if (window_layer) window_layer->x = hidden ? 640.0 * 495 : 0;
}

void SceneBattle::update() {
    update_gp_white();
    bool active = is_active();
    rpg::game_timer().update(active);
    rpg::game_screen().update();
    if (active && !is_busy()) update_battle_process();
    rpg::Scene::update();
    if (status_window && !status_window->is_closed()) status_window->update_status_requests();
    update_retry_fade();
    move.update();
    update_main_tone_filter();
    update_tkm_filters();
}

void SceneBattle::update_retry_fade() {
    if (current_retry_fade_in_anim_ < 0) return;
    auto& st = rpg::battle::st();
    if (st.waiting_for_retry_video) return;
    rpg::game_screen().start_fade_in(retry_fade_in_anim_);
    double per = 255.0 / retry_fade_in_anim_;
    current_retry_fade_in_anim_--;
    stress_bar->set_opacity(stress_bar->opacity() + per);
    for (auto& s : party_command_window->command_sprites) s->set_opacity(s->opacity() + per);
    help_window->set_window_alpha(help_window->opacity_value() + per);
    log_window->set_window_alpha(log_window->opacity_value() + per);
    log_window->set_visible(true);
    actor_window->set_window_alpha(actor_window->opacity_value() + per);
    face_windows_container->set_opacity(face_windows_container->opacity() + per);
    if (auto* w = dynamic_cast<g2d::Window*>(scroll_text_layer_.get())) w->set_window_opacity(w->window_opacity() + per);
    if (auto c = party_command_window->custom_cursor_container()) c->set_opacity(c->opacity() + per);
    st.battle_retried = false;
}

void SceneBattle::update_battle_process() {
    if (!is_any_input_window_active() || rpg::battle::is_aborting() || rpg::battle::is_battle_end()) {
        rpg::battle::update();
        change_input_window();
    }
}

bool SceneBattle::is_any_input_window_active() {
    return party_command_window->active || actor_command_window->active || skill_window->active || item_window->active || actor_window->active ||
           enemy_window->active;
}

void SceneBattle::change_input_window() {
    if (rpg::battle::is_inputting()) {
        if (rpg::battle::actor()) start_actor_command_selection();
        else start_party_command_selection();
    } else {
        end_command_selection();
    }
}

void SceneBattle::stop() {
    rpg::Scene::stop();
    if (rpg::game_troop().troop_id != 445) {
        if (needs_slow_fade_out()) fade_out(slow_fade_speed(), false);
        else fade_out(fade_speed(), false);
    }
    status_window->close();
    party_command_window->close();
    actor_command_window->close();
}

bool SceneBattle::needs_slow_fade_out() {
    return rpg::scene_manager::is_next_scene("Scene_Title") || rpg::scene_manager::is_next_scene("Scene_OmoriTitleScreen") ||
           rpg::scene_manager::is_next_scene("Scene_Gameover");
}

void SceneBattle::terminate() {
    rpg::Scene::terminate();
    rpg::game_party().on_battle_end();
    rpg::game_troop().on_battle_end();
    rpg::audio_manager::stop_me();
    if (spriteset) spriteset->release_links();
    remove_children();
    rpg::battle::replay_bgm_and_bgs();
    rpg::game_switches().set_value(92, false);
    rpg::game_switches().set_value(466, false);
    rpg::game_switches().set_value(1600, false);
    for (rpg::GameActor* a : rpg::game_party().members()) {
        a->remove_state(1);
        a->revive();
        a->played_collapse_sound = false;
    }
    auto& st = rpg::battle::st();
    st.battle_retried = false;
    if (st.log == log_window.get()) rpg::battle::unregister_scene();
    refs() = Refs();
    release_display_proxies();
}

void SceneBattle::start_party_command_selection() {
    if (yparam_bool("BECStartActCmd")) {
        select_next_command();
    } else {
        base_start_party_command_selection();
    }
    actor_command_window->open();
    actor_command_window->hide();
    party_command_window->show();
    help_window->hide();
    show_party_command();
    push_party_message();
}

void SceneBattle::base_start_party_command_selection() {
    refresh_status();
    status_window->deselect();
    status_window->open();
    actor_command_window->close();
    party_command_window->setup();
}

void SceneBattle::push_party_message() {
    rpg::GameParty& p = rpg::game_party();
    int size = p.size();
    std::string message;
    rpg::GameActor* leader = p.leader();
    std::string leader_name = leader ? leader->name() : std::string();
    if (size == 1) {
        message = rpg::js_format(general_text("message_104"), {leader_name});
    } else if (size == 2) {
        auto m = p.members();
        message = rpg::js_format(general_text("message_102"), {leader_name, m.size() > 1 ? m[1]->name() : std::string()});
    } else {
        message = rpg::js_format(general_text("message_100"), {leader_name});
    }
    add_log_command_message(message);
}

void SceneBattle::command_fight() {
    if (rpg::game_variables().value(22).to_int() == 5) {
        rpg::battle::select_next_command();
        rpg::ItemRef skill = rpg::ItemRef::skill(1);
        auto action = rpg::battle::inputting_action();
        if (action) action->set_skill(skill.id);
        if (rpg::battle::actor()) rpg::battle::actor()->set_last_battle_skill(skill);
        select_next_command();
        return;
    }
    select_next_command();
}

void SceneBattle::command_escape() {
    rpg::battle::st().escape_by_command = true;
    rpg::battle::process_escape();
    change_input_window();
    hide_party_command();
}

void SceneBattle::start_actor_command_selection() {
    rpg::battle::create_actions();
    rpg::GameActor* actor = rpg::battle::actor();
    if (!actor) return;
    status_window->select(actor->index());
    party_command_window->close();
    actor_command_window->setup(actor);
    status_window->refresh();
    party_command_window->open();
    party_command_window->hide();
    actor_command_window->set_openness(255);
    actor_command_window->show();
    help_window->hide();
    status_window->select(actor->battle_status_index());
    add_actor_action_prompt_log_text();
    show_party_command();
}

void SceneBattle::add_actor_action_prompt_log_text() {
    rpg::GameActor* actor = rpg::battle::actor();
    if (actor) add_log_command_message(rpg::js_format(general_text("message_104"), {actor->name()}));
}

void SceneBattle::add_log_command_message(const std::string& text, bool clear, bool instant) {
    if (clear) log_window->clear();
    if (instant) log_window->add_instant_text(text);
    else log_window->add_text(text);
}

void SceneBattle::command_attack() {
    auto action = rpg::battle::inputting_action();
    if (action) action->set_attack();
    select_enemy_selection();
}

void SceneBattle::command_skill() {
    help_window->clear();
    skill_window->set_actor(rpg::battle::actor());
    skill_window->set_stype_id(actor_command_window->current_ext().to_int());
    skill_window->refresh();
    skill_window->show();
    skill_window->activate();
    skill_window->show();
    show_skill_window();
}

void SceneBattle::command_item(const std::string& category) {
    item_window->set_category(category);
    show_item_window();
    help_window->clear();
    item_window->refresh();
    item_window->show();
    item_window->activate();
}

void SceneBattle::command_actor_command_action() {
    std::string symbol = actor_command_window->current_symbol();
    int id = actor_command_window->current_ext().to_int();
    auto action = rpg::battle::inputting_action();
    if (action) {
        if (symbol == "actionSkill") action->set_skill(id);
        if (symbol == "actionItem") action->set_item(id);
    }
    on_select_action();
}

void SceneBattle::select_next_command() {
    rpg::battle::select_next_command();
    change_input_window();
    help_window->clear();
    rpg::battle::stop_all_selection();
    if (!rpg::battle::is_inputting() && !rpg::battle::actor()) hide_party_command();
}

void SceneBattle::select_previous_command() {
    if (yparam_bool("BECStartActCmd")) {
        rpg::battle::select_previous_command();
        if (rpg::battle::is_inputting() && rpg::battle::actor()) start_actor_command_selection();
        else base_start_party_command_selection();
    } else {
        rpg::battle::select_previous_command();
        change_input_window();
    }
}

void SceneBattle::select_actor_selection() {
    help_window->clear();
    actor_window->refresh();
    actor_window->show();
    actor_window->activate();
    actor_window->auto_select();
    actor_window->select(status_window->index());
}

void SceneBattle::on_actor_ok() {
    auto action = rpg::battle::inputting_action();
    rpg::GameActor* target = rpg::game_party().member_at_status_index(actor_window->index());
    if (!action || !target) return;
    int actor_index = target->index();
    if (action->is_for_dead_friend() && !target->is_dead()) {
        play_buzzer();
        actor_window->activate();
        return;
    }
    rpg::GameActor* current = rpg::battle::actor();
    if (action->not_target_user() && current && current->name() == target->name()) {
        play_buzzer();
        actor_window->activate();
        return;
    }
    action->set_target(actor_window->index());
    actor_window->hide();
    skill_window->hide();
    item_window->hide();
    select_next_command();
    action->set_target(actor_index);
    hide_item_window();
    hide_skill_window();
    skill_window->visible = true;
    item_window->visible = true;
    help_window->hide();
    if (!rpg::battle::actor()) log_window->clear();
}

void SceneBattle::on_actor_cancel() {
    help_window->clear();
    actor_window->hide();
    std::string symbol = actor_command_window->current_symbol();
    if (symbol == "skill") {
        skill_window->show();
        skill_window->activate();
    } else if (symbol == "item") {
        item_window->show();
        item_window->activate();
    }
    rpg::battle::stop_all_selection();
    rpg::battle::clear_inputting_action();
    if (symbol == "actionSkill" || symbol == "actionItem") actor_command_window->activate();
    if (rpg::battle::actor()) status_window->select(rpg::battle::actor()->battle_status_index());
    if (symbol == "attack") {
        show_party_command();
    } else if (symbol == "skill") {
        show_party_command();
        show_skill_window();
    } else if (symbol == "item") {
        show_party_command();
        show_item_window();
    } else if (symbol == "guard") {
        item_window->show();
        item_window->activate();
        item_window->call_update_help();
        help_window->show();
    }
}

void SceneBattle::select_enemy_selection() {
    rpg::game_temp().taunt = true;
    help_window->clear();
    enemy_window->refresh();
    enemy_window->show();
    enemy_window->select(0);
    enemy_window->activate();
    enemy_window->auto_select();
    rpg::game_temp().taunt = false;
    hide_party_command();
    hide_skill_window();
    hide_item_window();
}

void SceneBattle::on_enemy_ok() {
    auto action = rpg::battle::inputting_action();
    if (action) action->set_target(enemy_window->enemy_index());
    enemy_window->hide();
    skill_window->hide();
    item_window->hide();
    select_next_command();
    hide_item_window();
    hide_skill_window();
    skill_window->visible = true;
    item_window->visible = true;
    help_window->hide();
    rpg::game_troop().select(nullptr);
    if (!rpg::battle::actor()) log_window->clear();
}

void SceneBattle::on_enemy_cancel() {
    help_window->clear();
    enemy_window->hide();
    std::string symbol = actor_command_window->current_symbol();
    if (symbol == "attack") {
        actor_command_window->activate();
    } else if (symbol == "skill") {
        skill_window->show();
        skill_window->activate();
    } else if (symbol == "item") {
        item_window->show();
        item_window->activate();
    }
    rpg::battle::stop_all_selection();
    rpg::battle::clear_inputting_action();
    if (symbol == "actionSkill" || symbol == "actionItem") actor_command_window->activate();
    if (symbol == "attack" || symbol == "actionSkill") {
        show_party_command();
    } else if (symbol == "skill") {
        show_party_command();
        show_skill_window();
    } else if (symbol == "item") {
        show_party_command();
        show_item_window();
    }
}

void SceneBattle::on_skill_ok() {
    auto action = rpg::battle::inputting_action();
    help_window->clear();
    rpg::ItemRef skill = skill_window->item();
    if (action) action->set_skill(skill.id);
    if (rpg::battle::actor()) rpg::battle::actor()->set_last_battle_skill(skill);
    on_select_action();
    skill_window->show();
    help_window->hide();
    if (!action) return;
    if (!action->needs_selection()) {
        hide_item_window();
        hide_skill_window();
    } else {
        add_log_command_message(general_text("message_105"));
        if (action->can_change_target_groups()) add_log_command_message(general_text("message_106"), false);
    }
}

void SceneBattle::on_skill_cancel() {
    help_window->clear();
    skill_window->hide();
    actor_command_window->activate();
    rpg::battle::clear_inputting_action();
    add_actor_action_prompt_log_text();
    skill_window->show();
    help_window->hide();
    hide_skill_window();
}

void SceneBattle::on_item_ok() {
    auto action = rpg::battle::inputting_action();
    help_window->clear();
    rpg::ItemRef item = item_window->item();
    if (action) action->set_item(item.id);
    rpg::game_party().set_last_item(item);
    on_select_action();
    item_window->show();
    help_window->hide();
    if (!action) return;
    if (!action->needs_selection()) {
        hide_item_window();
        hide_skill_window();
    } else {
        add_log_command_message(general_text("message_105"));
        if (action->can_change_target_groups()) add_log_command_message(general_text("message_106"), false);
    }
}

void SceneBattle::on_item_cancel() {
    help_window->clear();
    item_window->hide();
    actor_command_window->activate();
    rpg::battle::clear_inputting_action();
    item_window->show();
    hide_item_window();
    help_window->hide();
    add_actor_action_prompt_log_text();
}

void SceneBattle::on_select_action() {
    bool select_help = yparam_bool("BECSelectHelp");
    if (select_help) rpg::battle::force_selection();
    help_window->clear();
    auto action = rpg::battle::inputting_action();
    skill_window->hide();
    item_window->hide();
    if (!action || !action->needs_selection()) select_next_command();
    else if (action->is_for_opponent()) select_enemy_selection();
    else select_actor_selection();
    if (select_help) rpg::battle::reset_selection();
}

void SceneBattle::end_command_selection() {
    party_command_window->close();
    actor_command_window->close();
    status_window->deselect();
    party_command_window->open();
    actor_command_window->open();
}

void SceneBattle::group_target_swap(int type) {
    auto action = rpg::battle::inputting_action();
    input::update();
    if (!action || !action->can_change_target_groups()) return;
    if (type == 0) {
        action->set_target_group(1);
        actor_window->deselect();
        actor_window->deactivate();
        hide_party_command();
        hide_item_window();
        hide_skill_window();
        if (rpg::battle::actor()) status_window->select(rpg::battle::actor()->battle_status_index());
        select_enemy_selection();
    } else {
        action->set_target_group(0);
        enemy_window->deselect();
        enemy_window->deactivate();
        actor_window->activate();
        select_actor_selection();
    }
}

void SceneBattle::move_y(g2d::DisplayObject* obj, double to, int duration, menu::Easing easing) {
    if (!obj) return;
    move.start_move(menu::ObjectMovement::tween(obj, {{menu::prop_y(obj), to, duration}}, easing));
}

void SceneBattle::move_x(g2d::DisplayObject* obj, double to, int duration, menu::Easing easing) {
    if (!obj) return;
    move.start_move(menu::ObjectMovement::tween(obj, {{menu::prop_x(obj), to, duration}}, easing));
}

void SceneBattle::hide_all_menu_elements(int duration) {
    move_y(stress_bar.get(), 480, duration, menu::ease_in_circ);
    move_y(party_command_window.get(), 480 + 100, duration, menu::ease_in_circ);
    move_y(actor_command_window.get(), 480 + 100, duration, menu::ease_in_circ);
    move_y(log_window.get(), -100, duration, menu::ease_in_circ);
    move_y(help_window.get(), -100, duration, menu::ease_in_circ);
    for (size_t i = 0; i < face_windows.size(); i++) {
        auto& w = face_windows[i];
        move_x(w.get(), i % 2 ? 640 : -w->window_width(), duration, menu::ease_in_circ);
    }
}

void SceneBattle::show_all_menu_elements() {
    int duration = 15;
    move_y(stress_bar.get(), 480 - 56, duration, menu::ease_out_circ);
    move_y(log_window.get(), -4, duration, menu::ease_out_circ);
    move_y(help_window.get(), -4, duration, menu::ease_out_circ);
    for (auto& w : face_windows) {
        if (!w->actor()) continue;
        move_x(w.get(), w->home_x, duration, menu::ease_out_circ);
    }
}

void SceneBattle::show_party_command() {
    move_y(stress_bar.get(), 480 - 140, 15, menu::ease_out_circ);
    move_y(party_command_window.get(), 480 - 92, 15, menu::ease_out_circ);
    move_y(actor_command_window.get(), 480 - 92, 15, menu::ease_out_circ);
}

void SceneBattle::hide_party_command() {
    move_y(stress_bar.get(), 480 - 56, 15, menu::ease_in_circ);
    move_y(party_command_window.get(), 480, 15, menu::ease_in_circ);
    move_y(actor_command_window.get(), 480, 15, menu::ease_in_circ);
}

void SceneBattle::show_item_window() { move_y(item_window.get(), 480 - 65, 15, menu::ease_out_circ); }
void SceneBattle::hide_item_window() { move_y(item_window.get(), 480 + 30, 15, menu::ease_in_circ); }
void SceneBattle::show_skill_window() { move_y(skill_window.get(), 480 - 65, 15, menu::ease_out_circ); }
void SceneBattle::hide_skill_window() { move_y(skill_window.get(), 480 + 30, 15, menu::ease_in_circ); }

bool SceneBattle::needs_main_tone_update() {
    if (rpg::game_switches().value(1615)) return true;
    if (rpg::game_troop().is_humphrey_battle()) return true;
    js::Val* v = rpg::game_temp().expando.find(js::atom("_toneBelowUI"));
    return v && v->truthy();
}

void SceneBattle::update_main_tone_filter() {
    g2d::DisplayObject* objects[4] = {stress_bar.get(), face_windows_container.get(), log_window->scroll_text_sprite.get(), help_window.get()};
    g2d::DisplayObject* mains[5] = {actor_command_window.get(), log_window.get(), party_command_window.get(), skill_window.get(), item_window.get()};
    if (needs_main_tone_update()) {
        for (auto* o : objects) set_filters(o, {});
        for (auto* o : mains) set_filters(o, {});
    } else if (!has_filters(objects[0])) {
        for (auto* o : mains) set_filters(o, {tone_filter_});
        using_tone_filter_ = true;
    }
    bool busy = rpg::game_message().is_busy();
    bool not_needed = troop_in({891, 444, 451, 134, 135, 136});
    if (!using_tone_filter_ && busy && !not_needed) {
        using_tone_filter_ = true;
        for (auto* o : objects) set_filters(o, {main_status_tone_filter_});
    }
    if (using_tone_filter_) {
        g2d::Window* mw = message_window ? message_window_as_window(message_window.get()) : nullptr;
        double rate = mw ? mw->openness() / 255.0 : 0;
        double tone = -100 * rate;
        main_status_tone_[0] = tone;
        main_status_tone_[1] = tone;
        main_status_tone_[2] = tone;
        if (rate == 0 && !busy) {
            using_tone_filter_ = false;
            for (auto* o : objects) set_filters(o, {tone_filter_});
        }
    }
}

fx::TkmTargets SceneBattle::tkm_targets() {
    fx::TkmTargets t;
    t.scene = this;
    t.spriteset = spriteset.get();
    t.base_sprite = spriteset ? spriteset->base_sprite.get() : nullptr;
    t.picture_container = spriteset ? spriteset->picture_container.get() : nullptr;
    return t;
}

void SceneBattle::update_tkm_filters() {
    if (rpg::game_map().filter_con_order.empty()) return;
    tkm_.update(tkm_targets(), true);
}

void SceneBattle::remove_filter_controller(rpg::FilterController* f) { tkm_.remove(f, tkm_targets()); }

bool SceneBattle::js_get(uint32_t key, js::Val& out) {
    const std::string& n = js::atom_name(key);
    if (n == "_spriteset") out = spriteset ? spriteset->js() : js::Val();
    else if (n == "_logWindow") out = log_window ? log_window->js() : js::Val();
    else if (n == "_statusWindow") out = display_js(status_window.get());
    else if (n == "_helpWindow") out = display_js(help_window.get());
    else if (n == "_faceWindowsContainer") out = display_js(face_windows_container.get());
    else if (n == "_partyCommandWindow") out = display_js(party_command_window.get());
    else if (n == "_actorCommandWindow") out = display_js(actor_command_window.get());
    else if (n == "_skillWindow") out = display_js(skill_window.get());
    else if (n == "_itemWindow") out = display_js(item_window.get());
    else if (n == "_actorWindow") out = display_js(actor_window.get());
    else if (n == "_enemyWindow") out = display_js(enemy_window.get());
    else if (n == "_activeChainSkillWindow") out = display_js(chain_window.get());
    else if (n == "_stressBar") out = display_js(stress_bar.get());
    else if (n == "_lowHpOverlay") out = display_js(low_hp_overlay.get());
    else if (n == "_windowLayer") out = display_js(window_layer.get());
    else if (n == "_battleIntroContainer") out = display_js(battle_intro_container.get());
    else if (n == "_scrollTextWindow") out = display_js(scroll_text_layer_.get());
    else if (n == "_gpWhite") out = gp_white ? display_js(gp_white.get()) : js::Val();
    else if (n == "_messageWindow") {
        if (!message_window) {
            out = js::Val();
            return true;
        }
        WindowMessage* mw = message_window.get();
        js::Val o = js::make_object();
        js::vm().put(o, js::atom("terminateMessage"), js::make_native([mw](js::Vm&, const js::Val&, std::vector<js::Val>&) {
                         if (refs().scene) message_window_terminate(mw);
                         return js::Val();
                     }));
        js::vm().put(o, js::atom("isClosing"), js::make_native([mw](js::Vm&, const js::Val&, std::vector<js::Val>&) {
                         return js::Val::boolean(refs().scene && message_window_is_closing(mw));
                     }));
        js::vm().put(o, js::atom("isOpen"), js::make_native([mw](js::Vm&, const js::Val&, std::vector<js::Val>&) {
                         return js::Val::boolean(refs().scene && message_window_is_open(mw));
                     }));
        out = o;
    } else if (n == "setStartTint" || n == "hideAllMenuElements" || n == "showAllMenuElements" || n == "refreshStatus" || n == "startFadeIn" ||
               n == "startFadeOut") {
        std::string name = n;
        out = js::make_native([name](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
            SceneBattle* s = refs().scene;
            if (!s) return js::Val();
            if (name == "setStartTint") s->set_start_tint();
            else if (name == "hideAllMenuElements") s->hide_all_menu_elements(a.empty() || a[0].is_undef() ? 15 : a[0].to_int());
            else if (name == "showAllMenuElements") s->show_all_menu_elements();
            else if (name == "refreshStatus") s->refresh_status();
            else if (name == "startFadeIn") s->fade_in(a.empty() ? 0 : a[0].to_int(), a.size() > 1 && a[1].truthy());
            else s->fade_out(a.empty() ? 0 : a[0].to_int(), a.size() > 1 && a[1].truthy());
            return js::Val();
        });
    } else {
        return false;
    }
    return true;
}

bool SceneBattle::js_set(uint32_t key, const js::Val& v) {
    (void)key, (void)v;
    return false;
}

}

rpg::SceneFactory battle_scene() { return rpg::scene_of<btl::SceneBattle>("Scene_Battle"); }

static SceneRegistrar g_battle_scene_registrar("Scene_Battle", battle_scene());

}
