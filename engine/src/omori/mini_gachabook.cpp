#include <memory>
#include <string>
#include <vector>

#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/party.h"
#include "../rpg/system.h"
#include "scene_menu_base.h"
#include "scenes.h"
#include "ui_util.h"
#include "window_selectable.h"

namespace ui {

namespace {

std::string gacha_book_text(const char* code) { return lang_message_text(code); }

bool simple_display() { return js::string_to_number(data::plugin_param("GachaBook", "Simple Display", "0")) != 0; }

class WindowGachaBookStatus : public WindowBase {
public:
    void initialize(double x, double y, double w, double h) override {
        WindowBase::initialize(x, y, w, h);
        item_sprite_ = std::make_shared<g2d::Sprite>();
        item_sprite_->anchor_x = 0.5;
        item_sprite_->anchor_y = 0.5;
        item_sprite_->x = w / 2 - 20;
        item_sprite_->y = h / 2;
        add_child_to_back(item_sprite_);
        refresh();
    }

    void set_item(const rpg::ItemRef& item) {
        if (item_ != item) {
            item_ = item;
            refresh();
        }
    }

    void update() override {
        WindowBase::update();
        g2d::BitmapPtr b = item_sprite_->bitmap();
        if (b) {
            double bitmap_height = b->height();
            double contents_h = c().height();
            double scale = 1;
            if (bitmap_height > contents_h) scale = contents_h / bitmap_height;
            item_sprite_->scale_x = scale;
            item_sprite_->scale_y = scale;
        }
    }

    void refresh() {
        double x = 0;
        double y = 0;
        double lh = line_height();
        c().clear();
        rpg::GameSystem& sys = rpg::game_system();
        if (!item_.valid() || !sys.is_in_gacha_book(item_.class_name(), item_.id)) {
            item_sprite_->set_bitmap(nullptr);
            return;
        }
        const Value& d = item_.data();
        g2d::BitmapPtr bitmap;
        if (d["meta"]["gachaImage"].truthy()) {
            bitmap = load_picture(data::js_string(d["meta"]["gachaImage"]));
            bitmap->smooth = true;
        }
        item_sprite_->set_bitmap(bitmap);
        double icon_box_width = kIconWidth + 4;
        draw_item_name(item_, x, y);
        draw_item_number(item_, x + text_width(d["name"].string()) + icon_box_width, y);
        x = text_padding();
        y = lh + text_padding();
        if (!simple_display()) {
            std::string rank = "-";
            if (d["meta"]["gachaRank"].truthy()) rank = data::js_string(d["meta"]["gachaRank"]);
            change_text_color(system_color());
            draw_text(gacha_book_text("gacha_minigame.message_7"), x, y, 120);
            reset_text_color();
            draw_text(rank, x + 120, y, 120, "right");
            y += lh;
            double price_num = d["price"].num();
            std::string price = price_num > 0 ? number_text(price_num) : std::string("-");
            change_text_color(system_color());
            draw_text(gacha_book_text("gacha_minigame.message_4"), x, y, 120);
            reset_text_color();
            draw_text(price, x + 120, y, 120, "right");
            y += lh;
            if (item_.is_weapon() || item_.is_armor()) {
                std::string etype = data::js_string(data::system()["equipTypes"][d["etypeId"].as_int()]);
                change_text_color(system_color());
                draw_text(gacha_book_text("gacha_minigame.message_5"), x, y, 120);
                reset_text_color();
                draw_text(etype, x + 120, y, 120, "right");
                y += lh;
                std::string type;
                if (item_.is_weapon()) type = data::js_string(data::system()["weaponTypes"][d["wtypeId"].as_int()]);
                else type = data::js_string(data::system()["armorTypes"][d["atypeId"].as_int()]);
                change_text_color(system_color());
                draw_text(gacha_book_text("gacha_minigame.message_6"), x, y, 120);
                reset_text_color();
                draw_text(type, x + 120, y, 120, "right");
                double rewards_width = 220;
                x = c().width() - rewards_width;
                y = lh + text_padding();
                for (int i = 2; i < 8; i++) {
                    change_text_color(system_color());
                    draw_text(system_text("param", std::to_string(i)), x, y, 160);
                    reset_text_color();
                    draw_text(data::js_string(d["params"][i]), x + 160, y, 60, "right");
                    y += lh;
                }
            }
        }
        x = 0;
        y = text_padding() * 2 + lh * 7;
        draw_text_ex(d["description"].string(), x, y);
    }

