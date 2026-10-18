#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/random.h"
#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/system.h"
#include "scene_menu_base.h"
#include "scenes.h"
#include "ui_util.h"
#include "window_selectable.h"

namespace ui {

namespace {

struct BlackJackCard {
    std::string suit;
    std::string type;
    int index = 0;
    bool soft_ace = false;
    int value() const {
        if (type == "A") return soft_ace ? 1 : 11;
        if (type == "J" || type == "Q" || type == "K") return 10;
        return (int)rpg::js_parse_int(type);
    }
};

using CardPtr = std::shared_ptr<BlackJackCard>;
using Hand = std::vector<CardPtr>;

const Value& bj_lang(std::shared_ptr<Document>& keep, const char* key) {
    keep = data::lang_file("XX_BLUE");
    if (!keep) return Value::null_value();
    return keep->root()["Yin_Blackjack"][key];
}

std::string bj_string(const char* key) {
    std::shared_ptr<Document> keep;
    return data::js_string(bj_lang(keep, key));
}

std::string bj_list(const char* key, int i) {
    std::shared_ptr<Document> keep;
    return data::js_string(bj_lang(keep, key)[i]);
}

std::string bj_message(int n) { return data::lang_text("blackjack_minigame.message_" + std::to_string(n)); }

void play_bj_se(const char* name) { play_se(name, 100, 100, 0); }

void play_bj_bgm(const char* name) {
    rpg::AudioObj a;
    a.valid = true;
    a.name = name;
    a.volume = 100;
    a.pitch = 100;
    a.pan = 0;
    rpg::audio_manager::play_bgm(a);
}

void setup_minigame_font(g2d::Bitmap& b, const char* face) {
    b.font_bold = false;
    b.font_italic = false;
    b.font_size = 20;
    b.outline_color = "rgba(0, 0, 0, 0.5)";
    b.outline_width = 0;
    b.font_face = face;
}

class SceneBlackJack;

class WindowBjCommandBase : public WindowHorzCommand {
public:
    using WindowHorzCommand::initialize;
    int standard_padding() override { return 0; }
    int spacing() override { return 12; }
    double window_height() override { return fitting_height(1); }
    std::string item_text_align() override { return "center"; }
    void select(int index) override {
        index_ = index;
        stay_count = 0;
        ensure_cursor_visible();
        update_cursor();
        call_update_help();
        refresh();
    }

protected:
    void draw_button(int index, const std::string& normal, const std::string& selected, bool fixed_size, bool grey_disabled) {
        g2d::Bitmap& b = c();
        setup_minigame_font(b, "Helvetica");
        change_text_color(text_color(6));
        ItemRect rect = item_rect_for_text(index);
        g2d::BitmapPtr bitmap;
        if (current_symbol() == command_symbol(index) && current_data()) {
            bitmap = load_picture(selected);
            if (is_command_enabled(index)) change_text_color(text_color(0));
            else change_text_color(text_color(7));
        } else {
            bitmap = load_picture(normal);
        }
        if (grey_disabled && !is_command_enabled(index)) change_text_color("#939393");
        if (fixed_size) b.blt(bitmap.get(), 0, 0, 124, 36, rect.x - 22, rect.y);
        else b.blt(bitmap.get(), 0, 0, bitmap->width(), bitmap->height(), rect.x - 22, rect.y);
        draw_text(command_name(index), rect.x - 18, rect.y, rect.width, item_text_align());
    }
};

class WindowWager : public WindowBjCommandBase {
public:
    using WindowBjCommandBase::initialize;
    void initialize(double, double) override {
        double x = (640 - window_width()) / 2;
        double y = 480 - 86;
        WindowBjCommandBase::initialize(x, y);
        set_window_alpha(0);
        refresh();
    }
    double window_width() override { return 640 - 108; }
    void draw_item(int index) override { draw_button(index, "BJ-Button", "BJ-Button-Selected", true, false); }
    void make_command_list() override {
        add_command(bj_list("bets", 0), "5");
        add_command(bj_list("bets", 1), "25");
        add_command(bj_list("bets", 2), "100");
        add_command(bj_list("bets", 3), "quit");
    }
};

class WindowBlackJackCommands : public WindowBjCommandBase {
public:
    using WindowBjCommandBase::initialize;
    void initialize(double, double) override {
        double x = (640 - window_width()) / 2;
        double y = 480 - 86;
        WindowBjCommandBase::initialize(x, y);
        set_window_alpha(0);
        refresh();
    }
    int max_cols() override { return 2; }
    double window_width() override { return 302; }
    void draw_item(int index) override { draw_button(index, "BJ-ButtonLong", "BJ-ButtonLong-Selected", false, true); }
    void make_command_list() override {
        add_command(rpg::to_upper(bj_list("jack_commands", 0)), "stand");
        add_command(rpg::to_upper(bj_list("jack_commands", 1)), "hit");
    }
};

class WindowBJTitleCommands : public WindowBjCommandBase {
public:
    using WindowBjCommandBase::initialize;
    void initialize(double, double) override {
        double x = (640 - window_width()) / 2;
        double y = 480 - 86;
        WindowBjCommandBase::initialize(x, y);
        set_window_alpha(0);
        refresh();
        if (rpg::game_variables().number(827) > 0) select(1);
    }
    int max_cols() override { return 3; }
    double window_width() override { return 459; }
    void cursor_right(bool wrap) override {
        int i = index(), mi = max_items(), mc = max_cols();
        if (mc >= 2 && (i < mi - 1 || (wrap && is_horizontal()))) {
            bool exists = i + 1 >= 0 && i + 1 < (int)list_.size();
            if (exists && is_command_enabled(i + 1)) select((i + 1) % mi);
            else if (!exists) select(0);
            else select(2);
        }
    }
    void cursor_left(bool wrap) override {
        int i = index(), mi = max_items(), mc = max_cols();
        if (mc >= 2 && (i > 0 || (wrap && is_horizontal()))) {
            bool exists = i - 1 >= 0 && i - 1 < (int)list_.size();
            if (exists && is_command_enabled(i - 1)) select((i - 1 + mi) % mi);
            else if (!exists && is_command_enabled(mi - 1)) select(mi - 1);
            else select(0);
        }
    }
    void draw_item(int index) override { draw_button(index, "BJ-ButtonLong", "BJ-ButtonLong-Selected", false, true); }
    void make_command_list() override {
        add_command(rpg::to_upper(bj_list("maincommands", 0)), "new");
        add_command(rpg::to_upper(bj_list("maincommands", 1)), "continue", rpg::game_variables().number(827) >= 5);
        add_command(rpg::to_upper(bj_list("maincommands", 2)), "quit");
    }
};

class WindowMinigameTitle : public WindowBase {
public:
    using WindowBase::initialize;
    void setup() {
        double x = (640 - window_width()) / 2;
        double y = (480 - window_height()) / 3;
        WindowBase::initialize(x, y, window_width(), window_height());
        set_window_alpha(0);
    }
    double window_width() { return 640.0 / 2 + 64; }
    double window_height() { return fitting_height(3); }
    void refresh_text(const std::string& text) {
        g2d::Bitmap& b = c();
        b.clear();
        setup_minigame_font(b, "Times New Roman");
        change_text_color(text_color(6));
        if (!text.empty() && text.find("<br>") != std::string::npos) {
            double y = 0;
            size_t from = 0;
            while (true) {
                size_t at = text.find("<br>", from);
                std::string line = text.substr(from, at == std::string::npos ? std::string::npos : at - from);
                draw_text(rpg::trim(line), -18, y, window_width(), "center");
                y += 18;
                if (at == std::string::npos) break;
                from = at + 4;
            }
        } else {
            draw_text(text, -18, 18, window_width(), "center");
        }
    }
};

class WindowBlackJackData : public WindowBase {
public:
    using WindowBase::initialize;
    void setup(SceneBlackJack* scene) {
        scene_ = scene;
        WindowBase::initialize(0, 0, window_width(), window_height());
        set_window_alpha(0);
        player_score = 0;
        opponent_score = 0;
        current_earnings = 0;
        current_player_bonus = 1;
        show_opponent_hand = false;
        refresh();
    }
    double window_width() { return 640; }
    double window_height() { return 480; }
    void refresh();
    void get_card_image(int index, double x, double y) {
        g2d::BitmapPtr bitmap = load_picture("Cards");
        double pw = bitmap->width() / 13.0;
        double ph = bitmap->height() / 4.0;
        int n = index;
        double sx = (n % 13) * pw;
        double sy = std::floor(n / 13.0) * ph;
        c().blt(bitmap.get(), sx, sy, pw, ph, x, y);
    }

