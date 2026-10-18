#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/random.h"
#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/hooks.h"
#include "../rpg/input.h"
#include "../rpg/party.h"
#include "../rpg/system.h"
#include "scene_menu_base.h"
#include "scenes.h"
#include "ui_util.h"
#include "window_selectable.h"

namespace rpg {
namespace slot_machine {
double expectation = 0.5;
int scale = 1;
}
}

namespace ui {

namespace {

const int kOdds[3][6] = {{1, 2, 5, 10, 20, 100}, {2, 10, 20, 100, 200, 1000}, {20, 100, 200, 1000, 2000, 10000}};

const int kReel[5][18] = {{0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5},
                          {5, 4, 3, 2, 1, 0, 5, 4, 3, 2, 1, 0, 5, 4, 3, 2, 1, 0},
                          {0, 0, 1, 1, 5, 2, 3, 3, 4, 4, 5, 5, 0, 1, 2, 3, 4, 5},
                          {2, 2, 4, 1, 3, 5, 0, 2, 4, 1, 3, 5, 0, 2, 4, 1, 3, 5},
                          {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5}};

const int kReelLength = 18;
const double kCoinMax = 99999999;

int reel_at(int r, int idx) {
    if (idx < 0 || idx >= kReelLength) return -1;
    return kReel[r][idx];
}

int reel_index_of(int r, int value) {
    for (int i = 0; i < kReelLength; i++) {
        if (kReel[r][i] == value) return i;
    }
    return -1;
}

std::string lang_text_of(const char* code) { return lang_data(code)["text"].string(); }

int scale_value() { return rpg::slot_machine::scale; }

double get_coin() { return rpg::game_party().gold_; }

void set_coin(double v) { rpg::game_party().gold_ = v; }

void play_slot_se(const char* name) {
    rpg::AudioObj a;
    a.valid = true;
    a.name = name;
    a.volume = 90;
    a.pitch = 100;
    a.pan = 0;
    rpg::audio_manager::play_se(a);
}

g2d::BitmapPtr load_slot(const std::string& name) { return load_folder("slotmachine", name); }

std::string trim_js(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) b++;
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

std::string replace_win_coin(const std::string& text, const std::string& with) {
    static const std::string needle = "win coin";
    std::string lower = text;
    for (auto& ch : lower) ch = (char)std::tolower((unsigned char)ch);
    std::string out;
    size_t from = 0;
    size_t pos = lower.find(needle);
    while (pos != std::string::npos) {
        out += text.substr(from, pos - from);
        out += with;
        from = pos + needle.size();
        pos = lower.find(needle, from);
    }
    out += text.substr(from);
    return out;
}

class SLTReelSprite : public g2d::Sprite {
public:
    enum { STOP = 0, SPINNING = 1, SPIN = 2, STOPPING = 3 };
    static constexpr int FRAME_SPINNING = 40;
    static constexpr int FRAME_SPIN = 60;
    static constexpr int FRAME_STOPPING = 40;

    explicit SLTReelSprite(g2d::BitmapPtr b) : g2d::Sprite(std::move(b)) {}

    int status() const { return status_; }

    void update() override {
        g2d::Sprite::update();
        double total = num_spot_ * spot_height_;
        switch (status_) {
        case STOP: break;
        case SPINNING:
            if (spin_frame_ > FRAME_SPINNING) {
                status_ = SPIN;
                play_slot_se("Slots_Spin");
                break;
            }
            scroll_y_ = std::fmod(scroll_y_ + speed_low_, total);
            apply_frame();
            spin_frame_++;
            break;
        case SPIN:
            if (spin_frame_ > spin_end_frame_ + FRAME_SPINNING) {
                status_ = STOPPING;
                scroll_y_ = win_spot_ * spot_height_ - spot_height_ * 3;
                break;
            }
            scroll_y_ = std::fmod(scroll_y_ + speed_high_, total);
            apply_frame();
            spin_frame_++;
            break;
        case STOPPING:
            scroll_y_ = std::fmod(scroll_y_ + speed_low_, total);
            if (scroll_y_ > win_spot_ * spot_height_) {
                scroll_y_ = win_spot_ * spot_height_;
                status_ = STOP;
                play_slot_se("Slots_Stop");
            }
            apply_frame();
            break;
        }
    }

