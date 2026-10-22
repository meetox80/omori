#include "message.h"

#include <algorithm>
#include <cmath>

#include "../core/regex.h"
#include "../js/compile.h"
#include "../rpg/audio_manager.h"
#include "../rpg/battler.h"
#include "../rpg/config.h"
#include "../rpg/data.h"
#include "../rpg/enemy.h"
#include "../rpg/evalctx.h"
#include "../rpg/game.h"
#include "../rpg/hooks.h"
#include "../rpg/input.h"
#include "../rpg/interpreter.h"
#include "../rpg/map.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/state.h"
#include "../rpg/system.h"
#include "ui_util.h"
#include "window_selectable.h"

namespace ui {

namespace {

const char ESC = '\x1b';

std::string esc(const char* pattern) { return std::string(1, ESC) + pattern; }

double eval_lang(const Value& v, std::initializer_list<std::pair<const char*, js::Val>> vars) {
    if (v.is_num()) return v.num();
    if (!v.is_str()) return 0;
    auto prog = js::compile_cached(v.string());
    if (!prog) return 0;
    rpg::JsEval e(js::make_object());
    for (auto& kv : vars) e.set(kv.first, kv.second);
    if (!e.run(prog.get(), "LanguageManager eval")) return 0;
    return e.completion().to_num();
}

std::string param(const char* plugin, const char* key, const char* def) { return data::plugin_param(plugin, key, def); }

double param_num(const char* plugin, const char* key, double def) {
    std::string s = data::plugin_param(plugin, key, "");
    if (s.empty()) return def;
    double v = rpg::js_number(s);
    return v == v ? v : def;
}

bool skipping_cutscene() {
    js::Val* v = rpg::game_temp().expando.find(js::atom("_skippingCutscene"));
    return v && v->truthy();
}

bool can_skip_cutscene() {
    js::Val* v = rpg::game_temp().expando.find(js::atom("_canSkipCutscene"));
    return v && v->truthy();
}

bool system_flag(const char* name) {
    js::Val* v = rpg::game_system().expando.find(js::atom(name));
    return v && v->truthy();
}

js::Val system_value(const char* name) {
    js::Val* v = rpg::game_system().expando.find(js::atom(name));
    return v ? *v : js::Val();
}

std::string lang_text(const std::string& code) { return data::lang_text(code); }

std::string strip_s_upper(std::string s) {
    size_t p = s.find("[S]");
    if (p != std::string::npos) s.erase(p, 3);
    return rpg::to_upper(s);
}

void exit_map_interpreter() {
    if (rpg::game_map().interpreter) rpg::game_map().interpreter->command115();
}

}

class WindowNameBox;
class WindowChoiceList;
class WindowNumberInput;
class WindowEventItem;
class WindowMessageGold;
class WindowMessageFaceBox;

class WindowMessage : public WindowBase {
public:
    void initialize(rpg::Scene& scene);
    void init_members();
    void create_sub_windows(rpg::Scene& scene);
    double window_width();
    double window_height() { return fitting_height(num_visible_rows()); }
    void clear_flags();
    int num_visible_rows() { return 4; }
    void update() override;
    void check_to_not_close();
    bool can_start();
    void start_message();
    void update_placement();
    void update_background();
    void terminate_message();
    bool update_wait();
    bool update_loading();
    bool update_input();
    bool is_any_sub_window_active();
    bool update_message();
    bool base_update_message();
    void on_end_of_text();
    bool start_input();
    bool is_triggered();
    bool does_continue();
    bool are_settings_changed();
    void update_show_fast();
    void new_page(TextState& ts);
    void load_message_face();
    double new_line_x() {
        double v = 10;
        if (hooks.new_line_x && hooks.new_line_x(v)) return v;
        return 10;
    }
    void process_new_line(TextState& ts) override;
    void process_new_page(TextState& ts) override;
    bool is_end_of_text(const TextState& ts) const { return ts.index >= ts.text.size(); }
    bool needs_new_page(const TextState& ts);
    void process_escape_character(const std::string& code, TextState& ts) override;
    void start_wait(int count);
    void start_pause();
    double wordwrap_width() override;
    void adjust_window_settings();
    std::string standard_font_face() override { return rpg::game_system().msg_font_name; }
    int standard_font_size() override { return rpg::game_system().msg_font_size; }
    bool is_fast_forward();
    std::string convert_escape_characters(std::string text) override;
    std::string convert_name_box(std::string text);
    std::string convert_message_characters(std::string text);
    bool has_different_name_box_text();
    void update_position_placement();
    void update_position_placement_x();
    void update_position_placement_y();
    std::string convert_letter_sounds(std::string text);
    std::string convert_message_positions(std::string text);
    void set_message_position_event(double id) {
        lock_position_to_event_ = id;
        has_lock_position_ = true;
    }
    void get_fitted_message_rows(const std::string& text);
    double get_fitted_message_width(const std::string& text);
    void message_position_reset();
    std::string convert_extra_name_box_effects(std::string text);
    std::string convert_face_index_change(std::string text);
    void update_locked_position();
    void change_variable(const std::vector<std::string>& data, int operation);
    double line_height() override { return 24; }
    int standard_padding() override { return 8; }
    int standard_openness_type() override { return 2; }
    int open_speed() override { return 25; }
    int close_speed() override { return 25; }
    double calc_text_height(const TextState& ts, bool all) override { return WindowBase::calc_text_height(ts, all) - 7; }
    bool is_battle_message_tail_visible() { return rpg::game_switches().value(6); }
    void create_battle_message_tail();
    void create_face_box_windows();
    void update_open() override;
    void update_close() override;
    void refresh_pause_sign() override;
    void update_pause_sign() override;

    std::shared_ptr<WindowMessageGold> gold_window;
    std::shared_ptr<WindowChoiceList> choice_window;
    std::shared_ptr<WindowNumberInput> number_window;
    std::shared_ptr<WindowEventItem> item_window;
    std::shared_ptr<WindowNameBox> name_window;
    std::shared_ptr<g2d::Sprite> face_box_container;
    std::shared_ptr<WindowMessageFaceBox> face_box_window;
    std::shared_ptr<g2d::Sprite> battle_message_tail;
    bool has_text_state = false;
    TextState text_state;
    int background_ = 0;
    int position_type_ = 2;
    int wait_count_ = 0;
    bool show_fast_ = false;
    bool line_show_fast_ = false;
    bool pause_skip_ = false;
    bool needs_message_reset_ = false;
    bool checking_width_ = false;
    int sound_count_ = 0;
    bool has_lock_position_ = false;
    double lock_position_to_event_ = 0;
    MessageHooks hooks;
};

class WindowMessageGold : public WindowBase {
public:
    void override_init(double x, double y) {
        WindowBase::initialize(x, y, 128, 40);
        refresh();
    }
    int standard_padding() override { return 0; }
    double line_height() override { return 28; }
    void refresh() {
        double width = c().width() - text_padding() * 2;
        c().clear();
        const Value& pos = lang_data("XX_BLUE.Window_Gold")["drawCurrencyValue_position"];
        double x = text_padding();
        double dx = eval_lang(pos[0], {{"x", js::Val::num(x)}, {"width", js::Val::num(width)}});
        double dy = eval_lang(pos[1], {{"x", js::Val::num(x)}, {"width", js::Val::num(width)}});
        draw_currency_value(rpg::game_party().gold(), data::system()["currencyUnit"].string(), dx, dy, width);
    }
    void open() override {
        refresh();
        WindowBase::open();
    }
};

class WindowMessageFaceBox : public WindowBase {
public:
    void initialize() {
        clear();
        WindowBase::initialize(0, 0, 114, 114);
        create_background();
    }
    int standard_padding() override { return 5; }
    int standard_openness_type() override { return 2; }
    void create_background() {
        background_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create((int)width() - 10, (int)height() - 10));
        background_->x = 5;
        background_->y = 5;
        add_child_to_back(background_);
    }
    void clear() {
        stop_face_ = true;
        face_bitmap_.reset();
        face_name_.clear();
        face_index_ = 0;
    }
    void set_background_color(bool has, const std::string& color) {
        if (has && !color.empty()) background_->bitmap()->fill_all(color);
        else background_->bitmap()->clear();
    }
    void setup(const std::string& name, int index, bool has_color, const std::string& color) {
        face_index_ = index;
        face_name_ = name;
        face_bitmap_ = load_face(name);
        set_background_color(has_color, color);
    }
    void base_refresh() {
        c().clear();
        if (!face_name_.empty()) draw_face_box(face_name_, face_index_, 0, 0, 106, 106);
    }
    void refresh() {
        stop_face_ = false;
        face_ready_ = false;
        base_refresh();
    }
    void draw_face_box(const std::string& name, int index, double x, double y, double w, double h) {
        if (stop_face_) {
            face_ready_ = true;
            return;
        }
        g2d::BitmapPtr bitmap = face_bitmap_ ? face_bitmap_ : load_face(name);
        double pw = kFaceWidth, ph = kFaceHeight;
        double sw = std::min(w, pw), sh = std::min(h, ph);
        double dx = std::floor(x + std::max(w - pw, 0.0) / 2);
        double dy = std::floor(y + std::max(h - ph, 0.0) / 2);
        double sx = (index % 4) * pw + (pw - sw) / 2;
        double sy = std::floor(index / 4.0) * ph + (ph - sh) / 2;
        c().blt(bitmap.get(), sx, sy, sw, sh, dx, dy);
        clear();
        face_ready_ = true;
    }
    void render(const g2d::RenderState& rs) override {
        if (background_) {
            auto sc = sprite_container();
            background_->scale_x = sc->scale_x;
            background_->scale_y = sc->scale_y;
            background_->x = sc->x + 5;
            background_->y = sc->y + 5;
        }
        WindowBase::render(rs);
    }

private:
    std::shared_ptr<g2d::Sprite> background_;
    g2d::BitmapPtr face_bitmap_;
    std::string face_name_;
    int face_index_ = 0;
    bool stop_face_ = false;
    bool face_ready_ = false;
};