    bool has_hands = false;
    Hand player_hand, opponent_hand;
    double player_score = 0, opponent_score = 0;
    double current_earnings = 0;
    double current_player_bonus = 1;
    bool show_opponent_hand = false;

private:
    SceneBlackJack* scene_ = nullptr;
};

class SceneBlackJack : public SceneMenuBase {
public:
    SceneBlackJack() {
        rpg::game_system().save_bgm();
        rpg::audio_manager::stop_all();
    }
    const char* name() const override { return "Scene_BlackJack"; }

    void create() override {
        SceneMenuBase::create();
        title_commands_ = WindowBase::make<WindowBJTitleCommands>(0.0, 0.0);
        title_commands_->set_handler("new", [this] { start_game(); });
        title_commands_->set_handler("continue", [this] { continue_game(); });
        title_commands_->set_handler("quit", [this] { command_exit(); });
        title_commands_->set_handler("cancel", [this] { command_exit(); });
        add_window(title_commands_);
        title_commands_->visible = false;
        title_commands_->deactivate();
        data_window_ = std::make_shared<WindowBlackJackData>();
        data_window_->setup(this);
        data_window_->visible = false;
        add_window(data_window_);
        title_window_ = std::make_shared<WindowMinigameTitle>();
        title_window_->setup();
        add_window(title_window_);
        title_window_->visible = false;
        wager_window_ = WindowBase::make<WindowWager>(0.0, 0.0);
        wager_window_->set_handler("5", [this] { set_wager_amount(5); });
        wager_window_->set_handler("25", [this] { set_wager_amount(25); });
        wager_window_->set_handler("100", [this] { set_wager_amount(100); });
        wager_window_->set_handler("quit", [this] { command_quit(); });
        add_window(wager_window_);
        wager_window_->deactivate();
        wager_window_->visible = false;
        command_window_ = WindowBase::make<WindowBlackJackCommands>(0.0, 0.0);
        command_window_->set_handler("stand", [this] { command_stand(); });
        command_window_->set_handler("hit", [this] { command_hit(); });
        add_window(command_window_);
        command_window_->deactivate();
        command_window_->visible = false;
        start_fade_in(fade_speed(), false);
    }

