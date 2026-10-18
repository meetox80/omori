#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/random.h"
#include "../platform/platform.h"
#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/save.h"
#include "menu_common.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

namespace {

const char* kHangmanWord = "WELCOME TO BLACK SPACE";

int random_int(int max) { return (int)std::floor(rng::next() * max); }

std::vector<int> blackletter_item_ids() {
    std::vector<int> list;
    const Value& items = data::items();
    for (int i = 850; i < 876; i++) {
        const Value& item = items[i];
        if (!item.is_obj()) continue;
        const Value* bl = item["meta"].find("Blackletter");
        if (bl && bl->truthy()) list.push_back(i);
    }
    return list;
}

std::string blackletter_of(int id) { return rpg::trim(rpg::actor_meta(data::items()[id], "Blackletter")); }

std::string blackletter_clue_of(int id) { return rpg::trim(rpg::actor_meta(data::items()[id], "BlackLetterClue")); }

bool party_has(int id) { return rpg::game_party().has_item(rpg::ItemRef::item(id), false); }

std::string hangman_word() { return kHangmanWord; }

int black_letter_collection_state() {
    std::string word = hangman_word();
    std::vector<std::string> letters;
    for (char ch : word) {
        if (std::isspace((unsigned char)ch)) continue;
        std::string l = rpg::to_upper(std::string(1, ch));
        if (std::find(letters.begin(), letters.end(), l) == letters.end()) letters.push_back(l);
    }
    int right = 0, wrong = 0;
    for (int id : blackletter_item_ids()) {
        if (!party_has(id)) continue;
        std::string bl = rpg::to_upper(blackletter_of(id));
        if (std::find(letters.begin(), letters.end(), bl) != letters.end()) right++;
        else wrong++;
    }
    if (wrong >= 14) return 1;
    if (right >= (int)letters.size()) return 0;
    return 2;
}

void play_se_obj(const std::string& name, double volume, double pitch, double pan) {
    rpg::AudioObj a;
    a.valid = true;
    a.name = name;
    a.volume = volume;
    a.pitch = pitch;
    a.pan = pan;
    rpg::audio_manager::play_se(a);
}

}

class SpriteOmoBlackletter : public g2d::Sprite {
public:
    explicit SpriteOmoBlackletter(const std::string& letter) {
        anchor_x = 0.5;
        anchor_y = 0.5;
        letter_ = letter;
        setup_bitmap();
        flip_direction = 0;
    }

    void setup_bitmap() {
        if (letter_.empty()) {
            g2d::BitmapPtr b = g2d::Bitmap::create(32, 32);
            b->fill_rect(1, 30, 27, 2, "rgba(255, 255, 255, 1)");
            set_bitmap(b);
        } else {
            set_bitmap(load_system("Blackletters_menu"));
            int index = (unsigned char)letter_[0] - 65;
            double bx = (index % 5) * 32;
            double by = std::floor(index / 5.0) * 32;
            set_frame(bx, by, 32, 32);
        }
    }

    void setup_flip(int duration = 15, int times = 1) {
        flip_direction = -1;
        flip_duration = duration;
        flip_times = times;
        base_flip_duration = duration;
    }

    void update() override {
        g2d::Sprite::update();
        if (flip_direction != 0) {
            int d = flip_duration;
            if (d > 0) {
                scale_x = (scale_x * (d - 1) + flip_direction) / d;
                flip_duration--;
                if (flip_duration <= 0) {
                    if (flip_times > 0) {
                        flip_direction = flip_direction == -1 ? 1 : -1;
                        flip_duration = base_flip_duration;
                        flip_times--;
                    } else {
                        flip_direction = 0;
                    }
                }
            }
        }
    }

    const std::string& letter() const { return letter_; }

    int flip_direction = 0;
    int flip_duration = 0;
    int flip_times = 0;
    int base_flip_duration = 0;

private:
    std::string letter_;
};

class WindowPadded4 : public WindowBase {
public:
    int standard_padding() override { return 4; }
};

class WindowOmoBlackLetterList : public WindowCommand {
public:
    struct Entry {
        std::string letter;
        std::string clue;
        int item_id = 0;
    };