class WindowNameBox : public WindowBase {
public:
    void initialize(WindowMessage* parent) {
        parent_ = parent;
        WindowBase::initialize(0, 0, 240, window_height());
        text_.clear();
        last_name_text.clear();
        set_openness(0);
        close_counter_ = 0;
        deactivate();
        if (clear_box()) {
            set_back_opacity(0);
            set_window_opacity(0);
        }
        hide();
    }
    static bool clear_box() { return param("YEP_MessageCore", "Name Box Clear", "false") == "true"; }
    static double box_padding() { return param_num("YEP_MessageCore", "Name Box Padding", 0); }
    static double buffer_x() { return param_num("YEP_MessageCore", "Name Box Buffer X", 0); }
    static double buffer_y() { return param_num("YEP_MessageCore", "Name Box Buffer Y", 0); }
    double window_width() {
        reset_font_settings();
        double dw = name_text_width_ex(text_);
        dw += padding() * 2;
        return std::ceil(dw + box_padding());
    }
    double name_text_width_ex(const std::string& t) { return draw_text_ex(t, 0, c().height()); }
    double window_height() { return fitting_height(1); }
    std::string standard_font_face() override { return rpg::game_system().msg_font_name; }
    int standard_font_size() override {
        return (int)eval_lang(lang_data("XX_BLUE.Window_NameBox")["standardFontSize"], {});
    }
    int standard_padding() override { return 12; }
    double line_height() override { return 20; }
    int standard_openness_type() override { return 2; }
    void update() override;
    std::string refresh(const std::string& text, int position);
    std::string yep_refresh(const std::string& text, int position) {
        show();
        last_name_text = text;
        text_ = param("YEP_MessageCore", "Name Box Added Text", "") + text;
        position_ = position;
        set_width(window_width());
        create_contents();
        c().clear();
        reset_font_settings();
        change_text_color(text_color((int)param_num("YEP_MessageCore", "Name Box Color", 0)));
        double pad = box_padding() / 2;
        draw_text_ex(text_, pad, 0);
        parent_->adjust_window_settings();
        parent_->update_placement();
        adjust_position_x();
        adjust_position_y();
        open();
        activate();
        close_counter_ = 4;
        return "";
    }
    std::string refresh_dimmed(const std::string& text, int position) {
        std::string r = yep_refresh(text, position);
        set_background_type(1);
        return r;
    }
    std::string refresh_transparent(const std::string& text, int position) {
        std::string r = yep_refresh(text, position);
        set_background_type(2);
        return r;
    }
    void adjust_position_x() {
        WindowMessage* p = parent_;
        if (position_ == 1) {
            x = p->x + buffer_x();
        } else if (position_ == 2) {
            x = p->x + p->width() * 3 / 10 - width() / 2;
        } else if (position_ == 3) {
            x = p->x + p->width() / 2 - width() / 2;
        } else if (position_ == 4) {
            x = p->x + p->width() * 7 / 10 - width() / 2;
        } else {
            x = p->x + p->width() - width() - buffer_x();
        }
        x = std::clamp(x, 0.0, 640 - width());
    }
    void adjust_position_y() {
        WindowMessage* p = parent_;
        if (rpg::game_message().position_type == 0) {
            y = p->y + p->height() - buffer_y();
        } else {
            y = p->y - height() + buffer_y();
        }
        if (y < 0) y = p->y + p->height() - buffer_y();
    }
    std::string last_name_text;

private:
    WindowMessage* parent_ = nullptr;
    std::string text_;
    int position_ = 0;
    int close_counter_ = 0;
};

static WindowNameBox* g_name_window = nullptr;
static WindowMessage* g_message_window = nullptr;

void WindowNameBox::update() {
    WindowBase::update();
    if (active) return;
    if (is_closed()) return;
    if (is_closing()) return;
    if (close_counter_-- > 0) return;
    if (parent_->is_closing()) set_openness(parent_->openness());
    close();
}

std::string WindowNameBox::refresh(const std::string& text, int position) {
    show();
    last_name_text = text;
    text_ = param("YEP_MessageCore", "Name Box Added Text", "") + text;
    position_ = position;
    set_width(window_width());
    create_contents();
    c().clear();
    reset_font_settings();
    change_text_color(text_color((int)param_num("YEP_MessageCore", "Name Box Color", 0)));
    double pad = box_padding() / 2;
    const Value& loc = lang_data("XX_BLUE.Window_NameBox")["refresh_draw_text_position"];
    double dx = eval_lang(loc[0], {{"padding", js::Val::num(pad)}});
    double dy = eval_lang(loc[1], {{"padding", js::Val::num(pad)}});
    draw_text_ex(text_, dx, dy);
    parent_->adjust_window_settings();
    parent_->update_placement();
    adjust_position_y();
    y -= 4;
    open();
    activate();
    close_counter_ = 4;
    return "";
}