    void set_spot(int spot) {
        if (0 <= spot && spot < num_spot_) {
            scroll_y_ = spot_height_ * spot;
            apply_frame();
        }
    }

    void set_win_spot(int spot) { win_spot_ = spot; }
    void set_spin_end_frame(int frame) { spin_end_frame_ = frame; }

    void spin() {
        if (status_ == STOP) {
            status_ = SPINNING;
            spin_frame_ = 0;
        }
    }

private:
    int num_spot_ = 18;
    double spot_height_ = 44;
    int win_spot_ = 0;
    double scroll_y_ = 0;
    double speed_high_ = 30;
    double speed_low_ = 4;
    int status_ = STOP;
    int spin_frame_ = 0;
    int spin_end_frame_ = FRAME_SPIN;

    void apply_frame() { set_frame(0, spot_height_ * num_spot_ * 2 - scroll_y_ - spot_height_ * 2, 92, spot_height_ * 3); }
};

class LotLineSprite : public g2d::Sprite {
public:
    explicit LotLineSprite(g2d::BitmapPtr b) : g2d::Sprite(std::move(b)) {
        line1_ = std::make_shared<g2d::Sprite>(load_slot("bet_line_1"));
        line2_ = std::make_shared<g2d::Sprite>(load_slot("bet_line_2"));
        line3_ = std::make_shared<g2d::Sprite>(load_slot("bet_line_3"));
        line1_->move(0, 43);
        line2_->move(0, 1);
        line3_->move(0, 88);
        add_child(line1_);
        add_child(line2_);
        add_child(line3_);
    }

    void enable_line(int line) {
        if (line == 0) line1_->visible = true;
        else if (line == 1) line2_->visible = true;
        else if (line == 2) line3_->visible = true;
    }

    void clear() {
        line1_->visible = false;
        line2_->visible = false;
        line3_->visible = false;
    }

private:
    g2d::SpritePtr line1_, line2_, line3_;
};

class InstructionCursorSprite : public g2d::Sprite {
public:
    static constexpr int FRAME_BLINK = 10;
    static constexpr int FRAME_BLINK_END = 190;

    explicit InstructionCursorSprite(g2d::BitmapPtr b) : g2d::Sprite(std::move(b)) {}

    void blink() { counter = 1; }

    void update() override {
        g2d::Sprite::update();
        if (counter > 0) {
            if (counter > FRAME_BLINK_END + 1) {
                counter = 0;
            } else {
                int s = counter / FRAME_BLINK;
                visible = s % 2 == 0;
                counter++;
            }
        }
    }

    int counter = 0;
};

class WindowSlotInstruction : public WindowBase {
public:
    using WindowBase::initialize;

    void initialize(double x, double y, double w, double h) override {
        WindowBase::initialize(x, y, w, h);
        g2d::BitmapPtr b = load_slot("win_cursor");
        double cx = 28, cy = 22, cw = 168;
        for (int i = 2; i >= 0; i--) {
            for (int j = 5; j >= 0; j--) {
                auto sprite = std::make_shared<InstructionCursorSprite>(b);
                add_child(sprite);
                sprite->x = cx + i * (cw + 24);
                sprite->y = cy + j * 20;
                cursol_[2 - i].push_back(sprite);
            }
        }
        clear_cursor();
    }

    double line_height() override { return 20; }

    void refresh() {
        set_background_type(2);
        c().clear();
        if (has_odds_) {
            c().font_size = 22;
            double x = 28, y = -2, w = 168;
            for (int k = 5; k >= 0; k--) {
                draw_text(number_text(kOdds[2][k]), x, y, w, "right");
                y += line_height();
            }
            x += w + 24;
            y = -2;
            for (int k = 5; k >= 0; k--) {
                draw_text(number_text(kOdds[1][k]), x, y, w, "right");
                y += line_height();
            }
            x += w + 20;
            y = -2;
            for (int k = 5; k >= 0; k--) {
                draw_text(number_text(kOdds[0][k]), x, y, w, "right");
                y += line_height();
            }
        }
        c().font_size = standard_font_size();
    }