    void initialize(const std::string& word) {
        hangman_word_ = word;
        WindowCommand::initialize(0, 105);
        select_letter(rpg::game_party().last_black_letter());
    }
    using WindowCommand::initialize;

    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 10; }
    double window_width() override { return 386; }
    double window_height() override { return 360 - 55; }
    int max_cols() override { return 1; }
    double item_height() override { return 34 + 1; }
    int spacing() override { return 5; }
    double custom_cursor_rect_x_offset() override { return 8; }
    double custom_cursor_rect_y_offset() override { return 0; }
    void play_ok_sound() override {}

    void select_letter(std::string letter) {
        letter = rpg::to_upper(letter);
        for (size_t i = 0; i < entries_.size(); i++) {
            if (entries_[i].letter == letter) {
                select((int)i);
                break;
            }
        }
    }

    void make_command_list() override {
        entries_.clear();
        for (int id : blackletter_item_ids()) {
            Entry e;
            e.letter = blackletter_of(id);
            e.clue = blackletter_clue_of(id);
            e.item_id = id;
            entries_.push_back(e);
            add_command(data::js_string(data::items()[id]["name"]), "ok", true);
        }
    }

    void draw_item(int index) override {
        if (index < 0 || index >= (int)entries_.size()) return;
        ItemRect rect = item_rect(index);
        const Entry& e = entries_[(size_t)index];
        c().font_size = 24;
        change_text_color(text_color(0));
        draw_text(e.clue, 75, rect.y - 5, rect.width - 135);
        rect.x = rect.width - (rect.height + 10);
        rect.height -= 6;
        rect.width = rect.height;
        draw_black_letter_text(e.letter, 35, rect.y);
        rect.y += 3;
        c().fill_rect(rect.x, rect.y, rect.width, rect.height, "rgba(255, 255, 255, 1)");
        c().clear_rect(rect.x + 3, rect.y + 3, rect.width - 6, rect.height - 6);
        c().font_size = 20;
        int state = 2;
        if (party_has(e.item_id)) state = hangman_word_.find(e.letter) != std::string::npos ? 0 : 1;
        g2d::BitmapPtr symbols = load_system("HangmanStates");
        c().blt(symbols.get(), 20 * state, 0, 20, 20, rect.x + 5, rect.y + 4, 20, 20);
    }

protected:
    void update_transform_hook() override {
        double w = width(), h = height();
        double p = 28, q = p / 2;
        down_arrow_sprite()->move(w - q, h - q);
        up_arrow_sprite()->move(w - q, q);
    }

private:
    std::string hangman_word_;
    std::vector<Entry> entries_;
};

class WindowOmoBlackLetterWord : public WindowBase {
public:
    void initialize(const std::string& word, bool forced) {
        hangman_word_ = word;
        forced_ = forced;
        WindowBase::initialize(0, 480 - 70, 640, 70);
        letter_collection_state_ = black_letter_collection_state();
        spin_index_ = 0;
        intro_phase_ = -1;
        last_letter_ = rpg::game_party().last_black_letter();
        create_letter_sprites();
    }
    using WindowBase::initialize;

    int standard_padding() override { return 4; }

    void create_letter_sprites() {
        std::vector<std::string> blackletters;
        for (int id : blackletter_item_ids()) {
            if (party_has(id)) blackletters.push_back(blackletter_of(id));
        }
        letter_sprites_.clear();
        double sx = 29;
        for (char ch : hangman_word_) {
            std::string letter = rpg::to_upper(std::string(1, ch));
            if (letter != " ") {
                bool owned = std::find(blackletters.begin(), blackletters.end(), letter) != blackletters.end();
                auto sprite = std::make_shared<SpriteOmoBlackletter>(owned ? letter : std::string());
                sprite->x = sx;
                sprite->y = 34;
                if (forced_ && letter == last_letter_) {
                    sprite->set_opacity(0);
                    intro_phase_ = 0;
                }
                letter_sprites_.push_back(sprite);
                add_child(sprite);
                sx += 29;
            } else {
                sx += 20;
            }
        }
    }

