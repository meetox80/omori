#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/random.h"
#include "../js/vm.h"
#include "../rpg/audio_manager.h"
#include "../rpg/config.h"
#include "../rpg/data.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/save.h"
#include "boot.h"
#include "build_info.h"
#include "fx.h"
#include "menu_common.h"
#include "menu_options.h"
#include "scenes.h"
#include "ui_util.h"
#include "window_selectable.h"

namespace ui {

namespace {

using menu::ObjectMovement;

class WindowOmoTitleScreenBox : public WindowSelectable {
public:
    void initialize(const std::string& text) {
        text_ = text;
        WindowSelectable::initialize(0, 0, 160 - 30, 30);
        set_width(text_width(text_) + standard_padding() * 6);
        create_contents();
        set_window_opacity(0);
        set_contents_opacity(0);
        enabled_ = true;
        refresh();
        activate();
    }
    using WindowSelectable::initialize;

    int standard_padding() override { return 4; }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double custom_cursor_rect_x_offset() override { return -35; }
    double custom_cursor_rect_y_offset() override { return -7; }

    void set_text(const std::string& text, bool enabled = true) {
        if (text != text_ || enabled != enabled_) {
            enabled_ = enabled;
            text_ = text;
            refresh();
        }
    }

    void refresh() override {
        WindowSelectable::refresh();
        c().clear();
        const Value& loc = menu::blue("Window_OmoTitleScreenBox");
        c().font_size = loc["refresh_contents_fontsize"].as_int();
        change_paint_opacity(enabled_);
        const Value& pos = loc["refresh_drawText_position"];
        draw_text(text_, pos[0].num(), pos[1].num(), c().width(), "center");
    }

protected:
    void refresh_arrows() override {}

private:
    std::string text_;
    bool enabled_ = true;
};

struct FrameAnimation {
    g2d::Sprite* sprite = nullptr;
    double rect_x = 0, rect_y = 0, rect_w = 0, rect_h = 0;
    std::vector<int> frames;
    int frame_index = 0;
    int delay_count = 0;
    int delay = 0;
    bool active = true;
};

struct GlitchSettings {
    int timer = 0;
    int timing = 240;
    int max_timing = 240;
    int times = 5;
    int world = 2;
    bool active = false;
};

bool valid_world(double w) {
    static const int list[] = {0, 444, 445, 446, 447, 448, 449};
    for (int v : list) {
        if (w == v) return true;
    }
    return false;
}

double read_world_file(const char* name, bool& found) {
    std::string text;
    found = rpg::save::read_text(name, text) && !text.empty();
    return found ? rpg::js_parse_int(text) : std::nan("");
}

rpg::AudioObj audio(const std::string& name, double volume) {
    rpg::AudioObj a;
    a.valid = true;
    a.name = name;
    a.volume = volume;
    a.pitch = 100;
    a.pan = 0;
    return a;
}

class SceneOmoriTitleScreen : public menu::SceneBaseEX {
public:
    SceneOmoriTitleScreen() {
        bool has_title_data = false, has_file = false;
        double title_data = read_world_file("TITLEDATA", has_title_data);
        double file = has_title_data ? std::nan("") : read_world_file("File", has_file);
        double world = 0;
        if (has_title_data) world = title_data;
        else if (has_file) world = file;
        world_type_ = valid_world(world) ? (int)world : 0;
        command_active_ = false;
        options_active_ = false;
        command_index_ = 0;
        instant_intro_ = false;
        can_continue_ = false;
        for (int i = 1; i < 7; i++) {
            if (rpg::save::exists(i)) {
                can_continue_ = true;
                break;
            }
        }
        instant_intro_ = true;
    }

    const char* name() const override { return "Scene_OmoriTitleScreen"; }

    void prepare(int index, bool instant) {
        command_index_ = index;
        instant_intro_ = instant;
    }

    void create() override {
        menu::SceneBaseEX::create();
        create_filters();
        create_background();
        create_omori_sprite();
        create_title_sprites();
        create_title_commands();
        create_command_hints();
        create_version_text();
        create_option_windows_container();
        create_help_window();
        create_general_options_window();
        create_audio_options_window();
        create_system_options_window();
        create_exit_prompt_window();
        create_option_categories_window();
        update_world_bitmaps(world_type_);
        play_bgm();
        play_bgs();
        if (can_continue_) command_index_ = 1;
        update_command_window_selection();
    }