class WindowChoiceList : public WindowCommand {
public:
    void initialize(WindowMessage* mw) {
        message_window_ = mw;
        WindowCommand::initialize(0, 0);
        set_openness(0);
        deactivate();
        background_ = 0;
    }
    void start() {
        bool jukebox = system_flag("_jukeboxOn");
        array_choices_.assign(rpg::game_message().choices.size(), jukebox);
        update_placement();
        update_background();
        refresh();
        select_default();
        open();
        activate();
    }
    void select_default() { select(rpg::game_message().choice_default_type); }
    void update_placement() {
        rpg::GameMessage& m = rpg::game_message();
        int pt = m.choice_position_type;
        double message_y = message_window_->y;
        set_width(window_width());
        set_height(window_height());
        if (pt == 0) x = 0;
        else if (pt == 1) x = (640 - width()) / 2;
        else if (pt == 2) x = 640 - width();
        if (message_y >= 240) y = message_y - height();
        else y = message_y + message_window_->height();
        int mpt = m.position_type;
        if (mpt == 0) y = message_window_->height();
        else if (mpt == 2) y = 480 - message_window_->height() - height();
        x -= 18;
        y -= 8;
        if (message_y >= 240) y -= 4;
        else y += 4;
        if (m.face_count()) x += message_window_->face_box_window ? message_window_->face_box_window->x : 0;
    }
    void update_background() {
        background_ = rpg::game_message().choice_background;
        set_background_type(background_);
    }
    double window_width() override { return std::min(max_choice_width() + padding() * 2, 640.0); }
    int num_visible_rows() override {
        std::vector<std::string>& choices = rpg::game_message().choices;
        if (system_flag("_jukeboxOn")) return std::min<int>((int)choices.size(), 11);
        if (system_flag("_washingDishes")) return std::min<int>((int)choices.size(), 11);
        if (rpg::game_switches().value(804)) return std::min<int>((int)choices.size(), 7);
        double message_y = message_window_->y;
        double message_h = message_window_->height();
        make_command_list();
        int num_lines = max_choices_;
        int max_lines = rpg::game_system().msg_choice_max;
        if (message_y < 240 && message_y + message_h > 240) max_lines = 4;
        if (num_lines > max_lines) num_lines = max_lines;
        return std::max(1, num_lines);
    }
    std::string element(size_t i, int part) {
        const std::string& s = rpg::game_message().choices[i];
        if (i < array_choices_.size() && array_choices_[i]) {
            size_t comma = s.find(',');
            if (part == 0) return s.substr(0, comma);
            return comma == std::string::npos ? std::string() : s.substr(comma + 1);
        }
        if (part == 0) return s.empty() ? std::string() : s.substr(0, 1);
        return s.size() > 1 ? s.substr(1, 1) : std::string();
    }
    double max_choice_width() {
        std::vector<std::string>& choices = rpg::game_message().choices;
        if (system_flag("_jukeboxOn")) {
            double max_width = 96;
            for (size_t i = 0; i < choices.size(); i++) {
                double w;
                if (element(i, 0).find("farawaytown_") != std::string::npos) w = choice_text_width_ex(lang_text(element(i, 1))) + text_padding() * 2;
                else w = choice_text_width_ex(choice_display(i)) + text_padding() * 2;
                max_width = std::max(max_width, w);
            }
            return max_width + 32 + text_padding();
        }
        if (system_flag("_washingDishes")) {
            double max_width = 96;
            for (size_t i = 0; i < choices.size(); i++) {
                if (choices[i].find("farawaytown_") != std::string::npos) choices[i] = strip_s_upper(lang_text(choices[i]));
                double w = choice_text_width_ex(choices[i]) + text_padding() * 2;
                max_width = std::max(max_width, w);
            }
            return max_width + 40;
        }
        double max_width = 36;
        for (size_t i = 0; i < choices.size(); i++) {
            double w = choice_text_width_ex(choice_display(i)) + text_padding() * 2;
            max_width = std::max(max_width, w);
        }
        return max_width + 28;
    }
    std::string choice_display(size_t i) { return rpg::game_message().choices[i]; }
    double choice_text_width_ex(const std::string& text) {
        if (!contents()) return 0;
        return draw_text_ex(text, 0, c().height());
    }
    double contents_height() override { return max_items() * item_height(); }
    void make_command_list() override {
        std::vector<std::string>& choices = rpg::game_message().choices;
        if (system_flag("_jukeboxOn")) {
            for (size_t i = 0; i < choices.size(); i++) {
                if (element(i, 0).find("farawaytown_") != std::string::npos) {
                    std::string item = lang_text(element(i, 1));
                    choices[i] = rpg::to_upper(item);
                    if (i < array_choices_.size()) array_choices_[i] = false;
                }
            }
        }
        if (system_flag("_washingDishes")) {
            for (size_t i = 0; i < choices.size(); i++) {
                if (choices[i].find("farawaytown_") != std::string::npos) choices[i] = strip_s_upper(lang_text(choices[i]));
            }
        }
        max_choices_ = 0;
        cancel_allowed_ = true;
        rpg::GameSystem& sys = rpg::game_system();
        for (size_t i = 0; i < choices.size(); i++) {
            int ii = (int)i;
            if (!sys.is_choice_shown(ii)) {
                if (rpg::game_message().choice_cancel_type == ii) cancel_allowed_ = false;
                continue;
            }
            max_choices_ += 1;
            bool enabled = sys.is_choice_enabled(ii);
            add_command(choices[i], "choice", enabled, js::Val::num(ii));
            if (!enabled && rpg::game_message().choice_cancel_type == ii) cancel_allowed_ = false;
        }
    }
    void draw_item(int index) override {
        change_paint_opacity(is_command_enabled(index));
        ItemRect r = item_rect_for_text(index);
        draw_text_ex(command_name(index), r.x, r.y);
    }
    bool is_cancel_enabled() override {
        if (!cancel_allowed_) return false;
        return rpg::game_message().choice_cancel_type != -1;
    }
    bool is_ok_triggered() override {
        if (skipping_cutscene()) return true;
        return input::is_triggered(input::OK);
    }
    void call_ok_handler() override {
        if (system_flag("_jukeboxOn")) {
            std::string nevermind = lang_text("farawaytown_extras_pizzaminigame.message_17");
            if (list_[(size_t)index()].name == nevermind) {
                exit_map_interpreter();
            } else {
                js::Val list = system_value("_jukeboxList");
                js::Val entry = list.is_array() && index() >= 0 && (size_t)index() < list.o->items.size() ? list.o->items[(size_t)index()] : js::Val();
                js::Val code = entry.is_array() && !entry.o->items.empty() ? entry.o->items[0] : js::Val();
                std::string name = lang_text(code.to_str());
                rpg::AudioObj bgm;
                bgm.name = name;
                bgm.volume = name == "jb_omniboi" ? 90 : 100;
                bgm.pitch = 100;
                bgm.pan = 0;
                bgm.valid = true;
                rpg::audio_manager::play_bgm(bgm);
            }
            rpg::game_system().expando.set(js::atom("_jukeboxOn"), js::Val::boolean(false));
        }
        if (system_flag("_washingDishes")) {
            js::Val list = system_value("_dishList");
            js::Val code = list.is_array() && index() >= 0 && (size_t)index() < list.o->items.size() ? list.o->items[(size_t)index()] : js::Val();
            rpg::game_variables().set_value(806, js::Val::str(strip_s_upper(lang_text(code.to_str()))));
            rpg::game_variables().set_number(807, index());
        }
        if (rpg::game_switches().value(804)) {
            std::string yes = lang_text("XX_GENERAL.message_4");
            std::string no = lang_text("XX_GENERAL.message_5");
            std::string nevermind = lang_text("farawaytown_extras_pizzaminigame.message_17");
            const std::string& name = list_[(size_t)index()].name;
            if (name != yes && name != no) {
                if (name == nevermind) {
                    exit_map_interpreter();
                } else {
                    rpg::game_variables().set_number(814, index());
                    rpg::game_variables().set_value(815, js::Val::str(name));
                }
            }
        }
        rpg::game_message().on_choice(current_ext().to_int());
        message_window_->terminate_message();
        close();
    }
    void call_cancel_handler() override {
        rpg::game_message().on_choice(rpg::game_message().choice_cancel_type);
        message_window_->terminate_message();
        close();
    }
    std::string standard_font_face() override { return rpg::game_system().msg_font_name; }
    int standard_font_size() override { return (int)eval_lang(lang_data("XX_BLUE.Window_ChoiceList")["standardFontSize"], {}); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 10; }
    double custom_cursor_rect_text_x_offset() override { return 32; }
    double custom_cursor_rect_text_y_offset() override { return -5; }
    double line_height() override { return 30; }
    double calc_text_height(const TextState& ts, bool all) override { return WindowCommand::calc_text_height(ts, all) - 3; }
    void refresh_arrows() override {
        WindowCommand::refresh_arrows();
        if (system_flag("_washingDishes")) {
            down_arrow_->move(width() - 16, height() - 12);
            up_arrow_->move(width() - 16, 12);
        }
    }

private:
    WindowMessage* message_window_ = nullptr;
    int background_ = 0;
    int max_choices_ = 0;
    bool cancel_allowed_ = true;
    std::vector<bool> array_choices_;
};

