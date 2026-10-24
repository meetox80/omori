#include "bind_util.h"
#include "character.h"
#include "data.h"
#include "game.h"
#include "hooks.h"
#include "interpreter.h"
#include "party.h"

namespace rpg {
namespace bind {

namespace {

using GI = GameInterpreter;

Val list_js(GI& in) {
    const Value* l = in.list();
    return l ? js::data_view(*l, in.keep_) : Val::null();
}

}

void bind_interpreter() {
    auto k = js::klass_of<GI>(klass("Game_Interpreter"));
    k.num_field("_depth", &GI::depth_);
    k.num_field("_mapId", &GI::map_id_);
    k.num_field("_eventId", &GI::event_id_);
    k.num_field("_index", &GI::index_);
    k.num_field("_waitCount", &GI::wait_count);
    k.str_field("_waitMode", &GI::wait_mode);
    k.num_field("_indent", &GI::indent_);
    k.num_field("_freezeChecker", &GI::freeze_checker);
    k.num_field("_lastAnimatingPicture", &GI::last_animating_picture);
    k.get("_list", [](GI& in) { return list_js(in); });
    k.get("_params", [](GI& in) { return in.params_ ? js::data_view(*in.params_, in.keep_) : Val(); });
    k.get("_childInterpreter", [](GI& in) { return in.child ? in.child->to_js() : Val::null(); });
    k.prop(
        "_character", [](GI& in) { return in.character_.valid() && in.character_.get() ? in.character_.get()->to_js() : Val::null(); },
        [](GI& in, const Val& v) {
            GameCharacterBase* c = js::host_cast<GameCharacterBase>(v);
            in.character_ = c ? CharRef::of(c) : CharRef();
        });
    k.prop(
        "_branch",
        [](GI& in) {
            Val o = js::make_object();
            for (auto& kv : in.branch) js::vm().put(o, js::atom(std::to_string(kv.first)), kv.second);
            return o;
        },
        [](GI& in, const Val& v) {
            in.branch.clear();
            if (!v.is_obj()) return;
            std::vector<uint32_t> keys;
            js::ordered_keys(v.o, keys);
            for (uint32_t key : keys) {
                const Val* x = v.o->find(key);
                if (x) in.branch[(int)js::string_to_number(js::atom_name(key))] = *x;
            }
        });
    k.call("clear", [](GI& in, Args&) { in.clear(); });
    k.num("eventId", [](GI& in, Args&) { return (double)in.event_id(); });
    k.boolean("isOnCurrentMap", [](GI& in, Args&) { return in.is_on_current_map(); });
    k.boolean("setupReservedCommonEvent", [](GI& in, Args&) { return in.setup_reserved_common_event(); });
    k.boolean("isRunning", [](GI& in, Args&) { return in.is_running(); });
    k.call("update", [](GI& in, Args&) { in.update(); });
    k.boolean("updateChild", [](GI& in, Args&) { return in.update_child(); });
    k.boolean("updateWait", [](GI& in, Args&) { return in.update_wait(); });
    k.boolean("updateWaitCount", [](GI& in, Args&) { return in.update_wait_count(); });
    k.boolean("updateWaitMode", [](GI& in, Args&) { return in.update_wait_mode(); });
    k.call("setWaitMode", [](GI& in, Args& a) { in.set_wait_mode(js::arg(a, 0).to_str()); });
    k.call("wait", [](GI& in, Args& a) { in.wait(js::arg_int(a, 0)); });
    k.num("fadeSpeed", [](GI& in, Args&) { return (double)in.fade_speed(); });
    k.boolean("executeCommand", [](GI& in, Args&) { return in.execute_command(); });
    k.boolean("checkFreeze", [](GI& in, Args&) { return in.check_freeze(); });
    k.call("terminate", [](GI& in, Args&) { in.terminate(); });
    k.call("skipBranch", [](GI& in, Args&) { in.skip_branch(); });
    k.method("currentCommand", [](GI& in, js::Vm&, Args&) {
        const Value* c = in.current_command();
        return c ? js::data_view(*c, in.keep_) : Val();
    });
    k.num("nextEventCode", [](GI& in, Args&) { return (double)in.next_event_code(); });
    k.call("jumpTo", [](GI& in, Args& a) { in.jump_to(js::arg_int(a, 0)); });
    k.method("character", [](GI& in, js::Vm&, Args& a) {
        Val p = js::arg(a, 0);
        GameCharacter* c = nullptr;
        if (p.is_str()) {
            std::string s = p.to_str();
            double n = js::string_to_number(s);
            if (std::isnan(n)) {
                size_t pos = s.find("fcF:");
                if (pos != std::string::npos) {
                    GameFollower* f = game_player().followers_.follower((int)js::string_to_number(s.substr(pos + 4)));
                    return f ? f->to_js() : Val();
                }
            }
            c = in.character_id((int)n);
        } else {
            c = in.character_id(p.to_int());
        }
        return c ? c->to_js() : Val::null();
    });
    k.call("pluginCommand", [](GI& in, Args& a) {
        std::vector<std::string> args;
        Val list = js::arg(a, 1);
        if (list.is_array()) {
            for (auto& x : list.o->items) args.push_back(x.to_str());
        }
        in.plugin_command(js::arg(a, 0).to_str(), args);
    });
    k.call("addExtraFollower", [](GI&, Args& a) {
        GameFollowers& f = game_player().followers_;
        f.add_extra_follower(js::arg(a, 0), js::arg(a, 1).to_str(), js::arg_int(a, 2), js::has_arg(a, 3) ? js::arg_int(a, 3) : f.next_extra_follower_slot());
    });
    k.call("clearExtraFollowers", [](GI&, Args&) { game_player().followers_.clear_extra_followers(); });
    k.call("removeExtraFollower", [](GI&, Args& a) { game_player().followers_.remove_extra_follower(js::arg_int(a, 0)); });
    k.call("addQuest", [](GI&, Args& a) { game_party().add_quest(js::arg(a, 0).to_str(), js::arg(a, 1)); });
    k.call("removeQuest", [](GI&, Args& a) { game_party().remove_quest(js::arg(a, 0).to_str()); });
    k.call("setQuestCompleteState", [](GI&, Args& a) { game_party().set_quest_complete_state(js::arg(a, 0).to_str(), js::arg(a, 1).truthy()); });
    k.call("setQuestMessageIndex", [](GI&, Args& a) { game_party().set_quest_message_index(js::arg(a, 0).to_str(), js::arg(a, 1)); });
    k.call("setQuestStandByMessage", [](GI&, Args& a) { game_party().set_quest_stand_by_message(js::arg(a, 0)); });
    k.call("callAlbumMenu", [](GI& in, Args& a) { in.call_album_menu(js::arg_int(a, 0), js::has_arg(a, 1) ? js::arg_int(a, 1) : 1); });
    k.call("callBlackLetterMenu", [](GI& in, Args& a) { in.call_black_letter_menu(js::has_arg(a, 0) ? js::arg(a, 0).truthy() : true); });
    k.call("callQuestMenu", [](GI& in, Args&) { in.call_quest_menu(); });
    k.call("callSaveMenu", [](GI& in, Args& a) {
        in.call_save_menu(js::has_arg(a, 0) ? js::arg(a, 0).truthy() : true, js::has_arg(a, 1) ? js::arg(a, 1).truthy() : true);
    });
    k.method("generateMapFog", [](GI&, js::Vm&, Args&) { return held_js(std::make_shared<GameMapFog>()); });
    k.call("createMapFog", [](GI&, Args& a) {
        GameMapFog* fog = js::host_cast<GameMapFog>(js::arg(a, 1));
        std::shared_ptr<GameMapFog> sp = fog ? held_ptr(fog) : nullptr;
        if (!sp && fog) {
            for (auto& kv : game_map().map_fogs) {
                if (kv.second.get() == fog) sp = kv.second;
            }
        }
        game_map().add_map_fog("fog1", sp);
        scenes::add_map_fog(js::arg(a, 0).to_str());
    });
    k.call("setPictureAnimation", [](GI& in, Args& a) {
        double loops = js::has_arg(a, 3) ? js::arg_num(a, 3) : INFINITY;
        bool wait = js::has_arg(a, 4) ? js::arg(a, 4).truthy() : true;
        in.set_picture_animation(js::arg_int(a, 0), js::arg(a, 1), js::arg_num(a, 2), loops, wait);
    });
    k.call("setupPictureCustomFrames", [](GI& in, Args& a) {
        in.setup_picture_custom_frames(js::arg_int(a, 0), js::arg_num(a, 1), js::arg_num(a, 2), js::arg_int(a, 3), js::arg_int(a, 4));
    });
    k.call("removePictureCustomFrames", [](GI& in, Args& a) { in.remove_picture_custom_frames(js::arg_int(a, 0)); });
    k.call("setPictureFrameIndex", [](GI& in, Args& a) { in.set_picture_frame_index(js::arg_int(a, 0), js::arg_int(a, 1)); });
    k.call("waitForBitmap", [](GI& in, Args& a) {
        Val b = js::arg(a, 0);
        bool ok = true;
        if (b.truthy()) {
            js::Vm& vm = js::vm();
            Val ready = vm.get(b, js::atom("isReady"));
            bool is_ready = ready.is_function() ? vm.call(ready, b, {}).truthy() : true;
            double w = vm.get(b, js::atom("width")).to_num();
            double h = vm.get(b, js::atom("height")).to_num();
            ok = is_ready && w > 0 && h > 0;
        }
        in.wait_for_bitmap(ok);
    });
    k.call("showPizzaNote", [](GI& in, Args& a) { in.show_pizza_note(js::arg(a, 0)); });
    k.call("startSmashQTE", [](GI& in, Args& a) { in.start_smash_qte(js::arg(a, 0), js::arg(a, 1), js::arg(a, 2)); });
    k.call("startStopTheArrowQTE", [](GI& in, Args& a) { in.start_stop_the_arrow_qte(js::arg(a, 0), js::arg(a, 1), js::arg(a, 2), js::arg(a, 3)); });
    k.call("startHitTheMarkQTE", [](GI& in, Args& a) { in.start_hit_the_mark_qte(js::arg(a, 0), js::arg(a, 1), js::arg(a, 2), js::arg(a, 3)); });
    k.call("setupShop", [](GI& in, Args& a) { in.setup_shop(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
    k.call("setEventIcon", [](GI& in, Args& a) { in.set_event_icon(js::arg_int(a, 0), js::arg(a, 1).to_str(), js::arg_int(a, 2)); });
    k.call("showNameInputWindows", [](GI& in, Args& a) { in.show_name_input_windows(js::arg_str(a, 0), js::arg_int(a, 1, 7), js::arg_bool(a, 2, true)); });
    k.call("setupAmbience", [](GI& in, Args& a) { in.setup_ambience(js::arg(a, 0).to_str(), js::arg_num(a, 1), js::arg_num(a, 2), js::arg(a, 3).truthy()); });
    auto facing = [](GI& in, int mode, bool event_facing) {
        GameEvent* ev = game_map().event(in.event_id());
        if (!ev) return false;
        GamePlayer& pl = game_player();
        if (event_facing) {
            if (mode == 0) return ev->is_facing_towards(pl) && pl.is_position_front_of(ev->x_, ev->y_);
            if (mode == 1) return ev->is_facing_away(pl) && pl.is_position_back_of(ev->x_, ev->y_);
            return ev->is_facing_sideways(pl) && pl.is_position_side_of(ev->x_, ev->y_);
        }
        if (mode == 0) return pl.is_facing_towards(*ev) && ev->is_position_front_of(pl.x_, pl.y_);
        if (mode == 1) return pl.is_facing_away(*ev) && ev->is_position_back_of(pl.x_, pl.y_);
        return pl.is_facing_sideways(*ev) && ev->is_position_side_of(pl.x_, pl.y_);
    };
    k.boolean("checkEventFacingPlayerFront", [facing](GI& in, Args&) { return facing(in, 0, true); });
    k.boolean("checkEventFacingPlayerBack", [facing](GI& in, Args&) { return facing(in, 1, true); });
    k.boolean("checkEventFacingPlayerSide", [facing](GI& in, Args&) { return facing(in, 2, true); });
    k.boolean("checkPlayerFacingEventFront", [facing](GI& in, Args&) { return facing(in, 0, false); });
    k.boolean("checkPlayerFacingEventBack", [facing](GI& in, Args&) { return facing(in, 1, false); });
    k.boolean("checkPlayerFacingEventSide", [facing](GI& in, Args&) { return facing(in, 2, false); });
}

}

js::HostClass* GameInterpreter::js_class() const { return &bind::klass("Game_Interpreter").hc; }

}