    void start() override {
        menu::SceneBaseEX::start();
        init_frame_animations();
        if (instant_intro_) {
            title_text_container_->set_opacity(255);
            title_text_container_->y = -30;
            title_text_sprite_->set_opacity(255);
            if (omori_sprite_) omori_sprite_->set_opacity(255);
            light_bulb_lines_sprite_->set_opacity(255);
            for (auto& win : title_commands_) {
                win->y = (480 - win->height()) - 15;
                win->set_window_opacity(255);
                win->set_contents_opacity(255);
            }
            command_active_ = true;
            if (omori_sprite_) frame_animations_[1].active = true;
            else frame_animations_[0].active = true;
            glitch_.active = world_type_ == 445;
            return;
        }
        queue([this] {
            int duration = 60;
            g2d::Sprite* obj = title_text_container_.get();
            move.start_move(ObjectMovement::tween(obj, {{menu::prop_opacity(obj), 255, duration}}, menu::linear_tween));
            if (omori_sprite_) frame_animations_[1].active = true;
            else frame_animations_[0].active = true;
        });
        queue_wait_mode("movement");
        queue_wait(15);
        queue([this] {
            int duration = 60;
            g2d::Sprite* obj = title_text_sprite_.get();
            move.start_move(ObjectMovement::tween(obj, {{menu::prop_opacity(obj), 255, duration}}, menu::linear_tween));
            g2d::Sprite* lines = light_bulb_lines_sprite_.get();
            move.start_move(ObjectMovement::tween(lines, {{menu::prop_opacity(lines), 255, duration}}, menu::linear_tween));
        });
        queue_wait_mode("movement");
        queue_wait(30);
        queue([this] {
            int duration = 60;
            g2d::Sprite* obj = title_text_container_.get();
            move.start_move(ObjectMovement::tween(obj, {{menu::prop_y(obj), -30, duration}}, menu::linear_tween));
        });
        queue_wait_mode("movement");
        queue_wait(30);
        if (omori_sprite_) {
            queue([this] {
                int duration = 60;
                g2d::Sprite* obj = omori_sprite_.get();
                move.start_move(ObjectMovement::tween(obj, {{menu::prop_opacity(obj), 255, duration}}, menu::linear_tween));
            });
            queue_wait(30);
        }
        for (size_t i = 0; i < title_commands_.size(); i++) {
            queue([this, i] {
                int duration = 30;
                WindowOmoTitleScreenBox* obj = title_commands_[i].get();
                obj->select(-1);
                move.start_move(ObjectMovement::tween(obj,
                                                      {{menu::prop_y(obj), (480 - obj->height()) - 22, duration},
                                                       {menu::prop_window_opacity(obj), 255, duration},
                                                       {menu::prop_contents_opacity(obj), 255, duration}},
                                                      menu::ease_out_circ));
            });
            queue_wait(15);
        }
        queue([this] {
            glitch_.active = world_type_ == 445;
            command_active_ = true;
            update_command_window_selection();
        });
    }

    void update() override {
        menu::SceneBaseEX::update();
        update_frame_animations();
        update_command_input();
        update_effects();
        if (command_hints_ && hints_circle_ != rpg::confirm_on_circle()) refresh_command_hints();
        background_sprite_->origin_x += 0.5;
        background_sprite_->origin_y -= 0.5;
        if (omori_sprite_) horror_.update(*omori_sprite_);
    }

    void terminate() override {
        menu::SceneBaseEX::terminate();
        remove_children();
    }

private:
    int world_type_ = 0;
    bool command_active_ = false;
    bool options_active_ = false;
    bool hints_circle_ = false;
    int command_index_ = 0;
    bool instant_intro_ = false;
    bool can_continue_ = false;
    GlitchSettings glitch_;
    fx::Horror horror_;
    std::vector<FrameAnimation> frame_animations_;

