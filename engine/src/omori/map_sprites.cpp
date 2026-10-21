#include "map_sprites.h"

#include <algorithm>
#include <cmath>

#include "../core/regex.h"
#include "../rpg/character.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/map.h"
#include "../rpg/params.h"
#include "../rpg/state.h"
#include "../rpg/system.h"
#include "ui_util.h"

namespace ui {

namespace {

double param_num(const char* plugin, const char* key, double def) {
    std::string s = data::plugin_param(plugin, key, "");
    if (s.empty()) return def;
    double v = rpg::js_number(s);
    return v == v ? v : def;
}

}

g2d::Container* SpriteCharacter::label_layer = nullptr;

SpriteBalloon::SpriteBalloon() {
    anchor_x = 0.5;
    anchor_y = 1;
    z = 7;
    set_bitmap(load_system(rpg::game_variables().value(25).to_str()));
    set_frame(0, 0, 0, 0);
}

void SpriteBalloon::setup(int balloon_id) {
    balloon_id_ = balloon_id;
    duration_ = 8 * speed() + wait_time();
}

void SpriteBalloon::update() {
    SpriteBase::update();
    if (duration_ > 0) {
        duration_--;
        if (duration_ > 0) update_frame();
    }
}

int SpriteBalloon::frame_index() const {
    double index = (double)(duration_ - wait_time()) / speed();
    return 7 - std::max((int)std::floor(index), 0);
}

void SpriteBalloon::update_frame() {
    double w = 32, h = 24;
    set_frame(frame_index() * w, (balloon_id_ - 1) * h, w, h);
}

SpriteEventCon::SpriteEventCon() {
    eventcon_img_ = data::plugin_param("Liquid_EventIcons", "Default Sprite Sheet", "quest");
    if (eventcon_img_.empty()) eventcon_img_ = "quest";
    width_ = param_num("Liquid_EventIcons", "Icon Width", 32);
    if (!width_) width_ = 32;
    height_ = param_num("Liquid_EventIcons", "Icon Height", 24);
    if (!height_) height_ = 24;
    speed_ = (int)param_num("Liquid_EventIcons", "Speed", 16);
    if (!speed_) speed_ = 16;
    wait_time_ = (int)param_num("Liquid_EventIcons", "Wait Time", 64);
    if (!wait_time_) wait_time_ = 64;
    anchor_x = 0.5;
    anchor_y = 1;
    z = 7;
}

void SpriteEventCon::setup(const std::string& img, int index) {
    eventcon_img_ = img;
    eventcon_id_ = index;
    duration_ = 4 * speed_ + wait_time_;
    set_bitmap(load_system(eventcon_img_));
    set_frame(0, 0, 0, 0);
}

void SpriteEventCon::update() {
    SpriteBase::update();
    if (duration_ > 0) {
        duration_--;
        if (duration_ > 0) update_frame();
        else duration_ = 4 * speed_ + wait_time_;
    }
}

int SpriteEventCon::frame_index() const {
    double index = (double)(duration_ - wait_time_) / speed_;
    return 3 - std::max((int)std::floor(index), 0);
}

void SpriteEventCon::update_frame() { set_frame(frame_index() * width_, (eventcon_id_ - 1) * height_, width_, height_); }

SpriteInputButton::SpriteInputButton() {}

void SpriteInputButton::deactivate() {
    active_ = false;
    reset_animation();
    refresh_frame();
}

void SpriteInputButton::reset_animation() {
    icon_frame_ = 0;
    icon_frame_delay_ = icon_frame_delay_max_;
}

void SpriteInputButton::refresh_frame() {
    const double* f = icon_frame_ == 0 ? up_ : down_;
    set_frame(f[0], f[1], f[2], f[3]);
}

void SpriteInputButton::apply_key(const std::string& key) {
    set_bitmap(input_icon_bitmap());
    IconRect up, down;
    key_icon_rects(key, up, down);
    up_[0] = up.x;
    up_[1] = up.y;
    up_[2] = up.w;
    up_[3] = up.h;
    down_[0] = down.x;
    down_[1] = down.y;
    down_[2] = down.w;
    down_[3] = down.h;
    reset_animation();
    refresh_frame();
}

void SpriteInputButton::setup_key(const std::string& key) { apply_key(key); }

void SpriteInputButton::setup_input(const std::string& input) { apply_key(input_key_code(input)); }

void SpriteInputButton::update() {
    g2d::Sprite::update();
    if (active_) update_animation();
}

void SpriteInputButton::update_animation() {
    if (icon_frame_delay_ <= 0) {
        icon_frame_ = (icon_frame_ + 1) % 2;
        refresh_frame();
        icon_frame_delay_ = icon_frame_delay_max_;
    } else {
        icon_frame_delay_--;
    }
}

void WindowEventMiniLabel::initialize_label() {
    buffer_x_ = param_num("YEP_EventMiniLabel", "X Buffer", 0);
    buffer_y_ = param_num("YEP_EventMiniLabel", "Y Buffer", 36);
    font_size_ = (int)param_num("YEP_EventMiniLabel", "Font Size", 20);
    always_show_ = false;
    range_ = 500;
    req_facing_ = false;
    double width = param_num("YEP_EventMiniLabel", "Minimum Width", 136);
    WindowBase::initialize(0, 0, width, window_height_value());
    set_window_opacity(0);
    set_contents_opacity(0);
    character_ = nullptr;
    text_.clear();
}

double WindowEventMiniLabel::window_height_value() {
    double h = fitting_height(1);
    return std::max(h, 36.0 + standard_padding() * 2);
}

void WindowEventMiniLabel::set_character(rpg::GameCharacterBase* character) {
    set_text("");
    character_ = character;
    if (character->event_id()) gather_display_data();
}

void WindowEventMiniLabel::gather_display_data() {
    rpg::GameEvent* ev = dynamic_cast<rpg::GameEvent*>(character_);
    if (!ev) return;
    page_index_ = ev->page_index;
    range_ = 500;
    buffer_y_ = param_num("YEP_EventMiniLabel", "Y Buffer", 36);
    font_size_ = (int)param_num("YEP_EventMiniLabel", "Font Size", 20);
    always_show_ = false;
    req_facing_ = false;
    if (!ev->page()) {
        visible = false;
        return;
    }
    const Value* list = ev->list();
    std::string comment;
    if (list) {
        for (size_t i = 0; i < list->size(); i++) {
            const Value& c = (*list)[i];
            int code = c["code"].as_int();
            if (code == 108 || code == 408) comment += c["parameters"][0].string() + "\n";
        }
    }
    extract_notedata(comment);
}

void WindowEventMiniLabel::extract_notedata(const std::string& comment) {
    if (comment.empty()) return;
    std::string text;
    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i <= comment.size(); i++) {
        if (i == comment.size() || comment[i] == '\r' || comment[i] == '\n') {
            if (i > start || i < comment.size()) lines.push_back(comment.substr(start, i - start));
            while (i < comment.size() && (comment[i] == '\r' || comment[i] == '\n')) i++;
            start = i;
            if (i < comment.size()) i--;
        }
    }
    for (const std::string& line : lines) {
        re::Groups g;
        if (re::search("<(?:MINI WINDOW|MINI LABEL):[ ](.*)>", "i", line, &g)) {
            text = g[1];
        } else if (re::search("<(?:MINI WINDOW FONT SIZE|MINI LABEL FONT SIZE):[ ](\\d+)>", "i", line, &g)) {
            font_size_ = (int)rpg::js_parse_int(g[1]);
        } else if (re::search("<(?:MINI WINDOW Y BUFFER|MINI LABEL Y BUFFER):[ ]([\\+\\-]\\d+)>", "i", line, &g)) {
            buffer_y_ = rpg::js_parse_int(g[1]);
        } else if (re::search("<(?:ALWAYS SHOW MINI WINDOW|ALWAYS SHOW MINI LABEL)>", "i", line, &g)) {
            always_show_ = true;
        } else if (re::search("<(?:MINI WINDOW RANGE|MINI LABEL RANGE):[ ](\\d+)>", "i", line, &g)) {
            range_ = rpg::js_parse_int(g[1]);
        } else if (re::search("<(?:MINI WINDOW X BUFFER|MINI LABEL X BUFFER):[ ]([\\+\\-]\\d+)>", "i", line, &g)) {
            buffer_x_ = rpg::js_parse_int(g[1]);
        } else if (re::search("<(?:MINI WINDOW REQUIRE FACING|MINI LABEL REQUIRE FACING)>", "i", line, &g)) {
            req_facing_ = true;
        }
    }
    set_text(text);
    if (text_.empty() || !rpg::game_system().show_event_mini_label) {
        visible = false;
        set_contents_opacity(0);
    } else {
        visible = true;
        set_contents_opacity(req_facing_ ? 0 : 255);
    }
}