    void draw_item_number(const rpg::ItemRef& item, double x, double y) {
        draw_text(":", x, y, 4294967295.0);
        draw_text(number_text(rpg::game_party().num_items(item)), x + text_width("00"), y, 4294967295.0);
    }

private:
    rpg::ItemRef item_;
    std::shared_ptr<g2d::Sprite> item_sprite_;
};

int g_last_top_row = 0;
int g_last_index = 0;

class WindowGachaBookIndex : public WindowSelectable {
public:
    void initialize(double x, double y) {
        double w = 640;
        double h = fitting_height(6);
        WindowSelectable::initialize(x, y, w, h);
        refresh();
        set_top_row(g_last_top_row);
        select(g_last_index);
        activate();
    }
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }

    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int max_cols() override { return 3; }
    int max_items() override { return (int)list_.size(); }

    void set_status_window(WindowGachaBookStatus* w) {
        status_window_ = w;
        update_status();
    }

    void update() override {
        WindowSelectable::update();
        update_status();
    }

    void update_status() {
        if (status_window_) {
            int i = index();
            status_window_->set_item(i >= 0 && i < (int)list_.size() ? list_[(size_t)i] : rpg::ItemRef());
        }
    }

    void refresh() override {
        list_.clear();
        rpg::GameSystem& sys = rpg::game_system();
        auto collect = [&](rpg::ItemRef::Kind kind, const Value& table) {
            for (size_t i = 1; i < table.size(); i++) {
                rpg::ItemRef item{kind, (int)i};
                if (!item.valid()) continue;
                if (!item.data()["name"].truthy()) continue;
                if (sys.is_in_gacha(item.class_name(), item.id)) list_.push_back(item);
            }
        };
        collect(rpg::ItemRef::Item, data::items());
        collect(rpg::ItemRef::Weapon, data::weapons());
        collect(rpg::ItemRef::Armor, data::armors());
        create_contents();
        draw_all_items();
    }

    void draw_item(int index) override {
        const rpg::ItemRef& item = list_[(size_t)index];
        ItemRect rect = item_rect(index);
        double width = rect.width - text_padding();
        if (rpg::game_system().is_in_gacha_book(item.class_name(), item.id)) {
            draw_item_name(item, rect.x, rect.y, width);
        } else {
            double iw = kIconWidth + 4;
            draw_text(gacha_book_text("gacha_minigame.message_8"), rect.x + iw, rect.y, width - iw);
        }
    }

    void process_cancel() override {
        WindowSelectable::process_cancel();
        g_last_top_row = top_row();
        g_last_index = index();
    }

private:
    std::vector<rpg::ItemRef> list_;
    WindowGachaBookStatus* status_window_ = nullptr;
};

class SceneGachaBook : public SceneMenuBase {
public:
    const char* name() const override { return "Scene_GachaBook"; }

    void create() override {
        SceneMenuBase::create();
        index_window_ = std::make_shared<WindowGachaBookIndex>();
        index_window_->initialize(0, 0);
        index_window_->set_handler("cancel", [this] { pop_scene(); });
        double wy = index_window_->height();
        double ww = 640;
        double wh = 480 - wy;
        status_window_ = WindowBase::make<WindowGachaBookStatus>(0.0, wy, ww, wh);
        add_window(index_window_);
        add_window(status_window_);
        index_window_->set_status_window(status_window_.get());
    }

private:
    std::shared_ptr<WindowGachaBookIndex> index_window_;
    std::shared_ptr<WindowGachaBookStatus> status_window_;
};

}

rpg::SceneFactory gacha_book_scene() { return rpg::scene_of<SceneGachaBook>("Scene_GachaBook"); }

static SceneRegistrar g_gacha_book_registrar("Scene_GachaBook", gacha_book_scene());

}