    std::shared_ptr<g2d::TilingSprite> background_sprite_;
    std::shared_ptr<g2d::Sprite> omori_sprite_;
    std::shared_ptr<g2d::Sprite> title_text_container_;
    std::shared_ptr<g2d::Sprite> light_bulb_sprite_;
    std::shared_ptr<g2d::Sprite> light_bulb_lines_sprite_;
    std::shared_ptr<g2d::Sprite> lightbulb_cable_sprite_;
    std::shared_ptr<g2d::Sprite> title_text_sprite_;
    std::vector<std::shared_ptr<WindowOmoTitleScreenBox>> title_commands_;
    std::shared_ptr<g2d::Sprite> command_hints_;
    std::shared_ptr<g2d::Sprite> game_version_;
    std::shared_ptr<g2d::Sprite> options_windows_container_;
    std::shared_ptr<menu::WindowOmoMenuOptionsHelp> help_window_;
    std::shared_ptr<menu::WindowOmoMenuOptionsGeneral> general_options_window_;
    std::shared_ptr<menu::WindowOmoMenuOptionsAudio> audio_options_window_;
    std::shared_ptr<menu::WindowOmoMenuOptionsSystem> system_options_window_;
    std::shared_ptr<menu::WindowOmoMenuOptionsExitPromptWindow> exit_prompt_window_;
    std::shared_ptr<menu::WindowOmoMenuOptionsCategory> option_categories_window_;

    bool white_world() const {
        return world_type_ == 0 || world_type_ == 446 || world_type_ == 447 || world_type_ == 448 || world_type_ == 449;
    }

    void play_bgm() {
        std::string name = "user_title";
        if (world_type_ == 449 || world_type_ == 447) name = "duet_mari";
        else if (world_type_ == 448) name = "AMB_forest";
        else if (world_type_ == 446) name = "user_title";
        else if (world_type_ == 445) name = "bs_listening";
        else if (world_type_ == 444) name = "bs_listening";
        else name = "user_title";
        rpg::audio_manager::play_bgm(audio(name, 100));
    }

    void play_bgs() {
        std::string name;
        double volume = 0;
        if (world_type_ == 445) {
            name = "amb_kettle";
            volume = 90;
        } else if (world_type_ == 447 || world_type_ == 449) {
            name = "AMB_forest";
            volume = 50;
        }
        if (name.empty()) return;
        rpg::audio_manager::play_bgs(audio(name, volume));
    }

    void create_command_hints() {
        command_hints_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create((int)std::ceil(640 / 2.8), (int)std::ceil(480 / 8.0)));
        add_child(command_hints_);
        command_hints_->x = 640 - command_hints_->width();
        command_hints_->y = 0;
        refresh_command_hints();
    }

    void refresh_command_hints() {
        hints_circle_ = rpg::confirm_on_circle();
        g2d::Bitmap* bitmap = command_hints_->bitmap().get();
        bitmap->clear();
        const Value& hints = menu::plugin_data("optionsMenu", "buttonHints");
        std::string confirm = data::js_string(hints["confirm"]);
        std::string cancel = data::js_string(hints["cancel"]);
        double icon_size = 24;
        double padding_y = 4;
        const Value& loc = menu::blue("Scene_OmoriTitleScreen");
        bitmap->font_size = loc["commandHints_fontsize"].as_int();
        draw_key_icon(bitmap, input_key_code("ok"), 0, padding_y);
        js::Val self = js::make_object();
        js::Val hints_obj = js::make_object();
        js::Val bitmap_obj = js::make_object();
        js::vm().put(bitmap_obj, js::atom("measureTextWidth"),
                     js::make_native([bitmap](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
                         return js::Val::num(bitmap->measure_text_width(a.empty() ? std::string("undefined") : a[0].to_str()));
                     }));
        js::vm().put(hints_obj, js::atom("bitmap"), bitmap_obj);
        js::vm().put(self, js::atom("_commandHints"), hints_obj);
        std::initializer_list<std::pair<const char*, js::Val>> vars = {{"iconSize", js::Val::num(icon_size)},
                                                                       {"paddingY", js::Val::num(padding_y)},
                                                                       {"confirm", js::Val::str(confirm)},
                                                                       {"cancel", js::Val::str(cancel)}};
        const Value& confirm_pos = loc["commandHints_confirm_position"];
        bitmap->draw_text(confirm, menu::lang_num(confirm_pos[0], vars, self), menu::lang_num(confirm_pos[1], vars, self), bitmap->width(), 16,
                          "left");
        draw_key_icon(bitmap, input_key_code("escape"), icon_size + 15 + bitmap->measure_text_width(confirm), padding_y);
        const Value& cancel_pos = loc["commandHints_cancel_position"];
        bitmap->draw_text(cancel, menu::lang_num(cancel_pos[0], vars, self), menu::lang_num(cancel_pos[1], vars, self), bitmap->width(), 16,
                          "left");
    }