void WindowEventMiniLabel::set_text(const std::string& text) {
    if (text_ == text) return;
    text_ = text;
    refresh();
}

void WindowEventMiniLabel::refresh() {
    c().clear();
    double tx_width = draw_text_ex(text_, 0, c().height());
    tx_width += text_padding() * 2;
    double w = std::max(tx_width, param_num("YEP_EventMiniLabel", "Minimum Width", 136));
    w += standard_padding() * 2;
    set_width(w);
    set_height(window_height_value());
    create_contents();
    double wx = (c().width() - tx_width) / 2;
    draw_text_ex(text_, wx + text_padding(), 0);
}

void WindowEventMiniLabel::force_refresh() {
    refresh();
    update_opacity();
}

void WindowEventMiniLabel::update() {
    WindowBase::update();
    if (!character_) return;
    if (!character_->event_id()) return;
    update_page();
    if (text_.empty()) return;
    update_opacity();
}

void WindowEventMiniLabel::update_page() {
    rpg::GameEvent* ev = dynamic_cast<rpg::GameEvent*>(character_);
    if (!ev || page_index_ == ev->page_index) return;
    page_index_ = ev->page_index;
    c().clear();
    text_.clear();
    gather_display_data();
}

void WindowEventMiniLabel::update_opacity() {
    if (show_mini_label()) show_label();
    else hide_label();
}

