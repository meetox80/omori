#include <cmath>

#include "../js/compile.h"
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

using CB = GameCharacterBase;
using CH = GameCharacter;

GameCharacterBase* base_of(const Val& v) { return js::host_cast<GameCharacterBase>(v); }

RoutePtr route_of(const Val& v) {
    std::shared_ptr<Document> doc = js::route_doc(v);
    if (!doc) return nullptr;
    return route_from(doc->root(), doc);
}

void bind_character_base() {
    auto k = js::klass_of<CB>(klass("Game_CharacterBase"));
    k.get("x", [](CB& c) { return Val::num(c.x_); });
    k.get("y", [](CB& c) { return Val::num(c.y_); });
    k.num_field("_x", &CB::x_);
    k.num_field("_y", &CB::y_);
    k.num_field("_realX", &CB::real_x);
    k.num_field("_realY", &CB::real_y);
    k.num_field("_moveSpeed", &CB::move_speed_);
    k.num_field("_moveFrequency", &CB::move_frequency_);
    k.num_field("_opacity", &CB::opacity_);
    k.num_field("_blendMode", &CB::blend_mode_);
    k.num_field("_direction", &CB::direction_);
    k.num_field("_pattern", &CB::pattern_);
    k.num_field("_priorityType", &CB::priority_type);
    k.num_field("_tileId", &CB::tile_id_);
    k.str_field("_characterName", &CB::character_name_);
    k.num_field("_characterIndex", &CB::character_index_);
    k.bool_field("_isObjectCharacter", &CB::is_object_character_);
    k.bool_field("_walkAnime", &CB::walk_anime);
    k.bool_field("_stepAnime", &CB::step_anime);
    k.bool_field("_directionFix", &CB::direction_fix);
    k.bool_field("_through", &CB::through);
    k.bool_field("_transparent", &CB::transparent);
    k.num_field("_bushDepth", &CB::bush_depth_);
    k.num_field("_animationId", &CB::animation_id_);
    k.num_field("_balloonId", &CB::balloon_id_);
    k.bool_field("_animationPlaying", &CB::animation_playing);
    k.bool_field("_balloonPlaying", &CB::balloon_playing);
    k.num_field("_stopCount", &CB::stop_count);
    k.num_field("_jumpCount", &CB::jump_count);
    k.num_field("_jumpPeak", &CB::jump_peak);
    k.bool_field("_movementSuccess", &CB::movement_success);
    k.prop(
        "_spriteOffsetX", [](CB& c) { return c.has_sprite_offsets ? Val::num(c.sprite_offset_x) : Val(); },
        [](CB& c, const Val& v) {
            c.has_sprite_offsets = true;
            c.sprite_offset_x = v.to_num();
        });
    k.prop(
        "_spriteOffsetY", [](CB& c) { return c.has_sprite_offsets ? Val::num(c.sprite_offset_y) : Val(); },
        [](CB& c, const Val& v) {
            c.has_sprite_offsets = true;
            c.sprite_offset_y = v.to_num();
        });
    k.prop(
        "_customFrameX", [](CB& c) { return c.has_custom_frame_x ? Val::num(c.custom_frame_x) : Val(); },
        [](CB& c, const Val& v) { c.set_custom_frame_xy(v, c.has_custom_frame_y ? Val::num(c.custom_frame_y) : Val()); });
    k.prop(
        "_customFrameY", [](CB& c) { return c.has_custom_frame_y ? Val::num(c.custom_frame_y) : Val(); },
        [](CB& c, const Val& v) { c.set_custom_frame_xy(c.has_custom_frame_x ? Val::num(c.custom_frame_x) : Val(), v); });
    k.boolean("pos", [](CB& c, Args& a) { return c.pos(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.boolean("posNt", [](CB& c, Args& a) { return c.pos_nt(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.num("moveSpeed", [](CB& c, Args&) { return c.move_speed(); });
    k.call("setMoveSpeed", [](CB& c, Args& a) { c.set_move_speed(js::arg_num(a, 0)); });
    k.num("moveFrequency", [](CB& c, Args&) { return c.move_frequency(); });
    k.call("setMoveFrequency", [](CB& c, Args& a) { c.set_move_frequency(js::arg_num(a, 0)); });
    k.num("opacity", [](CB& c, Args&) { return c.opacity(); });
    k.call("setOpacity", [](CB& c, Args& a) { c.set_opacity(js::arg_num(a, 0)); });
    k.num("blendMode", [](CB& c, Args&) { return (double)c.blend_mode(); });
    k.call("setBlendMode", [](CB& c, Args& a) { c.set_blend_mode(js::arg_int(a, 0)); });
    k.boolean("isNormalPriority", [](CB& c, Args&) { return c.is_normal_priority(); });
    k.call("setPriorityType", [](CB& c, Args& a) { c.set_priority_type(js::arg_int(a, 0)); });
    k.boolean("isMoving", [](CB& c, Args&) { return c.is_moving(); });
    k.boolean("isJumping", [](CB& c, Args&) { return c.is_jumping(); });
    k.num("jumpHeight", [](CB& c, Args&) { return c.jump_height(); });
    k.boolean("isStopping", [](CB& c, Args&) { return c.is_stopping(); });
    k.call("resetStopCount", [](CB& c, Args&) { c.reset_stop_count(); });
    k.num("realMoveSpeed", [](CB& c, Args&) { return c.real_move_speed(); });
    k.num("distancePerFrame", [](CB& c, Args&) { return c.distance_per_frame(); });
    k.boolean("isDashing", [](CB& c, Args&) { return c.is_dashing(); });
    k.call("straighten", [](CB& c, Args&) { c.straighten(); });
    k.num("reverseDir", [](CB& c, Args& a) { return (double)c.reverse_dir(js::arg_int(a, 0)); });
    k.boolean("canPass", [](CB& c, Args& a) { return c.can_pass(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_int(a, 2)); });
    k.boolean("isCollidedWithCharacters", [](CB& c, Args& a) { return c.is_collided_with_characters(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("setPosition", [](CB& c, Args& a) { c.set_position(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("copyPosition", [](CB& c, Args& a) {
        CB* o = base_of(js::arg(a, 0));
        if (o) c.copy_position(*o);
    });
    k.call("locate", [](CB& c, Args& a) { c.locate(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.num("direction", [](CB& c, Args&) { return (double)c.direction(); });
    k.call("setDirection", [](CB& c, Args& a) { c.set_direction(js::arg_int(a, 0)); });
    k.boolean("isTile", [](CB& c, Args&) { return c.is_tile(); });
    k.boolean("isObjectCharacter", [](CB& c, Args&) { return c.is_object_character(); });
    k.num("shiftY", [](CB& c, Args&) { return c.shift_y(); });
    k.num("scrolledX", [](CB& c, Args&) { return c.scrolled_x(); });
    k.num("scrolledY", [](CB& c, Args&) { return c.scrolled_y(); });
    k.num("screenX", [](CB& c, Args&) { return c.screen_x(); });
    k.num("screenY", [](CB& c, Args&) { return c.screen_y(); });
    k.num("screenZ", [](CB& c, Args&) { return c.screen_z(); });
    k.boolean("isNearTheScreen", [](CB& c, Args&) { return c.is_near_the_screen(); });
    k.call("update", [](CB& c, Args&) { c.update(); });
    k.num("pattern", [](CB& c, Args&) { return (double)c.pattern(); });
    k.call("setPattern", [](CB& c, Args& a) { c.set_pattern(js::arg_int(a, 0)); });
    k.boolean("isOriginalPattern", [](CB& c, Args&) { return c.is_original_pattern(); });
    k.call("resetPattern", [](CB& c, Args&) { c.reset_pattern(); });
    k.call("refreshBushDepth", [](CB& c, Args&) { c.refresh_bush_depth(); });
    k.boolean("isOnLadder", [](CB& c, Args&) { return c.is_on_ladder(); });
    k.boolean("isOnBush", [](CB& c, Args&) { return c.is_on_bush(); });
    k.num("terrainTag", [](CB& c, Args&) { return (double)c.terrain_tag(); });
    k.num("regionId", [](CB& c, Args&) { return (double)c.region_id(); });
    k.call("increaseSteps", [](CB& c, Args&) { c.increase_steps(); });
    k.num("tileId", [](CB& c, Args&) { return (double)c.tile_id(); });
    k.str("characterName", [](CB& c, Args&) { return c.character_name(); });
    k.num("characterIndex", [](CB& c, Args&) { return (double)c.character_index(); });
    k.call("setImage", [](CB& c, Args& a) { c.set_image(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
    k.call("setTileImage", [](CB& c, Args& a) { c.set_tile_image(js::arg_int(a, 0)); });
    k.call("checkEventTriggerTouchFront", [](CB& c, Args& a) { c.check_event_trigger_touch_front(js::arg_int(a, 0)); });
    k.call("moveStraight", [](CB& c, Args& a) { c.move_straight(js::arg_int(a, 0)); });
    k.call("moveDiagonally", [](CB& c, Args& a) { c.move_diagonally(js::arg_int(a, 0), js::arg_int(a, 1)); });
    k.call("jump", [](CB& c, Args& a) { c.jump(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.boolean("hasWalkAnime", [](CB& c, Args&) { return c.has_walk_anime(); });
    k.call("setWalkAnime", [](CB& c, Args& a) { c.set_walk_anime(js::arg(a, 0).truthy()); });
    k.boolean("hasStepAnime", [](CB& c, Args&) { return c.has_step_anime(); });
    k.call("setStepAnime", [](CB& c, Args& a) { c.set_step_anime(js::arg(a, 0).truthy()); });
    k.boolean("isDirectionFixed", [](CB& c, Args&) { return c.is_direction_fixed(); });
    k.call("setDirectionFix", [](CB& c, Args& a) { c.set_direction_fix(js::arg(a, 0).truthy()); });
    k.boolean("isThrough", [](CB& c, Args&) { return c.is_through(); });
    k.call("setThrough", [](CB& c, Args& a) { c.set_through(js::arg(a, 0).truthy()); });
    k.boolean("isTransparent", [](CB& c, Args&) { return c.is_transparent(); });
    k.num("bushDepth", [](CB& c, Args&) { return c.bush_depth(); });
    k.call("setTransparent", [](CB& c, Args& a) { c.set_transparent(js::arg(a, 0).truthy()); });
    k.call("requestAnimation", [](CB& c, Args& a) { c.request_animation(js::arg_num(a, 0)); });
    k.call("requestBalloon", [](CB& c, Args& a) { c.request_balloon(js::arg_int(a, 0)); });
    k.num("animationId", [](CB& c, Args&) { return (double)c.animation_id(); });
    k.num("balloonId", [](CB& c, Args&) { return (double)c.balloon_id(); });
    k.call("startAnimation", [](CB& c, Args&) { c.start_animation(); });
    k.call("startBalloon", [](CB& c, Args&) { c.start_balloon(); });
    k.boolean("isAnimationPlaying", [](CB& c, Args&) { return c.is_animation_playing(); });
    k.boolean("isBalloonPlaying", [](CB& c, Args&) { return c.is_balloon_playing(); });
    k.call("endAnimation", [](CB& c, Args&) { c.end_animation(); });
    k.call("endBalloon", [](CB& c, Args&) { c.end_balloon(); });
    k.call("setCustomFrameXY", [](CB& c, Args& a) { c.set_custom_frame_xy(js::arg(a, 0), js::arg(a, 1)); });
    k.call("setCustomFrameWH", [](CB& c, Args& a) { c.set_custom_frame_wh(js::arg(a, 0), js::arg(a, 1)); });
    k.boolean("isFacingTowards", [](CB& c, Args& a) {
        CB* o = base_of(js::arg(a, 0));
        if (!o) js::vm().throw_type_error("Cannot read property 'x' of undefined");
        return c.is_facing_towards(*o);
    });
    k.boolean("isPositionFrontOf", [](CB& c, Args& a) {
        CB* o = base_of(js::arg(a, 0));
        if (!o) js::vm().throw_type_error("Cannot read property 'x' of undefined");
        return c.is_position_front_of(o->x_, o->y_);
    });
    k.call("setupEventcon", [](CB& c, Args& a) { c.setup_eventcon(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
    k.call("removeEventcon", [](CB& c, Args&) { c.remove_eventcon(); });
    k.call("clearHorrorEffects", [](CB& c, Args&) { horror::clear(c.horror_filters); });
    k.call("createHorrorEffect", [](CB& c, Args& a) { horror::create(c.horror_filters, js::arg(a, 0).to_str()); });
    k.call("removeHorrorEffect", [](CB& c, Args& a) { horror::remove(c.horror_filters, js::arg(a, 0).to_str()); });
    k.call("setHorrorEffectToValue", [](CB& c, Args& a) { horror::set_value(c.horror_filters, js::arg(a, 0).to_str(), js::arg(a, 1).to_str(), js::arg(a, 2)); });
    k.call("startSpin", [](CB& c, Args& a) { c.start_spin(js::arg_int(a, 0), js::arg_int(a, 1, 10)); });
    k.call("setupInputIcon", [](CB& c, Args& a) { c.setup_input_icon(js::arg(a, 0).to_str(), js::arg(a, 1), js::arg(a, 2)); });
    k.boolean("isEvent", [](CB& c, Args&) { return c.is_event(); });
    k.boolean("isPlayer", [](CB& c, Args&) { return c.is_player(); });
    k.boolean("isOnReflection", [](CB& c, Args&) { return game_map().is_on_reflection(c); });
}

void bind_character() {
    auto k = js::klass_of<CH>(klass("Game_Character", "Game_CharacterBase"));
    k.bool_field("_moveRouteForcing", &CH::move_route_forcing);
    k.num_field("_moveRouteIndex", &CH::move_route_index);
    k.num_field("_waitCount", &CH::wait_count);
    k.call("memorizeMoveRoute", [](CH& c, Args&) { c.memorize_move_route(); });
    k.call("restoreMoveRoute", [](CH& c, Args&) { c.restore_move_route(); });
    k.boolean("isMoveRouteForcing", [](CH& c, Args&) { return c.is_move_route_forcing(); });
    k.call("setMoveRoute", [](CH& c, Args& a) {
        RoutePtr r = route_of(js::arg(a, 0));
        if (r) c.set_move_route(r);
    });
    k.call("forceMoveRoute", [](CH& c, Args& a) {
        RoutePtr r = route_of(js::arg(a, 0));
        if (r) c.force_move_route(r);
    });
    k.num("deltaXFrom", [](CH& c, Args& a) { return c.delta_x_from(js::arg_num(a, 0)); });
    k.num("deltaYFrom", [](CH& c, Args& a) { return c.delta_y_from(js::arg_num(a, 0)); });
    k.call("moveRandom", [](CH& c, Args&) { c.move_random(); });
    k.call("moveTowardCharacter", [](CH& c, Args& a) {
        CB* o = base_of(js::arg(a, 0));
        if (o) c.move_toward_character(*o);
    });
    k.call("moveAwayFromCharacter", [](CH& c, Args& a) {
        CB* o = base_of(js::arg(a, 0));
        if (o) c.move_away_from_character(*o);
    });
    k.call("turnTowardCharacter", [](CH& c, Args& a) {
        CB* o = base_of(js::arg(a, 0));
        if (o) c.turn_toward_character(*o);
    });
    k.call("turnAwayFromCharacter", [](CH& c, Args& a) {
        CB* o = base_of(js::arg(a, 0));
        if (o) c.turn_away_from_character(*o);
    });
    k.call("turnTowardPlayer", [](CH& c, Args&) { c.turn_toward_player(); });
    k.call("turnAwayFromPlayer", [](CH& c, Args&) { c.turn_away_from_player(); });
    k.call("moveTowardPlayer", [](CH& c, Args&) { c.move_toward_player(); });
    k.call("moveAwayFromPlayer", [](CH& c, Args&) { c.move_away_from_player(); });
    k.call("moveForward", [](CH& c, Args&) { c.move_forward(); });
    k.call("moveBackward", [](CH& c, Args&) { c.move_backward(); });
    k.call("turnRight90", [](CH& c, Args&) { c.turn_right_90(); });
    k.call("turnLeft90", [](CH& c, Args&) { c.turn_left_90(); });
    k.call("turn180", [](CH& c, Args&) { c.turn_180(); });
    k.call("turnRightOrLeft90", [](CH& c, Args&) { c.turn_right_or_left_90(); });
    k.call("turnRandom", [](CH& c, Args&) { c.turn_random(); });
    k.call("swap", [](CH& c, Args& a) {
        CB* o = base_of(js::arg(a, 0));
        if (o) c.swap(*o);
    });
    k.num("findDirectionTo", [](CH& c, Args& a) { return (double)c.find_direction_to(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.num("searchLimit", [](CH& c, Args&) { return (double)c.search_limit(); });
    k.call("moveToPoint", [](CH& c, Args& a) { c.move_to_point(js::arg_num(a, 0), js::arg_num(a, 1), js::arg(a, 2).truthy()); });
    k.call("fadeTo", [](CH& c, Args& a) { c.fade_to(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("fadeIn", [](CH& c, Args& a) { c.fade_to(255, js::arg_num(a, 0)); });
    k.call("fadeOut", [](CH& c, Args& a) { c.fade_to(0, js::arg_num(a, 0)); });
    k.call("setFrame", [](CH& c, Args& a) { c.set_frame(js::arg(a, 0).to_str(), js::arg_int(a, 1), js::arg_int(a, 2), js::arg_int(a, 3)); });
    k.call("restoreFrame", [](CH& c, Args&) { c.restore_frame(); });
    k.call("jumpToPoint", [](CH& c, Args& a) { c.jump_to_point(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("teleportToPoint", [](CH& c, Args& a) { c.teleport_to_point(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("getToolGraphic", [](CH& c, Args& a) { c.get_tool_graphic(js::arg_int(a, 0)); });
}

void bind_followers() {
    {
        auto k = js::klass_of<GameFollower>(klass("Game_Follower", "Game_Character"));
        k.num_field("_memberIndex", &GameFollower::member_index);
        k.method("actor", [](GameFollower& f, js::Vm&, Args&) { return obj_js(f.actor()); });
        k.call("refresh", [](GameFollower& f, Args&) { f.refresh(); });
        k.boolean("isVisible", [](GameFollower& f, Args&) { return f.is_visible(); });
        k.call("chaseCharacter", [](GameFollower& f, Args& a) {
            CB* o = base_of(js::arg(a, 0));
            if (o) f.chase_character(*o);
        });
        k.call("setMovementGraphics", [](GameFollower& f, Args& a) { f.set_movement_graphics(js::arg(a, 0), js::arg(a, 1), js::arg(a, 2)); });
        k.call("setMovementGraphicData", [](GameFollower& f, Args& a) { f.set_movement_graphic_data(js::arg(a, 0).to_str(), js::arg(a, 1)); });
        k.call("clearMovementGraphics", [](GameFollower& f, Args&) { f.clear_movement_graphics(); });
        k.prop(
            "_characterMovementGraphics", [](GameFollower& f) { return f.movement_graphics_js(); },
            [](GameFollower& f, const Val& v) {
                if (v.is_nullish()) f.clear_movement_graphics();
                else f.set_movement_graphics_js(v);
            });
    }
    {
        auto k = js::klass_of<GameFollowers>(klass("Game_Followers"));
        k.get("_data", [](GameFollowers& f) {
            std::vector<Val> v;
            for (auto& x : f.data) v.push_back(x->to_js());
            return js::make_array(std::move(v));
        });
        k.bool_field("_visible", &GameFollowers::visible);
        k.bool_field("_gathering", &GameFollowers::gathering);
        k.boolean("isVisible", [](GameFollowers& f, Args&) { return f.is_visible(); });
        k.call("show", [](GameFollowers& f, Args&) { f.show(); });
        k.call("hide", [](GameFollowers& f, Args&) { f.hide(); });
        k.method("follower", [](GameFollowers& f, js::Vm&, Args& a) {
            GameFollower* x = f.follower(js::arg_int(a, 0));
            return x ? x->to_js() : Val();
        });
        k.method("visibleFollowers", [](GameFollowers& f, js::Vm&, Args&) { return arr_of(f.visible_followers()); });
        k.call("refresh", [](GameFollowers& f, Args&) { f.refresh(); });
        k.call("update", [](GameFollowers& f, Args&) { f.update(); });
        k.call("updateMove", [](GameFollowers& f, Args&) { f.update_move(); });
        k.call("jumpAll", [](GameFollowers& f, Args&) { f.jump_all(); });
        k.call("synchronize", [](GameFollowers& f, Args& a) { f.synchronize(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_int(a, 2)); });
        k.call("gather", [](GameFollowers& f, Args&) { f.gather(); });
        k.boolean("areGathering", [](GameFollowers& f, Args&) { return f.are_gathering(); });
        k.boolean("areMoving", [](GameFollowers& f, Args&) { return f.are_moving(); });
        k.boolean("areGathered", [](GameFollowers& f, Args&) { return f.are_gathered(); });
        k.boolean("isSomeoneCollided", [](GameFollowers& f, Args& a) { return f.is_someone_collided(js::arg_num(a, 0), js::arg_num(a, 1)); });
        k.num("nextExtraFollowerSlot", [](GameFollowers& f, Args&) { return (double)f.next_extra_follower_slot(); });
        k.num("lastExtraFollowerSlot", [](GameFollowers& f, Args&) { return (double)f.last_extra_follower_slot(); });
        k.call("clearExtraFollowers", [](GameFollowers& f, Args&) { f.clear_extra_followers(); });
        k.call("addExtraFollower", [](GameFollowers& f, Args& a) {
            f.add_extra_follower(js::arg(a, 0), js::arg(a, 1).to_str(), js::arg_int(a, 2), js::has_arg(a, 3) ? js::arg_int(a, 3) : f.next_extra_follower_slot());
        });
        k.call("removeExtraFollower", [](GameFollowers& f, Args& a) { f.remove_extra_follower(js::arg_int(a, 0)); });
        k.call("removeFollowerById", [](GameFollowers& f, Args& a) { f.remove_follower_by_id(js::arg(a, 0)); });
        k.call("realignExtraFollowers", [](GameFollowers& f, Args&) { f.realign_extra_followers(); });
        k.method("getFollowerById", [](GameFollowers& f, js::Vm&, Args& a) {
            GameFollower* x = f.get_follower_by_id(js::arg(a, 0));
            return x ? x->to_js() : Val();
        });
        k.method("all", [](GameFollowers& f, js::Vm&, Args&) { return arr_of(f.all()); });
    }
}

void bind_player() {
    auto k = js::klass_of<GamePlayer>(klass("Game_Player", "Game_Character"));
    k.get("_followers", [](GamePlayer& p) { return p.followers_.to_js(); });
    k.str_field("_vehicleType", &GamePlayer::vehicle_type);
    k.bool_field("_dashing", &GamePlayer::dashing);
    k.bool_field("_needsMapReload", &GamePlayer::needs_map_reload);
    k.bool_field("_transferring", &GamePlayer::transferring);
    k.num_field("_newMapId", &GamePlayer::new_map_id);
    k.num_field("_newX", &GamePlayer::new_x);
    k.num_field("_newY", &GamePlayer::new_y);
    k.num_field("_newDirection", &GamePlayer::new_direction);
    k.num_field("_fadeType", &GamePlayer::fade_type);
    k.num_field("_encounterCount", &GamePlayer::encounter_count);
    k.bool_field("_footstepDisabled", &GamePlayer::footstep_disabled);
    k.val_field("_footstepSound", &GamePlayer::footstep_sound);
    k.method("followers", [](GamePlayer& p, js::Vm&, Args&) { return p.followers_.to_js(); });
    k.call("refresh", [](GamePlayer& p, Args&) { p.refresh(); });
    k.call("reserveTransfer", [](GamePlayer& p, Args& a) {
        p.reserve_transfer(js::arg_int(a, 0), js::arg_num(a, 1), js::arg_num(a, 2), js::arg_int(a, 3), js::arg_int(a, 4));
    });
    k.call("requestMapReload", [](GamePlayer& p, Args&) { p.request_map_reload(); });
    k.boolean("isTransferring", [](GamePlayer& p, Args&) { return p.is_transferring(); });
    k.num("newMapId", [](GamePlayer& p, Args&) { return (double)p.new_map_id; });
    k.num("fadeType", [](GamePlayer& p, Args&) { return (double)p.fade_type; });
    k.call("performTransfer", [](GamePlayer& p, Args&) { p.perform_transfer(); });
    k.boolean("isInVehicle", [](GamePlayer& p, Args&) { return p.is_in_vehicle(); });
    k.boolean("isInBoat", [](GamePlayer& p, Args&) { return p.is_in_boat(); });
    k.boolean("isInShip", [](GamePlayer& p, Args&) { return p.is_in_ship(); });
    k.boolean("isInAirship", [](GamePlayer& p, Args&) { return p.is_in_airship(); });
    k.method("vehicle", [](GamePlayer& p, js::Vm&, Args&) { return obj_js(p.vehicle()); });
    k.call("center", [](GamePlayer& p, Args& a) { p.center(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("makeEncounterCount", [](GamePlayer& p, Args&) { p.make_encounter_count(); });
    k.num("makeEncounterTroopId", [](GamePlayer& p, Args&) { return (double)p.make_encounter_troop_id(); });
    k.boolean("getOnOffVehicle", [](GamePlayer& p, Args&) { return p.get_on_off_vehicle(); });
    k.call("showFollowers", [](GamePlayer& p, Args&) { p.show_followers(); });
    k.call("hideFollowers", [](GamePlayer& p, Args&) { p.hide_followers(); });
    k.call("gatherFollowers", [](GamePlayer& p, Args&) { p.gather_followers(); });
    k.boolean("areFollowersGathering", [](GamePlayer& p, Args&) { return p.are_followers_gathering(); });
    k.boolean("areFollowersGathered", [](GamePlayer& p, Args&) { return p.are_followers_gathered(); });
    k.call("clearBlockedMovementInputs", [](GamePlayer& p, Args&) { p.clear_blocked_movement_inputs(); });
    k.call("blockMovementInputs", [](GamePlayer& p, Args& a) {
        std::vector<int> dirs;
        for (auto& v : a) dirs.push_back(v.to_int());
        p.block_movement_inputs(dirs);
    });
    k.call("unblockMovementInputs", [](GamePlayer& p, Args& a) {
        std::vector<int> dirs;
        for (auto& v : a) dirs.push_back(v.to_int());
        p.unblock_movement_inputs(dirs);
    });
    k.call("clearAmbienceData", [](GamePlayer& p, Args&) { p.clear_ambience_data(); });
    k.boolean("canMove", [](GamePlayer& p, Args&) { return p.can_move(); });
    k.call("moveByInput", [](GamePlayer& p, Args&) { p.move_by_input(); });
    k.boolean("triggerAction", [](GamePlayer& p, Args&) { return p.trigger_action(); });
}

void bind_event() {
    auto k = js::klass_of<GameEvent>(klass("Game_Event", "Game_Character"));
    k.num_field("_mapId", &GameEvent::map_id);
    k.num_field("_eventId", &GameEvent::event_id_);
    k.num_field("_moveType", &GameEvent::move_type);
    k.prop(
        "_trigger", [](GameEvent& e) { return e.trigger_null ? Val::null() : Val::num(e.trigger); },
        [](GameEvent& e, const Val& v) {
            e.trigger_null = v.is_nullish();
            e.trigger = v.to_int();
        });
    k.bool_field("_starting", &GameEvent::starting);
    k.bool_field("_erased", &GameEvent::erased);
    k.num_field("_pageIndex", &GameEvent::page_index);
    k.num_field("_originalPattern", &GameEvent::original_pattern);
    k.num_field("_originalDirection", &GameEvent::original_direction);
    k.num_field("_prelockDirection", &GameEvent::prelock_direction);
    k.bool_field("_locked", &GameEvent::locked);
    k.get("_interpreter", [](GameEvent& e) { return e.interpreter ? e.interpreter->to_js() : Val::null(); });
    k.num("eventId", [](GameEvent& e, Args&) { return (double)e.event_id(); });
    k.method("event", [](GameEvent& e, js::Vm&, Args&) {
        const Value& v = e.event();
        return v.is_null() ? Val() : js::data_view(v, e.event_keep());
    });
    k.method("page", [](GameEvent& e, js::Vm&, Args&) {
        const Value* p = e.page();
        return p ? js::data_view(*p, e.event_keep()) : Val();
    });
    k.method("list", [](GameEvent& e, js::Vm&, Args&) {
        const Value* l = e.list();
        return l ? js::data_view(*l, e.event_keep()) : Val();
    });
    k.boolean("isCollidedWithEvents", [](GameEvent& e, Args& a) { return e.is_collided_with_events(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("lock", [](GameEvent& e, Args&) { e.lock(); });
    k.call("unlock", [](GameEvent& e, Args&) { e.unlock(); });
    k.call("start", [](GameEvent& e, Args&) { e.start(); });
    k.call("erase", [](GameEvent& e, Args&) { e.erase(); });
    k.call("refresh", [](GameEvent& e, Args&) { e.refresh(); });
    k.boolean("isStarting", [](GameEvent& e, Args&) { return e.starting; });
    k.call("clearStartingFlag", [](GameEvent& e, Args&) { e.starting = false; });
    k.num("stopCountThreshold", [](GameEvent& e, Args&) { return e.stop_count_threshold(); });
    k.call("updateSelfMovement", [](GameEvent& e, Args&) { e.update_self_movement(); });
    k.str("note", [](GameEvent& e, Args&) { return e.note(); });
}

void bind_vehicle() {
    auto k = js::klass_of<GameVehicle>(klass("Game_Vehicle", "Game_Character"));
    k.str_field("_type", &GameVehicle::type);
    k.num_field("_mapId", &GameVehicle::map_id);
    k.num_field("_altitude", &GameVehicle::altitude);
    k.bool_field("_driving", &GameVehicle::driving);
    k.boolean("isBoat", [](GameVehicle& v, Args&) { return v.is_boat(); });
    k.boolean("isShip", [](GameVehicle& v, Args&) { return v.is_ship(); });
    k.boolean("isAirship", [](GameVehicle& v, Args&) { return v.is_airship(); });
    k.call("refresh", [](GameVehicle& v, Args&) { v.refresh(); });
    k.call("setLocation", [](GameVehicle& v, Args& a) { v.set_location(js::arg_int(a, 0), js::arg_num(a, 1), js::arg_num(a, 2)); });
    k.call("getOn", [](GameVehicle& v, Args&) { v.get_on(); });
    k.call("getOff", [](GameVehicle& v, Args&) { v.get_off(); });
    k.call("playBgm", [](GameVehicle& v, Args&) { v.play_bgm(); });
    k.call("syncWithPlayer", [](GameVehicle& v, Args&) { v.sync_with_player(); });
}

void bind_map_fog() {
    auto k = js::klass_of<GameMapFog>(klass("Game_MapFog"));
    k.val_field("id", &GameMapFog::id);
    k.str_field("name", &GameMapFog::name);
    k.num_field("opacity", &GameMapFog::opacity);
    k.num_field("blendMode", &GameMapFog::blend_mode);
    k.num_field("scaleX", &GameMapFog::scale_x);
    k.num_field("scaleY", &GameMapFog::scale_y);
    k.num_field("moveX", &GameMapFog::move_x);
    k.num_field("moveY", &GameMapFog::move_y);
    k.num_field("width", &GameMapFog::width);
    k.num_field("height", &GameMapFog::height);
    k.bool_field("mapBind", &GameMapFog::map_bind);
    k.bool_field("visible", &GameMapFog::visible);
    k.bool_field("deactivateOnInvisible", &GameMapFog::deactivate_on_invisible);
    k.bool_field("active", &GameMapFog::active);
    k.num_field("priority", &GameMapFog::priority);
    k.prop(
        "origin", [](GameMapFog& f) { return f.origin_js; },
        [](GameMapFog& f, const Val& v) {
            f.origin_js = v;
            f.sync_points();
        });
    k.prop(
        "move", [](GameMapFog& f) { return f.move_js; },
        [](GameMapFog& f, const Val& v) {
            f.move_js = v;
            f.sync_points();
        });
    k.call("show", [](GameMapFog& f, Args&) { f.show(); });
}

void bind_map() {
    auto k = js::klass_of<GameMap>(klass("Game_Map"));
    k.num_field("_mapId", &GameMap::map_id_);
    k.num_field("_tilesetId", &GameMap::tileset_id);
    k.num_field("_displayX", &GameMap::display_x_);
    k.num_field("_displayY", &GameMap::display_y_);
    k.bool_field("_nameDisplay", &GameMap::name_display);
    k.bool_field("_needsRefresh", &GameMap::needs_refresh);
    k.get("_interpreter", [](GameMap& m) { return m.interpreter ? m.interpreter->to_js() : Val::null(); });
    k.get("_events", [](GameMap& m) {
        std::vector<Val> v;
        for (auto& e : m.events_) v.push_back(e ? e->to_js() : Val::null());
        return js::make_array(std::move(v));
    });
    k.num("mapId", [](GameMap& m, Args&) { return (double)m.map_id(); });
    k.num("tilesetId", [](GameMap& m, Args&) { return (double)m.tileset_id; });
    k.num("displayX", [](GameMap& m, Args&) { return m.display_x(); });
    k.num("displayY", [](GameMap& m, Args&) { return m.display_y(); });
    k.str("parallaxName", [](GameMap& m, Args&) { return m.parallax_name(); });
    k.str("battleback1Name", [](GameMap& m, Args&) { return m.battleback1_name(); });
    k.str("battleback2Name", [](GameMap& m, Args&) { return m.battleback2_name(); });
    k.call("requestRefresh", [](GameMap& m, Args&) { m.request_refresh(); });
    k.boolean("isNameDisplayEnabled", [](GameMap& m, Args&) { return m.is_name_display_enabled(); });
    k.call("disableNameDisplay", [](GameMap& m, Args&) { m.disable_name_display(); });
    k.call("enableNameDisplay", [](GameMap& m, Args&) { m.enable_name_display(); });
    k.method("vehicle", [](GameMap& m, js::Vm&, Args& a) { return obj_js(m.vehicle(js::arg(a, 0).to_str())); });
    k.method("boat", [](GameMap& m, js::Vm&, Args&) { return obj_js(m.boat()); });
    k.method("ship", [](GameMap& m, js::Vm&, Args&) { return obj_js(m.ship()); });
    k.method("airship", [](GameMap& m, js::Vm&, Args&) { return obj_js(m.airship()); });
    k.method("events", [](GameMap& m, js::Vm&, Args&) { return arr_of(m.events()); });
    k.method("event", [](GameMap& m, js::Vm&, Args& a) {
        GameEvent* e = m.event(js::arg_int(a, 0));
        return e ? e->to_js() : Val();
    });
    k.call("eraseEvent", [](GameMap& m, Args& a) { m.erase_event(js::arg_int(a, 0)); });
    k.method("tileset", [](GameMap& m, js::Vm&, Args&) { return data_js(m.tileset()); });
    k.str("displayName", [](GameMap& m, Args&) { return m.display_name(); });
    k.num("width", [](GameMap& m, Args&) { return (double)m.width(); });
    k.num("height", [](GameMap& m, Args&) { return (double)m.height(); });
    k.num("tileWidth", [](GameMap& m, Args&) { return m.tile_width(); });
    k.num("tileHeight", [](GameMap& m, Args&) { return m.tile_height(); });
    k.boolean("isLoopHorizontal", [](GameMap& m, Args&) { return m.is_loop_horizontal(); });
    k.boolean("isLoopVertical", [](GameMap& m, Args&) { return m.is_loop_vertical(); });
    k.boolean("isDashDisabled", [](GameMap& m, Args&) { return m.is_dash_disabled(); });
    k.num("screenTileX", [](GameMap& m, Args&) { return m.screen_tile_x(); });
    k.num("screenTileY", [](GameMap& m, Args&) { return m.screen_tile_y(); });
    k.num("adjustX", [](GameMap& m, Args& a) { return m.adjust_x(js::arg_num(a, 0)); });
    k.num("adjustY", [](GameMap& m, Args& a) { return m.adjust_y(js::arg_num(a, 0)); });
    k.num("roundX", [](GameMap& m, Args& a) { return m.round_x(js::arg_num(a, 0)); });
    k.num("roundY", [](GameMap& m, Args& a) { return m.round_y(js::arg_num(a, 0)); });
    k.num("xWithDirection", [](GameMap& m, Args& a) { return m.x_with_direction(js::arg_num(a, 0), js::arg_int(a, 1)); });
    k.num("yWithDirection", [](GameMap& m, Args& a) { return m.y_with_direction(js::arg_num(a, 0), js::arg_int(a, 1)); });
    k.num("roundXWithDirection", [](GameMap& m, Args& a) { return m.round_x_with_direction(js::arg_num(a, 0), js::arg_int(a, 1)); });
    k.num("roundYWithDirection", [](GameMap& m, Args& a) { return m.round_y_with_direction(js::arg_num(a, 0), js::arg_int(a, 1)); });
    k.num("deltaX", [](GameMap& m, Args& a) { return m.delta_x(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.num("deltaY", [](GameMap& m, Args& a) { return m.delta_y(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.num("distance", [](GameMap& m, Args& a) { return m.distance(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_num(a, 2), js::arg_num(a, 3)); });
    k.call("autoplay", [](GameMap& m, Args&) { m.autoplay(); });
    k.call("refreshIfNeeded", [](GameMap& m, Args&) { m.refresh_if_needed(); });
    k.call("refresh", [](GameMap& m, Args&) { m.refresh(); });
    k.call("refreshTileEvents", [](GameMap& m, Args&) { m.refresh_tile_events(); });
    k.method("eventsXy", [](GameMap& m, js::Vm&, Args& a) { return arr_of(m.events_xy(js::arg_num(a, 0), js::arg_num(a, 1))); });
    k.method("eventsXyNt", [](GameMap& m, js::Vm&, Args& a) { return arr_of(m.events_xy_nt(js::arg_num(a, 0), js::arg_num(a, 1))); });
    k.method("tileEventsXy", [](GameMap& m, js::Vm&, Args& a) { return arr_of(m.tile_events_xy(js::arg_num(a, 0), js::arg_num(a, 1))); });
    k.num("eventIdXy", [](GameMap& m, Args& a) { return (double)m.event_id_xy(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("scrollDown", [](GameMap& m, Args& a) { m.scroll_down(js::arg_num(a, 0)); });
    k.call("scrollLeft", [](GameMap& m, Args& a) { m.scroll_left(js::arg_num(a, 0)); });
    k.call("scrollRight", [](GameMap& m, Args& a) { m.scroll_right(js::arg_num(a, 0)); });
    k.call("scrollUp", [](GameMap& m, Args& a) { m.scroll_up(js::arg_num(a, 0)); });
    k.boolean("isValid", [](GameMap& m, Args& a) { return m.is_valid(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.boolean("checkPassage", [](GameMap& m, Args& a) { return m.check_passage(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_int(a, 2)); });
    k.num("tileId", [](GameMap& m, Args& a) { return (double)m.tile_id(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_int(a, 2)); });
    k.boolean("isPassable", [](GameMap& m, Args& a) { return m.is_passable(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_int(a, 2)); });
    k.boolean("isLadder", [](GameMap& m, Args& a) { return m.is_ladder(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.boolean("isBush", [](GameMap& m, Args& a) { return m.is_bush(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.boolean("isCounter", [](GameMap& m, Args& a) { return m.is_counter(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.boolean("isDamageFloor", [](GameMap& m, Args& a) { return m.is_damage_floor(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.num("terrainTag", [](GameMap& m, Args& a) { return (double)m.terrain_tag(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.num("regionId", [](GameMap& m, Args& a) { return (double)m.region_id(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.call("startScroll", [](GameMap& m, Args& a) { m.start_scroll(js::arg_int(a, 0), js::arg_num(a, 1), js::arg_num(a, 2)); });
    k.boolean("isScrolling", [](GameMap& m, Args&) { return m.is_scrolling(); });
    k.call("changeTileset", [](GameMap& m, Args& a) { m.change_tileset(js::arg_int(a, 0)); });
    k.call("changeBattleback", [](GameMap& m, Args& a) { m.change_battleback(js::arg(a, 0).to_str(), js::arg(a, 1).to_str()); });
    k.call("changeParallax", [](GameMap& m, Args& a) {
        m.change_parallax(js::arg(a, 0).to_str(), js::arg(a, 1).truthy(), js::arg(a, 2).truthy(), js::arg_num(a, 3), js::arg_num(a, 4));
    });
    k.boolean("isEventRunning", [](GameMap& m, Args&) { return m.is_event_running(); });
    k.call("unlockEvent", [](GameMap& m, Args& a) { m.unlock_event(js::arg_int(a, 0)); });
    k.boolean("isAnyEventStarting", [](GameMap& m, Args&) { return m.is_any_event_starting(); });
    k.num("currentMapLevel", [](GameMap& m, Args&) { return (double)m.current_map_level(); });
    k.call("setCurrentMapLevel", [](GameMap& m, Args& a) { m.set_current_map_level(js::arg_int(a, 0)); });
    k.call("spawnEvent", [](GameMap& m, Args& a) {
        m.spawn_event(js::arg_int(a, 0), js::arg_int(a, 1), js::arg_num(a, 2), js::arg_num(a, 3), js::arg(a, 4).truthy());
    });
    k.call("despawnEventId", [](GameMap& m, Args& a) { m.despawn_event_id(js::arg_int(a, 0)); });
    k.method("FirstSpawnedEvent", [](GameMap& m, js::Vm&, Args&) { return obj_js(m.first_spawned_event()); });
    k.method("LastSpawnedEvent", [](GameMap& m, js::Vm&, Args&) { return obj_js(m.last_spawned_event()); });
    k.method("FirstSpawnedEventID", [](GameMap& m, js::Vm&, Args&) {
        GameEvent* e = m.first_spawned_event();
        return e ? Val::num(e->event_id()) : Val::num(0);
    });
    k.method("LastSpawnedEventID", [](GameMap& m, js::Vm&, Args&) {
        GameEvent* e = m.last_spawned_event();
        return e ? Val::num(e->event_id()) : Val::num(0);
    });
    k.call("camTargetSet", [](GameMap& m, Args& a) {
        CB* c = base_of(js::arg(a, 0));
        m.cam_target_set(c ? CharRef::of(c) : CharRef(), js::arg_num(a, 1, 800));
    });
    k.call("setupCommonEvent", [](GameMap& m, Args& a) { m.setup_common_event(js::arg_int(a, 0)); });
    k.call("stopCurrentEvent", [](GameMap& m, Args&) { m.stop_current_event(); });
    k.call("setRegionMoveDirection", [](GameMap& m, Args& a) { m.set_region_move_direction(js::arg_int(a, 0), js::arg_int(a, 1)); });
    k.call("scrollToTarget", [](GameMap& m, Args& a) { m.scroll_to_target(js::arg_num(a, 0), js::arg_num(a, 1), js::arg_num(a, 2)); });
    k.call("clearMapFogs", [](GameMap& m, Args&) { m.clear_map_fogs(); });
    k.method("getMapFog", [](GameMap& m, js::Vm&, Args& a) {
        auto f = m.get_map_fog(js::arg(a, 0).to_str());
        return f ? f->to_js() : Val();
    });
    k.call("addMapFog", [](GameMap& m, Args& a) {
        GameMapFog* fog = js::host_cast<GameMapFog>(js::arg(a, 1));
        if (!fog) return;
        std::shared_ptr<GameMapFog> sp = held_ptr(fog);
        if (sp) m.add_map_fog(js::arg(a, 0).to_str(), sp);
    });
    k.call("removeMapFog", [](GameMap& m, Args& a) { m.remove_map_fog(js::arg(a, 0).to_str()); });
    k.call("overrideDisplayNameTo", [](GameMap& m, Args& a) { m.override_display_name_to(js::arg(a, 0).to_str()); });
    k.call("clearOverrideDisplayName", [](GameMap& m, Args&) { m.clear_override_display_name(); });
    k.boolean("isSlippery", [](GameMap& m, Args& a) { return m.is_slippery(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.boolean("isTiledMap", [](GameMap& m, Args&) { return m.is_tiled_map(); });
    k.str("note", [](GameMap& m, Args&) { return m.note(); });
}

}

void bind_characters() {
    bind_character_base();
    bind_character();
    bind_followers();
    bind_player();
    bind_event();
    bind_vehicle();
    bind_map_fog();
    bind_map();
}

}

js::HostClass* GameCharacterBase::js_class() const { return &bind::klass("Game_CharacterBase").hc; }
js::HostClass* GameCharacter::js_class() const { return &bind::klass("Game_Character").hc; }
js::HostClass* GameFollower::js_class() const { return &bind::klass("Game_Follower").hc; }
js::HostClass* GameFollowers::js_class() const { return &bind::klass("Game_Followers").hc; }
js::HostClass* GamePlayer::js_class() const { return &bind::klass("Game_Player").hc; }
js::HostClass* GameEvent::js_class() const { return &bind::klass("Game_Event").hc; }
js::HostClass* GameVehicle::js_class() const { return &bind::klass("Game_Vehicle").hc; }
js::HostClass* GameMapFog::js_class() const { return &bind::klass("Game_MapFog").hc; }
js::HostClass* GameMap::js_class() const { return &bind::klass("Game_Map").hc; }

}
