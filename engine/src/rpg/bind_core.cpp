#include <cmath>
#include <map>
#include <unordered_map>

#include "../js/compile.h"
#include "bind_util.h"
#include "character.h"
#include "data.h"
#include "game.h"
#include "interpreter.h"
#include "party.h"

namespace rpg {
namespace bind {

namespace {

struct KlassEntry {
    std::unique_ptr<js::Klass> k;
    std::string parent;
};

std::map<std::string, KlassEntry>& registry() {
    static std::map<std::string, KlassEntry> r;
    return r;
}

std::unordered_map<std::string, Val>& statics() {
    static std::unordered_map<std::string, Val> s;
    return s;
}

std::vector<std::unique_ptr<StaticObj>>& static_objs() {
    static std::vector<std::unique_ptr<StaticObj>> v;
    return v;
}

void merge_from_parent(const std::string& name, std::map<std::string, bool>& done) {
    if (done[name]) return;
    done[name] = true;
    auto& e = registry()[name];
    if (e.parent.empty()) return;
    merge_from_parent(e.parent, done);
    js::Klass* p = registry()[e.parent].k.get();
    js::Klass* k = e.k.get();
    for (auto& m : p->methods) k->methods.emplace(m.first, m.second);
    for (auto& g : p->getters) k->getters.emplace(g.first, g.second);
    for (auto& s : p->setters) k->setters.emplace(s.first, s.second);
}

std::string key_string(const Val& v) {
    if (v.is_array()) {
        std::string out;
        for (size_t i = 0; i < v.o->items.size(); i++) {
            if (i) out += ",";
            const Val& x = v.o->items[i];
            if (!x.is_nullish()) out += x.to_str();
        }
        return out;
    }
    return v.to_str();
}

double tone_at(const Val& t, size_t i) {
    if (!t.is_array() || i >= t.o->items.size()) return 0;
    return t.o->items[i].to_num();
}

Val tone_js(const double* t) { return js::make_array({Val::num(t[0]), Val::num(t[1]), Val::num(t[2]), Val::num(t[3])}); }

}

Val ints_of(const std::vector<int>& v) {
    std::vector<Val> out;
    for (int i : v) out.push_back(Val::num(i));
    return js::make_array(std::move(out));
}

Val obj_js(js::JsObject* o) { return o ? o->to_js() : Val::null(); }

GameBattler* battler_of(const Val& v) { return js::host_cast<GameBattler>(v); }
GameCharacter* character_of(const Val& v) { return js::host_cast<GameCharacter>(v); }
ItemRef item_of(const Val& v) { return ItemRef::from_js(v); }
Val item_js(const ItemRef& r) { return r.valid() ? r.to_js() : Val::null(); }
Val data_js(const Value& v) { return v.is_null() ? Val::null() : js::data_view(v, data::keep(), true); }

Val native(std::function<Val(Args&)> fn) {
    return js::make_native([fn](js::Vm&, const Val&, Args& a) { return fn(a); });
}

std::vector<int> ints_from(const Val& v) {
    std::vector<int> out;
    if (v.is_array()) {
        for (auto& x : v.o->items) out.push_back(x.to_int());
    } else if (!v.is_nullish()) {
        out.push_back(v.to_int());
    }
    return out;
}

std::string str_arg(const Args& a, size_t i, const std::string& def) { return js::arg_str(a, i, def); }

js::Klass& klass(const char* name, const char* parent) {
    auto& r = registry();
    auto it = r.find(name);
    if (it != r.end()) return *it->second.k;
    js::Klass* p = parent ? &klass(parent) : nullptr;
    KlassEntry e;
    e.k.reset(new js::Klass(name, p));
    e.parent = parent ? parent : "";
    js::Klass& ref = *e.k;
    r.emplace(name, std::move(e));
    return ref;
}

js::Klass* find_klass(const char* name) {
    auto it = registry().find(name);
    return it == registry().end() ? nullptr : it->second.k.get();
}

namespace {

std::unordered_map<js::JsObject*, std::shared_ptr<js::JsObject>>& held_map() {
    static std::unordered_map<js::JsObject*, std::shared_ptr<js::JsObject>> m;
    return m;
}

}

void hold(const std::shared_ptr<js::JsObject>& p) {
    if (p) held_map()[p.get()] = p;
}

std::shared_ptr<js::JsObject> held(js::JsObject* raw) {
    auto it = held_map().find(raw);
    return it == held_map().end() ? nullptr : it->second;
}

void purge_held() {
    auto& m = held_map();
    for (auto it = m.begin(); it != m.end();) {
        js::Obj* o = it->first->js_obj;
        if (!o || o->refs <= 1) it = m.erase(it);
        else ++it;
    }
}

namespace {

std::vector<std::function<void()>>& binders() {
    static std::vector<std::function<void()>> v;
    return v;
}

}

bool add_binder(std::function<void()> fn) {
    binders().push_back(std::move(fn));
    return true;
}

void run_binders() {
    for (auto& b : binders()) b();
}

Val static_object(const char* name) {
    auto it = statics().find(name);
    if (it != statics().end()) return it->second;
    static_objs().emplace_back(new StaticObj(&klass(name)));
    Val v = static_objs().back()->to_js();
    statics()[name] = v;
    return v;
}

void bind_core() {
    {
        auto k = js::klass_of<GameSwitches>(klass("Game_Switches"));
        k.method("value", [](GameSwitches& s, js::Vm&, Args& a) { return Val::boolean(s.value(js::arg_int(a, 0))); });
        k.call("setValue", [](GameSwitches& s, Args& a) { s.set_value(js::arg_int(a, 0), js::arg(a, 1).truthy()); });
        k.call("clear", [](GameSwitches& s, Args&) { s.clear(); });
        k.call("onChange", [](GameSwitches&, Args&) { game_map().request_refresh(); });
    }
    {
        auto k = js::klass_of<GameVariables>(klass("Game_Variables"));
        k.method("value", [](GameVariables& s, js::Vm&, Args& a) { return s.value(js::arg_int(a, 0)); });
        k.call("setValue", [](GameVariables& s, Args& a) { s.set_value(js::arg_int(a, 0), js::arg(a, 1)); });
        k.call("clear", [](GameVariables& s, Args&) { s.clear(); });
        k.call("onChange", [](GameVariables&, Args&) { game_map().request_refresh(); });
    }
    {
        auto k = js::klass_of<GameSelfSwitches>(klass("Game_SelfSwitches"));
        k.method("value", [](GameSelfSwitches& s, js::Vm&, Args& a) { return Val::boolean(s.value(key_string(js::arg(a, 0)))); });
        k.call("setValue", [](GameSelfSwitches& s, Args& a) { s.set_value(key_string(js::arg(a, 0)), js::arg(a, 1).truthy()); });
        k.call("clear", [](GameSelfSwitches& s, Args&) { s.clear(); });
        k.call("onChange", [](GameSelfSwitches&, Args&) { game_map().request_refresh(); });
    }
    {
        auto k = js::klass_of<GameSelfVariables>(klass("Game_SelfVariables"));
        k.method("value", [](GameSelfVariables& s, js::Vm&, Args& a) { return s.value(key_string(js::arg(a, 0))); });
        k.call("setValue", [](GameSelfVariables& s, Args& a) { s.set_value(key_string(js::arg(a, 0)), js::arg(a, 1)); });
        k.call("clear", [](GameSelfVariables& s, Args&) { s.clear(); });
        k.call("onChange", [](GameSelfVariables&, Args&) { game_map().request_refresh(); });
    }
    {
        auto k = js::klass_of<GameEventData>(klass("Game_EventData"));
        k.call("addCategory", [](GameEventData& d, Args& a) {
            Val c = js::arg(a, 0);
            if (c.truthy()) d.add_category(c.to_str());
        });
        k.call("removeCategory", [](GameEventData& d, Args& a) {
            Val c = js::arg(a, 0);
            if (c.truthy()) d.remove_category(c.to_str());
        });
        k.call("clearAll", [](GameEventData& d, Args&) { d.clear_all(); });
        k.call("clear", [](GameEventData& d, Args& a) {
            Val c = js::arg(a, 0);
            if (c.truthy()) d.clear(c.to_str());
        });
        k.boolean("hasCategory", [](GameEventData& d, Args& a) {
            Val c = js::arg(a, 0);
            return c.truthy() && d.has_category(c.to_str());
        });
        k.method("value", [](GameEventData& d, js::Vm& vm, Args& a) {
            std::string c = js::arg(a, 0).to_str();
            if (!d.has_category(c)) vm.throw_type_error("Cannot read property '" + key_string(js::arg(a, 1)) + "' of undefined");
            return d.value(c, key_string(js::arg(a, 1)));
        });
        k.method("valueExt", [](GameEventData& d, js::Vm& vm, Args& a) {
            Val c = js::arg(a, 0), m = js::arg(a, 1), e = js::arg(a, 2);
            if (!(c.truthy() && m.truthy() && e.truthy())) return Val();
            std::string key = m.to_str() + "," + e.to_str();
            if (!d.has_category(c.to_str())) vm.throw_type_error("Cannot read property '" + key + "' of undefined");
            return d.value(c.to_str(), key);
        });
        k.method("setValue", [](GameEventData& d, js::Vm& vm, Args& a) {
            std::string c = js::arg(a, 0).to_str();
            if (!d.has_category(c) && js::arg(a, 2).truthy()) vm.throw_type_error("Cannot set property '" + key_string(js::arg(a, 1)) + "' of undefined");
            d.set_value(c, key_string(js::arg(a, 1)), js::arg(a, 2));
            return Val();
        });
        k.method("setValueExt", [](GameEventData& d, js::Vm& vm, Args& a) {
            Val c = js::arg(a, 0), m = js::arg(a, 1), e = js::arg(a, 2);
            if (!(c.truthy() && m.truthy() && e.truthy())) return Val();
            std::string key = m.to_str() + "," + e.to_str();
            if (!d.has_category(c.to_str()) && js::arg(a, 3).truthy()) vm.throw_type_error("Cannot set property '" + key + "' of undefined");
            d.set_value(c.to_str(), key, js::arg(a, 3));
            return Val();
        });
        k.call("onChange", [](GameEventData&, Args&) { game_map().request_refresh(); });
    }
    {
        auto k = js::klass_of<GameTimer>(klass("Game_Timer"));
        k.call("start", [](GameTimer& t, Args& a) { t.start(js::arg_int(a, 0)); });
        k.call("stop", [](GameTimer& t, Args&) { t.stop(); });
        k.boolean("isWorking", [](GameTimer& t, Args&) { return t.is_working(); });
        k.num("seconds", [](GameTimer& t, Args&) { return (double)t.seconds(); });
        k.call("onExpire", [](GameTimer& t, Args&) { t.on_expire(); });
        k.call("pause", [](GameTimer& t, Args&) { t.pause(); });
        k.call("resume", [](GameTimer& t, Args&) { t.resume(); });
        k.call("gainFrames", [](GameTimer& t, Args& a) { t.gain_frames(js::arg_int(a, 0)); });
        k.call("changeDirection", [](GameTimer& t, Args& a) { t.change_direction(js::arg_int(a, 0)); });
        k.num_field("_frames", &GameTimer::frames);
        k.bool_field("_working", &GameTimer::working);
        k.bool_field("_paused", &GameTimer::paused);
        k.num_field("_direction", &GameTimer::direction);
    }
    {
        auto k = js::klass_of<GameMessage>(klass("Game_Message"));
        k.call("clear", [](GameMessage& m, Args&) { m.clear(); });
        k.call("add", [](GameMessage& m, Args& a) { m.add(js::arg(a, 0).to_str()); });
        k.call("newPage", [](GameMessage& m, Args&) { m.new_page(); });
        k.call("setFaceImage", [](GameMessage& m, Args& a) { m.set_face_image(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.call("setBackground", [](GameMessage& m, Args& a) { m.background = js::arg_int(a, 0); });
        k.call("setPositionType", [](GameMessage& m, Args& a) { m.position_type = js::arg_int(a, 0); });
        k.boolean("isBusy", [](GameMessage& m, Args&) { return m.is_busy(); });
        k.boolean("hasText", [](GameMessage& m, Args&) { return m.has_text(); });
        k.boolean("isChoice", [](GameMessage& m, Args&) { return m.is_choice(); });
        k.str("allText", [](GameMessage& m, Args&) { return m.all_text(); });
        k.str("faceName", [](GameMessage& m, Args&) { return m.face_name; });
        k.num("faceIndex", [](GameMessage& m, Args&) { return (double)m.face_index; });
        k.num("background", [](GameMessage& m, Args&) { return (double)m.background; });
        k.num("positionType", [](GameMessage& m, Args&) { return (double)m.position_type; });
        k.call("showLanguageMessage", [](GameMessage& m, Args& a) { m.show_language_message(js::arg(a, 0).to_str()); });
        k.get("_texts", [](GameMessage& m) {
            std::vector<Val> v;
            for (auto& t : m.texts) v.push_back(Val::str(t));
            return js::make_array(std::move(v));
        });
    }
    {
        auto k = js::klass_of<GameTemp>(klass("Game_Temp"));
        k.boolean("isPlaytest", [](GameTemp&, Args&) { return false; });
        k.call("reserveCommonEvent", [](GameTemp& t, Args& a) { t.reserve_common_event(js::arg_int(a, 0)); });
        k.call("clearCommonEvent", [](GameTemp& t, Args&) { t.clear_common_event(); });
        k.boolean("isCommonEventReserved", [](GameTemp& t, Args&) { return t.is_common_event_reserved(); });
        k.method("reservedCommonEvent", [](GameTemp& t, js::Vm&, Args&) { return t.common_event_id > 0 ? data_js(data::common_events()[t.common_event_id]) : Val::null(); });
        k.call("setDestination", [](GameTemp& t, Args& a) { t.set_destination(js::arg_int(a, 0), js::arg_int(a, 1)); });
        k.call("clearDestination", [](GameTemp& t, Args&) { t.clear_destination(); });
        k.boolean("isDestinationValid", [](GameTemp& t, Args&) { return t.is_destination_valid(); });
        k.num("destinationX", [](GameTemp& t, Args&) { return (double)t.destination_x; });
        k.num("destinationY", [](GameTemp& t, Args&) { return (double)t.destination_y; });
        k.call("forceActionQueue", [](GameTemp& t, Args& a) { t.force_action_queue(js::arg(a, 0)); });
        k.num_field("_commonEventId", &GameTemp::common_event_id);
        k.val_field("_forceActionQueue", &GameTemp::force_action_queue_list);
        k.bool_field("_taunt", &GameTemp::taunt);
        k.bool_field("_tauntMode", &GameTemp::taunt_mode);
        k.bool_field("_deathStateClear", &GameTemp::death_state_clear);
        k.bool_field("_recoverAllClear", &GameTemp::recover_all_clear);
        k.bool_field("_customLeaveEffectEval", &GameTemp::custom_leave_effect_eval);
        k.bool_field("_isGatheringAuraData", &GameTemp::is_gathering_aura_data);
        k.bool_field("_checkAllAliveMembers", &GameTemp::check_all_alive_members);
        k.bool_field("_stopMapEvents", &GameTemp::stop_map_events);
        k.bool_field("_stopMapPlayer", &GameTemp::stop_map_player);
        k.bool_field("_moveAllowPlayerCollision", &GameTemp::move_allow_player_collision);
        k.num_field("_loadLocationDirection", &GameTemp::load_location_direction);
        k.bool_field("_bypassLoadLocation", &GameTemp::bypass_load_location);
    }
    {
        auto k = js::klass_of<GamePicture>(klass("Game_Picture"));
        k.str("name", [](GamePicture& p, Args&) { return p.name; });
        k.num("origin", [](GamePicture& p, Args&) { return (double)p.origin; });
        k.num("x", [](GamePicture& p, Args&) { return p.x; });
        k.num("y", [](GamePicture& p, Args&) { return p.y; });
        k.num("scaleX", [](GamePicture& p, Args&) { return p.scale_x; });
        k.num("scaleY", [](GamePicture& p, Args&) { return p.scale_y; });
        k.num("opacity", [](GamePicture& p, Args&) { return p.opacity; });
        k.num("blendMode", [](GamePicture& p, Args&) { return (double)p.blend_mode; });
        k.num("angle", [](GamePicture& p, Args&) { return p.angle(); });
        k.method("tone", [](GamePicture& p, js::Vm&, Args&) { return p.has_tone ? tone_js(p.tone) : Val::null(); });
        k.call("show", [](GamePicture& p, Args& a) {
            p.show(js::arg(a, 0).to_str(), js::arg_int(a, 1), js::arg_num(a, 2), js::arg_num(a, 3), js::arg_num(a, 4), js::arg_num(a, 5), js::arg_num(a, 6), js::arg_int(a, 7));
        });
        k.call("move", [](GamePicture& p, Args& a) {
            p.move(js::arg_int(a, 0), js::arg_num(a, 1), js::arg_num(a, 2), js::arg_num(a, 3), js::arg_num(a, 4), js::arg_num(a, 5), js::arg_int(a, 6), js::arg_int(a, 7));
        });
        k.call("rotate", [](GamePicture& p, Args& a) { p.rotate(js::arg_num(a, 0)); });
        k.call("tint", [](GamePicture& p, Args& a) {
            double t[4] = {tone_at(js::arg(a, 0), 0), tone_at(js::arg(a, 0), 1), tone_at(js::arg(a, 0), 2), tone_at(js::arg(a, 0), 3)};
            p.tint(t, js::arg_int(a, 1));
        });
        k.call("erase", [](GamePicture& p, Args&) { p.erase(); });
        k.call("clearCustomFrame", [](GamePicture& p, Args&) { p.clear_custom_frame(); });
        k.call("setCustomFrameIndex", [](GamePicture& p, Args& a) { p.set_custom_frame_index(js::arg_int(a, 0)); });
        k.call("setupCustomFrame", [](GamePicture& p, Args& a) {
            std::vector<FrameRect> rects;
            Val list = js::arg(a, 0);
            if (list.is_array()) {
                js::Vm& vm = js::vm();
                for (auto& r : list.o->items) {
                    FrameRect f;
                    f.x = vm.get(r, js::atom("x")).to_num();
                    f.y = vm.get(r, js::atom("y")).to_num();
                    f.w = vm.get(r, js::atom("width")).to_num();
                    f.h = vm.get(r, js::atom("height")).to_num();
                    rects.push_back(f);
                }
            }
            p.setup_custom_frame(rects);
        });
        k.call("setCustomFrameAnimation", [](GamePicture& p, Args& a) {
            js::Vm& vm = js::vm();
            Val anim = js::arg(a, 0);
            FrameAnim fa;
            Val frames = vm.get(anim, js::atom("frames"));
            if (frames.is_array()) {
                for (auto& fr : frames.o->items) {
                    FrameAnim::F f;
                    f.frame = vm.get(fr, js::atom("frame")).to_int();
                    f.delay = vm.get(fr, js::atom("delay")).to_int();
                    f.max_delay = vm.get(fr, js::atom("maxDelay")).to_int();
                    fa.frames.push_back(f);
                }
            }
            fa.loops = vm.get(anim, js::atom("loops")).to_num();
            fa.index = vm.get(anim, js::atom("index")).to_int();
            p.set_custom_frame_animation(fa);
        });
        k.str_field("_name", &GamePicture::name);
        k.num_field("_origin", &GamePicture::origin);
        k.num_field("_x", &GamePicture::x);
        k.num_field("_y", &GamePicture::y);
        k.num_field("_scaleX", &GamePicture::scale_x);
        k.num_field("_scaleY", &GamePicture::scale_y);
        k.num_field("_opacity", &GamePicture::opacity);
        k.num_field("_blendMode", &GamePicture::blend_mode);
        k.num_field("_targetX", &GamePicture::target_x);
        k.num_field("_targetY", &GamePicture::target_y);
        k.num_field("_targetScaleX", &GamePicture::target_scale_x);
        k.num_field("_targetScaleY", &GamePicture::target_scale_y);
        k.num_field("_targetOpacity", &GamePicture::target_opacity);
        k.num_field("_duration", &GamePicture::duration);
        k.num_field("_angle", &GamePicture::angle_);
        k.num_field("_rotationSpeed", &GamePicture::rotation_speed);
    }
    {
        auto k = js::klass_of<GameScreen>(klass("Game_Screen"));
        k.num("brightness", [](GameScreen& s, Args&) { return s.brightness; });
        k.num_field("_toneDuration", &GameScreen::tone_duration);
        k.method("tone", [](GameScreen& s, js::Vm&, Args&) { return tone_js(s.tone); });
        k.method("flashColor", [](GameScreen& s, js::Vm&, Args&) { return tone_js(s.flash_color); });
        k.num("shake", [](GameScreen& s, Args&) { return s.shake; });
        k.num("zoomX", [](GameScreen& s, Args&) { return s.zoom_x; });
        k.num("zoomY", [](GameScreen& s, Args&) { return s.zoom_y; });
        k.num("zoomScale", [](GameScreen& s, Args&) { return s.zoom_scale; });
        k.str("weatherType", [](GameScreen& s, Args&) { return s.weather_type; });
        k.num("weatherPower", [](GameScreen& s, Args&) { return s.weather_power; });
        k.method("picture", [](GameScreen& s, js::Vm&, Args& a) {
            GamePicture* p = s.picture(js::arg_int(a, 0));
            return p ? p->to_js() : Val();
        });
        k.num("realPictureId", [](GameScreen& s, Args& a) { return (double)s.real_picture_id(js::arg_int(a, 0)); });
        k.call("clear", [](GameScreen& s, Args&) { s.clear(); });
        k.call("clearFade", [](GameScreen& s, Args&) { s.clear_fade(); });
        k.call("clearTone", [](GameScreen& s, Args&) { s.clear_tone(); });
        k.call("clearFlash", [](GameScreen& s, Args&) { s.clear_flash(); });
        k.call("clearShake", [](GameScreen& s, Args&) { s.clear_shake(); });
        k.call("clearZoom", [](GameScreen& s, Args&) { s.clear_zoom(); });
        k.call("clearWeather", [](GameScreen& s, Args&) { s.clear_weather(); });
        k.call("clearPictures", [](GameScreen& s, Args&) { s.clear_pictures(); });
        k.call("eraseBattlePictures", [](GameScreen& s, Args&) { s.erase_battle_pictures(); });
        k.call("startFadeOut", [](GameScreen& s, Args& a) { s.start_fade_out(js::arg_int(a, 0)); });
        k.call("startFadeIn", [](GameScreen& s, Args& a) { s.start_fade_in(js::arg_int(a, 0)); });
        k.call("startTint", [](GameScreen& s, Args& a) {
            double t[4] = {tone_at(js::arg(a, 0), 0), tone_at(js::arg(a, 0), 1), tone_at(js::arg(a, 0), 2), tone_at(js::arg(a, 0), 3)};
            s.start_tint(t, js::arg_int(a, 1));
        });
        k.call("startFlash", [](GameScreen& s, Args& a) {
            double t[4] = {tone_at(js::arg(a, 0), 0), tone_at(js::arg(a, 0), 1), tone_at(js::arg(a, 0), 2), tone_at(js::arg(a, 0), 3)};
            s.start_flash(t, js::arg_int(a, 1));
        });
        k.call("startShake", [](GameScreen& s, Args& a) { s.start_shake(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_int(a, 2)); });
        k.call("startZoom", [](GameScreen& s, Args& a) { s.start_zoom(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_num(a, 2), js::arg_int(a, 3)); });
        k.call("setZoom", [](GameScreen& s, Args& a) { s.set_zoom(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_num(a, 2)); });
        k.call("changeWeather", [](GameScreen& s, Args& a) { s.change_weather(js::arg(a, 0).to_str(), js::arg_num(a, 1), js::arg_int(a, 2)); });
        k.call("startFlashForDamage", [](GameScreen& s, Args&) { s.start_flash_for_damage(); });
        k.call("showPicture", [](GameScreen& s, Args& a) {
            s.show_picture(js::arg_int(a, 0), js::arg(a, 1).to_str(), js::arg_int(a, 2), js::arg_num(a, 3), js::arg_num(a, 4), js::arg_num(a, 5), js::arg_num(a, 6),
                           js::arg_num(a, 7), js::arg_int(a, 8));
        });
        k.call("movePicture", [](GameScreen& s, Args& a) {
            s.move_picture(js::arg_int(a, 0), js::arg_int(a, 1), js::arg_num(a, 2), js::arg_num(a, 3), js::arg_num(a, 4), js::arg_num(a, 5), js::arg_num(a, 6),
                           js::arg_int(a, 7), js::arg_int(a, 8));
        });
        k.call("rotatePicture", [](GameScreen& s, Args& a) { s.rotate_picture(js::arg_int(a, 0), js::arg_num(a, 1)); });
        k.call("tintPicture", [](GameScreen& s, Args& a) {
            double t[4] = {tone_at(js::arg(a, 1), 0), tone_at(js::arg(a, 1), 1), tone_at(js::arg(a, 1), 2), tone_at(js::arg(a, 1), 3)};
            s.tint_picture(js::arg_int(a, 0), t, js::arg_int(a, 2));
        });
        k.call("erasePicture", [](GameScreen& s, Args& a) { s.erase_picture(js::arg_int(a, 0)); });
        k.call("startBattleZoom", [](GameScreen& s, Args& a) { s.start_battle_zoom(js::arg_num(a, 0), js::arg_int(a, 1)); });
        k.boolean("isZooming", [](GameScreen& s, Args&) { return s.is_zooming(); });
        k.call("setCameraDuration", [](GameScreen& s, Args& a) { s.set_camera_duration(js::arg_int(a, 0)); });
        k.call("createHorrorEffect", [](GameScreen& s, Args& a) { horror::create(s.horror_filters, js::arg(a, 0).to_str()); });
        k.call("removeHorrorEffect", [](GameScreen& s, Args& a) { horror::remove(s.horror_filters, js::arg(a, 0).to_str()); });
        k.call("setHorrorEffectToValue", [](GameScreen& s, Args& a) { horror::set_value(s.horror_filters, js::arg(a, 0).to_str(), js::arg(a, 1).to_str(), js::arg(a, 2)); });
        k.call("clearHorrorEffects", [](GameScreen& s, Args&) { horror::clear(s.horror_filters); });
        k.call("startBlur", [](GameScreen& s, Args& a) { s.start_blur(js::arg_num(a, 0), js::arg_int(a, 1)); });
        k.call("startZoomBlur", [](GameScreen& s, Args& a) {
            s.start_zblur(js::arg_num(a, 0), js::arg_int(a, 1), js::arg_num(a, 2), js::arg_num(a, 3), js::arg_num(a, 4));
        });
        k.call("startGlow", [](GameScreen& s, Args& a) { s.start_glow(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_num(a, 2), js::arg_int(a, 3)); });
        k.call("startWave", [](GameScreen& s, Args& a) {
            s.start_wave(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_int(a, 2), js::arg_num(a, 3), js::arg_num(a, 4));
        });
        k.call("startDistortion", [](GameScreen& s, Args& a) { s.start_distortion(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_num(a, 2), js::arg_int(a, 3)); });
        k.num_field("_brightness", &GameScreen::brightness);
        k.num_field("_flashDuration", &GameScreen::flash_duration);
        k.num_field("_shakeDuration", &GameScreen::shake_duration);
        k.num_field("_zoomScale", &GameScreen::zoom_scale);
        k.val_field("_horrorFilters", &GameScreen::horror_filters);
    }
    {
        auto k = js::klass_of<GameSystem>(klass("Game_System"));
        k.boolean("isSaveEnabled", [](GameSystem& s, Args&) { return s.save_enabled; });
        k.call("initMessageFontSettings", [](GameSystem& s, Args&) { s.init_message_font_settings(); });
        k.str("getMessageFontName", [](GameSystem& s, Args&) { return s.msg_font_name; });
        k.call("setMessageFontName", [](GameSystem& s, Args& a) { s.msg_font_name = js::arg(a, 0).to_str(); });
        k.num("getMessageFontSize", [](GameSystem& s, Args&) { return (double)s.msg_font_size; });
        k.call("setMessageFontSize", [](GameSystem& s, Args& a) { s.msg_font_size = js::arg_int(a, 0); });
        k.num("getMessageFontOutline", [](GameSystem& s, Args&) { return (double)s.msg_font_outline; });
        k.call("setMessageFontOutline", [](GameSystem& s, Args& a) { s.msg_font_outline = js::arg_int(a, 0); });
        k.call("disableSave", [](GameSystem& s, Args&) { s.save_enabled = false; });
        k.call("enableSave", [](GameSystem& s, Args&) { s.save_enabled = true; });
        k.boolean("isMenuEnabled", [](GameSystem& s, Args&) { return s.menu_enabled; });
        k.call("disableMenu", [](GameSystem& s, Args&) { s.menu_enabled = false; });
        k.call("enableMenu", [](GameSystem& s, Args&) { s.menu_enabled = true; });
        k.boolean("isEncounterEnabled", [](GameSystem& s, Args&) { return s.encounter_enabled; });
        k.call("disableEncounter", [](GameSystem& s, Args&) { s.encounter_enabled = false; });
        k.call("enableEncounter", [](GameSystem& s, Args&) { s.encounter_enabled = true; });
        k.boolean("isFormationEnabled", [](GameSystem& s, Args&) { return s.formation_enabled; });
        k.call("disableFormation", [](GameSystem& s, Args&) { s.formation_enabled = false; });
        k.call("enableFormation", [](GameSystem& s, Args&) { s.formation_enabled = true; });
        k.num("battleCount", [](GameSystem& s, Args&) { return (double)s.battle_count; });
        k.num("winCount", [](GameSystem& s, Args&) { return (double)s.win_count; });
        k.num("escapeCount", [](GameSystem& s, Args&) { return (double)s.escape_count; });
        k.num("saveCount", [](GameSystem& s, Args&) { return (double)s.save_count; });
        k.num("versionId", [](GameSystem& s, Args&) { return (double)s.version_id; });
        k.num("playtime", [](GameSystem& s, Args&) { return (double)s.playtime(); });
        k.str("playtimeText", [](GameSystem& s, Args&) { return s.playtime_text(); });
        k.call("saveBgm", [](GameSystem& s, Args&) { s.save_bgm(); });
        k.call("replayBgm", [](GameSystem& s, Args&) { s.replay_bgm(); });
        k.call("saveWalkingBgm", [](GameSystem& s, Args&) { s.save_walking_bgm(); });
        k.call("replayWalkingBgm", [](GameSystem& s, Args&) { s.replay_walking_bgm(); });
        k.call("saveWalkingBgm2", [](GameSystem& s, Args&) { s.save_walking_bgm2(); });
        k.method("battleBgm", [](GameSystem& s, js::Vm&, Args&) { return s.battle_bgm().to_js(); });
        k.call("setBattleBgm", [](GameSystem& s, Args& a) { s.battle_bgm_ = AudioObj::from_js(js::arg(a, 0)); });
        k.method("victoryMe", [](GameSystem& s, js::Vm&, Args&) { return s.victory_me().to_js(); });
        k.call("setVictoryMe", [](GameSystem& s, Args& a) { s.victory_me_ = AudioObj::from_js(js::arg(a, 0)); });
        k.method("defeatMe", [](GameSystem& s, js::Vm&, Args&) { return s.defeat_me().to_js(); });
        k.call("setDefeatMe", [](GameSystem& s, Args& a) { s.defeat_me_ = AudioObj::from_js(js::arg(a, 0)); });
        k.method("windowTone", [](GameSystem& s, js::Vm&, Args&) {
            return js::make_array({Val::num(s.window_tone(0)), Val::num(s.window_tone(1)), Val::num(s.window_tone(2)), Val::num(s.window_tone(3))});
        });
        k.call("setWindowTone", [](GameSystem& s, Args& a) {
            s.has_window_tone = true;
            for (int i = 0; i < 4; i++) s.window_tone_[i] = tone_at(js::arg(a, 0), (size_t)i);
        });
        k.call("unlockAchievement", [](GameSystem& s, Args& a) { s.unlock_achievement(js::arg(a, 0).to_str()); });
        k.call("createHorrorEffect", [](GameSystem& s, Args& a) { s.create_horror_effect(js::arg(a, 0).to_str()); });
        k.call("removeHorrorEffect", [](GameSystem& s, Args& a) { s.remove_horror_effect(js::arg(a, 0).to_str()); });
        k.call("setHorrorEffectToValue", [](GameSystem& s, Args& a) { s.set_horror_effect_to_value(js::arg(a, 0).to_str(), js::arg(a, 1).to_str(), js::arg(a, 2)); });
        k.call("clearHorrorEffects", [](GameSystem& s, Args&) { s.clear_horror_effects(); });
        k.boolean("isChoiceShown", [](GameSystem& s, Args& a) { return s.is_choice_shown(js::arg_int(a, 0)); });
        k.boolean("isChoiceEnabled", [](GameSystem& s, Args& a) { return s.is_choice_enabled(js::arg_int(a, 0)); });
        k.call("hideChoice", [](GameSystem& s, Args& a) { s.hide_choice(js::arg_int(a, 0)); });
        k.call("showChoice", [](GameSystem& s, Args& a) { s.show_choice(js::arg_int(a, 0)); });
        k.call("disableChoice", [](GameSystem& s, Args& a) { s.disable_choice(js::arg_int(a, 0)); });
        k.call("enableChoice", [](GameSystem& s, Args& a) { s.enable_choice(js::arg_int(a, 0)); });
        k.call("clearChoiceSettings", [](GameSystem& s, Args&) { s.clear_choice_settings(); });
        k.num("getMagicalSeed", [](GameSystem& s, Args& a) { return s.get_magical_seed(js::arg_num(a, 0), js::has_arg(a, 1) ? js::arg(a, 1).truthy() : true); });
        k.call("growMagicalSeed", [](GameSystem& s, Args&) { s.grow_magical_seed(); });
        k.call("replantMagicalSeed", [](GameSystem& s, Args& a) { s.replant_magical_seed(js::arg_int(a, 0)); });
        k.call("addToGacha", [](GameSystem& s, Args& a) { s.add_to_gacha(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.call("removeFromGacha", [](GameSystem& s, Args& a) { s.remove_from_gacha(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.call("clearGacha", [](GameSystem& s, Args&) { s.clear_gacha(); });
        k.boolean("isInGacha", [](GameSystem& s, Args& a) { return s.is_in_gacha(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.call("addToGachaBook", [](GameSystem& s, Args& a) { s.add_to_gacha_book(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.call("removeFromGachaBook", [](GameSystem& s, Args& a) { s.remove_from_gacha_book(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.call("clearGachaBook", [](GameSystem& s, Args&) { s.clear_gacha_book(); });
        k.call("completeGachaBook", [](GameSystem& s, Args&) { s.complete_gacha_book(); });
        k.boolean("isInGachaBook", [](GameSystem& s, Args& a) { return s.is_in_gacha_book(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.call("setNewDamageCap", [](GameSystem& s, Args& a) { s.set_new_damage_cap(js::arg_num(a, 0), js::arg(a, 1).truthy()); });
        k.call("resetDamageSettings", [](GameSystem& s, Args&) { s.reset_damage_settings(); });
        k.num("maximumDamage", [](GameSystem& s, Args&) { return s.maximum_damage(); });
        k.num("maximumHealing", [](GameSystem& s, Args&) { return s.maximum_healing(); });
        k.call("onBeforeSave", [](GameSystem& s, Args&) { s.on_before_save(); });
        k.call("onAfterLoad", [](GameSystem& s, Args&) { s.on_after_load(); });
        k.bool_field("_saveEnabled", &GameSystem::save_enabled);
        k.bool_field("_menuEnabled", &GameSystem::menu_enabled);
        k.bool_field("_encounterEnabled", &GameSystem::encounter_enabled);
        k.bool_field("_formationEnabled", &GameSystem::formation_enabled);
        k.num_field("_battleCount", &GameSystem::battle_count);
        k.num_field("_winCount", &GameSystem::win_count);
        k.num_field("_escapeCount", &GameSystem::escape_count);
        k.num_field("_saveCount", &GameSystem::save_count);
        k.num_field("_framesOnSave", &GameSystem::frames_on_save);
        k.str_field("_battleSystem", &GameSystem::battle_system);
        k.num_field("_damageRate", &GameSystem::damage_rate);
        k.num_field("_flatDamage", &GameSystem::flat_damage);
        k.num_field("_healRate", &GameSystem::heal_rate);
        k.num_field("_flatHeal", &GameSystem::flat_heal);
        k.bool_field("_defaultDamageCap", &GameSystem::default_damage_cap);
        k.num_field("_critMult", &GameSystem::crit_mult);
        k.num_field("_flatCrit", &GameSystem::flat_crit);
        k.bool_field("_forceCritical", &GameSystem::force_critical);
        k.bool_field("_forceNoCritical", &GameSystem::force_no_critical);
        k.bool_field("_showEventMiniLabel", &GameSystem::show_event_mini_label);
        k.bool_field("_disableDashing", &GameSystem::disable_dashing);
        k.bool_field("_footstepsEnabled", &GameSystem::footsteps_enabled);
        k.bool_field("_wordWrap", &GameSystem::word_wrap_);
        k.bool_field("_fastForward", &GameSystem::fast_forward);
        k.num_field("_magicalSeed", &GameSystem::magical_seed);
        k.num_field("_originalMagicalSeed", &GameSystem::original_magical_seed);
    }
}

}

js::HostClass* GameSwitches::js_class() const { return &bind::klass("Game_Switches").hc; }
js::HostClass* GameVariables::js_class() const { return &bind::klass("Game_Variables").hc; }
js::HostClass* GameSelfSwitches::js_class() const { return &bind::klass("Game_SelfSwitches").hc; }
js::HostClass* GameSelfVariables::js_class() const { return &bind::klass("Game_SelfVariables").hc; }
js::HostClass* GameEventData::js_class() const { return &bind::klass("Game_EventData").hc; }
js::HostClass* GameTimer::js_class() const { return &bind::klass("Game_Timer").hc; }
js::HostClass* GameMessage::js_class() const { return &bind::klass("Game_Message").hc; }
js::HostClass* GameTemp::js_class() const { return &bind::klass("Game_Temp").hc; }
js::HostClass* GamePicture::js_class() const { return &bind::klass("Game_Picture").hc; }
js::HostClass* GameScreen::js_class() const { return &bind::klass("Game_Screen").hc; }
js::HostClass* GameSystem::js_class() const { return &bind::klass("Game_System").hc; }

js::Val game_screen_js() { return game_screen().to_js(); }
js::Val game_system_js() { return game_system().to_js(); }
js::Val game_timer_js() { return game_timer().to_js(); }
js::Val game_temp_js() { return game_temp().to_js(); }
js::Val game_message_js() { return game_message().to_js(); }

js::Val picture_js(int id) {
    GamePicture* p = game_screen().picture(id);
    return p ? p->to_js() : js::Val();
}

namespace {

std::unordered_map<std::string, std::unordered_map<uint32_t, js::Val>>& js_method_cache() {
    static std::unordered_map<std::string, std::unordered_map<uint32_t, js::Val>> c;
    return c;
}

bool resolve_js_method(const std::string& cls, uint32_t key, js::Val& out) {
    auto& cache = js_method_cache()[cls];
    auto it = cache.find(key);
    if (it != cache.end()) {
        out = it->second;
        return !out.is_undef();
    }
    const Value& bucket = data::root()["jsMethods"][cls.c_str()];
    const Value* prog = bucket.is_obj() ? bucket.find(js::atom_name(key).c_str()) : nullptr;
    js::Val fn;
    if (prog && prog->is_arr()) {
        js::ScopePtr scope = std::make_shared<js::Scope>();
        scope->function_scope = true;
        if (!js::vm().run(*prog, js::Val(), scope, &fn, data::keep())) {
            script_error(("JS METHOD " + cls + "." + js::atom_name(key)).c_str(), js::vm().last_error);
            fn = js::Val();
        }
    }
    cache[key] = fn;
    out = fn;
    return !fn.is_undef();
}

bool resolve_global(uint32_t key, js::Val& out) {
    const std::string& n = js::atom_name(key);
    if (n.empty()) return false;
    if (n[0] == '$') {
        if (!game_objects_exist()) {
            if (n.compare(0, 5, "$data") != 0) return false;
        }
        if (n == "$gameTemp") out = game_temp().to_js();
        else if (n == "$gameSystem") out = game_system().to_js();
        else if (n == "$gameScreen") out = game_screen().to_js();
        else if (n == "$gameTimer") out = game_timer().to_js();
        else if (n == "$gameMessage") out = game_message().to_js();
        else if (n == "$gameSwitches") out = game_switches().to_js();
        else if (n == "$gameVariables") out = game_variables().to_js();
        else if (n == "$gameSelfSwitches") out = game_self_switches().to_js();
        else if (n == "$gameSelfVariables") out = game_self_variables().to_js();
        else if (n == "$eventData") out = game_event_data().to_js();
        else if (n == "$gameActors") out = game_actors().to_js();
        else if (n == "$gameParty") out = game_party().to_js();
        else if (n == "$gameTroop") out = game_troop().to_js();
        else if (n == "$gameMap") out = game_map().to_js();
        else if (n == "$gamePlayer") out = game_player().to_js();
        else if (n == "$dataActors") out = bind::data_js(data::actors());
        else if (n == "$dataClasses") out = bind::data_js(data::classes());
        else if (n == "$dataSkills") out = bind::data_js(data::skills());
        else if (n == "$dataItems") out = bind::data_js(data::items());
        else if (n == "$dataWeapons") out = bind::data_js(data::weapons());
        else if (n == "$dataArmors") out = bind::data_js(data::armors());
        else if (n == "$dataEnemies") out = bind::data_js(data::enemies());
        else if (n == "$dataTroops") out = bind::data_js(data::troops());
        else if (n == "$dataStates") out = bind::data_js(data::states());
        else if (n == "$dataAnimations") out = bind::data_js(data::animations());
        else if (n == "$dataTilesets") out = bind::data_js(data::tilesets());
        else if (n == "$dataCommonEvents") out = bind::data_js(data::common_events());
        else if (n == "$dataSystem") out = bind::data_js(data::system());
        else if (n == "$dataMapInfos") out = bind::data_js(data::map_infos());
        else if (n == "$dataMap") out = game_objects_exist() && game_map().doc ? js::data_view(game_map().data(), game_map().keep()) : js::Val::null();
        else return false;
        return true;
    }
    return false;
}

}

void install_bindings() {
    static bool done = false;
    if (done) return;
    done = true;
    js::install_runtime_eval();
    bind::bind_core();
    bind::bind_characters();
    bind::bind_battlers();
    bind::bind_interpreter();
    bind::bind_managers();
    bind::bind_extras();
    bind::run_binders();
    std::map<std::string, bool> merged;
    for (auto& e : bind::registry()) bind::merge_from_parent(e.first, merged);
    js::set_js_method_resolver(resolve_js_method);
    js::vm().set_resolver(resolve_global);
}

}