    void create_version_text() {
        std::vector<std::string> lines = {"V1.0.8"};
        if (*OMORI_COMMIT) lines.push_back(std::string("v") + OMORI_COMMIT);
        lines.push_back("ported by kotwicha");
        double padding = 4;
        double line_height = 20;
        game_version_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create(240, (int)std::ceil(padding * 2 + 12 + line_height * lines.size())));
        add_child(game_version_);
        const Value& loc = menu::blue("Scene_OmoriTitleScreen");
        const Value& pos = loc["gameVersion_position"];
        game_version_->x = pos[0].num();
        game_version_->y = pos[1].num();
        g2d::Bitmap* bitmap = game_version_->bitmap().get();
        bitmap->font_size = loc["gameVersion_fontsize"].as_int();
        for (size_t i = 0; i < lines.size(); i++) bitmap->draw_text(lines[i], padding, padding + line_height * i, bitmap->width(), 16, "left");
    }

    void create_background() {
        background_sprite_ = std::make_shared<g2d::TilingSprite>();
        background_sprite_->move(0, 0, 640, 480);
        add_child(background_sprite_);
        if (world_type_ == 448 || world_type_ == 447 || world_type_ == 449) {
            background_sprite_->bitmap = load_parallax("!polaroidBG_FA_day");
        } else if (world_type_ == 446) {
            g2d::BitmapPtr bitmap = g2d::Bitmap::create(640, 480);
            bitmap->fill_all("rgba(255, 255, 255, 1)");
            background_sprite_->bitmap = bitmap;
        } else if (world_type_ == 445) {
            background_sprite_->bitmap = load_parallax("!parallax_black_space");
        } else if (world_type_ == 444) {
            g2d::BitmapPtr bitmap = g2d::Bitmap::create(640, 480);
            bitmap->fill_all("rgba(0, 0, 0, 1)");
            background_sprite_->bitmap = bitmap;
        } else if (world_type_ == 0) {
            g2d::BitmapPtr bitmap = g2d::Bitmap::create(640, 480);
            bitmap->fill_all("rgba(255, 255, 255, 1)");
            background_sprite_->bitmap = bitmap;
        }
    }

    void create_filters() { glitch_ = GlitchSettings(); }

    void create_omori_sprite() {
        if (world_type_ == 448) return;
        if (world_type_ == 0 || world_type_ == 446) omori_sprite_ = std::make_shared<g2d::Sprite>(load_picture("OMO_WS"));
        else if (world_type_ == 444) omori_sprite_ = std::make_shared<g2d::Sprite>(load_picture("OMO_BS"));
        else omori_sprite_ = std::make_shared<g2d::Sprite>(load_picture("OMO_RS"));
        omori_sprite_->anchor_x = 0.5;
        omori_sprite_->anchor_y = 1;
        omori_sprite_->x = 640 / 2.0;
        omori_sprite_->y = 480;
        omori_sprite_->set_opacity(0);
        omori_sprite_->set_frame(0, 0, 0, 0);
        omori_sprite_->visible = world_type_ != 4;
        add_child(omori_sprite_);
    }