    void refresh() { c().clear(); }

    void update() override {
        WindowBase::update();
        update_letter_animation();
    }

    void update_letter_animation() {
        if (intro_phase_ >= 0) {
            switch (intro_phase_) {
            case 0: {
                bool finished = true;
                for (auto& sprite : letter_sprites_) {
                    if (sprite->opacity() < 255) {
                        sprite->set_opacity(sprite->opacity() + 5);
                        finished = false;
                    }
                }
                if (finished) {
                    for (auto& sprite : letter_sprites_) {
                        if (sprite->letter() == last_letter_) sprite->setup_flip();
                    }
                    intro_phase_ = 1;
                }
                break;
            }
            case 1: {
                bool finished = true;
                for (auto& sprite : letter_sprites_) {
                    if (sprite->flip_duration > 0) finished = false;
                }
                if (finished) {
                    for (auto& sprite : letter_sprites_) {
                        if (!sprite->letter().empty()) sprite->setup_flip();
                    }
                    intro_phase_ = -1;
                }
                break;
            }
            }
            return;
        }
        if (letter_collection_state_ != 0) return;
        if (letter_sprites_.empty()) return;
        int n = (int)letter_sprites_.size();
        if (spin_index_ > 0) {
            if (spin_index_ >= n) return;
            auto& sprite = letter_sprites_[(size_t)spin_index_];
            auto& last_sprite = letter_sprites_[(size_t)spin_index_ - 1];
            if (last_sprite->flip_direction == -1 && last_sprite->flip_duration <= 3) {
                sprite->setup_flip();
                spin_index_ = (spin_index_ + 1) % n;
            }
        } else {
            auto& last_sprite = letter_sprites_[(size_t)n - 1];
            if (last_sprite->flip_direction == 0) {
                letter_sprites_[(size_t)spin_index_]->setup_flip();
                spin_index_ = (spin_index_ + 1) % n;
            }
        }
    }

private:
    std::string hangman_word_;
    bool forced_ = false;
    int letter_collection_state_ = 2;
    int spin_index_ = 0;
    int intro_phase_ = -1;
    std::string last_letter_;
    std::vector<std::shared_ptr<SpriteOmoBlackletter>> letter_sprites_;
};

class WindowOmoHangmanBody : public WindowBase {
public:
    struct BodyPart {
        std::string name;
        g2d::SpritePtr sprite;
        g2d::Rect src;
        int letter_index = 0;
    };

    void initialize(const std::string& word) {
        hangman_word_ = word;
        animation_delay_ = 30;
        inert_ = rpg::game_switches().value(45);
        double w = 254;
        WindowBase::initialize(640 - w, 0, w, 410);
        anim_part_ = -1;
        anim_duration_ = 10;
        create_background_sprite();
        create_body_sprites();
        create_letter_sprites();
    }
    using WindowBase::initialize;

    int standard_padding() override { return 4; }

    g2d::BitmapPtr sheet_bitmap() { return load_picture("HANGMAN_Sheet"); }

    bool is_busy_animating() {
        if (rpg::game_party().black_letter_index < (int)letter_sprites_.size()) return true;
        if (anim_part_ >= 0) return true;
        return false;
    }

    void create_background_sprite() {
        background_sprite_ = std::make_shared<g2d::Sprite>(sheet_bitmap());
        background_sprite_->x = 3;
        background_sprite_->y = 3;
        background_sprite_->set_frame(2, 1, 247, 403);
        add_child_to_back(background_sprite_);
    }

    void create_letter_sprites() {
        letter_sprites_.clear();
        std::vector<std::string> black_letters;
        for (int id : rpg::game_party().black_letters) black_letters.push_back(rpg::to_upper(blackletter_of(id)));
        if (inert_) return;
        int index = 0;
        for (const std::string& letter : black_letters) {
            bool in_word = false;
            for (char ch : hangman_word_) {
                if (std::string(1, ch) == letter) in_word = true;
            }
            if (letter != " " && !in_word) {
                auto sprite = std::make_shared<SpriteOmoBlackletter>(letter);
                sprite->x = 24 + ((index % 7) * 34);
                sprite->y = 24 + (std::floor(index / 7.0) * 34);
                sprite->set_opacity(index < rpg::game_party().black_letter_index ? 255 : 0);
                letter_sprites_.push_back(sprite);
                add_child(sprite);
                index++;
            }
        }
    }