void WindowEventMiniLabel::show_label() {
    if (contents_opacity() >= 255) return;
    set_contents_opacity(contents_opacity() + 16);
    visible = true;
}

void WindowEventMiniLabel::hide_label() {
    if (contents_opacity() <= 0) {
        if (visible) visible = false;
        return;
    }
    set_contents_opacity(contents_opacity() - 16);
}

bool WindowEventMiniLabel::show_mini_label() {
    if (always_show_) return true;
    if (!within_range()) return false;
    if (!meets_facing_requirements()) return false;
    if (data::plugin_param("YEP_EventMiniLabel", "Battle Transition", "false") != "true") {
        if (map_encounter_effect_duration() > 0) {
            set_contents_opacity(0);
            return false;
        }
    }
    return rpg::game_system().show_event_mini_label;
}

bool WindowEventMiniLabel::within_range() {
    if (range_ >= 500) return true;
    rpg::GamePlayer& p = rpg::game_player();
    if (range_ >= std::fabs(p.x() - character_->x())) {
        if (range_ >= std::fabs(p.y() - character_->y())) return true;
    }
    return false;
}

bool WindowEventMiniLabel::meets_facing_requirements() {
    if (!character_) return true;
    if (!req_facing_) return true;
    rpg::GamePlayer& p = rpg::game_player();
    double px = p.x(), py = p.y(), ex = character_->x(), ey = character_->y();
    switch (p.direction()) {
    case 1: return px >= ex && py <= ey;
    case 2: return py <= ey;
    case 3: return px <= ex && py <= ey;
    case 4: return px >= ex;
    case 6: return px <= ex;
    case 7: return px >= ex && py >= ey;
    case 8: return py >= ey;
    case 9: return px <= ex && py >= ey;
    default: return true;
    }
}

void ShadowSilhouette::render(const g2d::RenderState& rs) {
    if (renderable && bitmap_ && real_frame_.width > 0 && real_frame_.height > 0) {
        g2d::Mat m = rs.world;
        g2d::Mat off;
        off.tx = -anchor_x * real_frame_.width;
        off.ty = -anchor_y * real_frame_.height;
        m = m.mul(off);
        bitmap_->touch();
        g2d::draw_flat(bitmap_->image(), real_frame_.x, real_frame_.y, real_frame_.width, real_frame_.height, m, 0, rs.alpha, blend_mode, bitmap_->smooth,
                       rs);
    }
    render_children(rs);
}