    void create_title_sprites() {
        title_text_container_ = std::make_shared<g2d::Sprite>();
        title_text_container_->x = 167;
        title_text_container_->y = 130;
        title_text_container_->set_opacity(0);
        add_child(title_text_container_);
        light_bulb_sprite_ = std::make_shared<g2d::Sprite>(load_picture(white_world() ? "OMO_BULB_WS" : "OMO_BULB_BS"));
        light_bulb_sprite_->x = 120;
        light_bulb_sprite_->y = -2;
        title_text_container_->add_child(light_bulb_sprite_);
        light_bulb_lines_sprite_ = std::make_shared<g2d::Sprite>(load_picture(white_world() ? "OMO_BULB_WS_LINES" : "OMO_BULB_BS_LINES"));
        light_bulb_lines_sprite_->x = 120;
        light_bulb_lines_sprite_->y = 0;
        light_bulb_lines_sprite_->set_opacity(0);
        light_bulb_lines_sprite_->set_frame(0, 0, 68, 150);
        title_text_container_->add_child(light_bulb_lines_sprite_);
        g2d::BitmapPtr string = load_picture(white_world() ? "OMO_STRING_WS" : "OMO_STRING_BS");
        g2d::BitmapPtr bitmap = g2d::Bitmap::create(68, 300);
        bitmap->blt(string.get(), 0, 0, string->width(), string->height(), 0, 0, string->width(), 300);
        lightbulb_cable_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        lightbulb_cable_sprite_->x = light_bulb_sprite_->x;
        lightbulb_cable_sprite_->y = -282;
        title_text_container_->add_child(lightbulb_cable_sprite_);
        title_text_sprite_ = std::make_shared<g2d::Sprite>(load_picture(white_world() ? "OMO_TITLE_WS" : "OMO_TITLE_BS"));
        title_text_sprite_->set_opacity(0);
        title_text_container_->add_child(title_text_sprite_);
    }

    void create_title_commands() {
        title_commands_.clear();
        const Value& text_list = menu::blue("Omori_Title_Screen")["commands"];
        int count = (int)text_list.size();
        double center_x = std::floor((640 - (156.0 * count)) / 1.8);
        for (int i = 0; i < count; i++) {
            std::string text = data::js_string(text_list[i]);
            auto win = WindowBase::make<WindowOmoTitleScreenBox>(text);
            win->x = center_x + (i * (130 + 39));
            win->y = 480;
            if (i == command_index_) win->select(0);
            title_commands_.push_back(win);
            add_child(win);
        }
        if (title_commands_.size() > 1) title_commands_[1]->set_text(data::js_string(text_list[1]), can_continue_);
    }

    void create_option_windows_container() {
        options_windows_container_ = std::make_shared<g2d::Sprite>();
        options_windows_container_->x = 10;
        options_windows_container_->y = -406;
        options_windows_container_->set_opacity(255);
        add_child(options_windows_container_);
    }

    void create_help_window() {
        help_window_ = WindowBase::make<menu::WindowOmoMenuOptionsHelp>();
        help_window_->x = 0;
        help_window_->y = 480 - help_window_->height() - 10;
        options_windows_container_->add_child(help_window_);
    }

    void create_general_options_window() {
        general_options_window_ = WindowBase::make<menu::WindowOmoMenuOptionsGeneral>();
        general_options_window_->set_handler("cancel", [this] { on_option_window_cancel(); });
        general_options_window_->deactivate();
        help_window_->y = general_options_window_->y + general_options_window_->height();
        options_windows_container_->add_child(general_options_window_);
    }

    void create_audio_options_window() {
        audio_options_window_ = WindowBase::make<menu::WindowOmoMenuOptionsAudio>();
        audio_options_window_->set_handler("cancel", [this] { on_option_window_cancel(); });
        audio_options_window_->visible = false;
        options_windows_container_->add_child(audio_options_window_);
    }

    void create_system_options_window() {
        system_options_window_ = WindowBase::make<menu::WindowOmoMenuOptionsSystem>(0.0, 0.0);
        system_options_window_->set_handler("restoreConfig", [this] {
            menu::restore_default_config();
            general_options_window_->make_options_list();
            audio_options_window_->make_options_list();
            general_options_window_->refresh();
            general_options_window_->select(0);
            audio_options_window_->refresh();
            system_options_window_->refresh();
            system_options_window_->activate();
            input::clear();
        });
        system_options_window_->set_handler("cancel", [this] { on_option_window_cancel(); });
        system_options_window_->set_handler("exit", [this] { start_exit_prompt(); });
        options_windows_container_->add_child(system_options_window_);
    }