    void create_background() override {
        SceneMenuBase::create_background();
        main_image_ = std::make_shared<g2d::Sprite>();
        main_image_->set_bitmap(load_picture("blackjack_bg"));
        add_child_at(main_image_, 1);
        title_screen_active_ = true;
    }

    void update() override {
        rpg::Scene::update();
        if (exit_fade_ > 0) {
            exit_fade_--;
            if (logo_) logo_->set_opacity(logo_->opacity() - 12);
            if (exit_fade_ <= 0) {
                if (logo_) remove_child(logo_.get());
                if (main_image_) remove_child(main_image_.get());
                pop_scene();
                rpg::game_system().replay_bgm();
            }
            return;
        }
        if (results_delay_ > 0) {
            command_window_->deactivate();
            results_delay_--;
            if (results_delay_ <= 0) {
                show_result_image(match_outcome_);
                end_game(winner_);
                winner_.clear();
                match_outcome_.clear();
            }
        }
        if (music_delay_ > 0) {
            music_delay_ -= 1;
            if (music_delay_ <= 0) {
                play_bj_bgm("minigame_blackjack");
                main_image_->set_bitmap(load_picture("blackjack_layout"));
                title_commands_->deactivate();
                title_commands_->visible = false;
                wager_window_->activate();
                wager_window_->visible = true;
                wager_window_->select(0);
                title_window_->visible = true;
                title_window_->refresh_text(bj_message(2) + number_text(earnings_) + bj_message(3));
                start_fade_in(100);
            }
        }
        if (game_results_) {
            if (input::is_triggered(input::OK)) {
                to_title();
                game_results_ = false;
            }
        }
        if (title_screen_active_) {
            if (!logo_) {
                logo_ = std::make_shared<g2d::Sprite>();
                logo_->set_bitmap(load_picture("blackjack_logo"));
                logo_->x = (640 - 418) / 2.0;
                logo_->y = 480;
                add_child(logo_);
                frame_ = std::make_shared<g2d::Sprite>();
                frame_->set_bitmap(load_picture("blackjack_frame"));
                add_child(frame_);
            } else if (logo_ && logo_->y > 128) {
                logo_->y -= 2;
                if (input::is_triggered(input::OK) || input::is_triggered(input::CANCEL)) {
                    logo_->y = 128;
                    title_commands_->activate();
                }
            } else {
                if (title_commands_->visible == false) {
                    if (frame_) remove_child(frame_.get());
                    frame_.reset();
                    title_commands_->refresh();
                    title_commands_->visible = true;
                    title_commands_->activate();
                }
            }
        }
        if (timer_ > 0 && wager_window_->visible) {
            timer_--;
            if (timer_ <= 0) title_window_->refresh_text(bj_message(2) + number_text(earnings_) + bj_message(3));
        }
        if (showing_opponent_action_) {
            if (command_window_->active) command_window_->deactivate();
            if (timer_ > 0) timer_--;
            if (action_) {
                if (input::is_triggered(input::OK) || timer_ < 1) {
                    remove_child(action_.get());
                    command_window_->refresh();
                    command_window_->activate();
                    command_window_->visible = true;
                    update_data();
                    showing_opponent_action_ = false;
                }
            }
        }
        if (results_open_) {
            if (command_window_->active) command_window_->deactivate();
            if (timer_ > 0) {
                timer_--;
            } else if (input::is_triggered(input::OK)) {
                if (result_) remove_child(result_.get());
                command_window_->refresh();
                command_window_->deactivate();
                command_window_->select(2);
                command_window_->visible = false;
                data_window_->show_opponent_hand = false;
                if (earnings_ <= 0) {
                    game_over();
                } else {
                    title_window_->refresh_text(bj_message(2) + number_text(earnings_) + bj_message(3));
                    title_window_->visible = true;
                    data_window_->visible = false;
                    soft_reset();
                    wager_window_->activate();
                    wager_window_->visible = true;
                }
                update_data();
                results_open_ = false;
            }
        }
    }

