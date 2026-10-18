#include "window_selectable.h"

#include <algorithm>
#include <cmath>

#include "../rpg/action.h"
#include "../rpg/battle.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "ui_util.h"

namespace ui {

SpriteWindowCustomCursor::SpriteWindowCustomCursor(int idx, const std::string& name) {
    has_index = idx >= 0;
    index = idx;
    anchor_x = 0.5;
    anchor_y = 0.5;
    setup_bitmap(name);
    init_cursor_animation();
    update_cursor_animation();
}

void SpriteWindowCustomCursor::init_cursor_animation(double area, double speed, int duration) {
    active_ = true;
    sine_index_ = 0;
    speed_ = speed;
    sine_list_.clear();
    for (int i = 0; i < duration; i++) sine_list_.push_back(std::sin(i * (M_PI / (duration / 2.0))) * area);
}

void SpriteWindowCustomCursor::setup_bitmap(const std::string& name) { set_bitmap(load_system(name.empty() ? "cursor_menu" : name)); }

void SpriteWindowCustomCursor::update() {
    g2d::Sprite::update();
    if (active_) update_cursor_animation();
}

void SpriteWindowCustomCursor::update_cursor_animation() {
    if (sine_list_.empty()) return;
    size_t i = (size_t)std::floor(sine_index_);
    anchor_x = sine_list_[i % sine_list_.size()];
    sine_index_ = std::fmod(sine_index_ + speed_, (double)sine_list_.size());
}

void WindowSelectable::initialize(double x, double y, double w, double h) {
    WindowBase::initialize(x, y, w, h);
    index_ = -1;
    cursor_fixed_ = false;
    cursor_all_ = false;
    stay_count = 0;
    help_window = nullptr;
    handlers_.clear();
    scroll_x = 0;
    scroll_y = 0;
    deactivate();
    if (is_using_custom_cursor_rect_sprite()) init_custom_cursor_rect();
}

double WindowSelectable::item_width() { return std::floor((width() - padding() * 2 + spacing()) / max_cols() - spacing()); }

int WindowSelectable::max_rows() { return std::max((int)std::ceil((double)max_items() / max_cols()), 1); }

void WindowSelectable::activate() {
    WindowBase::activate();
    reselect();
}

void WindowSelectable::deactivate() {
    WindowBase::deactivate();
    reselect();
}

void WindowSelectable::select(int index) {
    index_ = index;
    stay_count = 0;
    ensure_cursor_visible();
    update_cursor();
    call_update_help();
}

int WindowSelectable::row() { return (int)std::floor((double)index() / max_cols()); }

int WindowSelectable::top_row() { return (int)std::floor(scroll_y / item_height()); }

int WindowSelectable::max_top_row() { return std::max(0, max_rows() - max_page_rows()); }

void WindowSelectable::set_top_row(int r) {
    double sy = std::min(std::max(r, 0), max_top_row()) * item_height();
    if (scroll_y != sy) {
        scroll_y = sy;
        refresh();
        update_cursor();
    }
}

int WindowSelectable::max_page_rows() {
    double page_h = height() - padding() * 2;
    return (int)std::floor(page_h / item_height());
}

int WindowSelectable::max_page_items() { return max_page_rows() * max_cols(); }

int WindowSelectable::bottom_row() { return std::max(0, top_row() + max_page_rows() - 1); }

ItemRect WindowSelectable::item_rect(int index) {
    ItemRect r;
    int mc = max_cols();
    r.width = item_width();
    r.height = item_height();
    r.x = (index % mc) * (r.width + spacing()) - scroll_x;
    r.y = std::floor((double)index / mc) * r.height - scroll_y;
    return r;
}

ItemRect WindowSelectable::item_rect_for_text(int index) {
    ItemRect r = item_rect(index);
    r.x += text_padding();
    r.width -= text_padding() * 2;
    r.x += custom_cursor_rect_text_x_offset();
    r.y += custom_cursor_rect_text_y_offset();
    r.width += custom_cursor_rect_text_width_offset();
    return r;
}

void WindowSelectable::set_help_window(WindowHelp* w) {
    help_window = w;
    call_update_help();
}

void WindowSelectable::show_help_window() {
    if (help_window) help_window->show();
}

void WindowSelectable::hide_help_window() {
    if (help_window) help_window->hide();
}

bool WindowSelectable::is_handled(const std::string& symbol) const {
    auto it = handlers_.find(symbol);
    return it != handlers_.end() && (bool)it->second;
}

void WindowSelectable::call_handler(const std::string& symbol) {
    if (!is_handled(symbol)) return;
    auto fn = handlers_[symbol];
    fn();
}

bool WindowSelectable::is_cursor_movable() {
    if (input_lock) return false;
    return is_open_and_active() && !cursor_fixed_ && !cursor_all_ && max_items() > 0;
}

void WindowSelectable::cursor_down(bool wrap) {
    int i = index(), mi = max_items(), mc = max_cols();
    if (i < mi - mc || (wrap && mc == 1)) select((i + mc) % mi);
}

void WindowSelectable::cursor_up(bool wrap) {
    int i = index(), mi = max_items(), mc = max_cols();
    if (i >= mc || (wrap && mc == 1)) select((i - mc + mi) % mi);
}

void WindowSelectable::cursor_right(bool wrap) {
    int i = index(), mi = max_items(), mc = max_cols();
    if (mc >= 2 && (i < mi - 1 || (wrap && is_horizontal()))) select((i + 1) % mi);
}

void WindowSelectable::cursor_left(bool wrap) {
    int i = index(), mi = max_items(), mc = max_cols();
    if (mc >= 2 && (i > 0 || (wrap && is_horizontal()))) select((i - 1 + mi) % mi);
}

void WindowSelectable::cursor_pagedown() {
    int i = index(), mi = max_items();
    if (top_row() + max_page_rows() < max_rows()) {
        set_top_row(top_row() + max_page_rows());
        select(std::min(i + max_page_items(), mi - 1));
    }
}

void WindowSelectable::cursor_pageup() {
    int i = index();
    if (top_row() > 0) {
        set_top_row(top_row() - max_page_rows());
        select(std::max(i - max_page_items(), 0));
    }
}

void WindowSelectable::scroll_down() {
    if (top_row() + 1 < max_rows()) set_top_row(top_row() + 1);
}

void WindowSelectable::scroll_up() {
    if (top_row() > 0) set_top_row(top_row() - 1);
}

void WindowSelectable::update() {
    WindowBase::update();
    update_arrows();
    process_cursor_move();
    process_handling();
    stay_count++;
    update_custom_cursor_rect_sprite_visibility();
}

void WindowSelectable::update_arrows() {
    int tr = top_row(), mtr = max_top_row();
    down_arrow_visible = mtr > 0 && tr < mtr;
    up_arrow_visible = tr > 0;
}

void WindowSelectable::process_cursor_move() {
    if (!is_cursor_movable()) return;
    int last = index();
    if (input::is_repeated(input::DOWN)) cursor_down(input::is_triggered(input::DOWN));
    if (input::is_repeated(input::UP)) cursor_up(input::is_triggered(input::UP));
    if (input::is_repeated(input::RIGHT)) cursor_right(input::is_triggered(input::RIGHT));
    if (input::is_repeated(input::LEFT)) cursor_left(input::is_triggered(input::LEFT));
    if (!is_handled("pagedown") && input::is_triggered(input::PAGEDOWN)) cursor_pagedown();
    if (!is_handled("pageup") && input::is_triggered(input::PAGEUP)) cursor_pageup();
    if (index() != last) play_cursor();
}

void WindowSelectable::process_handling() {
    if (!is_open_and_active()) return;
    if (is_ok_enabled() && is_ok_triggered()) process_ok();
    else if (is_cancel_enabled() && is_cancel_triggered()) process_cancel();
    else if (is_handled("pagedown") && input::is_triggered(input::PAGEDOWN)) process_pagedown();
    else if (is_handled("pageup") && input::is_triggered(input::PAGEUP)) process_pageup();
}

bool WindowSelectable::is_ok_triggered() {
    if (rpg::game_objects_exist()) {
        js::Val* v = rpg::game_temp().expando.find(js::atom("_skippingCutscene"));
        if (v && v->truthy()) return true;
    }
    return input::is_repeated(input::OK);
}

bool WindowSelectable::is_cancel_triggered() { return input::is_repeated(input::CANCEL); }

void WindowSelectable::process_ok() {
    if (is_current_item_enabled()) {
        play_ok_sound();
        update_input_data();
        deactivate();
        call_ok_handler();
    } else {
        play_buzzer_sound();
    }
}

void WindowSelectable::play_ok_sound() { play_ok(); }
void WindowSelectable::play_buzzer_sound() { play_buzzer(); }

void WindowSelectable::process_cancel() {
    play_cancel();
    update_input_data();
    deactivate();
    call_cancel_handler();
}

void WindowSelectable::process_pageup() {
    play_cursor();
    update_input_data();
    deactivate();
    call_handler("pageup");
}

void WindowSelectable::process_pagedown() {
    play_cursor();
    update_input_data();
    deactivate();
    call_handler("pagedown");
}

void WindowSelectable::update_input_data() { input::update(); }

void WindowSelectable::update_cursor() {
    if (is_using_custom_cursor_rect_sprite()) {
        if (custom_cursor_container_) {
            if (cursor_all_ && custom_cursor_sprites_.empty()) {
                int idx = index();
                int tr = top_row(), mc = max_cols(), pi = max_page_items();
                custom_cursor_sprites_.resize((size_t)std::max(pi, 0));
                for (int i = 0; i < pi; i++) {
                    int t = tr * mc + i;
                    if (t == idx) continue;
                    auto sp = std::make_shared<SpriteWindowCustomCursor>(t, custom_cursor_rect_bitmap_name());
                    sp->angle_ = custom_cursor_sprite_->angle_;
                    custom_cursor_container_->add_child(sp);
                    custom_cursor_sprites_[(size_t)i] = sp;
                }
            } else if (!custom_cursor_sprites_.empty()) {
                for (auto& sp : custom_cursor_sprites_) {
                    if (sp) custom_cursor_container_->remove_child(sp.get());
                }
                custom_cursor_sprites_.clear();
            }
        }
        update_custom_cursor_rect_sprite(custom_cursor_sprite_.get(), index(), false);
        for (auto& sp : custom_cursor_sprites_) {
            if (sp) update_custom_cursor_rect_sprite(sp.get(), sp->index, true);
        }
        return;
    }
    if (cursor_all_) {
        double all_h = max_rows() * item_height();
        set_cursor_rect(0, 0, c().width(), all_h);
        set_top_row(0);
    } else if (is_cursor_visible()) {
        ItemRect r = item_rect(index());
        set_cursor_rect(r.x, r.y, r.width, r.height);
    } else {
        set_cursor_rect(0, 0, 0, 0);
    }
}

bool WindowSelectable::is_cursor_visible() {
    int r = row();
    return r >= top_row() && r <= bottom_row();
}

void WindowSelectable::ensure_cursor_visible() {
    int r = row();
    if (r < top_row()) set_top_row(r);
    else if (r > bottom_row()) set_bottom_row(r);
}

void WindowSelectable::call_update_help() {
    if (active && help_window) update_help();
}

void WindowSelectable::update_help() { help_window->clear(); }

void WindowSelectable::set_help_window_item(const rpg::ItemRef& item) {
    if (help_window) help_window->set_item(item);
}

void WindowSelectable::draw_all_items() {
    int ti = top_index();
    for (int i = 0; i < max_page_items(); i++) {
        int idx = ti + i;
        if (idx < max_items()) draw_item(idx);
    }
}

void WindowSelectable::clear_item(int index) {
    ItemRect r = item_rect(index);
    c().clear_rect(r.x, r.y, r.width, r.height);
}

void WindowSelectable::redraw_item(int index) {
    if (index >= 0) {
        clear_item(index);
        draw_item(index);
    }
}

void WindowSelectable::refresh() {
    if (contents()) {
        c().clear();
        draw_all_items();
    }
}

bool WindowSelectable::is_custom_cursor_rect_sprite_visible() {
    if (openness() < 255) return false;
    if (!visible) return false;
    if (!is_cursor_visible()) return false;
    if (index() < 0) return false;
    return true;
}

void WindowSelectable::init_custom_cursor_rect() {
    custom_cursor_sprites_.clear();
    custom_cursor_container_ = std::make_shared<g2d::Sprite>();
    add_child(custom_cursor_container_);
    custom_cursor_sprite_ = std::make_shared<SpriteWindowCustomCursor>(-1, custom_cursor_rect_bitmap_name());
    custom_cursor_container_->add_child(custom_cursor_sprite_);
}

void WindowSelectable::update_custom_cursor_rect_sprite_visibility() {
    if (custom_cursor_sprite_) custom_cursor_sprite_->visible = is_custom_cursor_rect_sprite_visible();
    for (auto& sp : custom_cursor_sprites_) {
        if (sp) sp->visible = is_custom_cursor_rect_sprite_visible();
    }
}

void WindowSelectable::update_custom_cursor_rect_sprite(SpriteWindowCustomCursor* sprite, int idx, bool use_index) {
    if (!sprite) return;
    if (!use_index) idx = index();
    ItemRect r = item_rect(idx);
    sprite->visible = is_custom_cursor_rect_sprite_visible();
    sprite->x = r.x + custom_cursor_rect_x_offset();
    sprite->y = ((r.y + standard_padding()) + std::floor(r.height / 2)) + custom_cursor_rect_y_offset();
    sprite->active_ = active;
    static const double on[4] = {0, 0, 0, 0};
    static const double off[4] = {-80, -80, -80, 255};
    sprite->set_color_tone(active ? on : off);
}

void WindowSelectable::select_available(int idx) {
    int tr = top_row(), mr = max_top_row();
    select(std::min(idx, max_items() - 1));
    if (tr > mr) set_top_row(mr);
}

void WindowCommand::initialize(double x, double y) {
    clear_command_list();
    make_command_list();
    double w = window_width();
    double h = window_height();
    WindowSelectable::initialize(x, y, w, h);
    refresh();
    select(0);
    activate();
}

int WindowCommand::num_visible_rows() { return (int)std::ceil((double)max_items() / max_cols()); }

void WindowCommand::add_command(const std::string& name, const std::string& symbol, bool enabled, const js::Val& ext) {
    list_.push_back(CommandItem{name, symbol, enabled, ext});
}

const CommandItem* WindowCommand::current_data() const { return index_ >= 0 && index_ < (int)list_.size() ? &list_[(size_t)index_] : nullptr; }

bool WindowCommand::is_current_item_enabled() {
    const CommandItem* d = current_data();
    return d ? d->enabled : false;
}

std::string WindowCommand::current_symbol() const {
    const CommandItem* d = current_data();
    return d ? d->symbol : std::string();
}

js::Val WindowCommand::current_ext() const {
    const CommandItem* d = current_data();
    return d ? d->ext : js::Val::null();
}

int WindowCommand::find_symbol(const std::string& symbol) const {
    for (size_t i = 0; i < list_.size(); i++) {
        if (list_[i].symbol == symbol) return (int)i;
    }
    return -1;
}

void WindowCommand::select_symbol(const std::string& symbol) {
    int i = find_symbol(symbol);
    select(i >= 0 ? i : 0);
}

int WindowCommand::find_ext(const js::Val& ext) const {
    for (size_t i = 0; i < list_.size(); i++) {
        if (js::strict_equals(list_[i].ext, ext)) return (int)i;
    }
    return -1;
}

void WindowCommand::select_ext(const js::Val& ext) {
    int i = find_ext(ext);
    select(i >= 0 ? i : 0);
}

void WindowCommand::draw_item(int index) {
    ItemRect r = item_rect_for_text(index);
    std::string align = item_text_align();
    reset_text_color();
    change_paint_opacity(is_command_enabled(index));
    draw_text(command_name(index), r.x, r.y, r.width, align);
}

std::string WindowCommand::item_text_align() { return yparam_str("TextAlign"); }

void WindowCommand::call_ok_handler() {
    std::string symbol = current_symbol();
    if (is_handled(symbol)) call_handler(symbol);
    else if (is_handled("ok")) WindowSelectable::call_ok_handler();
    else activate();
}

void WindowCommand::refresh() {
    clear_command_list();
    make_command_list();
    create_contents();
    WindowSelectable::refresh();
}

void WindowHelp::initialize(int num_lines) {
    double w = 640;
    double h = fitting_height(num_lines ? num_lines : 3);
    WindowBase::initialize(0, 0, w, h);
    text_.clear();
}

void WindowHelp::set_text(const std::string& text) {
    if (text_ != text) {
        text_ = text;
        refresh();
    }
}

void WindowHelp::clear() {
    set_text("");
    c().clear();
}

void WindowHelp::set_item(const rpg::ItemRef& item) {
    std::string desc = item.valid() ? item.data()["description"].string() : std::string();
    if (yparam_bool("MSGDescWrap")) set_text(item.valid() ? "<WordWrap>" + desc : std::string());
    else set_text(desc);
}

void WindowHelp::refresh() {
    c().clear();
    draw_text_ex(text_, text_padding(), 0);
}

void WindowHelp::set_battler(rpg::GameBattler* battler) {
    c().clear();
    clear();
    reset_font_settings();
    if (!rpg::party_in_battle()) return;
    if (!battler) return;
    std::shared_ptr<rpg::GameAction> action = rpg::battle::inputting_action();
    if (special_selection_text(action.get())) draw_special_selection_text(action.get());
    else draw_battler(battler);
}

bool WindowHelp::special_selection_text(rpg::GameAction* action) {
    rpg::battle::reset_selection();
    if (!action) return false;
    return !action->needs_selection();
}

void WindowHelp::draw_battler(rpg::GameBattler* battler) {
    reset_font_settings();
    if (battler->all_icons().empty()) {
        std::string text = battler->name();
        double wy = (c().height() - line_height()) / 2;
        draw_text(text, 0, wy, c().width(), "center");
    } else {
        draw_battler_with_icons(battler);
    }
}

void WindowHelp::draw_special_selection_text(rpg::GameAction* action) {
    double wy = (c().height() - line_height()) / 2;
    std::string text;
    if (action->is_for_user()) {
        text = yparam_str("BECHelpUserTx");
    } else if (action->is_for_random()) {
        rpg::battle::start_all_selection();
        std::string fmt = yparam_str("BECHelpRandTx");
        std::string target;
        if (action->is_for_opponent() && action->num_targets() != 1) target = yparam_str("BECHelpEnemiesTx");
        else if (action->is_for_opponent() && action->num_targets() == 1) target = yparam_str("BECHelpEnemyTx");
        else if (action->is_for_friend() && action->num_targets() != 1) target = yparam_str("BECHelpAlliesTx");
        else target = yparam_str("BECHelpAllyTx");
        text = rpg::js_format(fmt, {target, group_digits(action->num_targets())});
    } else if (action->is_for_all()) {
        rpg::battle::start_all_selection();
        std::string fmt = yparam_str("BECHelpAllTx");
        text = rpg::js_format(fmt, {action->is_for_opponent() ? yparam_str("BECHelpEnemiesTx") : yparam_str("BECHelpAlliesTx")});
    }
    draw_text(text, 0, wy, c().width(), "center");
}

void WindowHelp::draw_battler_with_icons(rpg::GameBattler* battler) {
    auto icons = battler->all_icons();
    std::string text = battler->name();
    double wx = 0, wy = 0;
    draw_text(text, wx, wy, c().width(), "center");
    wy += line_height();
    double ww = std::min((double)icons.size() * kIconWidth, (double)c().width());
    wx = (c().width() - ww) / 2;
    draw_actor_icons(battler, wx, wy, ww);
}

}