    void create_exit_prompt_window() {
        exit_prompt_window_ = WindowBase::make<menu::WindowOmoMenuOptionsExitPromptWindow>(0.0, 0.0);
        exit_prompt_window_->x = (640 - exit_prompt_window_->width()) / 2;
        exit_prompt_window_->y = (480 - exit_prompt_window_->height()) / 2;
        exit_prompt_window_->set_handler("yes", [this] { on_exit_prompt_yes(); });
        exit_prompt_window_->set_handler("cancel", [this] { on_exit_prompt_cancel(); });
        add_child(exit_prompt_window_);
    }

    void start_exit_prompt() {
        exit_prompt_window_->select(1);
        exit_prompt_window_->open();
        exit_prompt_window_->activate();
    }

    void on_exit_prompt_cancel() {
        exit_prompt_window_->deactivate();
        exit_prompt_window_->close();
        system_options_window_->activate();
    }

    void on_exit_prompt_yes() {
        fade_out_all();
        rpg::scene_manager::exit_game();
    }

    void create_option_categories_window() {
        option_categories_window_ = WindowBase::make<menu::WindowOmoMenuOptionsCategory>(0.0, 0.0);
        option_categories_window_->deactivate();
        option_categories_window_->set_handler("ok", [this] { on_category_ok(); });
        option_categories_window_->set_handler("cancel", [this] { on_category_cancel(); });
        options_windows_container_->add_child(option_categories_window_);
        std::vector<WindowSelectable*> windows = option_windows();
        option_categories_window_->option_windows.assign(windows.begin(), windows.end());
        for (WindowSelectable* w : windows) {
            w->y = option_categories_window_->y + option_categories_window_->height();
            w->help_window = help_window_.get();
        }
        help_window_->y = windows[0]->y + windows[0]->height();
        option_categories_window_->call_update_help_ex();
    }

    std::vector<WindowSelectable*> option_windows() {
        return {general_options_window_.get(), audio_options_window_.get(), system_options_window_.get()};
    }

    void init_frame_animations() {
        frame_animations_.clear();
        if (omori_sprite_) {
            FrameAnimation omori;
            omori.sprite = omori_sprite_.get();
            omori.rect_w = 306;
            omori.rect_h = 350;
            omori.frames = {0, 1, 2};
            omori.delay = 20;
            frame_animations_.push_back(omori);
        }
        FrameAnimation lines;
        lines.sprite = light_bulb_lines_sprite_.get();
        lines.rect_w = 68;
        lines.rect_h = 150;
        lines.frames = {0, 1, 2, 1};
        lines.delay = 15;
        frame_animations_.push_back(lines);
        update_frame_animations();
    }