SpriteCharacter::SpriteCharacter(rpg::GameCharacterBase* character) : character_(character) {
    anchor_x = 0.5;
    anchor_y = 1;
}

SpriteCharacter::~SpriteCharacter() {}

void SpriteCharacter::update() {
    horror.sync(*this, &character_->horror_filters, true);
    SpriteBase::update();
    horror.update(*this);
    update_bitmap();
    update_frame();
    update_position();
    update_char_animation();
    update_balloon();
    update_other();
    update_mini_label();
    update_mini_label_zoom();
    update_eventcon();
    update_bobble();
    update_input_icon();
    update_shadow();
}

void SpriteCharacter::update_visibility() {
    SpriteBase::update_visibility();
    if (character_->is_transparent()) visible = false;
}

bool SpriteCharacter::is_image_changed() {
    return !has_image_ || tileset_id_ != rpg::game_map().tileset_id || tile_id_ != character_->tile_id() ||
           character_name_ != character_->character_name() || character_index_ != character_->character_index();
}

void SpriteCharacter::update_bitmap() {
    if (is_image_changed()) {
        has_image_ = true;
        tileset_id_ = rpg::game_map().tileset_id;
        tile_id_ = character_->tile_id();
        character_name_ = character_->character_name();
        character_index_ = character_->character_index();
        if (tile_id_ > 0) set_bitmap(g2d::Bitmap::empty());
        else set_character_bitmap();
    }
}

void SpriteCharacter::set_character_bitmap() {
    set_bitmap(load_character(character_name_));
    is_big_character_ = is_big_character(character_name_);
}

void SpriteCharacter::update_frame() {
    if (tile_id_ > 0) {
        double pw = pattern_width(), ph = pattern_height();
        double sx = ((tile_id_ / 128) % 2 * 8 + tile_id_ % 8) * pw;
        double sy = ((tile_id_ % 256) / 8 % 16) * ph;
        set_frame(sx, sy, pw, ph);
    } else {
        update_character_frame();
    }
}

void SpriteCharacter::update_character_frame() {
    if (character_->is_single_frame()) {
        set_frame(0, 0, bitmap() ? bitmap()->width() : 0, bitmap() ? bitmap()->height() : 0);
        return;
    }
    double pw = pattern_width(), ph = pattern_height();
    double sx = (character_block_x() + character_pattern_x()) * pw;
    double sy = (character_block_y() + character_pattern_y()) * ph;
    update_half_body_sprites();
    if (bush_depth_ > 0) {
        double d = bush_depth_;
        upper_body_->set_frame(sx, sy, pw, ph - d);
        lower_body_->set_frame(sx, sy + ph - d, pw, d);
        set_frame(sx, sy, 0, ph);
    } else {
        set_frame(sx, sy, pw, ph);
    }
}

double SpriteCharacter::character_block_x() {
    if (character_->has_custom_frame_x) return character_->custom_frame_x;
    if (is_big_character_) return 0;
    return character_->character_index() % 4 * 3;
}

double SpriteCharacter::character_block_y() {
    if (character_->has_custom_frame_y) return character_->custom_frame_y;
    if (is_big_character_) return 0;
    return std::floor(character_->character_index() / 4.0) * 4;
}

double SpriteCharacter::character_pattern_x() {
    if (character_->has_custom_frame_x) return 0;
    return character_->pattern();
}

double SpriteCharacter::character_pattern_y() {
    if (character_->has_custom_frame_y) return 0;
    return (character_->direction() - 2) / 2.0;
}

double SpriteCharacter::pattern_width() {
    if (character_->has_custom_frame_w) return character_->custom_frame_w;
    double pw;
    if (tile_id_ > 0) pw = rpg::game_map().tile_width();
    else if (is_big_character_) pw = (bitmap_ ? bitmap_->width() : 0) / 3.0;
    else pw = (bitmap_ ? bitmap_->width() : 0) / 12.0;
    if (character_->is_emf) return (pw * 3) / character_->emf.frame_num;
    return pw;
}