    double opponent_streak() const { return opponent_streak_; }
    double player_streak() const { return player_streak_; }
    double wager_amount() const { return wager_amount_; }

private:
    std::shared_ptr<WindowBJTitleCommands> title_commands_;
    std::shared_ptr<WindowBlackJackData> data_window_;
    std::shared_ptr<WindowMinigameTitle> title_window_;
    std::shared_ptr<WindowWager> wager_window_;
    std::shared_ptr<WindowBlackJackCommands> command_window_;
    g2d::SpritePtr main_image_, logo_, frame_, result_, action_;

    int exit_fade_ = 0;
    bool title_screen_active_ = true;
    Hand deck_, player_hand_, opponent_hand_;
    double player_score_ = 0, opponent_score_ = 0;
    std::string player_previous_play_, opponent_previous_play_;
    double player_streak_ = 0, opponent_streak_ = 0;
    double earnings_ = 100;
    double wager_amount_ = 0;
    bool game_in_progress_ = false;
    bool courtesy_options_ = false;
    bool results_open_ = false;
    bool showing_opponent_action_ = false;
    bool at_least_one_game_started_ = false;
    bool game_results_ = false;
    int music_delay_ = 0;
    int timer_ = 0;
    int results_delay_ = 0;
    std::string match_outcome_, winner_;