    void create_body_sprites() {
        body_sprite_container_ = std::make_shared<g2d::Sprite>();
        body_sprite_container_->y = 25;
        add_child(body_sprite_container_);
        if (inert_) {
            background_sprite_->set_frame(253, 4, 247, 403);
            body_sprite_container_->add_child(background_sprite_);
            return;
        }
        struct Src {
            const char* name;
            double x, y;
            g2d::Rect rect;
        };
        static const Src source[] = {
            {"rope", 71, 76, g2d::Rect(253, 447, 21, 63)},      {"base1", 84, 75, g2d::Rect(277, 447, 82, 21)},
            {"base2", 158, 88, g2d::Rect(253, 513, 21, 246)},   {"base3", 88, 324, g2d::Rect(277, 471, 126, 15)},
            {"head", 46, 131, g2d::Rect(277, 489, 64, 63)},     {"body", 68, 180, g2d::Rect(344, 489, 38, 52)},
            {"rightArm", 77, 192, g2d::Rect(385, 489, 7, 28)},  {"leftArm", 87, 190, g2d::Rect(395, 489, 7, 32)},
            {"rightLeg", 75, 228, g2d::Rect(344, 544, 12, 35)}, {"leftLeg", 93, 228, g2d::Rect(359, 544, 10, 32)},
            {"rightEye", 62, 165, g2d::Rect(277, 555, 12, 7)},  {"leftEye", 81, 161, g2d::Rect(292, 555, 16, 7)},
            {"mouth", 74, 172, g2d::Rect(277, 565, 11, 7)},     {"hair", 47, 134, g2d::Rect(277, 582, 65, 104)},
        };
        body_parts_.clear();
        g2d::BitmapPtr bitmap = sheet_bitmap();
        int i = 0;
        for (const Src& d : source) {
            auto sprite = std::make_shared<g2d::Sprite>(bitmap);
            if (i < rpg::game_party().black_letter_index) sprite->set_frame(d.rect.x, d.rect.y, d.rect.width, d.rect.height);
            else sprite->set_frame(d.rect.x, d.rect.y, 0, 0);
            sprite->y = d.y;
            sprite->x = d.x;
            BodyPart part;
            part.name = d.name;
            part.sprite = sprite;
            part.src = d.rect;
            part.letter_index = i;
            body_parts_.push_back(part);
            body_sprite_container_->add_child(sprite);
            i++;
        }
    }

    void refresh() { c().clear(); }

    void update() override {
        WindowBase::update();
        update_black_letter_index();
        update_body_animations();
    }

    void update_black_letter_index() {
        if (anim_part_ < 0 && animation_delay_ > 0) {
            animation_delay_--;
            return;
        }
        int index = rpg::game_party().black_letter_index;
        if (index < (int)letter_sprites_.size()) {
            if (anim_part_ < 0 && index < (int)body_parts_.size()) {
                anim_part_ = index;
                anim_duration_ = body_parts_[(size_t)index].name == "mouth" ? 2 : 20;
                rpg::game_party().black_letter_index++;
                animation_delay_ = 30;
            }
        }
    }

