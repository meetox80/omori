#include <cmath>
#include <cstdio>

#include "../core/fs.h"
#include "../js/random.h"
#include "../platform/platform.h"
#include "audio_manager.h"
#include "battle.h"
#include "bind_util.h"
#include "timers.h"
#include "character.h"
#include "data.h"
#include "game.h"
#include "input.h"
#include "interpreter.h"
#include "params.h"
#include "party.h"
#include "save.h"
#include "scene.h"

namespace rpg {
namespace bind {

namespace {

std::map<std::string, bool>& cutscene_data() {
    static std::map<std::string, bool> d;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::string text;
        if (save::read_text("CUTSCENE.json", text)) {
            js::Val v = js::json_parse_text(text);
            if (v.is_obj()) {
                std::vector<uint32_t> keys;
                js::ordered_keys(v.o, keys);
                for (uint32_t k : keys) {
                    const js::Val* x = v.o->find(k);
                    if (x && x->truthy()) d[js::atom_name(k)] = true;
                }
            }
        }
    }
    return d;
}

void save_cutscene_data() {
    js::Val o = js::make_object();
    for (auto& kv : cutscene_data()) js::vm().put(o, js::atom(kv.first), Val::boolean(kv.second));
    save::write_text("CUTSCENE.json", js::json_stringify(o));
}

js::HostClass& ambience_class() {
    static js::HostClass c;
    if (!c.name) {
        c.name = "AmbienceData";
        c.get = [](js::Vm&, js::Obj*, uint32_t key, js::Val& out) {
            AmbienceData& a = game_player().ambience;
            const std::string& k = js::atom_name(key);
            if (k == "name") out = Val::str(a.name);
            else if (k == "x") out = Val::num(a.x);
            else if (k == "y") out = Val::num(a.y);
            else if (k == "opacity") out = Val::num(a.opacity);
            else if (k == "useCustomPosition") out = Val::boolean(a.use_custom_position);
            else out = Val();
            return true;
        };
        c.set = [](js::Vm&, js::Obj*, uint32_t key, const js::Val& v) {
            AmbienceData& a = game_player().ambience;
            const std::string& k = js::atom_name(key);
            if (k == "name") a.name = v.to_str();
            else if (k == "x") a.x = v.to_num();
            else if (k == "y") a.y = v.to_num();
            else if (k == "opacity") a.opacity = v.to_num();
            else if (k == "useCustomPosition") a.use_custom_position = v.truthy();
            return true;
        };
    }
    return c;
}

struct PersistentTrack {
    std::string name;
    Val obj;
};

Val persistent_track(PersistentTrack& slot, const AudioObj& cur) {
    if (!cur.valid || cur.name.empty()) {
        slot = PersistentTrack();
        return Val::null();
    }
    if (slot.name != cur.name || !slot.obj.is_obj()) {
        slot.name = cur.name;
        slot.obj = cur.to_js();
    }
    return slot.obj;
}

PersistentTrack g_bgm_track, g_bgs_track;

void bind_character_extras() {
    using CH = GameCharacter;
    auto k = js::klass_of<CH>(klass("Game_Character"));
    k.call("rwait", [](CH& c, Args& a) { c.rwait(js::arg_int(a, 0), js::arg_int(a, 1)); });
    k.call("detector", [](CH& c, Args& a) { c.detector(js::arg_int(a, 0)); });
    k.call("turn_toward", [](CH& c, Args& a) {
        if (a.size() > 1) c.turn_toward_point(js::arg_num(a, 0), js::arg_num(a, 1));
        else if (a.size() == 1) c.turn_toward_event(js::arg_int(a, 0));
    });
    k.call("step_toward", [](CH& c, Args& a) {
        if (a.size() > 1) c.step_toward_point(js::arg_num(a, 0), js::arg_num(a, 1));
        else if (a.size() == 1) c.step_toward_event(js::arg_int(a, 0));
    });
    k.call("jump_to", [](CH& c, Args& a) { c.jump_to_galv(a); });
    k.call("repeat_begin", [](CH& c, Args& a) { c.repeat_begin(js::arg_int(a, 0)); });
    k.call("repeat_end", [](CH& c, Args&) { c.repeat_end(); });
    k.call("fade", [](CH& c, Args& a) { c.fade(js::arg_num(a, 0)); });
    k.call("getRandomNPCGraphic", [](CH& c, Args&) { c.get_random_npc_graphic(); });
    k.call("processRouteEnd", [](CH& c, Args&) { c.process_route_end(); });
    auto b = js::klass_of<GameCharacterBase>(klass("Game_CharacterBase"));
    b.call("clearSpin", [](GameCharacterBase& c, Args&) { c.clear_spin(); });
    b.boolean("isFacingAway", [](GameCharacterBase& c, Args& a) {
        GameCharacterBase* o = js::host_cast<GameCharacterBase>(js::arg(a, 0));
        if (!o) js::vm().throw_type_error("Cannot read property 'direction' of undefined");
        return c.is_facing_away(*o);
    });
    b.boolean("isFacingSideways", [](GameCharacterBase& c, Args& a) {
        GameCharacterBase* o = js::host_cast<GameCharacterBase>(js::arg(a, 0));
        if (!o) js::vm().throw_type_error("Cannot read property 'direction' of undefined");
        return c.is_facing_sideways(*o);
    });
    b.boolean("isPositionBackOf", [](GameCharacterBase& c, Args& a) {
        GameCharacterBase* o = js::host_cast<GameCharacterBase>(js::arg(a, 0));
        if (!o) js::vm().throw_type_error("Cannot read property 'x' of undefined");
        return c.is_position_back_of(o->x_, o->y_);
    });
    b.boolean("isPositionSideOf", [](GameCharacterBase& c, Args& a) {
        GameCharacterBase* o = js::host_cast<GameCharacterBase>(js::arg(a, 0));
        if (!o) js::vm().throw_type_error("Cannot read property 'x' of undefined");
        return c.is_position_side_of(o->x_, o->y_);
    });
    auto e = js::klass_of<GameEvent>(klass("Game_Event"));
    e.num_field("_activationDist", &GameEvent::activation_dist);
    e.str_field("_activationType", &GameEvent::activation_type);
    e.num_field("_alertBalloon", &GameEvent::alert_balloon);
    e.num_field("_alertCommonEvent", &GameEvent::alert_common_event);
    e.num_field("_alertLock", &GameEvent::alert_lock);
    e.bool_field("_alertPlayer", &GameEvent::alert_player);
    e.str_field("_alertSound", &GameEvent::alert_sound);
    e.num_field("_alertSoundVol", &GameEvent::alert_sound_vol);
    e.num_field("_alertSoundPitch", &GameEvent::alert_sound_pitch);
    e.num_field("_alertSoundPan", &GameEvent::alert_sound_pan);
    e.num_field("_alertTimer", &GameEvent::alert_timer);
    e.bool_field("_chasePlayer", &GameEvent::chase_player);
    e.num_field("_chaseRange", &GameEvent::chase_range);
    e.num_field("_chaseSpeed", &GameEvent::chase_speed);
    e.num_field("_defaultSpeed", &GameEvent::default_speed);
    e.bool_field("_fleePlayer", &GameEvent::flee_player);
    e.num_field("_fleeRange", &GameEvent::flee_range);
    e.num_field("_fleeSpeed", &GameEvent::flee_speed);
    e.bool_field("_seePlayer", &GameEvent::see_player);
    e.num_field("_sightLock", &GameEvent::sight_lock);
    e.bool_field("_returnAfter", &GameEvent::return_after);
    e.num_field("_returnWait", &GameEvent::return_wait);
    e.bool_field("_returnPhase", &GameEvent::return_phase);
    e.num_field("_returnFrames", &GameEvent::return_frames);
    e.num_field("_staggerCount", &GameEvent::stagger_count);
    auto p = js::klass_of<GamePlayer>(klass("Game_Player"));
    p.boolean("meetPlayerProximityConditions", [](GamePlayer& pl, Args& a) {
        GameEvent* ev = js::host_cast<GameEvent>(js::arg(a, 0));
        if (!ev) js::vm().throw_type_error("Cannot read property '_activationType' of undefined");
        return pl.meet_player_proximity_conditions(*ev);
    });
    p.method("getFollowers", [](GamePlayer& pl, js::Vm&, Args&) { return pl.followers_.to_js(); });
    p.get("_ambienceData", [](GamePlayer&) { return js::make_host(&ambience_class(), nullptr, 3); });
}

void bind_map_extras() {
    auto m = js::klass_of<GameMap>(klass("Game_Map"));
    m.call("randomPos", [](GameMap& map, Args& a) { map.random_pos(js::arg_int(a, 0), js::arg_int(a, 1)); });
    m.method("selfVariableValue", [](GameMap& map, js::Vm&, Args& a) {
        js::Val v = map.interpreter->self_variable_value(js::arg(a, 0));
        return v.truthy() ? v : Val::str("");
    });
}

void bind_screen_extras() {
    auto s = js::klass_of<GameScreen>(klass("Game_Screen"));
    s.get("_pictures", [](GameScreen& scr) {
        std::vector<Val> v;
        for (auto& p : scr.pictures) v.push_back(p ? p->to_js() : Val());
        return js::make_array(std::move(v));
    });
    auto p = js::klass_of<GamePicture>(klass("Game_Picture"));
    p.call("pinToMap", [](GamePicture& pic, Args& a) { pic.pinned_to_map = js::has_arg(a, 0) ? js::arg(a, 0).truthy() : true; });
    p.bool_field("_pinnedToMap", &GamePicture::pinned_to_map);
}

void bind_interpreter_extras() {
    auto k = js::klass_of<GameInterpreter>(klass("Game_Interpreter"));
    k.call("setPictureInAlbum", [](GameInterpreter&, Args& a) { game_party().add_picture_to_album(js::arg(a, 0).to_str(), js::arg_int(a, 1), js::arg(a, 2)); });
    k.call("clearPictureInAlbum", [](GameInterpreter&, Args& a) { game_party().remove_picture_to_album(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
    k.call("pinPictureToMap", [](GameInterpreter&, Args& a) {
        GamePicture* pic = game_screen().picture(js::arg_int(a, 0));
        if (pic) pic->pinned_to_map = js::has_arg(a, 1) ? js::arg(a, 1).truthy() : true;
    });
    k.boolean("commandShowLanguageMessage", [](GameInterpreter& in, Args& a) { return in.command_show_language_message(js::arg(a, 0).to_str()); });
    k.method("spritesetLowerLayer", [](GameInterpreter&, js::Vm& vm, Args&) {
        Scene* s = scene_manager::scene();
        if (!s) vm.throw_type_error("Cannot read property '_spriteset' of null");
        Val spriteset = vm.get(s->to_js(), js::atom("_spriteset"));
        if (spriteset.is_nullish()) vm.throw_type_error("Cannot read property '_baseSprite' of undefined");
        return vm.get(spriteset, js::atom("_baseSprite"));
    });
    k.method("clearSingingMiniGameTomatoes", [](GameInterpreter&, js::Vm& vm, Args& a) {
        Val game = game_temp().prop("_singingMiniGame");
        Val events = vm.get(game, js::atom("events"));
        bool all_zero = true;
        if (events.is_array()) {
            for (auto& ev : events.o->items) {
                Val singing = vm.get(ev, js::atom("_singing"));
                Val tomato = vm.get(singing, js::atom("tomato"));
                bool in_throw = vm.get(tomato, js::atom("_inThrowMotion")).truthy();
                double opacity = vm.get(tomato, js::atom("_opacity")).to_num();
                if (!in_throw && opacity > 0) {
                    double delay = vm.get(tomato, js::atom("_fadeOutDelay")).to_num();
                    if (delay > 0) vm.put(tomato, js::atom("_fadeOutDelay"), Val::num(delay - 1));
                    else vm.put(tomato, js::atom("_opacity"), Val::num(std::max(opacity - 25, 0.0)));
                }
            }
            for (auto& ev : events.o->items) {
                Val tomato = vm.get(vm.get(ev, js::atom("_singing")), js::atom("tomato"));
                if (vm.get(tomato, js::atom("_opacity")).to_num() > 0) all_zero = false;
            }
        }
        if (!all_zero) return Val();
        Val cb = js::arg(a, 0);
        return vm.call(cb, Val(), {});
    });
}

void bind_manager_extras() {
    {
        js::Klass& kl = klass("BattleManager");
        battle::State& s = battle::st();
        kl.field(
            "_showLowHpOverlay", [&s](js::JsObject&) { return Val::boolean(s.show_low_hp_overlay); },
            [&s](js::JsObject&, const Val& v) { s.show_low_hp_overlay = v.truthy(); });
        kl.field(
            "_hideLowHpOverlay", [&s](js::JsObject&) { return Val::boolean(s.hide_low_hp_overlay); },
            [&s](js::JsObject&, const Val& v) { s.hide_low_hp_overlay = v.truthy(); });
        kl.method("setShowEncounterEffect", [&s](js::JsObject&, js::Vm&, Args& a) {
            s.show_encounter_effect = js::arg(a, 0).truthy();
            return Val();
        });
    }
    {
        js::Klass& kl = klass("AudioManager");
        kl.getter("_currentBgm", [](js::JsObject&) { return persistent_track(g_bgm_track, audio_manager::current_bgm()); });
        kl.getter("_currentBgs", [](js::JsObject&) { return persistent_track(g_bgs_track, audio_manager::current_bgs()); });
        kl.method("updateBgmParameters", [](js::JsObject&, js::Vm&, Args& a) {
            AudioObj o = AudioObj::from_js(js::arg(a, 0));
            AudioObj cur = audio_manager::current_bgm();
            if (cur.valid && cur.name == o.name) audio_manager::play_bgm(o, cur.pos);
            return Val();
        });
        kl.method("updateBgsParameters", [](js::JsObject&, js::Vm&, Args& a) {
            AudioObj o = AudioObj::from_js(js::arg(a, 0));
            AudioObj cur = audio_manager::current_bgs();
            if (cur.valid && cur.name == o.name) audio_manager::play_bgs(o, cur.pos);
            return Val();
        });
    }
    {
        js::Klass& kl = klass("SceneManager");
        kl.getter("_smoothDeltaTime", [](js::JsObject&) { return Val::num(1); });
    }
    {
        js::Klass& kl = klass("LanguageManager");
        kl.method("getTextData", [](js::JsObject&, js::Vm& vm, Args& a) {
            std::shared_ptr<Document> doc = data::lang_file(js::arg(a, 0).to_str());
            if (!doc) vm.throw_type_error("Cannot read property '" + js::arg(a, 1).to_str() + "' of undefined");
            const Value& e = doc->root()[js::arg(a, 1).to_str().c_str()];
            return e.is_null() ? Val() : js::data_view(e, doc);
        });
    }
    {
        js::Klass& kl = klass("Permanent_Manager");
        kl.method("isCutsceneSkippable", [](js::JsObject&, js::Vm&, Args& a) { return Val::boolean(cutscene_data().count(js::arg(a, 0).to_str()) != 0); });
        kl.method("addCutscene", [](js::JsObject&, js::Vm&, Args& a) {
            std::string key = js::arg(a, 0).to_str();
            if (cutscene_data().count(key)) return Val();
            cutscene_data()[key] = true;
            save_cutscene_data();
            return Val();
        });
        kl.method("load", [](js::JsObject&, js::Vm&, Args&) {
            cutscene_data();
            return Val();
        });
        kl.method("save", [](js::JsObject&, js::Vm&, Args&) {
            save_cutscene_data();
            return Val();
        });
    }
    {
        js::Klass& kl = klass("Gamefall_OmoriFixes");
        kl.method("Print_Something_Txt", [](js::JsObject&, js::Vm&, Args&) {
            std::string name = game_actors().actor(8) ? game_actors().actor(8)->name() : std::string();
            std::string art = data::root()["somethingTxt"].string();
            std::string path = plat::data_root() + "___for_" + name + "___.txt";
            fs::write_file(path, art.data(), art.size());
            return Val();
        });
        kl.method("Force_Clear_Plot_Armor", [](js::JsObject&, js::Vm&, Args&) {
            game_actors().actor(1)->remove_state(299);
            game_actors().actor(1)->remove_state(300);
            game_temp().set_prop("_secondChance", Val::boolean(false));
            return Val();
        });
    }
    {
        js::Klass& kl = klass("Yanfly");
        auto spawn_template = [](const std::string& tmpl, bool in_region, const Val& a1, const Val& a2, const Val& a3) {
            const Value& t = data::root()["yanfly"]["EventSpawn"]["Template"][to_upper(tmpl).c_str()];
            if (!t.is_obj()) return;
            int map_id = (int)js_number(data::js_string(t["mapId"]));
            int event_id = (int)js_number(data::js_string(t["eventId"]));
            Val yan = static_object("Yanfly");
            js::Vm& vm = js::vm();
            if (in_region) vm.call(vm.get(yan, js::atom("SpawnEventInRegion")), yan, {Val::num(map_id), Val::num(event_id), a1, a2});
            else vm.call(vm.get(yan, js::atom("SpawnEvent")), yan, {Val::num(map_id), Val::num(event_id), a1, a2, a3});
        };
        kl.method("SpawnEventTemplate", [spawn_template](js::JsObject&, js::Vm&, Args& a) {
            spawn_template(js::arg(a, 0).to_str(), false, js::arg(a, 1), js::arg(a, 2), js::arg(a, 3));
            return Val();
        });
        kl.method("SpawnEventTemplateAt", [spawn_template](js::JsObject&, js::Vm&, Args& a) {
            spawn_template(js::arg(a, 0).to_str(), false, js::arg(a, 1), js::arg(a, 2), js::arg(a, 3));
            return Val();
        });
        kl.method("SpawnEventTemplateInRegion", [spawn_template](js::JsObject&, js::Vm&, Args& a) {
            spawn_template(js::arg(a, 0).to_str(), true, js::arg(a, 1), js::arg(a, 2), Val());
            return Val();
        });
    }
    {
        js::Klass& kl = klass("Input");
        auto key = [](const Val& v) { return input::key_of(v.to_str()); };
        kl.method("isPressed", [key](js::JsObject&, js::Vm&, Args& a) { return Val::boolean(input::is_pressed(key(js::arg(a, 0)))); });
        kl.method("isTriggered", [key](js::JsObject&, js::Vm&, Args& a) { return Val::boolean(input::is_triggered(key(js::arg(a, 0)))); });
        kl.method("isRepeated", [key](js::JsObject&, js::Vm&, Args& a) { return Val::boolean(input::is_repeated(key(js::arg(a, 0)))); });
        kl.method("isLongPressed", [key](js::JsObject&, js::Vm&, Args& a) { return Val::boolean(input::is_long_pressed(key(js::arg(a, 0)))); });
        kl.getter("dir4", [](js::JsObject&) { return Val::num(input::dir4()); });
        kl.getter("dir8", [](js::JsObject&) { return Val::num(input::dir8()); });
        kl.method("update", [](js::JsObject&, js::Vm&, Args&) {
            input::update();
            return Val();
        });
        kl.method("clear", [](js::JsObject&, js::Vm&, Args&) {
            input::clear();
            return Val();
        });
    }
    js::Vm& vm = js::vm();
    vm.set_global("Permanent_Manager", static_object("Permanent_Manager"));
    vm.set_global("Input", static_object("Input"));
    vm.set_global("setTimeout", js::make_native([](js::Vm&, const Val&, Args& a) {
                      Val fn = js::arg(a, 0);
                      std::vector<Val> extra;
                      for (size_t i = 2; i < a.size(); i++) extra.push_back(a[i]);
                      int id = timers::set_timeout(js::arg(a, 1).to_num(), [fn, extra]() mutable {
                          if (!fn.is_function()) return;
                          try {
                              js::vm().call(fn, Val(), extra);
                          } catch (js::Throw&) {
                              script_error("setTimeout", "callback threw");
                          }
                      });
                      return Val::num(id);
                  }));
    vm.set_global("clearTimeout", js::make_native([](js::Vm&, const Val&, Args& a) {
                      timers::clear_timeout(js::arg(a, 0).to_int());
                      return Val();
                  }));
}

}

void bind_extras() {
    bind_character_extras();
    bind_map_extras();
    bind_screen_extras();
    bind_interpreter_extras();
    bind_manager_extras();
}

}
}