    void set_odds() { has_odds_ = true; }

    void clear_cursor() {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 6; j++) {
                cursol_[i][(size_t)j]->visible = false;
                cursol_[i][(size_t)j]->counter = 0;
            }
        }
    }

    void blink_cursor(const bool array[3][6]) {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 6; j++) {
                if (array[i][j]) cursol_[i][(size_t)j]->blink();
            }
        }
    }

private:
    std::vector<std::shared_ptr<InstructionCursorSprite>> cursol_[3];
    bool has_odds_ = false;
};

class WindowSlotMachine : public WindowBase {
public:
    WindowSlotMachine() {
        coin_sprite = std::make_shared<g2d::Sprite>();
        add_child(coin_sprite);
        coin_sprite->move(250, 116);
        bet_sprite = std::make_shared<g2d::Sprite>();
        add_child(bet_sprite);
        bet_sprite->move(440, 116);
    }

    using WindowBase::initialize;

    void initialize(double x, double y, double w, double h) override {
        WindowBase::initialize(x, y, w, h);
        last_coin_ = 0;
        last_bet_ = 0;
        coin = 0;
        bet = 0;
    }

    void create_contents() override {
        WindowBase::create_contents();
        coin_sprite->set_bitmap(make_text_bitmap(c().measure_text_width("99999999"), (int)line_height()));
        bet_sprite->set_bitmap(make_text_bitmap(c().measure_text_width("999"), (int)line_height()));
    }

    void refresh() {
        set_background_type(2);
        refresh_coin();
        refresh_bet();
    }

    void update() override {
        WindowBase::update();
        if (last_coin_ != coin) {
            refresh_coin();
            last_coin_ = coin;
        }
        if (last_bet_ != bet) {
            refresh_bet();
            last_bet_ = bet;
        }
    }

    void refresh_coin() {
        coin_sprite->bitmap()->clear();
        coin_sprite->bitmap()->draw_text(number_text(coin), 0, 0, c().measure_text_width("9999"), line_height(), "right");
    }

    void refresh_bet() {
        bet_sprite->bitmap()->clear();
        bet_sprite->bitmap()->draw_text(number_text(bet), 0, 0, c().measure_text_width("999"), line_height(), "right");
    }

    double coin = 0;
    double bet = 0;
    g2d::SpritePtr coin_sprite;
    g2d::SpritePtr bet_sprite;

private:
    double last_coin_ = 0;
    double last_bet_ = 0;

    static g2d::BitmapPtr make_text_bitmap(int w, int h) {
        g2d::BitmapPtr b = g2d::Bitmap::create(w, h);
        b->font_face = yparam_str("DefaultFont");
        b->font_bold = false;
        return b;
    }
};

class WindowSlotCommand : public WindowHorzCommand {
public:
    using WindowHorzCommand::initialize;

    void initialize(double x, double y) override {
        bet_allow_ = true;
        spin_allow_ = false;
        WindowHorzCommand::initialize(x, y);
        set_window_opacity(0);
    }

    bool is_using_custom_cursor_rect_sprite() override { return true; }
    bool is_allow_bet() const { return bet_allow_; }
    bool is_allow_spin() const { return spin_allow_; }
    int standard_padding() override { return 0; }
    int spacing() override { return 6; }

    void enable_bet() {
        bet_allow_ = true;
        refresh();
    }
    void disable_bet() {
        bet_allow_ = false;
        refresh();
    }
    void enable_spin() {
        spin_allow_ = true;
        refresh();
    }
    void disable_spin() {
        spin_allow_ = false;
        refresh();
    }

    void make_command_list() override {
        add_command(lang_text_of("slot_machine_minigame.message_5"), "bet", bet_allow_);
        add_command(lang_text_of("slot_machine_minigame.message_6"), "spin", spin_allow_);
        add_command(lang_text_of("slot_machine_minigame.message_7"), "cancel");
    }

    double window_width() override { return 420; }
    double window_height() override { return 48; }
    double item_height() override { return 48; }
    int max_cols() override { return 3; }
    std::string item_text_align() override { return "left"; }