    void update_body_animations() {
        if (anim_part_ < 0) return;
        BodyPart& part = body_parts_[(size_t)anim_part_];
        g2d::Sprite* sprite = part.sprite.get();
        SpriteOmoBlackletter* letter_sprite =
            part.letter_index < (int)letter_sprites_.size() ? letter_sprites_[(size_t)part.letter_index].get() : nullptr;
        double d = anim_duration_;
        const std::string& n = part.name;
        if (n == "rope" || n == "base2" || n == "rightArm" || n == "leftArm" || n == "rightLeg" || n == "leftLeg" || n == "head" ||
            n == "body" || n == "hair") {
            sprite->set_height((sprite->height() * (d - 1) + part.src.height) / d);
            sprite->set_width(part.src.width);
        } else {
            sprite->set_height(part.src.height);
            sprite->set_width((sprite->width() * (d - 1) + part.src.width) / d);
        }
        if (letter_sprite) letter_sprite->set_opacity((letter_sprite->opacity() * (d - 1) + 255) / d);
        if (anim_duration_ >= 20) play_se_obj("SE_chalk", 60, random_int(80) + 70, 30);
        anim_duration_--;
        if (anim_duration_ <= 0) {
            anim_part_ = -1;
            anim_duration_ = 1;
        }
    }

private:
    std::string hangman_word_;
    int animation_delay_ = 30;
    bool inert_ = false;
    int anim_part_ = -1;
    int anim_duration_ = 10;
    g2d::SpritePtr background_sprite_;
    g2d::SpritePtr body_sprite_container_;
    std::vector<BodyPart> body_parts_;
    std::vector<std::shared_ptr<SpriteOmoBlackletter>> letter_sprites_;
};

class GlitchOverlay : public g2d::Sprite {
public:
    void set_slices(int value) {
        if (value == slices_) return;
        slices_ = value;
        sizes_.assign((size_t)std::max(value, 0), 0.0);
        offsets_.assign((size_t)std::max(value, 0), 0.0);
        randomize_sizes();
        randomize_offsets();
        dirty_ = true;
    }
    void set_direction(double value) {
        if (value == direction_) return;
        direction_ = value;
        dirty_ = true;
    }
    void set_seed(double value) { seed_ = value; }
    int slices() const { return slices_; }

    void update() override {
        g2d::Sprite::update();
        if (slices_ <= 0) {
            if (!children.empty()) remove_children();
            visible = false;
            return;
        }
        if (dirty_) rebuild();
        visible = true;
    }

private:
    int slices_ = 0;
    double direction_ = 0;
    double seed_ = 0;
    double offset_ = 100;
    bool dirty_ = false;
    std::vector<double> sizes_;
    std::vector<double> offsets_;

    void randomize_sizes() {
        if (slices_ <= 0) return;
        int last = slices_ - 1;
        double size = 512;
        double min = std::min(8 / size, 0.9 / slices_);
        double rest = 1;
        for (int i = 0; i < last; i++) {
            double average_width = rest / (last - i);
            double w = std::max(average_width * (1 - rng::next() * 0.6), min);
            sizes_[(size_t)i] = w;
            rest -= w;
        }
        sizes_[(size_t)last] = rest;
        for (int i = (int)sizes_.size() - 1; i > 0; i--) {
            int j = (int)std::floor(rng::next() * (i + 1));
            std::swap(sizes_[(size_t)i], sizes_[(size_t)j]);
        }
    }

    void randomize_offsets() {
        for (auto& o : offsets_) o = rng::next() * (rng::next() < 0.5 ? -1 : 1);
    }

    void rebuild() {
        dirty_ = false;
        remove_children();
        g2d::BitmapPtr shot = rpg::scene_manager::snap();
        if (!shot) return;
        double rad = direction_ * M_PI / 180.0;
        double cos_dir = std::cos(rad), sin_dir = std::sin(rad);
        double w = 640, h = 480;
        double y0 = 0;
        for (size_t i = 0; i < sizes_.size(); i++) {
            double sh = sizes_[i] * h;
            if (sh <= 0) continue;
            double disp = offsets_[i] * offset_;
            double dx = std::fmod(cos_dir * disp, w);
            double dy = sin_dir * disp;
            for (double base : {dx, dx < 0 ? dx + w : dx - w}) {
                auto s = std::make_shared<g2d::Sprite>(shot);
                s->set_frame(0, y0, w, sh);
                s->x = -base;
                s->y = y0 - dy;
                add_child(s);
            }
            y0 += sh;
        }
    }
};