class WindowNumberInput : public WindowSelectable {
public:
    void initialize(WindowMessage* mw) {
        message_window_ = mw;
        WindowSelectable::initialize(0, 0, 0, 0);
        number_ = 0;
        max_digits_ = 1;
        set_openness(0);
        deactivate();
    }
    void start() {
        rpg::GameMessage& m = rpg::game_message();
        max_digits_ = m.num_input_max_digits;
        number_ = rpg::game_variables().number(m.num_input_variable_id);
        number_ = std::clamp(number_, 0.0, std::pow(10.0, max_digits_) - 1);
        update_placement();
        create_contents();
        refresh();
        open();
        activate();
        select(0);
    }
    void update_placement() {
        double message_y = message_window_->y;
        set_width(max_cols() * item_width() + padding() * 2);
        set_height(fitting_height(1));
        x = (message_window_->x + message_window_->width()) - width();
        y = message_y - height() - 4;
    }
    int max_cols() override { return max_digits_; }
    int max_items() override { return max_digits_; }
    int spacing() override { return 0; }
    double item_width() override { return 32; }
    void update() override {
        WindowSelectable::update();
        process_digit_change();
    }
    void process_digit_change() {
        if (is_open_and_active()) {
            if (input::is_repeated(input::UP)) change_digit(true);
            else if (input::is_repeated(input::DOWN)) change_digit(false);
        }
    }
    void change_digit(bool up) {
        int idx = index();
        double place = std::pow(10.0, max_digits_ - 1 - idx);
        int n = (int)std::fmod(std::floor(number_ / place), 10);
        number_ -= n * place;
        n = up ? (n + 1) % 10 : (n + 9) % 10;
        number_ += n * place;
        refresh();
        play_cursor();
    }
    bool is_ok_enabled() override { return true; }
    bool is_cancel_enabled() override { return false; }
    bool is_ok_triggered() override {
        if (skipping_cutscene()) return true;
        return input::is_triggered(input::OK);
    }
    void process_ok() override {
        play_ok();
        rpg::game_variables().set_number(rpg::game_message().num_input_variable_id, number_);
        message_window_->terminate_message();
        update_input_data();
        deactivate();
        close();
    }
    void draw_item(int index) override {
        ItemRect r = item_rect(index);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%0*lld", max_digits_, (long long)number_);
        std::string s = buf;
        std::string ch = index < (int)s.size() ? s.substr((size_t)index, 1) : std::string();
        reset_text_color();
        draw_text(ch, r.x, r.y, r.width, "center");
    }
    int standard_padding() override { return 8; }

private:
    WindowMessage* message_window_ = nullptr;
    double number_ = 0;
    int max_digits_ = 1;
};

class WindowEventItem : public WindowSelectable {
public:
    void initialize(WindowMessage* mw) {
        message_window_ = mw;
        WindowSelectable::initialize(0, 0, 640, fitting_height(4));
        set_openness(0);
        deactivate();
        set_handler("ok", [this] { on_ok(); });
        set_handler("cancel", [this] { on_cancel(); });
    }
    int max_cols() override { return 2; }
    int spacing() override { return 48; }
    int max_items() override { return (int)data_.size(); }
    rpg::ItemRef item() const { return index_ >= 0 && (size_t)index_ < data_.size() ? data_[(size_t)index_] : rpg::ItemRef(); }
    bool is_current_item_enabled() override { return true; }
    void make_item_list() {
        data_.clear();
        int itype = rpg::game_message().item_choice_itype_id;
        for (const rpg::ItemRef& it : rpg::game_party().all_items()) {
            if (it.is_item() && it.data()["itypeId"].as_int() == itype) data_.push_back(it);
        }
    }
    double number_width() { return text_width(yparam_str("ItemQuantityFmt").empty() ? "000" : "×0,000"); }
    void draw_item(int index) override {
        rpg::ItemRef it = index >= 0 && (size_t)index < data_.size() ? data_[(size_t)index] : rpg::ItemRef();
        if (!it.valid()) return;
        double nw = number_width();
        ItemRect r = item_rect(index);
        r.width -= text_padding();
        change_paint_opacity(true);
        draw_item_name(it, r.x, r.y, r.width - nw);
        draw_item_number(it, r.x, r.y, r.width);
        change_paint_opacity(true);
    }
    void draw_item_number(const rpg::ItemRef& it, double x, double y, double width) {
        if (!yparam_bool("ItemQuantityFmt") && yparam_str("ItemQuantityFmt").empty()) {
            draw_text(":", x, y, width - text_width("00"), "right");
            draw_text(std::to_string((int)rpg::game_party().num_items(it)), x, y, width, "right");
            return;
        }
        std::string fmt = yparam_str("ItemQuantityFmt");
        std::string text = rpg::js_format(fmt, {group_digits(rpg::game_party().num_items(it))});
        int fs = c().font_size;
        double qfs = yparam_num("ItemQuantityFontSize", fs);
        c().font_size = (int)qfs;
        draw_text(text, x, y, width, "right");
        c().font_size = fs;
    }
    void refresh() override {
        make_item_list();
        create_contents();
        draw_all_items();
    }
    void start() {
        refresh();
        update_placement();
        select(0);
        open();
        activate();
    }
    void update_placement() {
        if (message_window_->y >= 240) y = 0;
        else y = 480 - height();
        int mpt = rpg::game_message().position_type;
        if (mpt == 0) y = 480 - height();
        else if (mpt == 2) y = 0;
    }
    void on_ok() {
        rpg::ItemRef it = item();
        int id = it.valid() ? it.id : 0;
        rpg::game_variables().set_number(rpg::game_message().item_choice_variable_id, id);
        message_window_->terminate_message();
        close();
    }
    void on_cancel() {
        rpg::game_variables().set_number(rpg::game_message().item_choice_variable_id, 0);
        message_window_->terminate_message();
        close();
    }

private:
    WindowMessage* message_window_ = nullptr;
    std::vector<rpg::ItemRef> data_;
};

class WindowScrollText : public WindowBase {
public:
    void initialize() {
        WindowBase::initialize(0, 0, 640, 480);
        set_window_opacity(0);
        hide();
        text_.clear();
        has_text_ = false;
        all_text_height_ = 0;
    }
    void update() override {
        WindowBase::update();
        rpg::GameMessage& m = rpg::game_message();
        if (m.scroll_mode) {
            if (has_text_) update_message();
            if (!has_text_ && m.has_text()) start_message();
        }
    }
    void start_message() {
        text_ = rpg::game_message().all_text();
        has_text_ = true;
        refresh();
        show();
    }
    void refresh() {
        TextState ts;
        ts.text = convert_escape_characters(text_);
        reset_font_settings();
        all_text_height_ = calc_text_height(ts, true);
        create_contents();
        origin_y = -height();
        draw_text_ex(text_, text_padding(), 1);
    }
    double contents_height() override { return std::max(all_text_height_, 1.0); }
    void update_message() {
        origin_y += scroll_speed();
        if (origin_y >= c().height()) terminate_message();
    }
    double scroll_speed() {
        double speed = rpg::game_message().scroll_speed / 2.0;
        if (is_fast_forward()) speed *= 3;
        return speed;
    }
    bool is_fast_forward() {
        if (rpg::game_message().scroll_no_fast) return false;
        return input::is_pressed(input::OK) || input::is_pressed(input::SHIFT);
    }
    void terminate_message() {
        text_.clear();
        has_text_ = false;
        rpg::game_message().clear();
        hide();
    }
    std::string standard_font_face() override { return rpg::game_system().msg_font_name; }
    int standard_font_size() override { return rpg::game_system().msg_font_size; }

private:
    std::string text_;
    bool has_text_ = false;
    double all_text_height_ = 0;
};

void WindowMessage::initialize(rpg::Scene& scene) {
    rpg::game_system().init_choice_show();
    rpg::game_system().init_choice_enable();
    needs_message_reset_ = false;
    double w = window_width();
    double h = window_height();
    WindowBase::initialize((640 - w) / 2, 0, w, h);
    set_openness(0);
    init_members();
    create_sub_windows(scene);
    update_placement();
}

