#include <cmath>

#include "../core/audio.h"
#include "../core/image.h"
#include "../js/random.h"
#include "action.h"
#include "audio_manager.h"
#include "battle.h"
#include "bind_util.h"
#include "config.h"
#include "data.h"
#include "enemy.h"
#include "game.h"
#include "hooks.h"
#include "params.h"
#include "party.h"
#include "save.h"
#include "scene.h"

namespace rpg {
namespace bind {

namespace {

void put(const Val& o, const char* k, const Val& v) { js::vm().put(o, js::atom(k), v); }

Val bitmap_js(const char* folder, const std::string& name) {
    int w = 0, h = 0;
    if (!name.empty()) images::logical_size(folder, name, w, h);
    Val o = js::make_object();
    put(o, "width", Val::num(w));
    put(o, "height", Val::num(h));
    put(o, "isReady", native([](Args&) { return Val::boolean(true); }));
    put(o, "addLoadListener", js::make_native([](js::Vm& vm, const Val& me, Args& a) {
            Val fn = js::arg(a, 0);
            if (fn.is_function()) vm.call(fn, Val(), {me});
            return Val();
        }));
    return o;
}

std::string video_key(const Val& v) { return v.is_undef() ? std::string("video") : v.to_str(); }

Val deep_copy(const Val& v, std::vector<std::pair<js::Obj*, Val>>& seen) {
    if (!v.is_obj()) return v;
    js::Obj* o = v.o;
    if (o->kind == js::Kind::Host || o->kind == js::Kind::Function || o->kind == js::Kind::Native) return v;
    for (auto& s : seen) {
        if (s.first == o) return s.second;
    }
    if (o->kind == js::Kind::Array) {
        Val arr = js::make_array();
        seen.push_back({o, arr});
        for (auto& x : o->items) arr.o->items.push_back(deep_copy(x, seen));
        return arr;
    }
    Val obj = js::make_object();
    seen.push_back({o, obj});
    std::vector<uint32_t> keys;
    js::ordered_keys(o, keys);
    for (uint32_t k : keys) {
        const Val* x = o->find(k);
        if (x) obj.o->set(k, deep_copy(*x, seen));
    }
    return obj;
}

void bind_battle_manager() {
    js::Klass& kl = klass("BattleManager");
    auto field = [&](const char* name, std::function<Val()> get, std::function<void(const Val&)> set) {
        kl.field(
            name, [get](js::JsObject&) { return get(); }, [set](js::JsObject&, const Val& v) { set(v); });
    };
    auto method = [&](const char* name, std::function<Val(Args&)> fn) {
        kl.method(name, [fn](js::JsObject&, js::Vm&, Args& a) { return fn(a); });
    };
    battle::State& s = battle::st();
    field("_phase", [&s]() { return Val::str(s.phase); }, [&s](const Val& v) { s.phase = v.to_str(); });
    field("_canEscape", [&s]() { return Val::boolean(s.can_escape); }, [&s](const Val& v) { s.can_escape = v.truthy(); });
    field("_canLose", [&s]() { return Val::boolean(s.can_lose); }, [&s](const Val& v) { s.can_lose = v.truthy(); });
    field("_battleTest", [&s]() { return Val::boolean(s.battle_test); }, [&s](const Val& v) { s.battle_test = v.truthy(); });
    field("_preemptive", [&s]() { return Val::boolean(s.preemptive); }, [&s](const Val& v) { s.preemptive = v.truthy(); });
    field("_surprise", [&s]() { return Val::boolean(s.surprise); }, [&s](const Val& v) { s.surprise = v.truthy(); });
    field("_actorIndex", [&s]() { return Val::num(s.actor_index); }, [&s](const Val& v) { s.actor_index = v.to_int(); });
    field("_escaped", [&s]() { return Val::boolean(s.escaped); }, [&s](const Val& v) { s.escaped = v.truthy(); });
    field("_escapeRatio", [&s]() { return Val::num(s.escape_ratio); }, [&s](const Val& v) { s.escape_ratio = v.to_num(); });
    field("_turnForced", [&s]() { return Val::boolean(s.turn_forced); }, [&s](const Val& v) { s.turn_forced = v.truthy(); });
    field("_victoryPhase", [&s]() { return Val::boolean(s.victory_phase); }, [&s](const Val& v) { s.victory_phase = v.truthy(); });
    field("_subject", [&s]() { return obj_js(battle::subject()); },
          [&s](const Val& v) {
              GameBattler* b = battler_of(v);
              s.subject = BattlerRef::of(b);
              s.has_subject = b != nullptr;
          });
    field("_action", [&s]() { return s.action ? held_js(s.action) : Val::null(); },
          [&s](const Val& v) {
              GameAction* a = js::host_cast<GameAction>(v);
              std::shared_ptr<GameAction> sp = a ? held_ptr(a) : nullptr;
              if (!sp && a) sp = a->clone();
              s.action = sp;
          });
    field("_targets", [&s]() { return arr_of(battle::targets()); },
          [&s](const Val& v) {
              std::vector<GameBattler*> t;
              if (v.is_array()) {
                  for (auto& x : v.o->items) t.push_back(battler_of(x));
              }
              battle::set_targets(t);
          });
    field("_allTargets", [&s]() { return arr_of(battle::all_targets()); },
          [&s](const Val& v) {
              s.all_targets.clear();
              if (v.is_array()) {
                  for (auto& x : v.o->items) {
                      GameBattler* b = battler_of(x);
                      if (b) s.all_targets.push_back(BattlerRef::of(b));
                  }
              }
          });
    field("_logWindow", [&s]() { return s.log ? s.log->js() : Val(); }, [](const Val&) {});
    field("_spriteset", [&s]() { return s.spriteset ? s.spriteset->js() : Val(); }, [](const Val&) {});
    field("_mapBgm", [&s]() { return s.has_map_bgm ? s.map_bgm.to_js() : Val(); },
          [&s](const Val& v) {
              s.has_map_bgm = !v.is_nullish();
              s.map_bgm = AudioObj::from_js(v);
          });
    field("_mapBgs", [&s]() { return s.has_map_bgs ? s.map_bgs.to_js() : Val(); },
          [&s](const Val& v) {
              s.has_map_bgs = !v.is_nullish();
              s.map_bgs = AudioObj::from_js(v);
          });
    field("_battleRetried", [&s]() { return Val::boolean(s.battle_retried); }, [&s](const Val& v) { s.battle_retried = v.truthy(); });
    field("_finalBattlePhase", [&s]() { return Val::num(s.final_battle_phase); }, [&s](const Val& v) { s.final_battle_phase = v.to_int(); });
    field("_pictureDisplayLayer", [&s]() { return Val::str(s.picture_display_layer); }, [&s](const Val& v) { s.picture_display_layer = v.to_str(); });
    field("_showDamage", [&s]() { return Val::boolean(s.show_damage); }, [&s](const Val& v) { s.show_damage = v.truthy(); });
    field("_showEncounterEffect", [&s]() { return Val::boolean(s.show_encounter_effect); }, [&s](const Val& v) { s.show_encounter_effect = v.truthy(); });
    field("waitingForRetryVideo", [&s]() { return Val::boolean(s.waiting_for_retry_video); }, [&s](const Val& v) { s.waiting_for_retry_video = v.truthy(); });
    field("_bypassMoveToStartLocation", [&s]() { return Val::boolean(s.bypass_move_to_start_location); },
          [&s](const Val& v) { s.bypass_move_to_start_location = v.truthy(); });
    field("_isFinalBattle", [&s]() { return Val::boolean(s.is_final_battle); }, [&s](const Val& v) { s.is_final_battle = v.truthy(); });
    field("_suspendEvents", [&s]() { return Val::boolean(s.suspend_events); }, [&s](const Val& v) { s.suspend_events = v.truthy(); });
    field("_escapeByCommand", [&s]() { return Val::boolean(s.escape_by_command); }, [&s](const Val& v) { s.escape_by_command = v.truthy(); });
    method("queueForceAction", [](Args& a) {
        GameBattler* u = battler_of(js::arg(a, 0));
        if (u) battle::queue_force_action(u, js::arg_int(a, 1), js::arg(a, 2), true);
        return Val();
    });
    method("queueForceScriptCall", [](Args& a) {
        battle::queue_force_script_call(js::arg(a, 0).to_str());
        return Val();
    });
    method("setPictureDisplayLayer", [](Args& a) {
        battle::set_picture_display_layer(js::arg(a, 0).to_str());
        return Val();
    });
    method("forceCustomBattleLog", [](Args& a) {
        battle::force_custom_battle_log(battler_of(js::arg(a, 0)));
        return Val();
    });
    method("addText", [](Args& a) {
        battle::add_text(js::arg(a, 0).to_str(), js::arg_int(a, 1));
        return Val();
    });
    method("abort", [](Args&) {
        battle::abort();
        return Val();
    });
    method("actionForceCritical", [](Args&) {
        game_system().force_critical = true;
        game_system().force_no_critical = false;
        return Val::boolean(true);
    });
    method("actionForceNoCritical", [](Args&) {
        game_system().force_critical = false;
        game_system().force_no_critical = true;
        return Val::boolean(true);
    });
    method("actionNormalCritical", [](Args&) {
        game_system().force_critical = false;
        game_system().force_no_critical = false;
        return Val::boolean(true);
    });
    method("actor", [](Args&) { return obj_js(battle::actor()); });
    method("inputtingAction", [](Args&) {
        auto a = battle::inputting_action();
        return a ? held_js(a) : Val::null();
    });
    method("isBattleTest", [](Args&) { return Val::boolean(battle::st().battle_test); });
    method("isBusy", [](Args&) { return Val::boolean(battle::is_busy()); });
    method("isInputting", [](Args&) { return Val::boolean(battle::is_inputting()); });
    method("isInTurn", [](Args&) { return Val::boolean(battle::is_in_turn()); });
    method("isTurnEnd", [](Args&) { return Val::boolean(battle::is_turn_end()); });
    method("isAborting", [](Args&) { return Val::boolean(battle::is_aborting()); });
    method("isBattleEnd", [](Args&) { return Val::boolean(battle::is_battle_end()); });
    method("canEscape", [](Args&) { return Val::boolean(battle::can_escape()); });
    method("canLose", [](Args&) { return Val::boolean(battle::can_lose()); });
    method("isEscaped", [](Args&) { return Val::boolean(battle::is_escaped()); });
    method("isDTB", [](Args&) { return Val::boolean(battle::is_dtb()); });
    method("isTurnBased", [](Args&) { return Val::boolean(battle::is_turn_based()); });
    method("isTickBased", [](Args&) { return Val::boolean(battle::is_tick_based()); });
    method("isForcedTurn", [](Args&) { return Val::boolean(battle::is_forced_turn()); });
    method("refreshStatus", [](Args&) {
        battle::refresh_status();
        return Val();
    });
    method("allBattleMembers", [](Args&) { return arr_of(battle::all_battle_members()); });
    method("processEscape", [](Args&) { return Val::boolean(battle::process_escape()); });
    method("processVictory", [](Args&) {
        battle::process_victory();
        return Val();
    });
    method("processDefeat", [](Args&) {
        battle::process_defeat();
        return Val();
    });
    method("processAbort", [](Args&) {
        battle::process_abort();
        return Val();
    });
    method("startTurn", [](Args&) {
        battle::start_turn();
        return Val();
    });
    method("endTurn", [](Args&) {
        battle::end_turn();
        return Val();
    });
    method("clearResults", [](Args&) {
        battle::clear_results();
        return Val();
    });
    method("makeActionTargets", [](Args& a) { return arr_of(battle::make_action_targets(js::arg(a, 0).to_str())); });
    method("playBattleBgm", [](Args&) {
        battle::play_battle_bgm();
        return Val();
    });
    method("replayBgmAndBgs", [](Args&) {
        battle::replay_bgm_and_bgs();
        return Val();
    });
    method("saveBgmAndBgs", [](Args&) {
        battle::save_bgm_and_bgs();
        return Val();
    });
    method("processRetry", [](Args&) {
        battle::process_retry();
        return Val();
    });
    method("setBattleIntro", [](Args& a) {
        battle::set_battle_intro(js::arg(a, 0));
        return Val();
    });
    method("isBattleIntroActive", [](Args&) { return Val::boolean(battle::is_battle_intro_active()); });
    method("getActorInputOrder", [](Args&) { return ints_of(battle::get_actor_input_order()); });
    method("forceAction", [](Args& a) {
        GameBattler* b = battler_of(js::arg(a, 0));
        if (b) battle::force_action(b);
        return Val();
    });
    method("isActionForced", [](Args&) { return Val::boolean(battle::is_action_forced()); });
    method("startFadeOut", [](Args& a) {
        battle::start_fade_out(js::arg_int(a, 0), js::arg(a, 1).truthy());
        return Val();
    });
    method("startFadeIn", [](Args& a) {
        battle::start_fade_in(js::arg_int(a, 0), js::arg(a, 1).truthy());
        return Val();
    });
    method("cameraX", [](Args&) { return Val::num(battle::camera_x()); });
    method("cameraY", [](Args&) { return Val::num(battle::camera_y()); });
    method("resetCamera", [](Args& a) {
        battle::reset_camera(js::arg_int(a, 0));
        return Val();
    });
}

Val scene_class(const char* name) {
    Val v = static_object(name);
    js::JsObject* o = js::as_object(v);
    if (o && o->prop("prototype").is_undef()) o->set_prop("prototype", js::make_object());
    return v;
}

std::string scene_name_of(const Val& v) {
    StaticObj* so = dynamic_cast<StaticObj*>(js::as_object(v));
    return so ? so->k->name : std::string();
}

void bind_scene_manager() {
    js::Klass& kl = klass("SceneManager");
    kl.getter("_scene", [](js::JsObject&) {
        Scene* s = scene_manager::scene();
        return s ? s->to_js() : Val::null();
    });
    kl.method("goto", [](js::JsObject&, js::Vm& vm, Args& a) {
        std::string n = scene_name_of(js::arg(a, 0));
        SceneFactory f = scene_manager::factory(n.c_str());
        if (!f) vm.throw_type_error("SceneManager.goto: unknown scene " + n);
        scene_manager::goto_scene(f);
        return Val();
    });
    kl.method("push", [](js::JsObject&, js::Vm& vm, Args& a) {
        std::string n = scene_name_of(js::arg(a, 0));
        SceneFactory f = scene_manager::factory(n.c_str());
        if (!f) vm.throw_type_error("SceneManager.push: unknown scene " + n);
        scene_manager::push(f);
        return Val();
    });
    kl.method("pop", [](js::JsObject&, js::Vm&, Args&) {
        scene_manager::pop();
        return Val();
    });
    kl.method("isSceneChanging", [](js::JsObject&, js::Vm&, Args&) { return Val::boolean(scene_manager::is_scene_changing()); });
    kl.method("isCurrentSceneBusy", [](js::JsObject&, js::Vm&, Args&) { return Val::boolean(scene_manager::is_current_scene_busy()); });
    kl.method("isNextScene", [](js::JsObject&, js::Vm&, Args& a) { return Val::boolean(scene_manager::is_next_scene(scene_name_of(js::arg(a, 0)).c_str())); });
    kl.method("isPreviousScene", [](js::JsObject&, js::Vm&, Args& a) {
        return Val::boolean(scene_manager::is_previous_scene(scene_name_of(js::arg(a, 0)).c_str()));
    });
    kl.method("snapForBackground", [](js::JsObject&, js::Vm&, Args&) {
        scene_manager::snap_for_background();
        return Val();
    });
    kl.method("currentWorldIndex", [](js::JsObject&, js::Vm&, Args&) { return Val::num(current_world_index()); });
}

Val audio_buffer_js(bool bgm) {
    Val o = js::make_object();
    put(o, "seek", native([bgm](Args&) { return Val::num(bgm ? audio::bgm_position() : 0); }));
    put(o, "isPlaying", native([bgm](Args&) { return Val::boolean(bgm ? audio::bgm_playing() : !audio_manager::current_bgs().name.empty()); }));
    put(o, "_gainNode", js::make_object());
    return o;
}

void bind_audio_manager() {
    js::Klass& kl = klass("AudioManager");
    auto method = [&](const char* name, std::function<Val(Args&)> fn) {
        kl.method(name, [fn](js::JsObject&, js::Vm&, Args& a) { return fn(a); });
    };
    auto vol = [&](const char* name, int Config::*m) {
        kl.field(
            name, [m](js::JsObject&) { return Val::num(config().*m); },
            [m](js::JsObject&, const Val& v) {
                config().*m = v.to_int();
                config_apply_volumes();
            });
    };
    vol("bgmVolume", &Config::bgm_volume);
    vol("bgsVolume", &Config::bgs_volume);
    vol("meVolume", &Config::me_volume);
    vol("seVolume", &Config::se_volume);
    kl.getter("_currentBgm", [](js::JsObject&) {
        AudioObj a = audio_manager::current_bgm();
        return a.valid && !a.name.empty() ? a.to_js() : Val::null();
    });
    kl.getter("_currentBgs", [](js::JsObject&) {
        AudioObj a = audio_manager::current_bgs();
        return a.valid && !a.name.empty() ? a.to_js() : Val::null();
    });
    kl.getter("_bgmBuffer", [](js::JsObject&) {
        AudioObj a = audio_manager::current_bgm();
        return a.valid && !a.name.empty() ? audio_buffer_js(true) : Val::null();
    });
    kl.getter("_bgsBuffer", [](js::JsObject&) {
        AudioObj a = audio_manager::current_bgs();
        return a.valid && !a.name.empty() ? audio_buffer_js(false) : Val::null();
    });
    kl.getter("_dangerBgs", [](js::JsObject&) {
        AudioObj a = audio_manager::current_danger_bgs();
        return a.valid ? a.to_js() : Val::null();
    });
    method("playBgm", [](Args& a) {
        audio_manager::play_bgm(AudioObj::from_js(js::arg(a, 0)), js::arg_num(a, 1));
        return Val();
    });
    method("replayBgm", [](Args& a) {
        audio_manager::replay_bgm(AudioObj::from_js(js::arg(a, 0)));
        return Val();
    });
    method("stopBgm", [](Args&) {
        audio_manager::stop_bgm();
        return Val();
    });
    method("fadeOutBgm", [](Args& a) {
        audio_manager::fade_out_bgm(js::arg_num(a, 0));
        return Val();
    });
    method("fadeInBgm", [](Args& a) {
        audio_manager::fade_in_bgm(js::arg_num(a, 0));
        return Val();
    });
    method("saveBgm", [](Args&) { return audio_manager::save_bgm().to_js(); });
    method("playBgs", [](Args& a) {
        audio_manager::play_bgs(AudioObj::from_js(js::arg(a, 0)), js::arg_num(a, 1));
        return Val();
    });
    method("replayBgs", [](Args& a) {
        audio_manager::replay_bgs(AudioObj::from_js(js::arg(a, 0)));
        return Val();
    });
    method("stopBgs", [](Args&) {
        audio_manager::stop_bgs();
        return Val();
    });
    method("fadeOutBgs", [](Args& a) {
        audio_manager::fade_out_bgs(js::arg_num(a, 0));
        return Val();
    });
    method("saveBgs", [](Args&) { return audio_manager::save_bgs().to_js(); });
    method("playMe", [](Args& a) {
        audio_manager::play_me(AudioObj::from_js(js::arg(a, 0)));
        return Val();
    });
    method("stopMe", [](Args&) {
        audio_manager::stop_me();
        return Val();
    });
    method("fadeOutMe", [](Args& a) {
        audio_manager::fade_out_me(js::arg_num(a, 0));
        return Val();
    });
    method("playSe", [](Args& a) {
        audio_manager::play_se(AudioObj::from_js(js::arg(a, 0)));
        return Val();
    });
    method("playStaticSe", [](Args& a) {
        audio_manager::play_static_se(AudioObj::from_js(js::arg(a, 0)));
        return Val();
    });
    method("stopSe", [](Args&) {
        audio_manager::stop_se();
        return Val();
    });
    method("stopAll", [](Args&) {
        audio_manager::stop_all();
        return Val();
    });
    method("isCurrentBgm", [](Args& a) {
        AudioObj cur = audio_manager::current_bgm();
        return Val::boolean(cur.valid && !cur.name.empty() && cur.name == AudioObj::from_js(js::arg(a, 0)).name);
    });
    method("isCurrentBgs", [](Args& a) {
        AudioObj cur = audio_manager::current_bgs();
        return Val::boolean(cur.valid && !cur.name.empty() && cur.name == AudioObj::from_js(js::arg(a, 0)).name);
    });
    method("fadeBgmTo", [](Args& a) {
        audio_manager::fade_bgm_to(js::arg_num(a, 0), js::arg_num(a, 1));
        return Val();
    });
    method("fadeBgsTo", [](Args& a) {
        audio_manager::fade_bgs_to(js::arg_num(a, 0), js::arg_num(a, 1));
        return Val();
    });
    method("loopCurrentME", [](Args&) {
        audio_manager::loop_current_me();
        return Val();
    });
    method("playCurrentBGMOnce", [](Args& a) {
        audio_manager::play_current_bgm_once(js::arg_int(a, 0));
        return Val();
    });
    method("playDangerBgs", [](Args& a) {
        audio_manager::play_danger_bgs(AudioObj::from_js(js::arg(a, 0)), js::arg_num(a, 1));
        return Val();
    });
    method("stopDangerBgs", [](Args&) {
        audio_manager::stop_danger_bgs();
        return Val();
    });
    method("fadeOutDangerBgs", [](Args& a) {
        audio_manager::fade_out_danger_bgs(js::arg_num(a, 0));
        return Val();
    });
    method("playToExtraBuffer", [](Args& a) {
        audio_manager::play_to_extra_buffer(js::arg_int(a, 0), js::arg(a, 1).to_str(), AudioObj::from_js(js::arg(a, 2)), js::arg_num(a, 3),
                                            js::has_arg(a, 4) ? js::arg(a, 4).truthy() : true);
        return Val();
    });
    method("StopExtraBuffer", [](Args& a) {
        audio_manager::stop_extra_buffer(js::arg_int(a, 0));
        return Val();
    });
    method("fadeoutExtraBuffer", [](Args& a) {
        audio_manager::fadeout_extra_buffer(js::arg_int(a, 0), js::arg_num(a, 1));
        return Val();
    });
    method("ClearExtraBuffers", [](Args&) {
        audio_manager::clear_extra_buffers();
        return Val();
    });
    method("areAllSECompleted", [](Args&) { return Val::boolean(audio_manager::are_all_se_completed()); });
}

void bind_sound_manager() {
    js::Klass& kl = klass("SoundManager");
    static const char* names[] = {"playCursor",       "playOk",         "playCancel",     "playBuzzer",     "playEquip",      "playSave",
                                  "playLoad",         "playBattleStart", "playEscape",     "playEnemyAttack", "playEnemyDamage", "playEnemyCollapse",
                                  "playBossCollapse1", "playBossCollapse2", "playActorDamage", "playActorCollapse", "playRecovery", "playMiss",
                                  "playEvasion",      "playMagicEvasion", "playReflection", "playShop",       "playUseItem",    "playUseSkill"};
    for (int i = 0; i < 24; i++) {
        kl.method(names[i], [i](js::JsObject&, js::Vm&, Args&) {
            audio_manager::play_system_sound(i);
            return Val();
        });
    }
    kl.method("playSystemSound", [](js::JsObject&, js::Vm&, Args& a) {
        audio_manager::play_system_sound(js::arg_int(a, 0));
        return Val();
    });
}

void bind_image_manager() {
    js::Klass& kl = klass("ImageManager");
    static const std::pair<const char*, const char*> loaders[] = {
        {"loadAnimation", "img/animations"}, {"loadBattleback1", "img/battlebacks1"}, {"loadBattleback2", "img/battlebacks2"},
        {"loadEnemy", "img/enemies"}, {"loadCharacter", "img/characters"}, {"loadFace", "img/faces"}, {"loadParallax", "img/parallaxes"},
        {"loadPicture", "img/pictures"}, {"loadSvActor", "img/sv_actors"}, {"loadSvEnemy", "img/sv_enemies"}, {"loadSystem", "img/system"},
        {"loadTileset", "img/tilesets"}, {"loadTitle1", "img/titles1"}, {"loadTitle2", "img/titles2"}, {"loadAtlas", "img/atlases"}};
    for (auto& l : loaders) {
        const char* folder = l.second;
        kl.method(l.first, [folder](js::JsObject&, js::Vm&, Args& a) { return bitmap_js(folder, js::arg(a, 0).to_str()); });
    }
    kl.method("isReady", [](js::JsObject&, js::Vm&, Args&) { return Val::boolean(true); });
    kl.method("reserveCharacter", [](js::JsObject&, js::Vm&, Args& a) { return bitmap_js("img/characters", js::arg(a, 0).to_str()); });
    kl.method("reservePicture", [](js::JsObject&, js::Vm&, Args& a) { return bitmap_js("img/pictures", js::arg(a, 0).to_str()); });
    kl.method("reserveFace", [](js::JsObject&, js::Vm&, Args& a) { return bitmap_js("img/faces", js::arg(a, 0).to_str()); });
    kl.method("reserveSystem", [](js::JsObject&, js::Vm&, Args& a) { return bitmap_js("img/system", js::arg(a, 0).to_str()); });
}

void bind_config_manager() {
    js::Klass& kl = klass("ConfigManager");
    auto num = [&](const char* name, int Config::*m, bool volume) {
        kl.field(
            name, [m](js::JsObject&) { return Val::num(config().*m); },
            [m, volume](js::JsObject&, const Val& v) {
                config().*m = v.to_int();
                if (volume) config_apply_volumes();
            });
    };
    auto flag = [&](const char* name, bool Config::*m) {
        kl.field(
            name, [m](js::JsObject&) { return Val::boolean(config().*m); }, [m](js::JsObject&, const Val& v) { config().*m = v.truthy(); });
    };
    num("bgmVolume", &Config::bgm_volume, true);
    num("bgsVolume", &Config::bgs_volume, true);
    num("meVolume", &Config::me_volume, true);
    num("seVolume", &Config::se_volume, true);
    flag("alwaysDash", &Config::always_dash);
    flag("commandRemember", &Config::command_remember);
    flag("battleCamera", &Config::battle_camera);
    flag("characterStrafe", &Config::character_strafe);
    flag("characterTurning", &Config::character_turning);
    flag("battleAnimations", &Config::battle_animations);
    num("battleAnimationSpeed", &Config::battle_animation_speed, false);
    num("battleLogSpeed", &Config::battle_log_speed, false);
    flag("menuAnimations", &Config::menu_animations);
    flag("gamepadTips", &Config::gamepad_tips);
    flag("textSkip", &Config::text_skip);
    kl.method("save", [](js::JsObject&, js::Vm&, Args&) {
        config_save();
        return Val();
    });
}

void bind_data_manager() {
    js::Klass& kl = klass("DataManager");
    auto method = [&](const char* name, std::function<Val(Args&)> fn) {
        kl.method(name, [fn](js::JsObject&, js::Vm&, Args& a) { return fn(a); });
    };
    method("isSkill", [](Args& a) { return Val::boolean(item_of(js::arg(a, 0)).is_skill()); });
    method("isItem", [](Args& a) { return Val::boolean(item_of(js::arg(a, 0)).is_item()); });
    method("isWeapon", [](Args& a) { return Val::boolean(item_of(js::arg(a, 0)).is_weapon()); });
    method("isArmor", [](Args& a) { return Val::boolean(item_of(js::arg(a, 0)).is_armor()); });
    method("isKeyItem", [](Args& a) {
        ItemRef r = item_of(js::arg(a, 0));
        return Val::boolean(r.is_item() && is_key_item(r.data()));
    });
    method("isConsumableItem", [](Args& a) {
        ItemRef r = item_of(js::arg(a, 0));
        return Val::boolean(r.is_item() && is_consumable_item(r.data()));
    });
    method("isToyItem", [](Args& a) {
        ItemRef r = item_of(js::arg(a, 0));
        return Val::boolean(r.is_item() && is_toy_item(r.data()));
    });
    method("saveGame", [](Args& a) { return Val::boolean(save::save_game(js::arg_int(a, 0))); });
    method("loadGame", [](Args& a) { return Val::boolean(save::load_game(js::arg_int(a, 0))); });
    method("latestSavefileId", [](Args&) { return Val::num(save::latest_slot()); });
    method("lastAccessedSavefileId", [](Args&) { return Val::num(save::last_accessed()); });
    method("isAnySavefileExists", [](Args&) { return Val::boolean(save::any_exists()); });
    method("maxSavefiles", [](Args&) { return Val::num(save::kMaxSlots); });
    method("writeToFile", [](Args& a) {
        save::write_text(js::arg(a, 1).to_str(), js::arg(a, 0).to_str());
        return Val();
    });
    method("writeToFileAsync", [](Args& a) {
        save::write_text(js::arg(a, 1).to_str(), js::arg(a, 0).to_str());
        Val cb = js::arg(a, 2);
        if (cb.is_function()) js::vm().call(cb, Val(), {});
        return Val();
    });
    method("readFromFile", [](Args& a) {
        std::string out;
        if (!save::read_text(js::arg(a, 0).to_str(), out)) return Val::num(0);
        return Val::str(out);
    });
    method("forceWriteToFile", [](Args& a) {
        save::write_text("TITLEDATA", js::has_arg(a, 0) ? js::arg(a, 0).to_str() : std::string("0"));
        return Val();
    });
}

void bind_language_manager() {
    js::Klass& kl = klass("LanguageManager");
    kl.method("getMessageData", [](js::JsObject&, js::Vm&, Args& a) {
        std::string code = js::arg(a, 0).to_str();
        size_t dot = code.find('.');
        if (dot == std::string::npos) return Val();
        std::shared_ptr<Document> doc = data::lang_file(code.substr(0, dot));
        if (!doc) return Val();
        const Value& e = doc->root()[code.substr(dot + 1).c_str()];
        return e.is_null() ? Val() : js::data_view(e, doc);
    });
    kl.method("getTextFrom", [](js::JsObject&, js::Vm&, Args& a) { return Val::str(data::lang_text(js::arg(a, 0).to_str())); });
    kl.getter("_language", [](js::JsObject&) { return Val::str(data::language()); });
    kl.method("setLanguage", [](js::JsObject&, js::Vm&, Args& a) {
        data::set_language(js::arg(a, 0).to_str());
        return Val();
    });
}

void bind_text_manager() {
    js::Klass& kl = klass("TextManager");
    kl.method("basic", [](js::JsObject&, js::Vm&, Args& a) { return Val::str(data::system()["terms"]["basic"][js::arg_int(a, 0)].string()); });
    kl.method("param", [](js::JsObject&, js::Vm&, Args& a) { return Val::str(data::system()["terms"]["params"][js::arg_int(a, 0)].string()); });
    kl.method("command", [](js::JsObject&, js::Vm&, Args& a) { return Val::str(data::system()["terms"]["commands"][js::arg_int(a, 0)].string()); });
    kl.method("message", [](js::JsObject&, js::Vm&, Args& a) {
        return Val::str(data::system()["terms"]["messages"][js::arg(a, 0).to_str().c_str()].string());
    });
    kl.getter("currencyUnit", [](js::JsObject&) { return Val::str(data::system()["currencyUnit"].string()); });
}

void bind_graphics() {
    js::Klass& kl = klass("Graphics");
    kl.getter("boxWidth", [](js::JsObject&) { return Val::num(640); });
    kl.getter("boxHeight", [](js::JsObject&) { return Val::num(480); });
    kl.getter("width", [](js::JsObject&) { return Val::num(640); });
    kl.getter("height", [](js::JsObject&) { return Val::num(480); });
    kl.field(
        "frameCount", [](js::JsObject&) { return Val::num((double)frame_count()); }, [](js::JsObject&, const Val& v) { set_frame_count((uint64_t)v.to_num()); });
    kl.method("_isFullScreen", [](js::JsObject&, js::Vm&, Args&) { return Val::boolean(false); });
}

void bind_yanfly() {
    js::Klass& kl = klass("Yanfly");
    auto method = [&](const char* name, std::function<Val(Args&)> fn) {
        kl.method(name, [fn](js::JsObject&, js::Vm&, Args& a) { return fn(a); });
    };
    auto spawn = [](int map_id, int event_id, double x, double y, bool preserved) {
        if (party_in_battle()) return;
        if (!params().preloaded_maps.count(map_id)) return;
        std::shared_ptr<const void> keep;
        if (!preloaded_event(map_id, event_id, &keep)) return;
        if (!game_map().events_xy(x, y).empty()) return;
        if (game_map().boat()->pos_nt(x, y) || game_map().ship()->pos_nt(x, y)) return;
        if (x < 0 || x >= game_map().width() || y < 0 || y >= game_map().height()) return;
        game_map().spawn_event(map_id, event_id, x, y, preserved);
    };
    method("SpawnEvent", [spawn](Args& a) {
        spawn(js::arg_int(a, 0), js::arg_int(a, 1), js::arg_num(a, 2), js::arg_num(a, 3), js::arg(a, 4).truthy());
        return Val();
    });
    method("SpawnEventAt", [spawn](Args& a) {
        spawn(js::arg_int(a, 0), js::arg_int(a, 1), js::arg_num(a, 2), js::arg_num(a, 3), js::arg(a, 4).truthy());
        return Val();
    });
    method("SpawnEventInRegion", [spawn](Args& a) {
        std::vector<int> regions = ints_from(js::arg(a, 2));
        auto points = game_map().valid_spawn_points(regions);
        if (points.empty()) return Val();
        auto& p = points[(size_t)std::floor(rng::next() * points.size())];
        spawn(js::arg_int(a, 0), js::arg_int(a, 1), p.first, p.second, js::arg(a, 3).truthy());
        return Val();
    });
    method("DespawnEventID", [](Args& a) {
        int id = js::arg_int(a, 0);
        game_map().despawn_event_id(id);
        return Val();
    });
    method("DespawnEvent", [](Args& a) {
        GameEvent* e = js::host_cast<GameEvent>(js::arg(a, 0));
        if (!e) return Val();
        if (!e->spawned) js::vm().throw_type_error("Cannot read property '_spawned' of undefined");
        game_map().despawn_event_id(e->event_id());
        return Val();
    });
    method("ClearSpawnedEvents", [](Args& a) {
        int map_id = js::arg(a, 0).truthy() ? js::arg_int(a, 0) : game_map().map_id();
        auto& data = game_system().get_map_spawned_event_data(map_id);
        size_t length = data.size();
        for (size_t i = 1; i < length; ++i) {
            if (i >= data.size() || !data[i]) continue;
            int event_id = data[i]->event_id();
            if (map_id == game_map().map_id()) {
                game_map().despawn_event_id(event_id);
            } else {
                long slot = (long)event_id - (long)params().spawner_id;
                if (slot >= 0 && slot < (long)data.size()) data[(size_t)slot] = nullptr;
            }
        }
        return Val();
    });
    kl.getter("Param", [](js::JsObject&) { return data_js(data::root()["yanfly"]["Param"]); });
}

void bind_omori_fixes() {
    js::Klass& kl = klass("Gamefall_OmoriFixes");
    auto method = [&](const char* name, std::function<Val(Args&)> fn) {
        kl.method(name, [fn](js::JsObject&, js::Vm&, Args& a) { return fn(a); });
    };
    method("Window_Shake", [](Args&) { return Val::boolean(false); });
    method("Random_Player_Emotion", [](Args& a) {
        GameBattler* t = battler_of(js::arg(a, 0));
        if (!t) js::vm().throw_type_error("Cannot read property 'isStateAffected' of undefined");
        int rand = (int)std::floor(rng::next() * 3);
        auto chain = [t](int a1, int a2, int a3) {
            if (t->is_state_affected(a2)) t->add_state(a3);
            else if (t->is_state_affected(a1)) t->add_state(a2);
            else t->add_state(a1);
        };
        if (rand == 0) chain(10, 11, 12);
        else if (rand == 1) chain(14, 15, 16);
        else chain(6, 7, 8);
        battle::refresh_status();
        return Val();
    });
    method("Process_Second_Chance_Message", [](Args& a) {
        GameActor* t = js::host_cast<GameActor>(js::arg(a, 0));
        if (!t) js::vm().throw_type_error("target.actorId is not a function");
        if (t->actor_id() != 1) return Val();
        if (!game_switches().value(1613)) return Val();
        game_switches().set_value(1613, false);
        game_temp().set_prop("_secondChance", Val::boolean(true));
        game_switches().set_value(2000, true);
        battle::refresh_status();
        auto entry = [](const char* code) { return js::make_array({Val::str("EVAL"), js::make_array({Val::str(code)})}); };
        Val list;
        if (game_switches().value(6)) {
            list = js::make_array({entry("$gameSwitches.setValue(6, false)"), entry("$gameMessage.showLanguageMessage(\"xx_battle_text.message_1000\")"),
                                   entry("$gameSwitches.setValue(6, true)")});
        } else {
            list = js::make_array({entry("$gameMessage.showLanguageMessage(\"xx_battle_text.message_1000\")")});
        }
        game_temp().set_prop("_addToFinishActions", list);
        return Val();
    });
    method("parseNoEffectEmotion", [](Args& a) {
        std::string tname = js::arg(a, 0).to_str();
        std::string em = js::arg(a, 1).to_str();
        if (to_lower(em).find("afraid") != std::string::npos) js::vm().throw_error("ReferenceError: target is not defined");
        std::string s = tname + " can't get " + em;
        if (s.size() >= 40) {
            size_t idx = 0;
            for (long i = 40; i >= 0; i--) {
                if ((size_t)i < s.size() && s[(size_t)i] == ' ') {
                    idx = (size_t)i;
                    break;
                }
            }
            std::string head = trim(s.substr(0, idx));
            std::string tail = s.substr(idx);
            size_t k = tail.find_first_not_of(" \t\r\n");
            tail = k == std::string::npos ? std::string() : tail.substr(k);
            s = head + "\r\n" + tail;
        }
        return Val::str(s);
    });
    method("parseNoStateChange", [](Args& a) {
        std::string first = js::arg(a, 0).to_str() + "'s " + js::arg(a, 1).to_str() + " can't go";
        std::string second = "any " + js::arg(a, 2).to_str();
        std::string complete = first + " " + second;
        if (complete.size() < 40) {
            battle::add_text(complete, 16);
        } else {
            battle::add_text(first, 1);
            battle::add_text(second, 16);
        }
        return Val();
    });
}

void bind_event_tremble() {
    js::Klass& kl = klass("Fuku_EventTremble");
    auto target = [](const Val& id) -> GameCharacterBase* {
        int n = id.to_int();
        if (n < 0) return &game_player();
        return game_map().event(n);
    };
    kl.getter("Version", [](js::JsObject&) { return Val::num(100); });
    kl.method("startObject", [](js::JsObject&, js::Vm&, Args& a) {
        GameCharacterBase* c = js::host_cast<GameCharacterBase>(js::arg(a, 0));
        if (!c) return Val::boolean(false);
        c->start_tremble(js::arg_num(a, 1), js::arg_num(a, 2), js::arg_num(a, 3));
        return Val::boolean(true);
    });
    kl.method("start", [target](js::JsObject&, js::Vm&, Args& a) {
        GameCharacterBase* c = target(js::arg(a, 0));
        if (!c) return Val::boolean(false);
        c->start_tremble(js::arg_num(a, 1), js::arg_num(a, 2), js::arg_num(a, 3));
        return Val::boolean(true);
    });
    kl.method("stopObject", [](js::JsObject&, js::Vm&, Args& a) {
        GameCharacterBase* c = js::host_cast<GameCharacterBase>(js::arg(a, 0));
        return Val::boolean(c && c->stop_tremble());
    });
    kl.method("stop", [target](js::JsObject&, js::Vm&, Args& a) {
        GameCharacterBase* c = target(js::arg(a, 0));
        return Val::boolean(c && c->stop_tremble());
    });
    kl.method("speed2cycleframe", [](js::JsObject&, js::Vm&, Args& a) { return Val::num(M_PI * 2 / js::arg_num(a, 0)); });
}

void bind_video_player() {
    js::Klass& kl = klass("ysp_VideoPlayer");
    kl.method("loadVideo", [](js::JsObject&, js::Vm&, Args& a) {
        video::load(js::arg(a, 0).to_str());
        return Val();
    });
    kl.method("releaseVideo", [](js::JsObject&, js::Vm&, Args& a) {
        video::release(js::arg(a, 0).to_str());
        return Val();
    });
    kl.method("newVideo", [](js::JsObject&, js::Vm&, Args& a) {
        video::create(js::arg(a, 0).to_str(), video_key(js::arg(a, 1)));
        return Val();
    });
    kl.method("playVideoById", [](js::JsObject&, js::Vm&, Args& a) {
        video::play(video_key(js::arg(a, 0)));
        return Val();
    });
    kl.method("stopVideoById", [](js::JsObject&, js::Vm&, Args& a) {
        video::stop(video_key(js::arg(a, 0)));
        return Val();
    });
    kl.method("setLoopById", [](js::JsObject&, js::Vm&, Args& a) {
        video::set_loop(video_key(js::arg(a, 0)));
        return Val();
    });
    kl.method("isReady", [](js::JsObject&, js::Vm&, Args&) { return Val::boolean(video::is_ready()); });
    kl.method("hasVideoFinished", [](js::JsObject&, js::Vm&, Args& a) { return Val::boolean(video::has_finished(video_key(js::arg(a, 0)))); });
    kl.method("getVideoById", [](js::JsObject&, js::Vm&, Args& a) {
        std::string id = video_key(js::arg(a, 0));
        return video::exists(id) ? js::make_object() : Val();
    });
    kl.method("pauseVideos", [](js::JsObject&, js::Vm&, Args&) {
        video::pause_all();
        return Val();
    });
    kl.method("playVideos", [](js::JsObject&, js::Vm&, Args&) {
        video::play_all();
        return Val();
    });
}

void bind_pixi() {
    js::Klass& kl = klass("PIXI_filters");
    kl.getter("ColorMatrixFilter", [](js::JsObject&) {
        static Val ctor = js::make_native([](js::Vm&, const Val&, Args&) {
            Val f = js::make_object();
            js::vm().put(f, js::atom("_colorMatrix"), Val::str("identity"));
            js::vm().put(f, js::atom("_grayscale"), Val::num(0));
            js::vm().put(f, js::atom("grayscale"), js::make_native([](js::Vm& vm, const Val& self, Args& a) {
                             vm.put(self, js::atom("_colorMatrix"), Val::str("grayscale"));
                             vm.put(self, js::atom("_grayscale"), Val::num(js::arg_num(a, 0)));
                             vm.put(self, js::atom("_multiply"), Val::boolean(js::arg(a, 1).truthy()));
                             return Val();
                         }));
            return f;
        });
        return ctor;
    });
}

}

void bind_managers() {
    bind_battle_manager();
    bind_scene_manager();
    bind_audio_manager();
    bind_sound_manager();
    bind_image_manager();
    bind_config_manager();
    bind_data_manager();
    bind_language_manager();
    bind_text_manager();
    bind_graphics();
    bind_yanfly();
    bind_omori_fixes();
    bind_event_tremble();
    bind_video_player();
    bind_pixi();
    js::Vm& vm = js::vm();
    static const char* globals[] = {"BattleManager", "SceneManager", "AudioManager", "SoundManager", "ImageManager", "ConfigManager",
                                    "DataManager",   "LanguageManager", "TextManager", "Graphics", "Yanfly"};
    for (const char* g : globals) vm.set_global(g, static_object(g));
    Val gamefall = js::make_object();
    vm.put(gamefall, js::atom("OmoriFixes"), static_object("Gamefall_OmoriFixes"));
    vm.set_global("Gamefall", gamefall);
    Val fuku = js::make_object();
    vm.put(fuku, js::atom("EventTremble"), static_object("Fuku_EventTremble"));
    vm.set_global("Fuku_Plugins", fuku);
    Val ysp = js::make_object();
    vm.put(ysp, js::atom("VideoPlayer"), static_object("ysp_VideoPlayer"));
    vm.set_global("ysp", ysp);
    Val pixi = js::make_object();
    vm.put(pixi, js::atom("filters"), static_object("PIXI_filters"));
    vm.set_global("PIXI", pixi);
    Val json_ex = js::make_object();
    vm.put(json_ex, js::atom("makeDeepCopy"), native([](Args& a) {
               std::vector<std::pair<js::Obj*, Val>> seen;
               return deep_copy(js::arg(a, 0), seen);
           }));
    vm.set_global("JsonEx", json_ex);
    static const char* scene_names[] = {"Scene_Boot",      "Scene_Title",          "Scene_OmoriTitleScreen", "Scene_Map",         "Scene_Battle",
                                        "Scene_Gameover",  "Scene_Menu",           "Scene_OmoMenuBase",      "Scene_OmoriFile",   "Scene_OmoBlackLetterMenu",
                                        "Scene_OmoriQuest", "Scene_OmoriPhotoAlbum", "Scene_OmoriItemShop",    "Scene_SlotMachine", "Scene_Gacha",
                                        "Scene_GachaBook", "Scene_OmoriName",      "Scene_OmoriBestiary",    "Scene_SplashScreens", "Scene_Save",
                                        "Scene_Load",      "Scene_Options",        "Scene_Shop"};
    for (const char* n : scene_names) vm.set_global(n, scene_class(n));
    for (const std::string& n : scene_manager::registered_names()) vm.set_global(n.c_str(), scene_class(n.c_str()));
}

}
}