class SceneOmoBlackLetterMenu : public SceneBaseEX {
public:
    explicit SceneOmoBlackLetterMenu(bool forced) {
        forced_ = forced;
        crashing_ = false;
        collection_state_ = black_letter_collection_state();
        inert_ = rpg::game_switches().value(45);
        if (collection_state_ == 1 && !inert_) {
            double time = 32.0 / 60.0;
            rpg::audio_manager::fade_out_bgm(time);
            rpg::audio_manager::fade_out_bgs(time);
            rpg::audio_manager::fade_out_me(time);
        }
    }

    const char* name() const override { return "Scene_OmoBlackLetterMenu"; }

    void start() override {
        SceneBaseEX::start();
        start_fade_in(slow_fade_speed());
        if (collection_state_ == 0) rpg::game_switches().set_value(15, false);
    }

    void create() override {
        SceneBaseEX::create();
        create_background();
        window_container_ = std::make_shared<g2d::Sprite>();
        add_child(window_container_);
        rpg::GameParty& party = rpg::game_party();
        party.black_letter_index = 0;
        party.black_letters.clear();
        for (int id : blackletter_item_ids()) {
            if (party_has(id)) party.black_letters.push_back(id);
        }
        create_hangman_header_window();
        create_hangman_header_help_window();
        create_black_letter_list_window();
        create_hangman_body_window();
        create_hangman_word_window();
        hangman_something_ = std::make_shared<g2d::Sprite>(load_picture("hangman_something"));
        hangman_something_->scale_x = 0.1;
        hangman_something_->scale_y = 0.1;
        hangman_something_->x = 640 - 200;
        hangman_something_->y = 150;
        hangman_something_->set_opacity(0);
        window_container_->add_child(hangman_something_);
        glitch_ = std::make_shared<GlitchOverlay>();
        glitch_->visible = false;
        add_child(glitch_);
    }

    void create_background() {
        background_sprite_ = std::make_shared<g2d::Sprite>(rpg::scene_manager::background_bitmap());
        add_child(background_sprite_);
    }

    void create_hangman_header_window() {
        header_window_ = WindowBase::make<WindowPadded4>(0.0, 0.0, 386.0, 45.0);
        header_window_->draw_black_letter_text(data::js_string(blue("Omori_Blackletter_Menu")["hangman"]), 5, 2, 26);
        window_container_->add_child(header_window_);
    }

    void create_hangman_header_help_window() {
        auto& header = header_window_;
        help_window_ = WindowBase::make<WindowPadded4>(0.0, header->height(), header->width(), 60.0);
        help_window_->c().font_size = 22;
        const Value& loc = blue("Omori_Blackletter_Menu");
        help_window_->draw_text(data::js_string(loc["collect_desc"]), 5, -7, 400);
        help_window_->draw_text(data::js_string(loc["incorrect_desc"]), 5, 15, 400);
        window_container_->add_child(help_window_);
    }

    void create_black_letter_list_window() {
        list_window_ = std::shared_ptr<WindowOmoBlackLetterList>(new WindowOmoBlackLetterList());
        list_window_->initialize(hangman_word());
        if (!forced_) list_window_->select(0);
        window_container_->add_child(list_window_);
    }

    void create_hangman_body_window() {
        body_window_ = std::shared_ptr<WindowOmoHangmanBody>(new WindowOmoHangmanBody());
        body_window_->initialize(hangman_word());
        window_container_->add_child(body_window_);
    }

    void create_hangman_word_window() {
        word_window_ = std::shared_ptr<WindowOmoBlackLetterWord>(new WindowOmoBlackLetterWord());
        word_window_->initialize(hangman_word(), forced_);
        window_container_->add_child(word_window_);
    }

    void update() override {
        SceneBaseEX::update();
        if (!crashing_) {
            if (!inert_ && collection_state_ == 1) {
                if (!body_window_->is_busy_animating()) start_crash();
            } else if (input::is_triggered(input::CANCEL)) {
                if (!is_busy()) {
                    rpg::scene_manager::pop();
                    input::clear();
                }
            }
        }
        update_window_shake();
    }