void WindowMessage::init_members() {
    background_ = 0;
    position_type_ = 2;
    wait_count_ = 0;
    has_text_state = false;
    clear_flags();
}

double WindowMessage::window_width() {
    if (rpg::party_in_battle()) return 360;
    return 640 - 32;
}

void WindowMessage::clear_flags() {
    show_fast_ = false;
    line_show_fast_ = false;
    pause_skip_ = false;
}

void WindowMessage::create_sub_windows(rpg::Scene& scene) {
    gold_window = std::shared_ptr<WindowMessageGold>(new WindowMessageGold());
    gold_window->override_init(0, 0);
    gold_window->x = 640 - gold_window->width();
    gold_window->set_openness(0);
    choice_window = std::shared_ptr<WindowChoiceList>(new WindowChoiceList());
    choice_window->initialize(this);
    number_window = std::shared_ptr<WindowNumberInput>(new WindowNumberInput());
    number_window->initialize(this);
    item_window = std::shared_ptr<WindowEventItem>(new WindowEventItem());
    item_window->initialize(this);
    name_window = std::shared_ptr<WindowNameBox>(new WindowNameBox());
    name_window->initialize(this);
    g_name_window = name_window.get();
    scene.add_child(name_window);
    create_face_box_windows();
    if (std::string(scene.name()) == "Scene_Battle") create_battle_message_tail();
    name_window->x = -300;
    gold_window->x -= 20;
    gold_window->y += 13;
}

void WindowMessage::create_face_box_windows() {
    face_box_container = std::make_shared<g2d::Sprite>();
    double tx = x + width() + 2;
    face_box_container->x = ((640 - 16) + tx + 2) - tx;
    face_box_window = std::shared_ptr<WindowMessageFaceBox>(new WindowMessageFaceBox());
    face_box_window->initialize();
    face_box_window->x = -(face_box_window->width() + 2);
    face_box_window->set_openness(0);
    face_box_container->add_child(face_box_window);
}

void WindowMessage::create_battle_message_tail() {
    battle_message_tail = std::make_shared<g2d::Sprite>(load_system("battle_speech_tail"));
    battle_message_tail->y = 4;
    battle_message_tail->visible = false;
    battle_message_tail->anchor_x = 0.5;
    battle_message_tail->anchor_y = 1;
    add_child(battle_message_tail);
}

void WindowMessage::update() {
    check_to_not_close();
    WindowBase::update();
    while (!is_opening() && !is_closing()) {
        if (update_wait()) break;
        if (update_loading()) break;
        if (update_input()) break;
        if (update_message()) break;
        if (can_start()) {
            start_message();
        } else {
            start_input();
            break;
        }
    }
    if (has_lock_position_) update_locked_position();
}

void WindowMessage::check_to_not_close() {
    if (is_closing() && is_open()) {
        if (does_continue()) open();
    }
}

bool WindowMessage::can_start() { return rpg::game_message().has_text() && !rpg::game_message().scroll_mode; }

void WindowMessage::start_message() {
    init_letter_effects();
    name_window->deactivate();
    text_state = TextState();
    text_state.index = 0;
    text_state.text = convert_escape_characters(rpg::game_message().all_text());
    has_text_state = true;
    new_page(text_state);
    prefetch_text_fonts(text_state.text);
    update_placement();
    update_background();
    open();
}

void WindowMessage::update_placement() {
    position_type_ = rpg::game_message().position_type;
    y = position_type_ * (480 - height()) / 2;
    gold_window->y = y > 0 ? 0 : 480 - gold_window->height();
    update_position_placement();
    face_box_container->y = y - 126;
    if (battle_message_tail) {
        battle_message_tail->x = width() / 2;
        battle_message_tail->visible = is_battle_message_tail_visible();
    }
    if (position_type_ == 0) y += 8;
    if (position_type_ == 2) {
        if (rpg::party_in_battle()) {
            int v = (int)rpg::game_variables().number(22);
            if (rpg::game_switches().value(41) || v == 2 || v == 5 || v == 6) y -= 11;
            else y -= 65;
        } else {
            y -= 8;
        }
    }
    gold_window->y = y > 0 ? 12 : 480 - gold_window->height();
}

void WindowMessage::update_background() {
    background_ = rpg::game_message().background;
    set_background_type(background_);
}

void WindowMessage::terminate_message() {
    if (hooks.terminate_message && hooks.terminate_message()) return;
    name_window->deactivate();
    close();
    gold_window->close();
    rpg::game_message().clear();
    if (param("YEP_X_ExtMesPack1", "Reset Sounds", "false") == "true") rpg::game_system().init_message_sounds();
    if (needs_message_reset_) message_position_reset();
    has_lock_position_ = false;
    if (hooks.after_terminate_message) hooks.after_terminate_message();
}

bool WindowMessage::update_wait() {
    if (is_fast_forward()) return false;
    if (wait_count_ > 0) {
        wait_count_--;
        return true;
    }
    return false;
}

bool WindowMessage::update_loading() { return false; }

bool WindowMessage::update_input() {
    if (pause && is_fast_forward()) {
        if (!has_text_state) {
            pause = false;
            terminate_message();
        }
    }
    if (is_any_sub_window_active()) return true;
    if (pause) {
        if (is_triggered()) {
            input::update();
            pause = false;
            if (!has_text_state) terminate_message();
        }
        return true;
    }
    return false;
}

bool WindowMessage::is_any_sub_window_active() { return choice_window->active || number_window->active || item_window->active; }

bool WindowMessage::base_update_message() {
    if (!has_text_state) return false;
    while (!is_end_of_text(text_state)) {
        if (needs_new_page(text_state)) new_page(text_state);
        update_show_fast();
        process_character(text_state);
        if (!show_fast_ && !line_show_fast_) break;
        if (pause || wait_count_ > 0) break;
    }
    if (is_end_of_text(text_state)) on_end_of_text();
    return true;
}

bool WindowMessage::update_message() {
    bool state = base_update_message();
    if (state) {
        if (sound_count_-- <= 0) {
            rpg::GameSystem& sys = rpg::game_system();
            sound_count_ = sys.msg_sound_interval;
            if (sys.msg_sound_enable) rpg::audio_manager::play_se(rpg::AudioObj::from_js(sys.get_message_sound()));
        }
    }
    return state;
}

void WindowMessage::on_end_of_text() {
    rpg::tddp_ff::next_text_ff_disabled = false;
    if (!start_input()) {
        if (!pause_skip_) start_pause();
        else terminate_message();
    }
    has_text_state = false;
}

bool WindowMessage::start_input() {
    rpg::GameMessage& m = rpg::game_message();
    if (m.is_choice()) {
        choice_window->start();
        return true;
    }
    if (m.is_number_input()) {
        number_window->start();
        return true;
    }
    if (m.is_item_choice()) {
        item_window->start();
        return true;
    }
    return false;
}

bool WindowMessage::is_triggered() {
    if (skipping_cutscene()) return true;
    if (can_skip_cutscene() && input::is_repeated(input::CANCEL)) return false;
    return input::is_repeated(input::OK) || input::is_repeated(input::CANCEL);
}

bool WindowMessage::are_settings_changed() {
    return background_ != rpg::game_message().background || position_type_ != rpg::game_message().position_type;
}

bool WindowMessage::does_continue() {
    rpg::GameMessage& m = rpg::game_message();
    bool value = m.has_text() && !m.scroll_mode && !are_settings_changed();
    if (!value) return false;
    if (has_different_name_box_text()) return false;
    return true;
}

void WindowMessage::update_show_fast() {
    if (is_fast_forward()) show_fast_ = true;
    if (!rpg::config().text_skip) return;
    if (is_triggered()) show_fast_ = true;
}