    void update_world_bitmaps(int world) {
        std::string title_text_bitmap = "OMO_TITLE_WS";
        std::string lightbulb_bitmap = "OMO_BULB_WS";
        std::string string_bitmap = "OMO_STRING_WS";
        std::string omori_bitmap = "OMO_WS";
        std::string lines_bitmap = "OMO_BULB_WS_LINES";
        switch (world) {
            case 0:
                break;
            case 446:
                break;
            case 2:
                omori_bitmap = "OMO_RS";
                title_text_bitmap = "OMO_TITLE_BS";
                lightbulb_bitmap = "OMO_BULB_BS";
                string_bitmap = "OMO_STRING_BS";
                lines_bitmap = "OMO_BULB_BS_LINES";
                break;
            case 3:
                title_text_bitmap = "OMO_TITLE_BS";
                lightbulb_bitmap = "OMO_BULB_BS";
                string_bitmap = "OMO_STRING_BS";
                omori_bitmap = "OMO_WS";
                lines_bitmap = "OMO_BULB_BS_LINES";
                break;
            case 444:
                omori_bitmap = "OMO_BS";
                title_text_bitmap = "OMO_TITLE_BS";
                lightbulb_bitmap = "OMO_BULB_BS";
                string_bitmap = "OMO_STRING_BS";
                lines_bitmap = "OMO_BULB_BS_LINES";
                break;
            case 445:
                omori_bitmap = "OMO_RS";
                title_text_bitmap = "OMO_TITLE_BS";
                lightbulb_bitmap = "OMO_BULB_BS";
                string_bitmap = "OMO_STRING_BS";
                lines_bitmap = "OMO_BULB_BS_LINES";
                break;
            case 449:
            case 447:
                omori_bitmap = "OMO_RS";
                title_text_bitmap = "OMO_TITLE_WS";
                lightbulb_bitmap = "OMO_BULB_WS";
                string_bitmap = "OMO_STRING_WS";
                lines_bitmap = "OMO_BULB_WS_LINES";
                break;
            case 448:
                title_text_bitmap = "OMO_TITLE_WS";
                lightbulb_bitmap = "OMO_BULB_WS";
                string_bitmap = "OMO_STRING_WS";
                lines_bitmap = "OMO_BULB_WS_LINES";
                break;
        }
        if (omori_sprite_) omori_sprite_->set_bitmap(load_picture(omori_bitmap));
        title_text_sprite_->set_bitmap(load_picture(title_text_bitmap));
        light_bulb_sprite_->set_bitmap(load_picture(lightbulb_bitmap));
        light_bulb_lines_sprite_->set_bitmap(load_picture(lines_bitmap));
        if (omori_sprite_) omori_sprite_->set_width(306);
        if (omori_sprite_) omori_sprite_->set_height(350);
        light_bulb_lines_sprite_->set_width(68);
        light_bulb_lines_sprite_->set_height(150);
        g2d::BitmapPtr string = load_picture(string_bitmap);
        g2d::Bitmap* bitmap = lightbulb_cable_sprite_->bitmap().get();
        bitmap->clear();
        bitmap->blt(string.get(), 0, 0, string->width(), string->height(), 0, 0, string->width(), 300);
    }

    void set_glitch_slices(int slices) {
        if (!omori_sprite_) return;
        horror_.remove_glitch(*omori_sprite_);
        if (slices <= 0) return;
        horror_.create_glitch(*omori_sprite_);
        horror_.set_glitch_slices(slices);
        horror_.set_glitch_offset(100);
        horror_.set_glitch_animated(false);
    }

    void update_effects() {
        GlitchSettings& glitch = glitch_;
        if (glitch.active) {
            glitch.timing--;
            if (glitch.timing <= 0) {
                glitch.timer--;
                if (glitch.timer <= 0) {
                    glitch.timer = 3;
                    if (glitch.times % 2 == 0) set_glitch_slices(0);
                    else set_glitch_slices(10 + rng::below(10));
                    glitch.times--;
                    if (glitch.times <= 0) {
                        glitch.world = glitch.world == 3 ? 2 : 3;
                        update_world_bitmaps(glitch.world);
                        set_glitch_slices(0);
                        glitch.times = 5;
                        glitch.timing = glitch.max_timing;
                    }
                }
            }
        }
    }

    void update_command_input() {
        if (command_active_ && !move.is_moving()) {
            if (input::is_repeated(input::LEFT)) {
                if (command_index_ > 0) {
                    command_index_--;
                    play_se("sys_cursor1", 90, 100, 0);
                    update_command_window_selection();
                }
            }
            if (input::is_repeated(input::RIGHT)) {
                if (command_index_ < (int)title_commands_.size() - 1) {
                    command_index_++;
                    play_se("sys_cursor1", 90, 100, 0);
                    update_command_window_selection();
                }
            }
            if (input::is_triggered(input::OK)) {
                switch (command_index_) {
                    case 0:
                        play_se("SE_load", 90, 100, 0);
                        command_new_game();
                        command_active_ = false;
                        options_active_ = false;
                        break;
                    case 1:
                        if (can_continue_) {
                            play_se("SYS_select", 90, 100, 0);
                            command_continue();
                            command_active_ = false;
                            options_active_ = false;
                        } else {
                            play_buzzer();
                        }
                        break;
                    case 2:
                        play_se("SYS_select", 90, 100, 0);
                        command_options();
                        options_active_ = true;
                        command_active_ = false;
                        break;
                }
            }
        }
    }

    void update_command_window_selection() {
        for (size_t i = 0; i < title_commands_.size(); i++) {
            if ((int)i == command_index_) title_commands_[i]->select(0);
            else title_commands_[i]->deselect();
        }
    }