    void draw_item(int index) override {
        ItemRect rect = item_rect_for_text(index);
        draw_option_back(index);
        reset_text_color();
        change_paint_opacity(is_command_enabled(index));
        draw_text(command_name(index), rect.x + 24, rect.y + 2, rect.width, "left");
    }

    void draw_option_back(int index) {
        ItemRect rect = item_rect_for_text(index);
        change_paint_opacity(true);
        double m = 4;
        double x = 5 + ((rect.x - 26) < 0 ? 0 : rect.x - 26);
        double y = rect.y;
        double w = rect.width + 6;
        double h = rect.height;
        g2d::Bitmap* skin = windowskin().get();
        if (w > 0 && h > 0 && skin) {
            double p = 96;
            c().blt(skin, 0, 0, p, p, x, 0, w, h);
            for (y = 0; y < h; y += p) {
                for (x = 0; x < w; x += p) c().blt(skin, 0, p, p, p, x, y, p, p);
            }
            c().adjust_tone(tone_[0], tone_[1], tone_[2]);
        }
        x = 5 + ((rect.x - 26) < 0 ? 0 : rect.x - 26);
        y = rect.y;
        w = rect.width + 6;
        h = rect.height;
        if (w > 0 && h > 0 && skin) {
            double p = 96, q = 96;
            c().blt(skin, p + m, 0 + 0, p - m * 2, m, m + x, 0, w - m * 2, m);
            c().blt(skin, p + m, 0 + q - m, p - m * 2, m, m + x, h - m, w - m * 2, m);
            c().blt(skin, p + 0, 0 + m, m, p - m * 2, x, m, m, h - m * 2);
            c().blt(skin, p + q - m, 0 + m, m, p - m * 2, x + (w - m), m, m, h - m * 2);
            c().blt(skin, p + 0, 0 + 0, m, m, x, 0, m, m);
            c().blt(skin, p + q - m, 0 + 0, m, m, x + (w - m), 0, m, m);
            c().blt(skin, p + 0, 0 + q - m, m, m, x, h - m, m, m);
            c().blt(skin, p + q - m, 0 + q - m, m, m, x + (w - m), h - m, m, m);
        }
    }

    g2d::Sprite* cursor_rect_sprite() const { return custom_cursor_sprite_.get(); }

private:
    bool bet_allow_ = true;
    bool spin_allow_ = false;
};

class WindowReplayCommand : public WindowCommand {
public:
    using WindowCommand::initialize;

    void initialize(double x, double y) override {
        WindowCommand::initialize(x, y);
        set_openness(0);
    }

    bool is_using_custom_cursor_rect_sprite() override { return true; }

    void make_command_list() override {
        add_command(lang_text_of("XX_GENERAL.message_4"), "yes");
        add_command(lang_text_of("XX_GENERAL.message_5"), "no");
    }

    double window_width() override { return 96; }
    double window_height() override { return 76; }
    double line_height() override { return 26; }
    int standard_padding() override { return 10; }

    void draw_item(int index) override {
        ItemRect rect = item_rect_for_text(index);
        std::string align = item_text_align();
        reset_text_color();
        change_paint_opacity(is_command_enabled(index));
        draw_text(command_name(index), rect.x + 8, rect.y, rect.width, align);
    }

    std::string item_text_align() override { return "left"; }

    g2d::Sprite* cursor_rect_sprite() const { return custom_cursor_sprite_.get(); }
};

class WindowSlotsIntroduction : public WindowBase {
public:
    using WindowBase::initialize;

    void initialize(double x, double y) { WindowBase::initialize(x, y, 606, 64); }

    double contents_height() override { return height() * 3 - standard_padding() * 2; }
    double line_height() override { return 26; }
    int standard_padding() override { return 10; }

    void set_text(const std::string& text) {
        c().clear();
        if (text.find("<br>") != std::string::npos) {
            std::vector<std::string> lines;
            size_t from = 0;
            while (true) {
                size_t pos = text.find("<br>", from);
                if (pos == std::string::npos) {
                    lines.push_back(text.substr(from));
                    break;
                }
                lines.push_back(text.substr(from, pos - from));
                from = pos + 4;
            }
            double y = -2;
            for (auto& line : lines) {
                draw_text_ex(trim_js(line), 8, y);
                y += line_height();
            }
        } else {
            draw_text_ex(text, 8, -2);
        }
    }
};