double SpriteCharacter::pattern_height() {
    if (character_->has_custom_frame_h) return character_->custom_frame_h;
    if (tile_id_ > 0) return rpg::game_map().tile_height();
    if (is_big_character_) return (bitmap_ ? bitmap_->height() : 0) / 4.0;
    return (bitmap_ ? bitmap_->height() : 0) / 8.0;
}

void SpriteCharacter::update_half_body_sprites() {
    if (bush_depth_ > 0) {
        create_half_body_sprites();
        upper_body_->set_bitmap(bitmap());
        upper_body_->visible = true;
        upper_body_->y = -bush_depth_;
        lower_body_->set_bitmap(bitmap());
        lower_body_->visible = true;
        upper_body_->set_blend_color(blend_color());
        lower_body_->set_blend_color(blend_color());
        upper_body_->set_color_tone(color_tone());
        lower_body_->set_color_tone(color_tone());
        upper_body_->blend_mode = blend_mode;
        lower_body_->blend_mode = blend_mode;
    } else if (upper_body_) {
        upper_body_->visible = false;
        lower_body_->visible = false;
    }
}

void SpriteCharacter::create_half_body_sprites() {
    if (!upper_body_) {
        upper_body_ = std::make_shared<g2d::Sprite>();
        upper_body_->anchor_x = 0.5;
        upper_body_->anchor_y = 1;
        add_child(upper_body_);
    }
    if (!lower_body_) {
        lower_body_ = std::make_shared<g2d::Sprite>();
        lower_body_->anchor_x = 0.5;
        lower_body_->anchor_y = 1;
        lower_body_->set_opacity(128);
        add_child(lower_body_);
    }
}

void SpriteCharacter::update_position() {
    x = character_->screen_x();
    y = character_->screen_y();
    z = character_->screen_z();
}

void SpriteCharacter::update_char_animation() {
    setup_animation();
    if (!is_animation_playing()) character_->end_animation();
    if (!is_balloon_playing()) character_->end_balloon();
    if (!is_eventcon_playing()) character_->end_eventcon();
}

void SpriteCharacter::update_other() {
    set_opacity(character_->opacity());
    blend_mode = character_->blend_mode();
    bush_depth_ = character_->bush_depth();
}

void SpriteCharacter::setup_animation() {
    if (character_->animation_id() > 0) {
        const Value& anim = data::animations()[character_->animation_id()];
        start_animation(&anim, false, 0);
        character_->start_animation();
    }
}

void SpriteCharacter::setup_balloon() {
    if (character_->balloon_id() > 0) {
        start_balloon();
        character_->start_balloon();
    }
}

void SpriteCharacter::start_balloon() {
    if (!balloon_sprite_) balloon_sprite_ = std::make_shared<SpriteBalloon>();
    balloon_sprite_->setup(character_->balloon_id());
    if (parent) parent->add_child(balloon_sprite_);
}

void SpriteCharacter::update_balloon() {
    setup_balloon();
    if (balloon_sprite_) {
        balloon_sprite_->x = x;
        balloon_sprite_->y = y - height();
        if (!balloon_sprite_->is_playing()) end_balloon();
    }
}

void SpriteCharacter::end_balloon() {
    if (balloon_sprite_) {
        if (parent) parent->remove_child(balloon_sprite_.get());
        balloon_sprite_.reset();
    }
}

void SpriteCharacter::update_mini_label() {
    setup_mini_label();
    if (!mini_label_) return;
    position_mini_label();
}

void SpriteCharacter::setup_mini_label() {
    if (parent && !has_label_delay_) {
        has_label_delay_ = true;
        label_delay_ = (int)parent->index_of(this);
    }
    if (label_delay_ > 0) {
        label_delay_--;
        return;
    }
    if (mini_label_) {
        if (!mini_label_->text().empty()) {
            if (!mini_label_->parent && label_layer) label_layer->add_child(mini_label_);
        } else if (mini_label_->parent) {
            mini_label_->parent->remove_child(mini_label_.get());
        }
        return;
    }
    if (!label_layer || mini_label_absent_) return;
    if (!may_have_mini_label(character_)) {
        mini_label_absent_ = true;
        return;
    }
    mini_label_ = std::shared_ptr<WindowEventMiniLabel>(new WindowEventMiniLabel());
    mini_label_->initialize_label();
    mini_label_->set_character(character_);
    if (mini_label_->text().empty()) return;
    label_layer->add_child(mini_label_);
}