    void update_frame_animations() {
        for (FrameAnimation& anim : frame_animations_) {
            if (!anim.active) continue;
            if (anim.delay_count <= 0) {
                anim.frame_index = (anim.frame_index + 1) % (int)anim.frames.size();
                int frame = anim.frames[(size_t)anim.frame_index];
                anim.sprite->set_frame(frame * anim.rect_w, anim.rect_y, anim.rect_w, anim.rect_h);
                anim.delay_count = anim.delay;
            } else {
                anim.delay_count--;
            }
        }
    }

    void command_new_game() {
        setup_new_game();
        fade_out_all();
        rpg::scene_manager::goto_scene(map_scene());
    }

    void command_continue() { rpg::scene_manager::push(omori_file_scene(false, true)); }

    void command_options() {
        queue([this] {
            for (auto& win : title_commands_) {
                int duration = 15;
                WindowOmoTitleScreenBox* obj = win.get();
                move.start_move(ObjectMovement::tween(
                    obj,
                    {{menu::prop_y(obj), 480, duration}, {menu::prop_window_opacity(obj), 255, duration}, {menu::prop_contents_opacity(obj), 255, duration}},
                    menu::ease_out_circ));
            }
            int duration = 35;
            g2d::Sprite* obj = options_windows_container_.get();
            move.start_move(ObjectMovement::tween(obj, {{menu::prop_y(obj), 37, duration}, {menu::prop_opacity(obj), 255, duration}}, menu::ease_out_circ));
        });
        queue_wait_mode("movement");
        queue([this] { option_categories_window_->activate(); });
    }

    void on_category_ok() {
        std::vector<WindowSelectable*> windows = option_windows();
        int index = option_categories_window_->index();
        WindowSelectable* category_window = index >= 0 && index < (int)windows.size() ? windows[(size_t)index] : nullptr;
        if (category_window) {
            category_window->activate();
            category_window->select(0);
            category_window->refresh();
        } else {
            option_categories_window_->activate();
        }
    }

    void on_category_cancel() {
        rpg::config_save();
        queue([this] {
            for (auto& win : title_commands_) {
                int duration = 15;
                WindowOmoTitleScreenBox* obj = win.get();
                move.start_move(ObjectMovement::tween(obj,
                                                      {{menu::prop_y(obj), (480 - obj->height()) - 22, duration},
                                                       {menu::prop_window_opacity(obj), 255, duration},
                                                       {menu::prop_contents_opacity(obj), 255, duration}},
                                                      menu::ease_out_circ));
            }
            int duration = 25;
            g2d::Sprite* obj = options_windows_container_.get();
            move.start_move(
                ObjectMovement::tween(obj, {{menu::prop_y(obj), -406, duration}, {menu::prop_opacity(obj), 255, duration}}, menu::ease_out_circ));
        });
        queue_wait_mode("movement");
        queue([this] { command_active_ = true; });
    }

    void on_option_window_cancel() {
        std::vector<WindowSelectable*> windows = option_windows();
        int index = option_categories_window_->index();
        WindowSelectable* category_window = index >= 0 && index < (int)windows.size() ? windows[(size_t)index] : nullptr;
        if (category_window) {
            category_window->select(0);
            category_window->refresh();
        }
        help_window_->clear();
        option_categories_window_->activate();
    }
};

rpg::SceneFactory title_factory(bool prepared, int command_index) {
    return rpg::SceneFactory{"Scene_OmoriTitleScreen", [prepared, command_index]() -> std::unique_ptr<rpg::Scene> {
                                 auto scene = std::unique_ptr<SceneOmoriTitleScreen>(new SceneOmoriTitleScreen());
                                 if (prepared) scene->prepare(command_index, true);
                                 return std::unique_ptr<rpg::Scene>(scene.release());
                             }};
}

}

rpg::SceneFactory title_scene() { return title_factory(false, 0); }

rpg::SceneFactory title_scene_prepared(int command_index) { return title_factory(true, command_index); }

static SceneRegistrar g_title_registrar("Scene_OmoriTitleScreen", title_scene());

}