void WindowMessage::new_page(TextState& ts) {
    sound_count_ = 0;
    adjust_window_settings();
    c().clear();
    reset_font_settings();
    clear_flags();
    load_message_face();
    ts.x = new_line_x();
    ts.y = 0;
    ts.left = new_line_x();
    ts.height = calc_text_height(ts, false);
}

void WindowMessage::load_message_face() {
    rpg::GameMessage& m = rpg::game_message();
    if (!m.face_name.empty()) {
        face_box_window->setup(m.face_name, m.face_index, m.has_face_background_color, m.face_background_color);
        face_box_window->refresh();
        face_box_window->open();
    } else {
        face_box_window->close();
        face_box_window->clear();
        face_box_window->refresh();
    }
}

void WindowMessage::process_new_line(TextState& ts) {
    line_show_fast_ = false;
    WindowBase::process_new_line(ts);
    if (needs_new_page(ts)) start_pause();
}

void WindowMessage::process_new_page(TextState& ts) {
    WindowBase::process_new_page(ts);
    if (ts.index < ts.text.size() && ts.text[ts.index] == '\n') ts.index++;
    ts.y = c().height();
    start_pause();
}

bool WindowMessage::needs_new_page(const TextState& ts) { return !is_end_of_text(ts) && ts.y + ts.height > c().height(); }

void WindowMessage::process_escape_character(const std::string& code, TextState& ts) {
    if (hooks.process_escape_character) hooks.process_escape_character(code, ts);
    if (code == "COM") {
        int id = (int)rpg::js_parse_int(obtain_escape_param(ts).to_str());
        if (rpg::party_in_battle()) rpg::game_troop().setup_common_event(id);
        else rpg::game_map().setup_common_event(id);
        return;
    }
    if (code == "VAR" || code == "VARA" || code == "VARS" || code == "VARX" || code == "VARD" || code == "VARM") {
        int op = code == "VAR" ? 0 : code == "VARA" ? 1 : code == "VARS" ? 2 : code == "VARX" ? 3 : code == "VARD" ? 4 : 5;
        change_variable(obtain_escape_params_array(ts), op);
        return;
    }
    rpg::GameSystem& sys = rpg::game_system();
    if (code == "EMP") {
        js::Val id = obtain_escape_param(ts);
        if (id.is_num() && id.to_num() == 0) sys.msg_sound_enable = true;
        if (id.is_num() && id.to_num() == 1) sys.msg_sound_enable = false;
        if (id.is_num() && id.to_num() == 2) sys.init_message_sounds();
        return;
    }
    if (code == "LSN") {
        sys.msg_sound_name = obtain_escape_string(ts);
        return;
    }
    if (code == "LSV") {
        sys.msg_sound_vol = obtain_escape_param(ts).to_num();
        return;
    }
    if (code == "LSPIV") {
        sys.msg_sound_pitch_var = obtain_escape_param(ts).to_num();
        return;
    }
    if (code == "LSPI") {
        sys.msg_sound_pitch = obtain_escape_param(ts).to_num();
        return;
    }
    if (code == "LSPAV") {
        sys.msg_sound_pan_var = obtain_escape_param(ts).to_num();
        sys.msg_sound_pan = obtain_escape_param(ts).to_num();
        return;
    }
    if (code == "LSPA") {
        sys.msg_sound_pan = obtain_escape_param(ts).to_num();
        return;
    }
    if (code == "LSI") {
        sys.msg_sound_interval = obtain_escape_param(ts).to_int();
        return;
    }
    if (code == "LSRESET") {
        sys.init_message_sounds();
        return;
    }
    if (code == "!") {
        if (!is_fast_forward()) start_pause();
        return;
    }
    if (code == "W") start_wait(obtain_escape_param_int(ts));
    if (code == "$") {
        gold_window->open();
    } else if (code == ".") {
        start_wait(15);
    } else if (code == "|") {
        start_wait(60);
    } else if (code == ">") {
        line_show_fast_ = true;
    } else if (code == "<") {
        line_show_fast_ = false;
    } else if (code == "^") {
        pause_skip_ = true;
    } else {
        WindowBase::process_escape_character(code, ts);
    }
}

void WindowMessage::change_variable(const std::vector<std::string>& d, int operation) {
    if (d.size() < 2) return;
    int var_id = (int)rpg::js_parse_int(d[0]);
    double old_val = rpg::game_variables().number(var_id);
    double val = rpg::js_parse_int(d[1]);
    switch (operation) {
    case 0: rpg::game_variables().set_number(var_id, val); break;
    case 1: rpg::game_variables().set_number(var_id, old_val + val); break;
    case 2: rpg::game_variables().set_number(var_id, old_val - val); break;
    case 3: rpg::game_variables().set_number(var_id, old_val * val); break;
    case 4: rpg::game_variables().set_number(var_id, old_val / val); break;
    case 5: rpg::game_variables().set_number(var_id, std::fmod(old_val, val)); break;
    }
}

void WindowMessage::start_wait(int count) {
    if (checking_width_) return;
    if (check_word_wrap_mode_) return;
    wait_count_ = count;
    if (is_fast_forward()) wait_count_ = 0;
    sound_count_ = 0;
}

void WindowMessage::start_pause() {
    if (checking_width_) return;
    if (check_word_wrap_mode_) return;
    start_wait(10);
    pause = true;
}

double WindowMessage::wordwrap_width() {
    if (param("YEP_MessageCore", "Tight Wrap", "false") == "true" && !rpg::game_message().face_name.empty()) return c().width() - new_line_x();
    return WindowBase::wordwrap_width() - 10;
}

void WindowMessage::adjust_window_settings() {
    set_width(window_width());
    set_height(std::min(window_height(), 480.0));
    if (std::fabs(480 - height()) < line_height()) set_height(480);
    create_contents();
    x = (640 - width()) / 2;
}

bool WindowMessage::is_fast_forward() {
    if (!rpg::game_system().fast_forward) return false;
    return input::is_pressed(input::key_of(param("YEP_MessageCore", "Fast Forward Key", "pagedown")));
}

std::string WindowMessage::convert_escape_characters(std::string text) {
    if (hooks.convert_escape_characters) text = hooks.convert_escape_characters(text);
    text = WindowBase::convert_escape_characters(text);
    text = convert_name_box(text);
    text = convert_message_characters(text);
    return text;
}

std::string WindowMessage::convert_name_box(std::string text) {
    text = convert_message_positions(text);
    text = convert_letter_sounds(text);
    text = convert_extra_name_box_effects(text);
    WindowNameBox* nw = g_name_window ? g_name_window : name_window.get();
    struct P {
        const char* pat;
        int pos;
    };
    static const P pats[] = {{"N\\<(.*?)\\>", 1}, {"N1\\<(.*?)\\>", 1}, {"N2\\<(.*?)\\>", 2}, {"N3\\<(.*?)\\>", 3},
                             {"NC\\<(.*?)\\>", 3}, {"N4\\<(.*?)\\>", 4}, {"N5\\<(.*?)\\>", 5}, {"NR\\<(.*?)\\>", 5}};
    for (const P& p : pats) {
        text = re::replace_fn(esc(p.pat).c_str(), "gi", text, [&](const re::Groups& g) { return nw->refresh(g[1], p.pos); });
    }
    return text;
}

std::string WindowMessage::convert_extra_name_box_effects(std::string text) {
    WindowNameBox* nw = g_name_window ? g_name_window : name_window.get();
    struct P {
        const char* pat;
        int pos;
    };
    static const P dimmed[] = {{"ND\\<(.*?)\\>", 1}, {"ND1\\<(.*?)\\>", 1}, {"ND2\\<(.*?)\\>", 2}, {"ND3\\<(.*?)\\>", 3},
                               {"NDC\\<(.*?)\\>", 3}, {"ND4\\<(.*?)\\>", 4}, {"ND5\\<(.*?)\\>", 5}, {"NDR\\<(.*?)\\>", 5}};
    static const P transparent[] = {{"NT\\<(.*?)\\>", 1}, {"NT1\\<(.*?)\\>", 1}, {"NT2\\<(.*?)\\>", 2}, {"NT3\\<(.*?)\\>", 3},
                                    {"NTC\\<(.*?)\\>", 3}, {"NT4\\<(.*?)\\>", 4}, {"NT5\\<(.*?)\\>", 5}, {"NTR\\<(.*?)\\>", 5}};
    for (const P& p : dimmed) {
        text = re::replace_fn(esc(p.pat).c_str(), "gi", text, [&](const re::Groups& g) { return nw->refresh_dimmed(g[1], p.pos); });
    }
    for (const P& p : transparent) {
        text = re::replace_fn(esc(p.pat).c_str(), "gi", text, [&](const re::Groups& g) { return nw->refresh_transparent(g[1], p.pos); });
    }
    return text;
}