bool SpriteCharacter::may_have_mini_label(rpg::GameCharacterBase* character) {
    if (!character->event_id()) return false;
    rpg::GameEvent* ev = dynamic_cast<rpg::GameEvent*>(character);
    if (!ev || !ev->page()) return false;
    const Value* list = ev->list();
    if (!list) return false;
    for (size_t i = 0; i < list->size(); i++) {
        const Value& c = (*list)[i];
        int code = c["code"].as_int();
        if (code != 108 && code != 408) continue;
        const Value& text = c["parameters"][0];
        const char* s = text.str();
        size_t len = text.size();
        for (size_t k = 0; k + 4 <= len; k++) {
            if ((s[k] | 0x20) == 'm' && (s[k + 1] | 0x20) == 'i' && (s[k + 2] | 0x20) == 'n' && (s[k + 3] | 0x20) == 'i') return true;
        }
    }
    return false;
}

void SpriteCharacter::position_mini_label() {
    if (!mini_label_->parent) return;
    WindowEventMiniLabel* win = mini_label_.get();
    double width = win->width() * win->scale_x;
    win->x = x + width / -2 + win->buffer_x();
    double h = win->height() * win->scale_y;
    double buffer = win->buffer_y() * win->scale_y;
    win->y = y + (height() * -1) - h + buffer;
}

void SpriteCharacter::update_mini_label_zoom() {
    if (!mini_label_ || !mini_label_->parent || !label_layer) return;
    mini_label_->scale_x = 1 / label_layer->scale_x;
    mini_label_->scale_y = 1 / label_layer->scale_y;
}

void SpriteCharacter::refresh_mini_label() {
    if (mini_label_) mini_label_->force_refresh();
}

void SpriteCharacter::setup_eventcon() {
    if (character_->eventcon_index > 0) {
        start_eventcon();
        character_->start_eventcon();
        if (!current_eventcon_id_) current_eventcon_id_ = character_->eventcon_index;
        if (current_eventcon_img_.empty()) current_eventcon_img_ = character_->eventcon_img;
    }
}

void SpriteCharacter::start_eventcon() {
    if (!eventcon_sprite_ || current_eventcon_id_ != character_->eventcon_index || current_eventcon_img_ != character_->eventcon_img) {
        end_eventcon();
        eventcon_sprite_ = std::make_shared<SpriteEventCon>();
        eventcon_sprite_->setup(character_->eventcon_img, character_->eventcon_index);
        if (parent) parent->add_child(eventcon_sprite_);
        current_eventcon_id_ = character_->eventcon_index;
        current_eventcon_img_ = character_->eventcon_img;
    }
}

void SpriteCharacter::update_eventcon() {
    setup_eventcon();
    if (eventcon_sprite_) {
        eventcon_sprite_->x = x;
        eventcon_sprite_->y = y - height();
        if (!character_->is_eventcon_playing()) end_eventcon();
    }
}

void SpriteCharacter::end_eventcon() {
    if (eventcon_sprite_) {
        if (parent) parent->remove_child(eventcon_sprite_.get());
        eventcon_sprite_.reset();
    }
}

void SpriteCharacter::update_bobble() {
    if (character_->has_bobble) {
        const rpg::Bobble& b = character_->bobble;
        anchor_y = b.base + std::sin((double)rpg::frame_count() * b.speed) * b.amplitude;
    } else {
        anchor_y = 1;
    }
}