    void start_crash() {
        crashing_ = true;
        rpg::game_switches().set_value(45, true);
        rpg::save::save_game(rpg::save::kCrashSlot);
        crash_count_ = 0;
        rpg::AudioObj bgs;
        bgs.valid = true;
        bgs.name = "SEheartbeat";
        bgs.volume = 50;
        bgs.pitch = 100;
        rpg::audio_manager::play_bgs(bgs);
        queue([this] {
            start_function_wait([this] {
                crash_count_++;
                hangman_something_->set_opacity(hangman_something_->opacity() + 50);
                if (crash_count_ >= 24) {
                    crash_count_ = 0;
                    return false;
                }
                return true;
            });
        });
        queue_glitch(2, 0);
        queue_glitch(2, 0);
        queue_glitch(2, 0);
        queue_glitch(2, 0);
        queue([] { plat::request_exit(); });
        queue([this] {
            hangman_something_->x = 100;
            hangman_something_->y = 0;
            hangman_something_->scale_x = 1;
            hangman_something_->scale_y = 1;
            add_child(hangman_something_);
            if (glitch_) add_child(glitch_);
            play_se_obj("mini_sme_move", 100, 100, 0);
            start_window_shake(30);
        });
        queue_glitch(2, 3);
        queue_glitch(2, 3);
        queue_glitch(2, 3);
        queue_wait(2);
        queue([this] {
            hangman_something_->set_opacity(0);
            crashing_ = false;
        });
    }

    void queue_glitch(int duration, int wait_frames) {
        queue([this] {
            play_se_obj("mini_sme_move", 100, 100, 0);
            glitch_->set_seed(rng::next());
            glitch_->set_slices(15 + random_int(10));
            glitch_->set_direction(random_int(10) * std::sin((double)rpg::scene_manager::frame_count()));
        });
        queue_wait(duration);
        queue([this] {
            glitch_->set_slices(0);
            glitch_->set_direction(0);
        });
        if (wait_frames > 0) queue_wait(wait_frames);
    }

    void start_window_shake(int duration) {
        window_shake_duration_ = duration;
        pivot_x = 640 / 2;
        pivot_y = 480 / 2;
    }

    void stop_window_shake() {
        scale_x = 1.0;
        scale_y = 1.0;
        x = 0;
        y = 0;
        pivot_x = 0;
        pivot_y = 0;
        window_shake_duration_ = 0;
    }

    void update_window_shake() {
        if (window_shake_duration_ > 0) {
            window_shake_duration_--;
            double s = std::sin((double)rpg::scene_manager::frame_count());
            x = random_int(640 / 2) * s;
            y = random_int(480 / 2) * s;
            x += random_int(200) * s;
            y += random_int(200) * s;
            scale_x = 1.0 + rng::next();
            scale_y = 1.0 + rng::next();
            if (random_int(100) > 50) scale_x = -scale_x;
            if (random_int(100) < 5) scale_y = -scale_y;
            if (window_shake_duration_ <= 0) stop_window_shake();
        }
    }

private:
    bool forced_ = false;
    bool crashing_ = false;
    bool inert_ = false;
    int collection_state_ = 2;
    int crash_count_ = 0;
    int window_shake_duration_ = 0;
    g2d::SpritePtr background_sprite_;
    g2d::SpritePtr window_container_;
    g2d::SpritePtr hangman_something_;
    std::shared_ptr<GlitchOverlay> glitch_;
    std::shared_ptr<WindowPadded4> header_window_;
    std::shared_ptr<WindowPadded4> help_window_;
    std::shared_ptr<WindowOmoBlackLetterList> list_window_;
    std::shared_ptr<WindowOmoHangmanBody> body_window_;
    std::shared_ptr<WindowOmoBlackLetterWord> word_window_;
};

}

rpg::SceneFactory black_letter_menu_scene(bool forced) {
    return rpg::SceneFactory{"Scene_OmoBlackLetterMenu",
                             [forced] { return std::unique_ptr<rpg::Scene>(new menu::SceneOmoBlackLetterMenu(forced)); }};
}

static SceneRegistrar r_black_letter_menu("Scene_OmoBlackLetterMenu", black_letter_menu_scene(false));

}