    void start_game() {
        start_fade_out(25);
        at_least_one_game_started_ = false;
        title_screen_active_ = false;
        if (logo_) remove_child(logo_.get());
        logo_.reset();
        music_delay_ = 30;
        earnings_ = 100;
    }

    void command_quit() {
        if (game_in_progress_) {
            command_window_->activate();
            return;
        }
        pay_out();
        hard_reset();
        data_window_->c().clear();
        command_window_->deactivate();
        command_window_->visible = false;
        game_results_ = true;
    }

    void game_over() {
        rpg::audio_manager::stop_all();
        hard_reset();
        command_window_->deactivate();
        command_window_->visible = false;
        wager_window_->visible = false;
        wager_window_->deactivate();
        data_window_->visible = false;
        title_window_->refresh_text(bj_message(0));
        title_window_->visible = true;
        game_results_ = true;
    }

    void continue_game() {
        start_game();
        earnings_ = rpg::game_variables().number(827);
        update_data();
    }

    void to_title() {
        rpg::audio_manager::stop_all();
        title_screen_active_ = true;
        main_image_->set_bitmap(load_picture("blackjack_bg"));
        wager_window_->visible = false;
        wager_window_->deactivate();
        title_window_->visible = false;
        title_screen_active_ = true;
        if (rpg::game_variables().number(827) > 0) title_commands_->select(1);
    }

    void command_exit() {
        exit_fade_ = 30;
        start_fade_out(30);
    }

    void set_wager_amount(double amount) {
        if (!(earnings_ >= amount)) {
            title_window_->refresh_text(bj_message(1) + number_text(amount));
            timer_ = 90;
            wager_window_->activate();
            return;
        }
        wager_amount_ = amount;
        title_window_->visible = false;
        wager_window_->deactivate();
        wager_window_->visible = false;
        update_data();
        data_window_->visible = true;
        command_window_->activate();
        command_window_->visible = true;
        command_window_->select(0);
        command_deal();
    }

    void draw_card(Hand& hand) {
        hand.insert(hand.begin(), deck_.empty() ? CardPtr() : deck_[0]);
        if (!deck_.empty()) deck_.erase(deck_.begin());
    }

    void command_deal() {
        data_window_->visible = true;
        title_window_->visible = false;
        deck_.clear();
        soft_reset();
        static const char* suits[] = {"Diamond", "Spade", "Heart", "Club"};
        static const char* types[] = {"K", "Q", "J", "10", "9", "8", "7", "6", "5", "4", "3", "2", "A"};
        int index = 0;
        for (const char* suit : suits) {
            for (const char* type : types) {
                auto card = std::make_shared<BlackJackCard>();
                card->suit = suit;
                card->type = type;
                card->index = index;
                deck_.push_back(card);
                index++;
            }
        }
        int cur = (int)deck_.size();
        while (cur != 0) {
            int loc = (int)std::floor(rng::next() * cur);
            cur -= 1;
            std::swap(deck_[(size_t)cur], deck_[(size_t)loc]);
        }
        draw_card(player_hand_);
        draw_card(player_hand_);
        draw_card(opponent_hand_);
        draw_card(opponent_hand_);
        game_in_progress_ = true;
        courtesy_options_ = false;
        at_least_one_game_started_ = true;
        command_window_->select(0);
        check_win_conditions();
        update_data();
        switch_phase("player");
    }

    void command_hit() {
        play_bj_se("mini_bj_cardplace");
        draw_card(player_hand_);
        player_previous_play_ = "hit";
        check_win_conditions();
        update_data();
        if (game_in_progress_) switch_phase("opponent");
    }

    void opponent_hit() {
        show_action_image("BJ-HIT");
        play_bj_se("mini_bj_cardplace");
        draw_card(opponent_hand_);
        opponent_previous_play_ = "hit";
        check_win_conditions();
        update_data();
        if (game_in_progress_) switch_phase("player");
    }

    void command_stand() {
        player_previous_play_ = "stand";
        check_win_conditions();
        update_data();
        if (game_in_progress_) switch_phase("opponent");
    }