void SpriteCharacter::update_input_icon() {
    rpg::InputIcon& input = character_->input_icon;
    bool has = character_->has_input_icon;
    if (input_icon_sprite_) {
        if (has) {
            if (input_icon_sprite_->scale_x < 1) {
                double s = input_icon_sprite_->scale_x + 0.1;
                input_icon_sprite_->scale_x = s;
                input_icon_sprite_->scale_y = s;
                input.has_scale = true;
                input.scale = s;
            }
            input_icon_sprite_->x = x;
            input_icon_sprite_->y = y - height() - input_icon_sprite_->height() * input_icon_sprite_->anchor_y;
        } else {
            if (input_icon_sprite_->scale_x > 0.0) {
                double s = std::max(input_icon_sprite_->scale_x - 0.1, 0.0);
                input_icon_sprite_->scale_x = s;
                input_icon_sprite_->scale_y = s;
                input_icon_sprite_->x = x;
                input_icon_sprite_->y = y - height() - input_icon_sprite_->height() * input_icon_sprite_->anchor_y;
            } else {
                if (parent) parent->remove_child(input_icon_sprite_.get());
                input_icon_sprite_.reset();
            }
        }
    } else if (has) {
        double s = input.has_scale ? input.scale : 0.0;
        input_icon_sprite_ = std::make_shared<SpriteInputButton>();
        input_icon_sprite_->scale_x = s;
        input_icon_sprite_->scale_y = s;
        input_icon_sprite_->anchor_x = 0.5;
        input_icon_sprite_->anchor_y = 0.5;
        input_icon_sprite_->activate();
        input_icon_sprite_->z = 7;
        if (parent) parent->add_child(input_icon_sprite_);
        input_icon_sprite_->x = x;
        input_icon_sprite_->y = y - height() - input_icon_sprite_->height() * input_icon_sprite_->anchor_y;
        if (input.key.truthy()) input_icon_sprite_->setup_key(input.input);
        else input_icon_sprite_->setup_input(input.input);
    }
}

void SpriteCharacter::update_shadow() {
    if (character_->has_shadow) {
        const rpg::ShadowData& sh = character_->shadow;
        if (!shadow_sprite_) {
            shadow_sprite_ = std::make_shared<ShadowSilhouette>(bitmap());
            shadow_sprite_->anchor_x = 0.5;
            shadow_sprite_->anchor_y = 1;
            shadow_sprite_->scale_x = -1;
            add_child(shadow_sprite_);
        }
        const g2d::Rect& f = frame();
        shadow_sprite_->set_frame(f.x, f.y, f.width, f.height);
        shadow_sprite_->set_opacity(sh.opacity);
        shadow_sprite_->rotation = sh.angle * M_PI / 180.0;
        shadow_sprite_->scale_y = sh.scale_y;
        shadow_sprite_->x = sh.x;
        shadow_sprite_->y = sh.y;
    } else if (shadow_sprite_) {
        remove_child(shadow_sprite_.get());
        shadow_sprite_.reset();
    }
}

void SpriteCharacter::detach_attachments() {
    end_balloon();
    end_eventcon();
    if (input_icon_sprite_ && input_icon_sprite_->parent) input_icon_sprite_->parent->remove_child(input_icon_sprite_.get());
    if (mini_label_ && mini_label_->parent) mini_label_->parent->remove_child(mini_label_.get());
}

SpriteCharacterReflect::SpriteCharacterReflect(rpg::GameCharacterBase* character) : SpriteCharacter(character) { visible = false; }

void SpriteCharacterReflect::update() {
    SpriteCharacter::update();
    update_reflect();
}

void SpriteCharacterReflect::update_reflect() {
    visible = !character_->reflections.empty();
    if (character_->reflections.empty()) return;
    const auto& refl = rpg::game_map().reflections;
    int ri = character_->reflections[0];
    if (ri < 0 || (size_t)ri >= refl.size()) return;
    const rpg::Reflection& r = refl[(size_t)ri];
    scale_y = r.cast > 0 ? -1 : 1;
    set_opacity(r.opacity ? r.opacity : 255);
}

void SpriteCharacterReflect::update_position() {
    x = character_->screen_x();
    y = character_->screen_y();
    z = character_->screen_z();
    if (character_->reflections.empty()) return;
    const auto& refl = rpg::game_map().reflections;
    int ri = character_->reflections[0];
    if (ri < 0 || (size_t)ri >= refl.size()) return;
    const rpg::Reflection& r = refl[(size_t)ri];
    y += rpg::game_map().tile_height() * r.cast;
    if (r.cast > 0) y -= rpg::game_map().tile_height();
    y += r.offset;
}

}