class SceneSlotMachine : public SceneMenuBase {
public:
    SceneSlotMachine() {
        exit_fade_ = 0;
        rpg::game_system().save_bgm();
        rpg::AudioObj bgm;
        bgm.valid = true;
        bgm.name = "event_sw_gameshow";
        bgm.volume = 100;
        bgm.pitch = 100;
        bgm.pan = 0;
        rpg::audio_manager::play_bgm(bgm);
        bet_ = 0;
        coin_ = get_coin();
        spin_start_ = false;
        roll_count_ = 0;
        win_coin_ = 0;
        correct_coin_ = 0;
        win_step_ = 0;
        double ex = rpg::slot_machine::expectation;
        static const double base[6] = {0.70, 0.63, 0.54, 0.44, 0.38, 0.31};
        for (int k = 0; k < 6; k++) probability_[0][k] = base[k];
        for (int k = 0; k < 6; k++) probability_[1][k] = (1 / probability_[0][k]) * ex * (1.0 / kOdds[0][k]);
        for (int k = 0; k < 6; k++) probability_[2][k] = (1 / (probability_[0][k] * probability_[1][k])) * ex * (1.0 / kOdds[1][k]);
        for (int k = 0; k < 6; k++)
            probability_[3][k] = (1 / (probability_[0][k] * probability_[1][k] * probability_[2][k])) * ex * (1.0 / kOdds[2][k]);
        if (coin_ > kCoinMax) coin_ = kCoinMax;
    }

    const char* name() const override { return "Scene_SlotMachine"; }

    void create() override {
        start_fade_in(30, false);
        create_background();
        background_sprite->set_bitmap(load_slot("bg"));
        create_reels();
        create_bet_line();
        create_scale();
        update_actor();
        create_window_layer();
        create_help_window();
        create_instruction();
        create_slot_machine();
        create_slot_command();
        create_replay_command();
        refresh_status();
        if (coin_ < scale_value()) slot_command_->disable_bet();
    }

    void start() override {
        make_reel();
        instruction_->refresh();
        slot_window_->refresh();
        intro_->set_text(lang_text_of("slot_machine_minigame.message_0"));
    }

    void update() override {
        SceneMenuBase::update();
        if (exit_fade_ > 0) {
            exit_fade_--;
            fade(background_sprite.get());
            for (auto& r : reels_) fade(r.get());
            fade(bet_line_.get());
            intro_->set_window_alpha(intro_->opacity_value() - 50);
            intro_->set_contents_opacity(intro_->contents_opacity() - 50);
            replay_->set_window_alpha(replay_->opacity_value() - 50);
            replay_->set_contents_opacity(replay_->contents_opacity() - 50);
            fade(replay_->cursor_rect_sprite());
            slot_command_->set_contents_opacity(slot_command_->contents_opacity() - 50);
            fade(slot_command_->cursor_rect_sprite());
            instruction_->set_contents_opacity(instruction_->contents_opacity() - 50);
            instruction_->clear_cursor();
            fade(slot_window_->coin_sprite.get());
            fade(slot_window_->bet_sprite.get());
            fade(scale_sprite_.get());
            if (exit_fade_ <= 0) {
                pop_scene();
                rpg::game_system().replay_bgm();
            }
            return;
        }
        double result = 0;
        if (spin_start_ && !is_spinning()) {
            spin_start_ = false;
            show_result();
        } else if (is_win_counting()) {
            if (win_coin_ <= win_step_) {
                win_coin_ = 0;
                result = coin_ + correct_coin_;
                slot_window_->coin = result;
                correct();
                play_slot_se("Slots_Cha-Ching");
            } else {
                win_coin_ -= win_step_;
                result = coin_ + correct_coin_ - win_coin_;
                slot_window_->coin = result;
                play_slot_se("Slots_GainMoney");
            }
        }
        if (input::is_repeated(input::UP) && slot_command_->active) {
            if (slot_command_->is_allow_bet()) {
                play_ok();
                bet_command();
            } else {
                play_buzzer();
            }
        }
        if (input::is_repeated(input::DOWN) && slot_command_->active) {
            if (slot_command_->is_allow_spin() && !spin_start_) {
                play_ok();
                spin_command();
            }
        }
    }

private:
    int exit_fade_ = 0;
    int bet_ = 0;
    double coin_ = 0;
    int win_spot_[5] = {0, 0, 0, 0, 0};
    bool spin_start_ = false;
    int roll_count_ = 0;
    double win_coin_ = 0;
    double correct_coin_ = 0;
    double win_step_ = 0;
    std::string win_message_;
    double probability_[4][6];