    void opponent_stand() {
        show_action_image("BJ-STAY");
        opponent_previous_play_ = "stand";
        check_win_conditions();
        update_data();
        if (game_in_progress_) switch_phase("player");
    }

    void soft_reset() {
        deck_.clear();
        player_hand_.clear();
        opponent_hand_.clear();
        player_previous_play_.clear();
        opponent_previous_play_.clear();
        game_in_progress_ = false;
        courtesy_options_ = false;
        results_open_ = false;
        showing_opponent_action_ = false;
    }

    void hard_reset() {
        deck_.clear();
        player_score_ = 0;
        opponent_score_ = 0;
        player_hand_.clear();
        opponent_hand_.clear();
        player_streak_ = 0;
        opponent_streak_ = 0;
        earnings_ = rpg::game_variables().number(827);
        wager_amount_ = 0;
        game_in_progress_ = false;
        courtesy_options_ = false;
        results_open_ = false;
        showing_opponent_action_ = false;
    }

    void switch_phase(const std::string& owner) {
        if (owner == "opponent") {
            if (current_hand_value("player") >= 21) {
                command_window_->activate();
                return;
            } else if (current_hand_value("opponent") < 17) {
                opponent_hit();
                command_window_->activate();
            } else {
                opponent_stand();
                command_window_->activate();
            }
        } else {
            command_window_->activate();
        }
    }

    static int card_value(const CardPtr& c) { return c ? c->value() : 0; }

    int current_hand_value(const std::string& owner) {
        bool player = owner == "player";
        Hand& hand = player ? player_hand_ : opponent_hand_;
        int value = 0;
        for (auto& c : hand) value += card_value(c);
        if (value > 21) {
            for (size_t i = 0; i < hand.size(); i++) {
                bool cond = player ? (hand[i] && hand[i]->type == "A" && value > 21) : value > 21;
                if (cond) {
                    if (hand[i]) hand[i]->soft_ace = true;
                    value -= 10;
                }
                if (value <= 21) break;
            }
        }
        value = 0;
        for (auto& c : hand) value += card_value(c);
        return value;
    }

    void courtesy_options() {
        courtesy_options_ = true;
        switch_phase("player");
    }

    void end_game(const std::string& winner) {
        game_in_progress_ = false;
        if (winner == "player") {
            player_streak_ += 1;
            opponent_streak_ = 0;
        } else if (winner == "opponent") {
            player_streak_ = 0;
            opponent_streak_ += 1;
        } else {
            opponent_streak_ = 0;
            player_streak_ = 0;
        }
        if (winner == "player") {
            player_score_ += 1;
            earnings_ += wager_amount_ * player_streak_;
        } else if (winner == "opponent") {
            opponent_score_ += 1;
            earnings_ -= wager_amount_ * opponent_streak_;
        }
        update_data();
    }

    void update_data() {
        data_window_->has_hands = true;
        data_window_->player_hand = player_hand_;
        data_window_->opponent_hand = opponent_hand_;
        data_window_->player_score = player_score_;
        data_window_->opponent_score = opponent_score_;
        data_window_->current_earnings = earnings_;
        data_window_->current_player_bonus = player_streak_ + 1;
        data_window_->refresh();
    }

    void show_result_image(const std::string& filename) {
        if (winner_ == "player") play_bj_se("mini_bj_win");
        else if (winner_ == "opponent") play_bj_se("mini_bj_lose");
        command_window_->deactivate();
        data_window_->show_opponent_hand = true;
        update_data();
        result_ = std::make_shared<g2d::Sprite>();
        result_->set_bitmap(load_picture(filename));
        add_child(result_);
        results_open_ = true;
        timer_ = 120;
    }

    void show_action_image(const std::string& filename) {
        timer_ = 90;
        command_window_->deactivate();
        update_data();
        action_ = std::make_shared<g2d::Sprite>();
        action_->set_bitmap(load_picture(filename));
        add_child(action_);
        showing_opponent_action_ = true;
    }