std::string WindowMessage::convert_letter_sounds(std::string text) {
    text = re::replace_all(esc("LSON").c_str(), "gi", text, esc("EMP[0]"));
    text = re::replace_all(esc("LSOFF").c_str(), "gi", text, esc("EMP[1]"));
    text = re::replace_all(esc("LSR").c_str(), "gi", text, esc("EMP[2]"));
    return text;
}

std::string WindowMessage::convert_message_positions(std::string text) {
    rpg::GameSystem& sys = rpg::game_system();
    auto pos_value = [](const std::string& s) -> js::Val {
        if (re::search("auto", "i", s)) return js::Val::str("auto");
        return js::Val::num(rpg::js_parse_int(s));
    };
    text = re::replace_fn(esc("MSGPOSX\\[(.*?)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        needs_message_reset_ = true;
        sys.msg_position_x = pos_value(g[1]);
        return std::string();
    });
    text = re::replace_fn(esc("MSGPOSY\\[(.*?)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        needs_message_reset_ = true;
        sys.msg_position_y = pos_value(g[1]);
        return std::string();
    });
    text = re::replace_fn(esc("MSGEVENT\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (!rpg::party_in_battle()) {
            needs_message_reset_ = true;
            set_message_position_event(rpg::js_parse_int(g[1]));
        }
        return std::string();
    });
    text = re::replace_fn(esc("MSGACTOR\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (rpg::party_in_battle()) {
            needs_message_reset_ = true;
            set_message_position_event(rpg::js_parse_int(g[1]));
        }
        return std::string();
    });
    auto party_actor = [](const std::string& s) -> double {
        int idx = std::max(0, (int)rpg::js_parse_int(s) - 1);
        auto all = rpg::game_party().all_members();
        if ((size_t)idx < all.size() && all[(size_t)idx]) return all[(size_t)idx]->actor_id();
        return 480.0 * 495;
    };
    text = re::replace_fn(esc("MSGPARTY\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (rpg::party_in_battle()) {
            needs_message_reset_ = true;
            set_message_position_event(party_actor(g[1]));
        }
        return std::string();
    });
    text = re::replace_fn(esc("MSGENEMY\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (rpg::party_in_battle()) {
            needs_message_reset_ = true;
            int enemy_id = std::max(0, (int)rpg::js_parse_int(g[1]) - 1);
            if ((size_t)enemy_id < rpg::game_troop().members().size()) set_message_position_event(-enemy_id);
        }
        return std::string();
    });
    auto fit = [&](const std::string& t) {
        checking_width_ = true;
        get_fitted_message_rows(t);
        double value = get_fitted_message_width(t);
        sys.message_width_expr = js::number_to_string(value);
        checking_width_ = false;
    };
    text = re::replace_fn(esc("AUTOEVENT\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (!rpg::party_in_battle() && !checking_width_) {
            needs_message_reset_ = true;
            set_message_position_event(rpg::js_parse_int(g[1]));
            fit(text);
        }
        return std::string();
    });
    text = re::replace_fn(esc("AUTOACTOR\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (rpg::party_in_battle() && !checking_width_) {
            needs_message_reset_ = true;
            set_message_position_event(rpg::js_parse_int(g[1]));
            fit(text);
        }
        return std::string();
    });
    text = re::replace_fn(esc("AUTOPARTY\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (rpg::party_in_battle() && !checking_width_) {
            needs_message_reset_ = true;
            set_message_position_event(party_actor(g[1]));
            fit(text);
        }
        return std::string();
    });
    text = re::replace_fn(esc("AUTOENEMY\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (rpg::party_in_battle() && !checking_width_) {
            needs_message_reset_ = true;
            int enemy_id = std::max(0, (int)rpg::js_parse_int(g[1]) - 1);
            if ((size_t)enemy_id < rpg::game_troop().members().size()) set_message_position_event(-enemy_id);
            fit(text);
        }
        return std::string();
    });
    text = re::replace_fn(esc("MSGROWS\\[(.*?)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (!checking_width_) {
            checking_width_ = true;
            needs_message_reset_ = true;
            if (re::search("auto", "i", g[1])) get_fitted_message_rows(text);
            else sys.message_rows_expr = js::number_to_string(rpg::js_parse_int(g[1]));
            checking_width_ = false;
        }
        return std::string();
    });
    text = re::replace_fn(esc("MSGWIDTH\\[(.*?)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        if (!checking_width_) {
            checking_width_ = true;
            needs_message_reset_ = true;
            double value = re::search("auto", "i", g[1]) ? get_fitted_message_width(text) : rpg::js_parse_int(g[1]);
            sys.message_width_expr = js::number_to_string(value);
            checking_width_ = false;
        }
        return std::string();
    });
    text = re::replace_fn(esc("AUTO").c_str(), "gi", text, [&](const re::Groups&) {
        if (!checking_width_) {
            checking_width_ = true;
            needs_message_reset_ = true;
            get_fitted_message_rows(text);
            double value = get_fitted_message_width(text);
            sys.message_width_expr = js::number_to_string(value);
            checking_width_ = false;
        }
        return std::string();
    });
    text = re::replace_fn(esc("MSGRESET").c_str(), "gi", text, [&](const re::Groups&) {
        message_position_reset();
        needs_message_reset_ = true;
        return std::string();
    });
    return text;
}

void WindowMessage::get_fitted_message_rows(const std::string& text) {
    double h = text_height_ex(text);
    rpg::game_system().message_rows_expr = js::number_to_string(h / line_height());
}

double WindowMessage::get_fitted_message_width(const std::string& text) {
    double value = 0;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        value = std::max(value, text_width_ex(line));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    value += new_line_x();
    value += standard_padding() * 2;
    value += text_padding();
    return value;
}

void WindowMessage::message_position_reset() {
    rpg::GameSystem& sys = rpg::game_system();
    sys.init_message_position();
    sys.message_rows_expr = param("YEP_MessageCore", "Default Rows", "4");
    sys.message_width_expr = param("YEP_MessageCore", "Default Width", "Graphics.boxWidth");
    needs_message_reset_ = false;
}

std::string WindowMessage::convert_message_characters(std::string text) {
    text = re::replace_fn(esc("AF\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        rpg::GameActor* a = rpg::game_actors().actor((int)rpg::js_parse_int(g[1]));
        if (a) rpg::game_message().set_face_image(a->face_name(), a->face_index());
        return std::string();
    });
    text = re::replace_fn(esc("PF\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        auto m = rpg::game_party().members();
        int i = (int)rpg::js_parse_int(g[1]) - 1;
        rpg::GameActor* a = i >= 0 && (size_t)i < m.size() ? m[(size_t)i] : nullptr;
        if (a) rpg::game_message().set_face_image(a->face_name(), a->face_index());
        return std::string();
    });
    text = convert_face_index_change(text);
    return text;
}

std::string WindowMessage::convert_face_index_change(std::string text) {
    return re::replace_fn(esc("FACEINDEX\\[(\\d+)\\]").c_str(), "gi", text, [&](const re::Groups& g) {
        rpg::game_message().face_index = (int)rpg::js_parse_int(g[1]);
        return std::string();
    });
}