    g2d::BitmapPtr reel_bitmap_;
    std::vector<std::shared_ptr<SLTReelSprite>> reels_;
    std::shared_ptr<LotLineSprite> bet_line_;
    g2d::SpritePtr scale_sprite_;
    std::shared_ptr<WindowSlotsIntroduction> intro_;
    std::shared_ptr<WindowSlotInstruction> instruction_;
    std::shared_ptr<WindowSlotMachine> slot_window_;
    std::shared_ptr<WindowSlotCommand> slot_command_;
    std::shared_ptr<WindowReplayCommand> replay_;

    static void fade(g2d::DisplayObject* o) {
        if (o) o->set_opacity(o->opacity() - 50);
    }

    void make_reel() {
        for (int i = 0; i < 5; i++) {
            g2d::BitmapPtr b = reels_[(size_t)i]->bitmap();
            for (int j = 0; j < 18; j++) {
                for (int k = 0; k < 3; k++) {
                    b->blt(reel_bitmap_.get(), kReel[i][j] * 92, 0, 92, 44, 0, (44 * 18 * 3) - j * 44 - k * (18 * 44) - 44);
                }
            }
            reels_[(size_t)i]->set_spot((int)std::floor(rng::next() * 18));
        }
    }

    bool is_spinning() {
        bool v = false;
        for (int i = 0; i < 5; i++) v = v || reels_[(size_t)i]->status() != SLTReelSprite::STOP;
        return v;
    }

    bool is_win_counting() const { return win_coin_ > 0; }

    void create_help_window() {
        intro_ = WindowBase::make<WindowSlotsIntroduction>(0.0, 0.0);
        intro_->set_width(606);
        add_window(intro_);
        intro_->set_height(64);
        intro_->x = 18;
        intro_->y = 480 - intro_->height() - 10;
    }

    void create_instruction() {
        instruction_ = WindowBase::make<WindowSlotInstruction>(0.0, 0.0, 640.0, 26.0 * 6 + 18 * 2);
        instruction_->set_odds();
        add_window(instruction_);
    }

    void create_slot_machine() {
        slot_window_ = WindowBase::make<WindowSlotMachine>(18.0, instruction_->height(), 606.0, 130.0);
        slot_window_->coin = coin_;
        slot_window_->bet = bet_;
        add_window(slot_window_);
    }

    void create_slot_command() {
        slot_command_ = WindowBase::make<WindowSlotCommand>(0.0, 0.0);
        slot_command_->set_handler("bet", [this] { bet_command(); });
        slot_command_->set_handler("spin", [this] { spin_command(); });
        slot_command_->set_handler("cancel", [this] { cancel_command(); });
        add_window(slot_command_);
        slot_command_->x = (640 - slot_command_->width()) / 2;
        slot_command_->y = intro_->y - slot_command_->height() - 4;
    }

    void create_replay_command() {
        replay_ = WindowBase::make<WindowReplayCommand>(0.0, 0.0);
        replay_->set_handler("yes", [this] { replay_command(); });
        replay_->set_handler("no", [this] { cancel_command(); });
        replay_->set_handler("cancel", [this] { cancel_command(); });
        add_window(replay_);
        replay_->x = (640 - 16) - replay_->width();
        replay_->y = intro_->y - 4 - replay_->height();
    }

