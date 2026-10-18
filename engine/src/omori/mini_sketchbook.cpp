#include <cmath>
#include <memory>
#include <string>

#include "../js/vm.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "scene_menu_base.h"
#include "scenes.h"
#include "ui_util.h"
#include "window_base.h"

namespace ui {

namespace {

class SceneSketchbook : public SceneMenuBase {
public:
    SceneSketchbook() {
        page_ = 0;
        max_pages_ = rpg::game_variables().value(42).to_num();
        sketch_book_name_ = rpg::game_variables().value(43);
        js::Val size = rpg::game_variables().value(41);
        width_ = js::vm().get(size, js::Val::num(0)).to_num();
        height_ = js::vm().get(size, js::Val::num(1)).to_num();
    }

    const char* name() const override { return "Scene_Sketchbook"; }

    void create() override {
        SceneMenuBase::create();
        create_sketch_window();
        create_sketch_book_sprite();
        create_cursor_sprites();
        on_page_change();
    }

    void create_sketch_window() {
        back_window_ = WindowBase::make<WindowBase>(0.0, 0.0, width_, height_);
        back_window_->x = (640 - back_window_->width()) / 2;
        back_window_->y = (480 - back_window_->height()) / 2;
        add_child(back_window_);
    }

    void create_sketch_book_sprite() {
        sketch_book_sprite_ = std::make_shared<g2d::Sprite>();
        sketch_book_sprite_->x = back_window_->x + 10;
        sketch_book_sprite_->y = back_window_->y + 10;
        add_child(sketch_book_sprite_);
    }

    void create_cursor_sprites() {
        g2d::BitmapPtr bitmap = load_system("ACSArrows");
        left_cursor_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        left_cursor_sprite_->anchor_x = 0.5;
        left_cursor_sprite_->anchor_y = 0.5;
        left_cursor_sprite_->set_frame(64, 0, 32, 29);
        left_cursor_sprite_->x = back_window_->x - 10;
        left_cursor_sprite_->y = back_window_->y + (back_window_->height() / 2);
        add_child(left_cursor_sprite_);
        right_cursor_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        right_cursor_sprite_->anchor_x = 0.5;
        right_cursor_sprite_->anchor_y = 0.5;
        right_cursor_sprite_->set_frame(32, 0, 32, 29);
        right_cursor_sprite_->x = back_window_->x + back_window_->width() + 10;
        right_cursor_sprite_->y = back_window_->y + (back_window_->height() / 2);
        add_child(right_cursor_sprite_);
    }

    void update() override {
        SceneMenuBase::update();
        double fc = (double)rpg::frame_count();
        left_cursor_sprite_->anchor_x = 0.5 - (std::sin(fc * 0.2) * 0.1);
        right_cursor_sprite_->anchor_x = 0.5 + (std::sin(fc * 0.2) * 0.1);
        if (input::is_repeated(input::LEFT)) {
            if (page_ > 0) {
                play_cursor();
                page_--;
                on_page_change();
            }
            return;
        }
        if (input::is_repeated(input::RIGHT)) {
            if (page_ < max_pages_) {
                play_cursor();
                page_++;
                on_page_change();
            }
            return;
        }
        if (input::is_triggered(input::MENU)) {
            pop_scene();
            return;
        }
    }

    void on_page_change() {
        left_cursor_sprite_->visible = page_ > 0;
        right_cursor_sprite_->visible = page_ < max_pages_;
        sketch_book_sprite_->set_bitmap(load_picture(page_name()));
    }

    std::string page_name() const {
        if (sketch_book_name_.is_str()) return sketch_book_name_.to_str() + number_text(page_);
        if (sketch_book_name_.is_obj()) return sketch_book_name_.to_str() + number_text(page_);
        return number_text(sketch_book_name_.to_num() + page_);
    }

private:
    int page_ = 0;
    double max_pages_ = 0;
    js::Val sketch_book_name_;
    double width_ = 0, height_ = 0;
    std::shared_ptr<WindowBase> back_window_;
    std::shared_ptr<g2d::Sprite> sketch_book_sprite_, left_cursor_sprite_, right_cursor_sprite_;
};

}

rpg::SceneFactory sketchbook_scene() { return rpg::scene_of<SceneSketchbook>("Scene_Sketchbook"); }

static SceneRegistrar g_sketchbook_registrar("Scene_Sketchbook", sketchbook_scene());

}