    void check_win_conditions() {
        int pv = current_hand_value("player");
        int ov = current_hand_value("opponent");
        if (courtesy_options_) {
            courtesy_options_ = false;
            setup_results("BJ-LOSE", "opponent");
            results_delay_ = 180;
            return;
        }
        if (pv == 21 && ov == 21) {
            setup_results("BJ-TIE", "");
            results_delay_ = 120;
            return;
        } else if (pv == 21) {
            setup_results("BJ-NATURAL", "player");
            results_delay_ = 120;
            return;
        } else if (ov == 21) {
            update_data();
            courtesy_options();
            return;
        }
        if (pv > 21) {
            setup_results("BJ-BUST", "opponent");
            results_delay_ = 120;
            return;
        } else if (ov > 21) {
            setup_results("BJ-WIN", "player");
            results_delay_ = 120;
            return;
        }
        if (player_previous_play_ == "stand" && opponent_previous_play_ == "stand") {
            if (pv == ov) {
                setup_results("BJ-TIE", "");
                results_delay_ = 120;
            } else if (pv > ov) {
                setup_results("BJ-WIN", "player");
                results_delay_ = 120;
            } else {
                setup_results("BJ-LOSE", "opponent");
                results_delay_ = 120;
            }
            return;
        }
        update_data();
    }

    void setup_results(const std::string& outcome, const std::string& winner) {
        match_outcome_ = outcome;
        winner_ = winner;
    }

    void pay_out() {
        title_window_->visible = true;
        if (at_least_one_game_started_) title_window_->refresh_text(bj_message(2) + number_text(earnings_) + "!");
        else title_window_->refresh_text(bj_message(8) + number_text(earnings_) + "!");
        rpg::game_variables().set_value(827, js::Val::num(earnings_));
    }
};

void WindowBlackJackData::refresh() {
    g2d::Bitmap& b = c();
    b.clear();
    setup_minigame_font(b, "Times New Roman");
    change_text_color(text_color(6));
    double opp_streak = scene_ ? scene_->opponent_streak() : 0;
    double ply_streak = scene_ ? scene_->player_streak() : 0;
    double wager = scene_ ? scene_->wager_amount() : 0;
    std::string streak_fmt = bj_string("streak_string");
    std::string dealer_streak = opp_streak > 0 ? rpg::js_format(streak_fmt, {number_text(opp_streak)}) : std::string();
    std::string player_streak = ply_streak > 0 ? rpg::js_format(streak_fmt, {number_text(ply_streak)}) : std::string();
    draw_text(rpg::js_format(bj_string("dealer_streak"), {dealer_streak}), 48, 32, window_width());
    draw_text(rpg::js_format(bj_string("player_streak"), {player_streak}), 48, 196, window_width());
    draw_text(rpg::js_format(bj_string("score"), {number_text(opponent_score), number_text(player_score)}), 0, 22, window_width() - 76, "right");
    draw_text(rpg::js_format(bj_string("bet"), {number_text(wager), number_text(current_player_bonus), number_text(current_earnings)}), 0, 40,
              window_width() - 76, "right");
    double spacing = 96;
    if (has_hands) {
        for (size_t i = 0; i < player_hand.size(); i++) {
            if (player_hand.size() > 5) spacing = 96 - ((double)(player_hand.size() - 5) * 14);
            if (player_hand[i]) get_card_image(player_hand[i]->index, 64 + spacing * i, 232);
        }
        if (opponent_hand.size() > 5) spacing = 96 - ((double)(opponent_hand.size() - 5) * 14);
        for (size_t i = 0; i < opponent_hand.size(); i++) {
            if (i == 0 && !show_opponent_hand) {
                g2d::BitmapPtr bitmap = load_picture("CardBack");
                b.blt(bitmap.get(), 0, 0, bitmap->width(), bitmap->height(), 64 + spacing * i, 68);
            } else if (opponent_hand[i]) {
                get_card_image(opponent_hand[i]->index, 64 + spacing * i, 68);
            }
        }
    }
    reset_text_color();
}

}

rpg::SceneFactory blackjack_scene() { return rpg::scene_of<SceneBlackJack>("Scene_BlackJack"); }

static SceneRegistrar g_blackjack_registrar("Scene_BlackJack", blackjack_scene());

}