    void create_reels() {
        reel_bitmap_ = load_slot("reel");
        reels_.clear();
        for (int i = 0; i < 5; i++) {
            auto sprite = std::make_shared<SLTReelSprite>(g2d::Bitmap::create(92, 44 * 18 * 3));
            reels_.push_back(sprite);
            sprite->x = 84 + i * 95;
            sprite->y = 172;
            sprite->set_spin_end_frame(SLTReelSprite::FRAME_SPIN + i * 44);
            sprite->set_frame(0, 0, 92, 44 * 3);
            add_child(sprite);
        }
    }

    void create_bet_line() {
        bet_line_ = std::make_shared<LotLineSprite>(load_slot("line_base"));
        bet_line_->x = 34;
        bet_line_->y = 180;
        bet_line_->clear();
        add_child(bet_line_);
    }

    void create_scale() {
        g2d::BitmapPtr bitmap;
        int s = scale_value();
        if (s == 10) bitmap = load_slot("scale_x10");
        else if (s == 100) bitmap = load_slot("scale_x100");
        else bitmap = load_slot("scale_x1");
        scale_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        scale_sprite_->x = 640 - 76;
        scale_sprite_->y = 172;
        add_child(scale_sprite_);
    }

    void cancel_command() {
        start_fade_out(30);
        bet_ = 0;
        refresh_status();
        set_coin(coin_);
        exit_fade_ = 30;
    }

    void bet_command() {
        if (bet_ < 3) {
            bet_++;
            slot_command_->enable_spin();
        }
        if (bet_ > 2) slot_command_->disable_bet();
        if (coin_ - bet_ * scale_value() < scale_value()) slot_command_->disable_bet();
        refresh_status();
        slot_command_->activate();
    }

    void spin_command() {
        coin_ -= bet_ * scale_value();
        slot_command_->deactivate();
        slot_command_->close();
        intro_->close();
        draw_lot(win_spot_);
        roll_count_++;
        spin_start_ = true;
        for (int i = 0; i < 5; i++) reels_[(size_t)i]->set_win_spot(win_spot_[i]);
        for (int i = 0; i < 5; i++) reels_[(size_t)i]->spin();
    }

    void show_result() {
        roll_count_ = 0;
        double win = judge(win_spot_);
        double tmp = win;
        if (coin_ + win > kCoinMax) win = kCoinMax - coin_;
        win_coin_ = correct_coin_ = win;
        double time = 60 * 10;
        if (win_coin_ < time) win_step_ = 1;
        else win_step_ = (double)(int32_t)(win_coin_ / time);
        rpg::audio_manager::stop_se();
        if (win_coin_ > 0) {
            win_message_ = replace_win_coin(lang_text_of("slot_machine_minigame.message_1"), number_text(tmp));
            intro_->set_height(intro_->height() + slot_command_->height());
            intro_->y = 480 - intro_->height() - 10;
            replay_->y = intro_->y - 4 - replay_->height();
            intro_->set_text(win_message_);
            intro_->open();
            play_slot_se("Slots_Win");
        } else {
            intro_->set_height(intro_->height() + slot_command_->height());
            intro_->y = 480 - intro_->height() - 10;
            replay_->y = intro_->y - 4 - replay_->height();
            intro_->set_text(lang_text_of("slot_machine_minigame.message_2") + "\n");
            intro_->open();
            replay_->open();
            replay_->activate();
            play_slot_se("Slots_Lose");
        }
    }

    static bool check_line(const int line[5], int match, int& value_out) {
        int value = -2;
        int count = 0;
        for (int i = 0; i < 5; i++) {
            if (line[i] != value) {
                value = line[i];
                count = 0;
            }
            count += 1;
            if (match <= count) {
                value_out = value;
                return true;
            }
        }
        return false;
    }

    double judge_line(const int line[5], int min_bet, bool cursor_array[3][6]) {
        int base = line[0];
        if (bet_ > min_bet) {
            int i = 1;
            int v = 0;
            if (check_line(line, 5, v)) {
                base = v;
                i = 5;
            } else if (check_line(line, 4, v)) {
                base = v;
                i = 4;
            } else if (check_line(line, 3, v)) {
                base = v;
                i = 3;
            }
            i--;
            if (i > 1) {
                double win = (double)scale_value() * kOdds[i - 2][base];
                cursor_array[i - 2][base] = true;
                return win;
            }
        }
        return 0;
    }