bool WindowMessage::has_different_name_box_text() {
    const std::vector<std::string>& texts = rpg::game_message().texts;
    bool open = name_window->is_open();
    bool has_name = false;
    std::string name;
    for (std::string text : texts) {
        if (text.empty()) continue;
        text = convert_macro_text(text);
        for (auto& ch : text) {
            if (ch == ESC) ch = '\\';
        }
        re::Groups g;
        if (re::search("\\\\(?:N|N1|N2|N3|N4|N5|NC|NR)<(.*)>", "i", text, &g) ||
            re::search("\\\\(?:ND|ND1|ND2|ND3|ND4|ND5|NDC|NDR)<(.*)>", "i", text, &g) ||
            re::search("\\\\(?:NT|NT1|NT2|NT3|NT4|NT5|NTC|NTR)<(.*)>", "i", text, &g)) {
            name = g[1];
            has_name = true;
        }
        if (has_name && !name.empty()) {
            name = re::replace_fn("\\\\V\\[(\\d+)\\]", "gi", name, [](const re::Groups& m) { return rpg::game_variables().value(m.num(1)).to_str(); });
            name = re::replace_fn("\\\\V\\[(\\d+)\\]", "gi", name, [](const re::Groups& m) { return rpg::game_variables().value(m.num(1)).to_str(); });
            name = re::replace_fn("\\\\N\\[(\\d+)\\]", "gi", name, [this](const re::Groups& m) { return actor_name(m.num(1)); });
            name = re::replace_fn("\\\\P\\[(\\d+)\\]", "gi", name, [this](const re::Groups& m) { return party_member_name(m.num(1)); });
            for (auto& ch : name) {
                if (ch == '\\') ch = ESC;
            }
        }
        bool truthy = has_name && !name.empty();
        if (truthy && !open) return true;
        if (truthy && name != name_window->last_name_text) return true;
    }
    if (open && !(has_name && !name.empty())) return true;
    return false;
}

void WindowMessage::update_position_placement() {
    rpg::GameSystem& sys = rpg::game_system();
    if (!(sys.msg_position_x.is_str() && sys.msg_position_x.to_str() == "auto")) update_position_placement_x();
    if (!(sys.msg_position_y.is_str() && sys.msg_position_y.to_str() == "auto")) update_position_placement_y();
}

void WindowMessage::update_position_placement_x() {
    rpg::GameSystem& sys = rpg::game_system();
    x = sys.msg_position_x.to_num();
    x -= std::floor(width() * sys.msg_anchor_x);
    x = std::max(0.0, x);
    x = std::min(x, 640 - width());
}

void WindowMessage::update_position_placement_y() {
    rpg::GameSystem& sys = rpg::game_system();
    y = sys.msg_position_y.to_num();
    y -= std::floor(height() * sys.msg_anchor_y);
    y = std::max(0.0, y);
    y = std::min(y, 480 - height());
}

void WindowMessage::update_locked_position() {
    double event_id = lock_position_to_event_;
    double px = 640 / 2.0;
    double py = 480;
    if (rpg::party_in_battle()) {
        rpg::GameBattler* battler = nullptr;
        if (event_id > 0) {
            rpg::GameActor* a = rpg::game_actors().actor((int)event_id);
            auto bm = rpg::game_party().battle_members();
            if (a && std::find(bm.begin(), bm.end(), a) != bm.end()) battler = a;
        } else {
            int idx = (int)(-event_id);
            auto members = rpg::game_troop().members();
            if (idx >= 0 && (size_t)idx < members.size()) battler = members[(size_t)idx];
        }
        if (battler) {
            px = battler->sprite_pos_x();
            py = battler->sprite_pos_y() - battler->sprite_height();
        }
    } else {
        if (event_id <= 0) {
            rpg::GamePlayer& p = rpg::game_player();
            px = p.screen_x();
            py = p.screen_y() - p.sprite_height();
        } else {
            rpg::GameEvent* ev = rpg::game_map().event((int)event_id);
            if (ev) {
                px = ev->screen_x();
                py = ev->screen_y() - ev->sprite_height();
            }
        }
    }
    rpg::GameSystem& sys = rpg::game_system();
    sys.msg_position_x = js::Val::num(px);
    sys.msg_position_y = js::Val::num(py);
    update_position_placement();
    name_window->adjust_position_x();
    name_window->adjust_position_y();
}

void WindowMessage::update_open() {
    if (!opening_) return;
    WindowBase::update_open();
    double rate = openness() / 255.0;
    double d = std::max(rate * 20, 1.0);
    name_window->x = (name_window->x * (d - 1) + 77) / d;
    if (battle_message_tail) {
        battle_message_tail->y = sprite_container()->y + 4;
        battle_message_tail->scale_y = rate;
    }
}

void WindowMessage::update_close() {
    if (!closing_) return;
    WindowBase::update_close();
    double rate = openness() / 255.0;
    double d = std::max(rate * 20, 1.0);
    name_window->x = (name_window->x * (d - 1) + -300) / d;
    face_box_window->set_openness(std::min(face_box_window->openness(), openness()));
    if (battle_message_tail) {
        battle_message_tail->y = sprite_container()->y + 4;
        battle_message_tail->scale_y = rate;
    }
}

void WindowMessage::refresh_pause_sign() {
    pause_sign_->set_bitmap(load_system("cursor_menu"));
    pause_sign_->anchor_x = 0.5;
    pause_sign_->anchor_y = 1;
    pause_sign_->alpha = 0;
    pause_sign_->move(width_ - 40, height_ - 10);
}

void WindowMessage::update_pause_sign() {
    if (!pause) pause_sign_->alpha = 0;
    else if (pause_sign_->alpha < 1) pause_sign_->alpha = std::min(pause_sign_->alpha + 0.1, 1.0);
    pause_sign_->visible = is_open();
    pause_sign_->anchor_x = 0.5 + std::sin(animation_count * 0.1) * 0.1;
}

std::shared_ptr<WindowMessage> create_message_window(rpg::Scene& scene) {
    auto w = std::shared_ptr<WindowMessage>(new WindowMessage());
    w->initialize(scene);
    w->refresh_pause_sign();
    scene.add_window(w);
    scene.add_window(w->gold_window);
    scene.add_window(w->choice_window);
    scene.add_window(w->number_window);
    scene.add_window(w->item_window);
    g_message_window = w.get();
    return w;
}

std::shared_ptr<WindowScrollText> create_scroll_text_window(rpg::Scene& scene) {
    auto w = std::shared_ptr<WindowScrollText>(new WindowScrollText());
    w->initialize();
    scene.add_window(w);
    return w;
}

WindowMessage* current_message_window() { return g_message_window; }

void forget_message_window(WindowMessage* w) {
    if (g_message_window == w) g_message_window = nullptr;
    if (w && g_name_window == w->name_window.get()) g_name_window = nullptr;
}

bool message_window_is_closing(WindowMessage* w) { return w && w->is_closing(); }
bool message_window_is_open(WindowMessage* w) { return w && w->is_open(); }
bool message_window_is_any_sub_window_active(WindowMessage* w) { return w && w->is_any_sub_window_active(); }
void message_window_terminate(WindowMessage* w) {
    if (w) w->terminate_message();
}
g2d::Window* message_window_as_window(WindowMessage* w) { return w; }
std::shared_ptr<g2d::Sprite> message_face_box_container(WindowMessage* w) { return w ? w->face_box_container : nullptr; }

void set_message_hooks(WindowMessage* w, MessageHooks hooks) {
    if (w) w->hooks = std::move(hooks);
}

WindowBase* message_window_base(WindowMessage* w) { return w; }

void message_window_draw_text_page(WindowMessage* w, const std::string& text, double x, double y) {
    if (!w) return;
    w->text_state = TextState();
    w->text_state.index = 0;
    w->text_state.text = w->convert_escape_characters(text);
    w->has_text_state = true;
    w->new_page(w->text_state);
    w->text_state.x = x;
    w->text_state.y = y;
    w->show_fast_ = true;
    w->pause_skip_ = true;
    w->word_wrap_ = true;
    w->pause = false;
    w->sound_count_ = 99;
    w->update_message();
}

void message_window_reopen_name_box(WindowMessage* w) {
    if (w && w->name_window && w->name_window->openness() > 0) w->name_window->open();
}

}