    double judge(const int spot[5]) {
        int result1[5], result2[5], result3[5];
        for (int r = 0; r < 5; r++) {
            result1[r] = kReel[r][(spot[r] + 1) % kReelLength];
            result2[r] = kReel[r][spot[r]];
            result3[r] = kReel[r][(win_spot_[r] - 1 + kReelLength) % kReelLength];
        }
        bool cursor_array[3][6] = {};
        double return_value = 0;
        return_value += judge_line(result1, 1, cursor_array);
        return_value += judge_line(result2, 0, cursor_array);
        return_value += judge_line(result3, 2, cursor_array);
        instruction_->blink_cursor(cursor_array);
        return return_value;
    }

    void draw_lot(int spot[5]) {
        for (int r = 0; r < 5; r++) spot[r] = (int)std::floor(rng::next() * kReelLength);
        bool target1 = true, target2 = true, target3 = true;
        for (int i = 1; i < 5; i++) {
            for (int j = 0; j < kReelLength; j++) {
                if (is_win(spot, i)) spot[i] = (spot[i] + 1) % kReelLength;
                else break;
            }
            int l = kReelLength;
            int l1 = kReel[i - 1][(spot[i - 1] + 1 + l) % l];
            int l2 = kReel[i - 1][(spot[i - 1] + 0 + l) % l];
            int l3 = kReel[i - 1][(spot[i - 1] - 1 + l) % l];
            double r = rng::next();
            if (r < probability_[i - 1][l2] && target2) {
                int s = reel_index_of(i, l2);
                if (s >= 0) {
                    spot[i] = (s + 0 + l) % l;
                    target1 = false;
                    target3 = false;
                }
            }
            r = rng::next();
            if (r < probability_[i - 1][l1] && target1) {
                int s = reel_index_of(i, l1);
                if (s >= 0) {
                    spot[i] = (s - 1 + l) % l;
                    target2 = false;
                    target3 = false;
                }
            }
            r = rng::next();
            if (r < probability_[i - 1][l3] && target3) {
                int s = reel_index_of(i, l3);
                if (s >= 0) {
                    spot[i] = (s + 1 + l) % l;
                    target1 = false;
                    target2 = false;
                }
            }
        }
    }

    static bool is_win(const int spot[5], int r) {
        return reel_at(r - 1, (spot[r - 1] + 1) % kReelLength) == reel_at(r, (spot[r] + 1) % kReelLength) ||
               reel_at(r - 1, (spot[r - 1] + 0) % kReelLength) == reel_at(r, (spot[r] + 0) % kReelLength) ||
               reel_at(r - 1, (spot[r - 1] - 1) % kReelLength) == reel_at(r, (spot[r] - 1) % kReelLength);
    }

    void correct() {
        coin_ += correct_coin_;
        correct_coin_ = 0;
        if (coin_ >= kCoinMax) intro_->set_text(lang_text_of("slot_machine_minigame.message_4"));
        else intro_->set_text(win_message_);
        win_message_.clear();
        replay_->open();
        replay_->activate();
    }

    void replay_command() {
        slot_command_->enable_bet();
        slot_command_->disable_spin();
        if (coin_ < scale_value()) slot_command_->disable_bet();
        slot_command_->select(0);
        replay_->close();
        intro_->set_height(64);
        intro_->y = 480 - intro_->height() - 10;
        replay_->y = intro_->y - 4 - replay_->height();
        slot_command_->open();
        slot_command_->activate();
        intro_->set_text(lang_text_of("slot_machine_minigame.message_0"));
        bet_ = 0;
        refresh_status();
    }

    void refresh_status() {
        slot_window_->bet = bet_ * scale_value();
        slot_window_->coin = coin_ - bet_ * scale_value();
        if (bet_ == 0) bet_line_->clear();
        else bet_line_->enable_line(bet_ - 1);
    }
};

}

rpg::SceneFactory slot_machine_scene() { return rpg::scene_of<SceneSlotMachine>("Scene_SlotMachine"); }

static SceneRegistrar g_slot_machine_registrar("Scene_SlotMachine", slot_machine_scene());

}
