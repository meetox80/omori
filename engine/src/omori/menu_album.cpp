#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/vm.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "menu_common.h"
#include "message.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

namespace {

std::string meta_trim(const rpg::ItemRef& item, const char* key) { return rpg::trim(item_meta_str(item, key)); }

double meta_number(const rpg::ItemRef& item, const char* key) {
    if (!item.valid()) return std::nan("");
    const Value* v = item.data()["meta"].find(key);
    if (!v) return std::nan("");
    if (v->is_num()) return v->num();
    if (v->t == Value::Bool) return v->b ? 1 : 0;
    return js::string_to_number(v->string());
}

std::string pic_name(const std::string& group, const std::string& name) { return group + "_" + name; }

double pixi_width(const g2d::Sprite* s) { return s->bitmap() ? std::abs(s->scale_x) * s->width() : 0; }

double pixi_height(const g2d::Sprite* s) { return s->bitmap() ? std::abs(s->scale_y) * s->height() : 0; }

const double kToneClear[4] = {0, 0, 0, 0};
const double kToneGray[4] = {0, 0, 0, 255};
const double kToneInactive[4] = {-80, -80, -80, 255};

void play_turn_page() { play_se("SE_turn_page", 90, 100, 0); }

}

struct AlbumItem {
    int id = 0;
    std::string group;
    std::string graphics_name;
    std::string thumbnail_name;
    std::string text;
    bool has_placing_message = false;
    std::string placing_message;
    std::string back_graphics_name;
};

struct AlbumData {
    int id = 0;
    double max_pages = 0;
    double max_images = 0;
    std::string group;
    std::string book_image_name;
    std::string book_cover_image_name;
    std::string page_text_image_name;
    std::string background_name;
    std::string background_parallax;
    double parallax_speed_x = 0;
    double parallax_speed_y = 0;
    std::string default_back_image_name;
    std::vector<double> pictures;
    std::vector<AlbumItem> album_items;
};

class SceneOmoriPhotoAlbum;

class SpriteOmoriAlbumPictureCursor : public g2d::Sprite {
public:
    explicit SpriteOmoriAlbumPictureCursor(const AlbumData* data) : album_data_(data) {
        picture = nullptr;
        create_sprites();
        selected_ = false;
    }

    void create_sprites() {
        shadow_sprite_ = std::make_shared<g2d::Sprite>(load_picture("photo_shadow"));
        shadow_sprite_->visible = false;
        add_child(shadow_sprite_);
        picture_sprite_ = std::make_shared<g2d::Sprite>();
        picture_sprite_->x = -15;
        picture_sprite_->y = -15;
        add_child(picture_sprite_);
    }

    void select() { selected_ = true; }
    void deselect() { selected_ = false; }

    void set_picture_data(const AlbumItem* p) {
        picture = p;
        if (picture) set_picture_bitmap(load_picture(pic_name(album_data_->group, p->thumbnail_name)));
        else set_picture_bitmap(nullptr);
    }

    void set_picture_bitmap(g2d::BitmapPtr bitmap) {
        bool has = (bool)bitmap;
        picture_sprite_->set_bitmap(std::move(bitmap));
        shadow_sprite_->visible = has;
    }

    void update() override {
        g2d::Sprite::update();
        move.update();
        update_selected_animation();
    }

    void update_selected_animation() {
        if (selected_) {
            if (picture_sprite_->x > -7) picture_sprite_->x = std::max(picture_sprite_->x - 1, -7.0);
            if (picture_sprite_->y > -7) picture_sprite_->y = std::max(picture_sprite_->y - 1, -7.0);
        } else {
            if (picture_sprite_->x < 0) picture_sprite_->x = std::min(picture_sprite_->x + 1, 0.0);
            if (picture_sprite_->y < 0) picture_sprite_->y = std::min(picture_sprite_->y + 1, 0.0);
        }
    }

    const AlbumItem* picture = nullptr;
    ObjectMovement move;

private:
    const AlbumData* album_data_;
    g2d::SpritePtr shadow_sprite_;
    g2d::SpritePtr picture_sprite_;
    bool selected_ = false;
};

class SpriteOmoriAlbumBigPicture : public g2d::Sprite {
public:
    SpriteOmoriAlbumBigPicture() {
        anchor_x = 0.5;
        anchor_y = 0.5;
        flip_count_ = 0;
        flip_target_ = 0;
        facing = 0;
    }

    bool is_flipping() const { return flip_count_ > 0; }

    void setup_bitmaps(g2d::BitmapPtr front, g2d::BitmapPtr back) {
        front_bitmap = std::move(front);
        back_bitmap = std::move(back);
        set_bitmap(facing == 0 ? front_bitmap : back_bitmap);
    }

    void start_flip(int f, int duration = 10) {
        if (facing != f) {
            facing = f;
            flip_count_ = duration;
            flip_target_ = 0;
        }
    }

    void start_toggle_flip(int duration = 10) { start_flip((facing + 1) % 2, duration); }

    void update() override {
        g2d::Sprite::update();
        update_flipping_animation();
    }

    void update_flipping_animation() {
        if (flip_count_ > 0) {
            double d = flip_count_;
            scale_x = (scale_x * (d - 1) + flip_target_) / d;
            flip_count_--;
            if (flip_count_ == 0 && flip_target_ == 0) {
                flip_target_ = (flip_target_ + 1) % 2;
                set_bitmap(facing == 0 ? front_bitmap : back_bitmap);
                flip_count_ = 10;
            }
        }
    }

    int facing = 0;
    g2d::BitmapPtr front_bitmap;
    g2d::BitmapPtr back_bitmap;

private:
    int flip_count_ = 0;
    int flip_target_ = 0;
};

class WindowOmoriAlbumLegend : public WindowBase {
public:
    void initialize() {
        WindowBase::initialize(-5, -1, window_width(), window_height());
        set_text_list("placing");
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    double window_width() { return 640 + 10; }
    double window_height() { return 35; }
    int standard_padding() override { return 4; }

    void set_text_list(const std::string& name) {
        text_list_ = name;
        refresh();
    }

    void refresh(bool can_read = true) {
        c().clear();
        const Value& text_list = plugin_data("albumMenu", "legendText")[text_list_.c_str()];
        if (!text_list.is_arr()) return;
        size_t n = text_list.size();
        double segment_width = std::floor(c().width() / (double)n);
        for (size_t i = 0; i < n; i++) {
            const Value& entry = text_list[i];
            if (rpg::to_lower(text_list_) == "browsing" && i == 0 && !can_read) continue;
            const Value& inputs = entry["inputs"];
            std::vector<IconRect> input_rects;
            for (size_t k = 0; k < inputs.size(); k++) {
                IconRect up, down;
                key_icon_rects(input_key_code(data::js_string(inputs[k])), up, down);
                input_rects.push_back(up);
            }
            std::string text = data::js_string(entry["text"]);
            double tw = std::floor(text_width(text)) + 8;
            double iw = 0;
            for (auto& r : input_rects) iw += r.w + 2;
            double sx = (i * segment_width) + ((segment_width - (tw + iw)) / 2);
            c().draw_text(text, sx, -4, tw, c().height(), "left");
            double ix = sx + tw;
            for (size_t k = 0; k < inputs.size(); k++) {
                draw_key_icon(&c(), input_key_code(data::js_string(inputs[k])), ix, 2);
                ix += input_rects[k].w;
            }
        }
    }

private:
    std::string text_list_;
};

class WindowOmoriPictureList : public WindowSelectable {
public:
    void initialize(const AlbumData* data) {
        data_.clear();
        album_data_ = data;
        WindowSelectable::initialize(0, 0, window_width(), window_height());
        refresh();
    }
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 8; }
    double window_width() { return 640; }
    double window_height() { return 116 + 15; }
    int max_items() override { return (int)data_.size(); }
    int max_cols() override { return 5; }
    double item_height() override { return 100 + 15; }
    double item_width() override { return WindowSelectable::item_width() - 5; }
    int spacing() override { return 5; }
    double custom_cursor_rect_x_offset() override { return -8; }
    double custom_cursor_rect_y_offset() override { return -35; }
    bool is_current_item_enabled() override { return index() >= 0 && index() < (int)data_.size(); }

    ItemRect item_rect(int index) override {
        ItemRect r = WindowSelectable::item_rect(index);
        r.x += 20;
        return r;
    }

    const AlbumItem* picture() {
        int i = index();
        return i >= 0 && i < (int)data_.size() ? data_[(size_t)i] : nullptr;
    }

    void select_picture_by_id(int id) {
        for (size_t i = 0; i < data_.size(); i++) {
            if (data_[i]->id == id) {
                select((int)i);
                break;
            }
        }
    }

    void picture_cursor_position(double& px, double& py) {
        ItemRect r = item_rect(index());
        double padding = standard_padding() * 2;
        px = x + padding + r.x + 6;
        py = y + padding + r.y - 1;
    }

    void cursor_down(bool wrap) override {
        int idx = index();
        int mi = max_items();
        int mc = max_cols();
        if (idx + mc > mi - 1 && row() < max_rows() - 1) {
            select(mi - 1);
            return;
        }
        if (idx < mi - mc || (wrap && mc == 1)) {
            select((idx + mc) % mi);
            return;
        }
    }

    void make_item_list() {
        data_.clear();
        const std::string& group = album_data_->group;
        js::Val positions = js::vm().get(rpg::game_party().album_picture_positions, js::atom(group));
        for (const AlbumItem& it : album_data_->album_items) {
            if (!rpg::game_party().album_has_picture(group, js::Val::num(it.id)).truthy()) data_.push_back(&it);
        }
        auto index_of = [&positions](int id) -> long {
            if (!positions.is_array()) return -1;
            const auto& items = positions.o->items;
            for (size_t i = 0; i < items.size(); i++) {
                if (js::strict_equals(items[i], js::Val::num(id))) return (long)i;
            }
            return -1;
        };
        std::stable_sort(data_.begin(), data_.end(), [&index_of](const AlbumItem* a, const AlbumItem* b) { return index_of(a->id) < index_of(b->id); });
    }

    void refresh() override {
        make_item_list();
        WindowSelectable::refresh();
    }

    void clear_selected() { clear_item(index()); }

    void set_index_raw(int i) { index_ = i; }

    void draw_item(int index) override {
        if (index < 0 || index >= (int)data_.size()) return;
        ItemRect r = item_rect(index);
        const AlbumItem* d = data_[(size_t)index];
        g2d::BitmapPtr bitmap = load_picture(pic_name(album_data_->group, d->thumbnail_name));
        double w = bitmap->width(), h = bitmap->height();
        double bx = r.x + (r.width - w) / 2;
        double by = r.y + (r.height - h) / 2;
        c().blt(bitmap.get(), 0, 0, w, h, bx, by);
    }

protected:
    void update_transform_hook() override {
        double w = width(), h = height();
        double p = 28, q = p / 2;
        down_arrow_sprite()->move(w - q, h - q);
        up_arrow_sprite()->move(w - q, q);
    }

private:
    const AlbumData* album_data_ = nullptr;
    std::vector<const AlbumItem*> data_;
};

class SpriteOmoriPictureAlbum : public g2d::Sprite {
public:
    SpriteOmoriPictureAlbum(const AlbumData* data, int page) : album_data_(data) {
        active_ = true;
        index = 0;
        this->page = page;
        picture_positions_ = {{18, 19}, {119, 108}, {18, 202}, {245, 19}, {346, 108}, {245, 202}};
        home_x_ = 0;
        home_y_ = 0;
        placing_picture_ = nullptr;
        use_picture_cursor = true;
        create_background_sprite();
        create_contents_sprite();
        create_picture_border_sprite();
        create_page_sprite();
        create_page_legend_sprite();
        create_cursor_sprites();
        refresh_page(2);
    }

    int max_pages() const { return (int)album_data_->max_pages - 1; }
    int max_page_pictures() const { return 6; }

    void activate() {
        active_ = true;
        update_cursor_position();
    }

    void deactivate() {
        active_ = false;
        update_cursor_position();
    }

    int picture_index() const { return (page * max_page_pictures()) + index; }

    void set_home(double hx, double hy) {
        home_x_ = hx;
        home_y_ = hy;
    }

    const AlbumItem* picture(int idx, int pg) const {
        int page_index = (pg * max_page_pictures()) + idx;
        js::Val item_id = rpg::game_party().get_album_picture_at_index(album_data_->group, page_index);
        if (item_id.truthy()) {
            for (const AlbumItem& it : album_data_->album_items) {
                if (js::strict_equals(js::Val::num(it.id), item_id)) return &it;
            }
            return nullptr;
        }
        return nullptr;
    }

    const AlbumItem* picture() const { return picture(index, page); }

    void set_placing_picture(const AlbumItem* p) {
        placing_picture_ = p;
        update_picture_border_visibility();
    }

    void create_background_sprite() {
        background_sprite_ = std::make_shared<g2d::Sprite>(load_picture(pic_name(album_data_->group, album_data_->book_image_name)));
        add_child(background_sprite_);
    }

    void create_contents_sprite() {
        contents_sprite_ = std::make_shared<g2d::Sprite>(
            g2d::Bitmap::create(std::max(1, (int)pixi_width(background_sprite_.get())), std::max(1, (int)pixi_height(background_sprite_.get()))));
        add_child(contents_sprite_);
    }

    void create_picture_border_sprite() {
        border_sprite_ = std::make_shared<g2d::Sprite>(load_picture("puzzle_border"));
        border_amplitude_ = 128;
        border_angle_ = 0;
        border_angle_speed_ = 0.12;
        border_delay_ = 0;
        border_sprite_->visible = false;
        add_child(border_sprite_);
    }

    void create_page_sprite() {
        double width = pixi_width(contents_sprite_.get()) / 2;
        double height = pixi_height(contents_sprite_.get());
        cover_page_sprite_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create(std::max(1, (int)width), std::max(1, (int)height)));
        cover_page_sprite_->x = std::floor(pixi_width(background_sprite_.get()) / 2);
        cover_page_sprite_->anchor_x = 0;
        cover_page_sprite_->anchor_y = 0;
        add_child(cover_page_sprite_);
        page_sprite_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create(std::max(1, (int)width), std::max(1, (int)height)));
        page_sprite_->x = std::floor(pixi_width(background_sprite_.get()) / 2);
        page_sprite_->set_opacity(255);
        page_sprite_->anchor_x = 1;
        page_sprite_->anchor_y = 0;
        add_child(page_sprite_);
    }

    void create_page_legend_sprite() {
        g2d::BitmapPtr bitmap = g2d::Bitmap::create((int)pixi_width(background_sprite_.get()) + 130, 60);
        bitmap->fill_all("rgba(32, 32, 32, 0.5)");
        double width = 60;
        double bw = bitmap->width(), bh = bitmap->height();
        bitmap->fill_rect(0, 0, width, bh, "rgba(0, 0, 0, 1)");
        bitmap->fill_rect(1, 1, width - 2, bh - 2, "rgba(255, 255, 255, 1)");
        bitmap->fill_rect(4, 4, width - 8, bh - 8, "rgba(0, 0, 0, 1)");
        bitmap->fill_rect(bw - width, 0, width, bh, "rgba(0, 0, 0, 1)");
        bitmap->fill_rect((bw - width) + 1, 1, width - 2, bh - 2, "rgba(255, 255, 255, 1)");
        bitmap->fill_rect((bw - width) + 4, 4, width - 8, bh - 8, "rgba(0, 0, 0, 1)");
        bitmap->clear_rect(width, 0, bw - (width * 2), bh);
        draw_aligned_key_icon(bitmap.get(), input_key_code("pageup"), 0, 5, width, 32, "center");
        draw_aligned_key_icon(bitmap.get(), input_key_code("pagedown"), bw - width, 5, width, 32, "center");
        page_legend_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        page_legend_sprite_->x = -50 - 15;
        add_child(page_legend_sprite_);
    }

    void create_cursor_sprites() {
        g2d::BitmapPtr bitmap = load_system("ACSArrows");
        cursor_sprite_ = std::make_shared<SpriteWindowCustomCursor>(-1, "cursor_menu");
        cursor_sprite_->x = -10;
        cursor_sprite_->y = 70;
        add_child(cursor_sprite_);
        left_page_cursor_ = std::make_shared<g2d::Sprite>(bitmap);
        left_page_cursor_->x = -52;
        left_page_cursor_->y = 28;
        left_page_cursor_->set_frame(64, 0, 32, 29);
        left_page_cursor_->set_color_tone(kToneGray);
        add_child(left_page_cursor_);
        right_page_cursor_ = std::make_shared<g2d::Sprite>(bitmap);
        right_page_cursor_->x = 435 + 40;
        right_page_cursor_->y = left_page_cursor_->y;
        right_page_cursor_->set_frame(32, 0, 32, 29);
        right_page_cursor_->set_color_tone(kToneGray);
        add_child(right_page_cursor_);
        update_page_cursor_visibility();
    }

    g2d::BitmapPtr make_page_bitmap(int side) {
        double pw = pixi_width(page_sprite_.get()), ph = pixi_height(page_sprite_.get());
        g2d::BitmapPtr bitmap = g2d::Bitmap::create(std::max(1, (int)pw), std::max(1, (int)ph));
        g2d::Bitmap* contents = contents_sprite_->bitmap().get();
        g2d::Bitmap* page_bitmap = background_sprite_->bitmap().get();
        double w = bitmap->width(), h = bitmap->height();
        double sx = w * side;
        if (side == 0) bitmap->blt(page_bitmap, sx + 2, 2, w - 2, h - 4, 2, 2);
        else bitmap->blt(page_bitmap, sx, 2, w - 2, h - 4, 0, 2);
        bitmap->blt(contents, sx, 0, w, h, 0, 0);
        return bitmap;
    }

    void refresh_page(int side) { refresh_page(side, page); }

    void refresh_page(int side, int pg) {
        int start_range = 0, end_range = 0;
        int page_pics = max_page_pictures() / 2;
        g2d::Bitmap& contents = *contents_sprite_->bitmap();
        double half_width = contents.width() / 2.0;
        std::string filename = pic_name(album_data_->group, album_data_->page_text_image_name + std::to_string(pg + 1));
        g2d::BitmapPtr text_bitmap = load_picture(filename);
        contents.paint_opacity = 179;
        switch (side) {
        case 0:
            start_range = 0;
            end_range = page_pics;
            contents.clear_rect(0, 0, half_width, contents.height());
            contents.blt(text_bitmap.get(), 0, 0, half_width, contents.height(), 0, 0);
            break;
        case 1:
            start_range = page_pics;
            end_range = page_pics * 2;
            contents.clear_rect(half_width, 0, half_width, contents.height());
            contents.blt(text_bitmap.get(), half_width, 0, half_width, contents.height(), half_width, 0);
            break;
        case 2:
            start_range = 0;
            end_range = page_pics * 2;
            contents.clear();
            contents.blt(text_bitmap.get(), 0, 0, text_bitmap->width(), contents.height(), 0, 0);
            break;
        }
        contents.paint_opacity = 255;
        for (int i = start_range; i < end_range; i++) {
            if ((pg * max_page_pictures()) + i > album_data_->max_images) break;
            const AlbumItem* d = picture(i, pg);
            const auto& pos = picture_positions_[(size_t)i];
            if (d) {
                g2d::BitmapPtr bitmap = load_picture(pic_name(album_data_->group, d->thumbnail_name));
                contents.blt(bitmap.get(), 0, 0, bitmap->width(), bitmap->height(), pos[0], pos[1]);
            }
        }
    }

    void show_picture_border(int idx) {
        border_sprite_->visible = true;
        border_sprite_->x = picture_positions_[(size_t)idx][0] - 34;
        border_sprite_->y = picture_positions_[(size_t)idx][1] - 46;
        border_angle_ = 215;
        border_sprite_->set_opacity(0);
        border_skip_delay_ = true;
        update_picture_border();
    }

    void hide_picture_border() {
        border_sprite_->visible = false;
        border_delay_ = 0;
        border_sprite_->set_opacity(0);
    }

    void update() override {
        g2d::Sprite::update();
        move.update();
        update_cursor_input();
        update_picture_border();
    }

    void update_picture_border() {
        if (!border_sprite_->visible) return;
        if (border_delay_ <= 0) {
            border_amplitude_ = 130;
            border_angle_speed_ = 0.05;
            double change = 127 + (std::sin(border_angle_ * border_angle_speed_) * border_amplitude_);
            border_sprite_->set_opacity(change);
            border_angle_ = std::fmod(border_angle_ + 1, 500.0);
            if (border_sprite_->opacity() <= 0) {
                if (border_skip_delay_) {
                    border_skip_delay_ = false;
                    border_delay_ = 0;
                } else {
                    border_delay_ = 120;
                }
                border_angle_ = 100;
            }
        } else {
            border_delay_--;
        }
    }

    void update_cursor_input() {
        if (!active_ || move.is_moving()) return;
        if (input::is_repeated(input::PAGEUP)) {
            int old_page = page;
            int pg = std::clamp(page - 1, 0, max_pages());
            if (old_page != pg) hide_picture_border();
            turn_page_right();
            return;
        }
        if (input::is_repeated(input::PAGEDOWN)) {
            int old_page = page;
            int pg = std::clamp(page - 1, 0, max_pages());
            if (old_page != pg) hide_picture_border();
            turn_page_left();
            return;
        }
        if (input::is_repeated(input::DOWN)) {
            if (index == 2 || index >= 5) return;
            int page_index = (page * max_page_pictures()) + index;
            if (page_index < album_data_->max_images) {
                play_cursor();
                index++;
                update_cursor_position();
            }
            return;
        }
        if (input::is_repeated(input::UP)) {
            if (index == 0 || index == 3) return;
            play_cursor();
            index--;
            update_cursor_position();
            return;
        }
        if (input::is_repeated(input::RIGHT)) {
            int page_index = (page * max_page_pictures()) + index;
            if (index == 4) {
                turn_page_left();
                return;
            }
            if (index < 3) {
                if (page_index + 3 <= album_data_->max_images) index += 3;
            } else {
                if (album_data_->max_images > 4) index = 4;
            }
            play_cursor();
            update_cursor_position();
            return;
        }
        if (input::is_repeated(input::LEFT)) {
            if (index == 0 || index == 2) {
                turn_page_right();
                return;
            }
            index -= index == 1 ? 1 : 3;
            play_cursor();
            update_cursor_position();
            return;
        }
    }

    void update_cursor_position();

    void update_page_cursor_visibility() {
        left_page_cursor_->set_color_tone(page > 0 ? kToneClear : kToneGray);
        right_page_cursor_->set_color_tone(page < max_pages() ? kToneClear : kToneGray);
    }

    void update_picture_border_visibility() {
        const AlbumItem* p = placing_picture_;
        if (p) {
            int max_page_pics = max_page_pictures();
            int start_index = page * max_page_pics;
            bool hide_border = true;
            for (int i = 0; i < max_page_pics; i++) {
                size_t at = (size_t)(start_index + i);
                if (at < album_data_->pictures.size() && album_data_->pictures[at] == p->id) {
                    show_picture_border(i);
                    hide_border = false;
                    break;
                }
            }
            if (hide_border) hide_picture_border();
        } else {
            hide_picture_border();
        }
    }

    void turn_page_right() {
        int old_page = page;
        page = std::clamp(page - 1, 0, max_pages());
        update_page_cursor_visibility();
        if (old_page != page) {
            g2d::BitmapPtr left_bitmap = make_page_bitmap(0);
            g2d::BitmapPtr right_bitmap = make_page_bitmap(1);
            refresh_page(0);
            play_turn_page();
            page_sprite_->anchor_x = 0;
            page_sprite_->set_bitmap(left_bitmap);
            page_sprite_->anchor_x = 1;
            cover_page_sprite_->set_bitmap(right_bitmap);
            cover_page_sprite_->anchor_x = 0;
            refresh_page(2);
            g2d::Sprite* obj = page_sprite_.get();
            ObjectMovement::Move data = ObjectMovement::tween(obj, {{prop_scale_x(obj), 0, 20}}, ease_out_circ);
            data.on_finish = [this, obj] {
                page_sprite_->set_bitmap(make_page_bitmap(1));
                obj->anchor_x = 0;
                ObjectMovement::Move data2 = ObjectMovement::tween(obj, {{prop_scale_x(obj), 1.0, 15}}, ease_out_circ);
                data2.on_finish = [this] {
                    page_sprite_->set_bitmap(nullptr);
                    cover_page_sprite_->set_bitmap(nullptr);
                };
                move.start_move(std::move(data2));
                update_picture_border_visibility();
            };
            move.start_move(std::move(data));
        }
    }

    void turn_page_left() {
        int old_page = page;
        page = std::clamp(page + 1, 0, max_pages());
        int page_index = (page * max_page_pictures()) + index;
        if (page_index > album_data_->max_images) {
            index -= (int)std::fmod((double)page_index, album_data_->max_images);
            update_cursor_position();
        }
        update_picture_border_visibility();
        if (old_page != page) {
            g2d::BitmapPtr left_bitmap = make_page_bitmap(0);
            g2d::BitmapPtr right_bitmap = make_page_bitmap(1);
            play_turn_page();
            page_sprite_->anchor_x = 0;
            page_sprite_->set_bitmap(right_bitmap);
            page_sprite_->scale_x = 1;
            update_page_cursor_visibility();
            cover_page_sprite_->set_bitmap(left_bitmap);
            cover_page_sprite_->anchor_x = 1;
            refresh_page(2);
            g2d::Sprite* obj = page_sprite_.get();
            ObjectMovement::Move data = ObjectMovement::tween(obj, {{prop_scale_x(obj), 0, 20}}, ease_out_circ);
            data.on_finish = [this, obj] {
                page_sprite_->set_bitmap(make_page_bitmap(0));
                obj->anchor_x = 1;
                ObjectMovement::Move data2 = ObjectMovement::tween(obj, {{prop_scale_x(obj), 1.0, 15}}, ease_out_circ);
                data2.on_finish = [this] {
                    page_sprite_->set_bitmap(nullptr);
                    cover_page_sprite_->set_bitmap(nullptr);
                };
                move.start_move(std::move(data2));
            };
            move.start_move(std::move(data));
        }
    }

    int index = 0;
    int page = 0;
    bool use_picture_cursor = true;
    SpriteOmoriAlbumPictureCursor* picture_cursor = nullptr;
    SceneOmoriPhotoAlbum* scene = nullptr;
    ObjectMovement move;

private:
    const AlbumData* album_data_;
    bool active_ = true;
    std::vector<std::vector<double>> picture_positions_;
    double home_x_ = 0, home_y_ = 0;
    const AlbumItem* placing_picture_ = nullptr;
    g2d::SpritePtr background_sprite_;
    g2d::SpritePtr contents_sprite_;
    g2d::SpritePtr border_sprite_;
    double border_amplitude_ = 128;
    double border_angle_ = 0;
    double border_angle_speed_ = 0.12;
    int border_delay_ = 0;
    bool border_skip_delay_ = false;
    g2d::SpritePtr cover_page_sprite_;
    g2d::SpritePtr page_sprite_;
    g2d::SpritePtr page_legend_sprite_;
    std::shared_ptr<SpriteWindowCustomCursor> cursor_sprite_;
    g2d::SpritePtr left_page_cursor_;
    g2d::SpritePtr right_page_cursor_;
};

class SceneOmoriPhotoAlbum : public SceneBaseEX {
public:
    const char* name() const override { return "Scene_OmoriPhotoAlbum"; }

    SceneOmoriPhotoAlbum() {
        interface_mode_ = 0;
        viewing_mode_ = false;
        placing_mode_ = false;
        browsing_mode_ = false;
        viewing_message_ = false;
        viewing_index_ = 0;
        block_all_input_ = false;
        flip_show_count_ = 0;
        flip_show_delay_ = 100;
        required_pictures_ = 0;
        starting_page_ = 1;
    }

    void prepare(const rpg::ItemRef& item, int interface_mode = 0, int required_amount = 0, int page = 1) {
        required_pictures_ = required_amount;
        interface_mode_ = interface_mode;
        starting_page_ = page - 1;
        album_data_.id = item.id;
        album_data_.max_pages = meta_number(item, "AlbumMaxPages");
        album_data_.max_images = meta_number(item, "AlbumMaxPictures");
        album_data_.group = meta_trim(item, "AlbumGroup");
        album_data_.book_image_name = meta_trim(item, "AlbumBookImage");
        album_data_.book_cover_image_name = meta_trim(item, "AlbumBookCoverImage");
        album_data_.page_text_image_name = meta_trim(item, "AlbumTextPageImage");
        album_data_.background_name = meta_trim(item, "AlbumBackground");
        album_data_.background_parallax = meta_trim(item, "AlbumParallax");
        album_data_.parallax_speed_x = meta_number(item, "ParallaxSpeedX");
        album_data_.parallax_speed_y = meta_number(item, "ParallaxSpeedY");
        album_data_.default_back_image_name = meta_trim(item, "AlbumDefaultBackImage");
        std::vector<double> pictures;
        for (int i = 0; i < album_data_.max_pages; i++) {
            std::string key = "AlbumPage" + std::to_string(i + 1) + "Pictures";
            std::string d = item_meta_has(item, key.c_str()) ? item_meta_str(item, key.c_str()) : std::string();
            if (!d.empty()) {
                size_t start = 0;
                while (true) {
                    size_t comma = d.find(',', start);
                    std::string part = d.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                    pictures.push_back(js::string_to_number(part));
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            } else {
                for (int k = 0; k < 6; k++) pictures.push_back(0);
            }
        }
        std::vector<js::Val> pic_vals;
        for (double p : pictures) pic_vals.push_back(js::Val::num(p));
        rpg::game_party().randomize_album_picture_positions(album_data_.group, js::make_array(pic_vals), false);
        album_data_.pictures = pictures;
        album_data_.album_items.clear();
        for (const rpg::ItemRef& it : rpg::game_party().items()) {
            if (!item_meta_has(it, "AlbumGraphicsName")) continue;
            AlbumItem d;
            d.id = it.id;
            d.group = meta_trim(it, "AlbumGroup");
            d.graphics_name = meta_trim(it, "AlbumGraphicsName");
            d.thumbnail_name = meta_trim(it, "AlbumThumbnailName");
            d.text = meta_trim(it, "AlbumText");
            if (item_meta_has(it, "AlbumPlacingMessage")) {
                d.has_placing_message = true;
                d.placing_message = meta_trim(it, "AlbumPlacingMessage");
            }
            if (item_meta_has(it, "AlbumBackGraphicsName")) d.back_graphics_name = meta_trim(it, "AlbumBackGraphicsName");
            else d.back_graphics_name = album_data_.default_back_image_name;
            if (d.group == album_data_.group) album_data_.album_items.push_back(d);
        }
    }

    void start() override {
        start_fade_in(slow_fade_speed());
        if (interface_mode_ == 0) start_placing();
        else start_browsing();
    }

    void start_placing() {
        if (rpg::game_switches().value(42)) {
            block_all_input_ = true;
            list_window_->y = 480;
            static const char* messages[] = {"dreamworld_extras_misc.message_2", "dreamworld_extras_misc.message_3"};
            for (const char* m : messages) {
                std::string message = m;
                queue([this, message] { start_message(message); });
                queue_wait_mode("message");
            }
            queue_wait(60);
            queue([this] {
                WindowOmoriPictureList* obj = list_window_.get();
                move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), 480 - list_window_->height(), 15}, {prop_window_opacity(obj), 255, 15}}, ease_out_circ));
            });
            queue_wait(15);
            queue([this] {
                list_window_->activate();
                list_window_->select(0);
                block_all_input_ = false;
            });
        } else {
            list_window_->y = 480 - list_window_->height();
            list_window_->activate();
            list_window_->select(0);
        }
    }

    void start_browsing() {
        list_window_->y = 480;
        album_sprite_->y = ((480 - 320) / 2.0);
        legend_window_->y = -6;
        legend_window_->set_window_opacity(255);
        legend_window_->set_contents_opacity(255);
        legend_window_->set_text_list("browsing");
        album_sprite_->use_picture_cursor = false;
        album_sprite_->activate();
        browsing_mode_ = true;
    }

    void create() override {
        SceneBaseEX::create();
        create_background();
        create_legend_window();
        create_picture_album();
        create_album_picture_list_window();
        create_viewing_sprites();
        create_message_window();
        create_picture_cursor();
    }

    void create_background() {
        background_parallax_ = std::make_shared<g2d::TilingSprite>();
        background_parallax_->move(0, 0, 640, 480);
        add_child(background_parallax_);
        if (!album_data_.background_parallax.empty()) background_parallax_->bitmap = load_picture(pic_name(album_data_.group, album_data_.background_parallax));
        background_sprite_ = std::make_shared<g2d::Sprite>();
        add_child(background_sprite_);
        if (!album_data_.background_name.empty()) background_sprite_->set_bitmap(load_picture(pic_name(album_data_.group, album_data_.background_name)));
    }

    void create_legend_window() {
        legend_window_ = WindowBase::make<WindowOmoriAlbumLegend>();
        legend_window_->y = -legend_window_->height();
        legend_window_->set_window_opacity(0);
        legend_window_->set_contents_opacity(0);
        add_child(legend_window_);
    }

    void create_picture_cursor() {
        picture_cursor_ = std::make_shared<SpriteOmoriAlbumPictureCursor>(&album_data_);
        picture_cursor_->x = 42;
        picture_cursor_->y = 364;
        add_child(picture_cursor_);
    }

    void create_picture_album() {
        album_sprite_ = std::make_shared<SpriteOmoriPictureAlbum>(&album_data_, starting_page_);
        album_sprite_->x = (640 - 453) / 2.0;
        album_sprite_->y = ((480 - 320) / 2.0) - 65;
        album_sprite_->deactivate();
        album_sprite_->scene = this;
        add_child(album_sprite_);
        closed_album_sprite_ = std::make_shared<g2d::Sprite>(load_picture(pic_name(album_data_.group, album_data_.book_cover_image_name)));
        closed_album_sprite_->anchor_x = 0.5;
        closed_album_sprite_->anchor_y = 0.5;
        closed_album_sprite_->x = 640 / 2.0;
        closed_album_sprite_->y = 480 / 2.0;
        closed_album_sprite_->visible = false;
        add_child(closed_album_sprite_);
    }

    void create_album_picture_list_window() {
        list_window_ = std::shared_ptr<WindowOmoriPictureList>(new WindowOmoriPictureList());
        list_window_->initialize(&album_data_);
        list_window_->x = 0;
        list_window_->y = 480 - list_window_->height();
        list_window_->set_handler("ok", [this] { start_picture_placing(); });
        add_child(list_window_);
    }

    void create_viewing_sprites() {
        viewing_picture_ = std::make_shared<SpriteOmoriAlbumBigPicture>();
        add_child(viewing_picture_);
        viewing_picture_slide_ = std::make_shared<SpriteOmoriAlbumBigPicture>();
        add_child(viewing_picture_slide_);
        g2d::BitmapPtr pointer_bitmap = load_system("ACSArrows");
        g2d::BitmapPtr box_bitmap = g2d::Bitmap::create(45, 25);
        box_bitmap->fill_all("rgba(0, 0, 0, 1)");
        box_bitmap->fill_rect(1, 1, box_bitmap->width() - 2, box_bitmap->height() - 2, "rgba(255, 255, 255, 1)");
        box_bitmap->fill_rect(4, 4, box_bitmap->width() - 8, box_bitmap->height() - 8, "rgba(0, 0, 0, 1)");
        left_box_ = std::make_shared<g2d::Sprite>(box_bitmap);
        left_box_->x = -left_box_->width();
        left_box_->y = (480 - left_box_->height() - 125) / 2;
        left_box_->set_opacity(0);
        add_child(left_box_);
        auto left_pointer = std::make_shared<g2d::Sprite>(pointer_bitmap);
        left_pointer->x = 4;
        left_pointer->y = -2;
        left_pointer->set_frame(2 * 32, 0, 32, 32);
        left_box_->add_child(left_pointer);
        right_box_ = std::make_shared<g2d::Sprite>(box_bitmap);
        right_box_->x = 640 - right_box_->width();
        right_box_->y = (480 - right_box_->height() - 125) / 2;
        right_box_->set_opacity(0);
        add_child(right_box_);
        auto right_pointer = std::make_shared<g2d::Sprite>(pointer_bitmap);
        right_pointer->x = 38;
        right_pointer->y = -2;
        right_pointer->scale_x = -1;
        right_pointer->set_frame(2 * 32, 0, 32, 32);
        right_box_->add_child(right_pointer);
        std::string text = data::js_string(plugin_data("albumMenu", "flipText"));
        int flip_width = pointer_bitmap->measure_text_width(text);
        g2d::BitmapPtr flip_bitmap = g2d::Bitmap::create(flip_width + 80, 30);
        draw_key_icon(flip_bitmap.get(), input_key_code("shift"), 0, 4);
        flip_bitmap->draw_text(text, 0, -5, flip_bitmap->width(), flip_bitmap->height(), "center");
        flip_box_ = std::make_shared<g2d::Sprite>(flip_bitmap);
        flip_box_->x = (640 - flip_bitmap->width()) / 2.0;
        flip_box_->y = 480 - 30;
        flip_box_->set_opacity(0);
        add_child(flip_box_);
    }

    void create_message_window() {
        create_window_layer();
        message_window_ = ui::create_message_window(*this);
    }

    void update() override {
        SceneBaseEX::update();
        update_parallax_movement();
        if (block_all_input_) return;
        if (closed_album_sprite_->visible) {
            if (input::is_triggered(input::OK) || input::is_triggered(input::CANCEL)) {
                block_all_input_ = true;
                queue([this] { start_fade_out(30, false); });
                queue_wait(30);
                queue([this] { pop_scene(); });
                input::clear();
                return;
            }
        }
        if (viewing_message_) {
            if (!rpg::game_message().is_busy()) end_message();
            return;
        } else if (placing_mode_) {
            update_picture_placing();
            return;
        } else if (browsing_mode_) {
            if (input::is_triggered(input::SHIFT)) {
                const AlbumItem* p = album_sprite_->picture();
                if (p && has_comment(p)) start_message(p->text);
                return;
            }
            if (input::is_triggered(input::CANCEL)) {
                if (move.is_moving()) return;
                block_all_input_ = true;
                pop_scene();
                return;
            }
            if (input::is_triggered(input::OK)) {
                const AlbumItem* p = album_sprite_->picture();
                if (p) start_viewing_picture();
                return;
            }
        } else if (viewing_mode_) {
            update_viewing_mode();
        } else {
            if (input::is_triggered(input::CANCEL)) {
                if (move.is_moving()) return;
                int album_size = rpg::game_party().album_size(album_data_.group);
                if (album_size < required_pictures_) return;
                list_window_->deactivate();
                block_all_input_ = true;
                queue([this] { start_fade_out(30, false); });
                queue_wait(30);
                queue([this] { pop_scene(); });
                return;
            }
        }
    }

    void update_parallax_movement() {
        background_parallax_->origin_x += album_data_.parallax_speed_x;
        background_parallax_->origin_y += album_data_.parallax_speed_y;
    }

    void tween_list_window(double to_y, double to_opacity, double to_contents, bool has_contents, int duration) {
        WindowOmoriPictureList* obj = list_window_.get();
        std::vector<ObjectMovement::TweenSpec> specs = {{prop_y(obj), to_y, duration}, {prop_window_opacity(obj), to_opacity, duration}};
        if (has_contents) specs.push_back({prop_contents_opacity(obj), to_contents, duration});
        move.start_move(ObjectMovement::tween(obj, specs, ease_out_circ));
    }

    void tween_legend(double to_y, double to_opacity, double to_contents, int duration) {
        WindowOmoriAlbumLegend* obj = legend_window_.get();
        move.start_move(ObjectMovement::tween(
            obj, {{prop_y(obj), to_y, duration}, {prop_window_opacity(obj), to_opacity, duration}, {prop_contents_opacity(obj), to_contents, duration}}, ease_out_circ));
    }

    ObjectMovement::Move album_tween(double to_y, double to_opacity, int duration, Easing easing) {
        SpriteOmoriPictureAlbum* obj = album_sprite_.get();
        return ObjectMovement::tween(obj, {{prop_y(obj), to_y, duration}, {prop_opacity(obj), to_opacity, duration}}, easing);
    }

    void tween_box_x(g2d::Sprite* obj, double to_x, double to_opacity, int duration) {
        move.start_move(ObjectMovement::tween(obj, {{prop_x(obj), to_x, duration}, {prop_opacity(obj), to_opacity, duration}}, ease_out_circ));
    }

    void start_message(const std::string& message) {
        rpg::game_message().show_language_message(message);
        album_sprite_->deactivate();
        int duration = 15;
        if (browsing_mode_) {
            move.start_move(album_tween(((480 - 320) / 2.0) - 55, 255, duration, ease_out_circ));
            tween_list_window(480, 255, 0, false, duration);
            tween_legend(-legend_window_->height(), 0, 0, duration);
        }
        viewing_message_ = true;
    }

    void end_message() {
        viewing_message_ = false;
        if (browsing_mode_) {
            int duration = 15;
            ObjectMovement::Move data = album_tween(((480 - 320) / 2.0), 255, duration, ease_out_circ);
            data.on_finish = [this] { album_sprite_->activate(); };
            move.start_move(std::move(data));
            tween_legend(-6, 255, 255, duration);
        }
    }

    void update_picture_placing() {
        if (input::is_triggered(input::CANCEL)) {
            end_picture_placing();
            return;
        }
        if (input::is_triggered(input::OK)) {
            place_picture_in_album();
            return;
        }
    }

    void start_picture_placing() {
        placing_mode_ = true;
        double px = 0, py = 0;
        list_window_->picture_cursor_position(px, py);
        const AlbumItem* p = list_window_->picture();
        queue([this, px, py, p] {
            list_window_->clear_selected();
            list_window_->set_index_raw(-1);
            picture_cursor_->x = px;
            picture_cursor_->y = py;
            picture_cursor_->select();
            picture_cursor_->set_picture_data(p);
        });
        queue_wait(8);
        queue([this] {
            int duration = 15;
            tween_list_window(480, 0, 0, true, duration);
            tween_legend(-6, 255, 255, duration);
            SpriteOmoriPictureAlbum* obj = album_sprite_.get();
            move.start_move(album_tween(((480 - 320) / 2.0), 255, duration, ease_out_circ));
            album_sprite_->picture_cursor = picture_cursor_.get();
            album_sprite_->index = 0;
            album_sprite_->set_home(obj->x, ((480 - 320) / 2.0));
            album_sprite_->update_cursor_position();
        });
        queue_wait_mode("movement");
        queue([this, p] {
            album_sprite_->activate();
            album_sprite_->set_placing_picture(p);
        });
    }

    void end_picture_placing() {
        placing_mode_ = false;
        album_sprite_->picture_cursor = nullptr;
        album_sprite_->hide_picture_border();
        album_sprite_->deactivate();
        block_all_input_ = true;
        const AlbumItem* p = picture_cursor_->picture;
        list_window_->refresh();
        if (p) {
            list_window_->select_picture_by_id(p->id);
            list_window_->clear_selected();
        } else {
            list_window_->select(0);
        }
        double px = 0, py = 0;
        list_window_->picture_cursor_position(px, py);
        queue([this, px] {
            int duration = 15;
            tween_list_window(480 - list_window_->height(), 255, 255, true, duration);
            tween_legend(-legend_window_->height(), 0, 0, duration);
            move.start_move(album_tween(((480 - 320) / 2.0) - 65, 255, duration, ease_out_circ));
            SpriteOmoriAlbumPictureCursor* obj = picture_cursor_.get();
            move.start_move(
                ObjectMovement::tween(obj, {{prop_x(obj), px, duration}, {prop_y(obj), (480 - list_window_->height()) + 15, duration}}, ease_out_circ));
        });
        queue_wait_mode("movement");
        queue([this] { picture_cursor_->deselect(); });
        queue_wait(5);
        queue([this] {
            block_all_input_ = false;
            input::update();
            picture_cursor_->set_picture_data(nullptr);
            list_window_->redraw_current_item();
            list_window_->activate();
        });
    }

    void place_picture_in_album() {
        const AlbumItem* p = picture_cursor_->picture;
        if (!p) return;
        size_t at = (size_t)album_sprite_->picture_index();
        double picture_id = at < album_data_.pictures.size() ? album_data_.pictures[at] : std::nan("");
        if (p->id == picture_id) {
            block_all_input_ = true;
            placing_mode_ = false;
            queue([this] { picture_cursor_->deselect(); });
            queue_wait(5);
            queue([this, p] {
                int page_index = album_sprite_->picture_index();
                rpg::game_party().add_picture_to_album(album_data_.group, page_index, js::Val::num(p->id));
                album_sprite_->refresh_page(2);
                list_window_->refresh();
                album_sprite_->hide_picture_border();
                album_sprite_->picture_cursor = nullptr;
                album_sprite_->hide_picture_border();
                album_sprite_->deactivate();
                picture_cursor_->set_picture_data(nullptr);
            });
            if (p->has_placing_message && !p->placing_message.empty()) {
                std::string message = p->placing_message;
                queue([this, message] { start_message(message); });
                queue_wait_mode("message");
            }
            queue([this] {
                if (rpg::game_party().album_size(album_data_.group) >= album_data_.max_images) {
                    queue([this] { start_album_full_animation(); });
                    return;
                }
                list_window_->select(0);
                int duration = 15;
                tween_list_window(480 - list_window_->height(), 255, 255, true, duration);
                tween_legend(-legend_window_->height(), 0, 0, duration);
                move.start_move(album_tween(((480 - 320) / 2.0) - 65, 255, duration, ease_out_circ));
            });
            queue_wait_mode("movement");
            queue([this] {
                list_window_->activate();
                block_all_input_ = false;
            });
        }
    }

    void start_album_full_animation() {
        queue([this] { start_fade_out(30, false); });
        queue_wait(40);
        block_all_input_ = true;
        bool needs_cover_to_show = album_data_.id != 914 || (album_data_.id == 914 && rpg::game_map().map_id() == 393);
        if (needs_cover_to_show) {
            queue([this] {
                album_sprite_->visible = false;
                legend_window_->visible = false;
                legend_window_->deactivate();
                album_sprite_->deactivate();
                list_window_->deactivate();
                closed_album_sprite_->visible = true;
            });
            queue([this] { start_fade_in(30, false); });
            queue_wait(30);
            queue([this] { block_all_input_ = false; });
            return;
        }
        queue([this] { pop_scene(); });
    }

    bool has_comment(const AlbumItem* p) {
        if (!p) return false;
        if (p->text.empty()) return false;
        const Value& d = lang_data(p->text);
        return data::js_string(d["text"]) != "...";
    }

    g2d::BitmapPtr picture_bitmap(const std::string& name) { return load_picture(pic_name(album_data_.group, name)); }

    void start_viewing_picture() {
        int duration = 25;
        const AlbumItem* p = album_sprite_->picture();
        viewing_mode_ = true;
        browsing_mode_ = false;
        viewing_index_ = (album_sprite_->page * album_sprite_->max_page_pictures()) + album_sprite_->index;
        flip_show_delay_ = -1;
        viewing_picture_->facing = 0;
        viewing_picture_->setup_bitmaps(picture_bitmap(p->graphics_name), picture_bitmap(p->back_graphics_name));
        viewing_picture_->x = 640 / 2.0;
        viewing_picture_->y = 480 + (pixi_height(viewing_picture_.get()) / 2);
        album_sprite_->deactivate();
        SpriteOmoriAlbumBigPicture* vp = viewing_picture_.get();
        move.start_move(ObjectMovement::tween(vp, {{prop_x(vp), 640 / 2.0, duration}, {prop_y(vp), 480 / 2.0, duration}, {prop_opacity(vp), 255, duration}},
                                              ease_out_circ));
        duration = 15;
        move.start_move(album_tween(480, 0, duration, ease_in_circ));
        duration = 25;
        tween_legend(-legend_window_->height(), 0, 0, duration);
        if (has_comment(p)) {
            duration = 25;
            g2d::Sprite* obj = flip_box_.get();
            move.start_move(ObjectMovement::tween(obj, {{prop_opacity(obj), 255, duration}}, ease_in_circ));
        }
        if (previous_viewing_picture_exists()) tween_box_x(left_box_.get(), 80, 255, duration);
        if (next_viewing_picture_exists()) tween_box_x(right_box_.get(), (640 - right_box_->width()) - 80, 255, duration);
    }

    void end_viewing_picture() {
        viewing_mode_ = false;
        browsing_mode_ = true;
        int page_max = album_sprite_->max_page_pictures();
        int pg = (int)std::floor(viewing_index_ / (double)page_max);
        album_sprite_->page = pg;
        album_sprite_->index = viewing_index_ % page_max;
        album_sprite_->update_cursor_position();
        album_sprite_->refresh_page(2);
        int duration = 25;
        ObjectMovement::Move data = album_tween(((480 - 320) / 2.0), 255, duration, ease_out_circ);
        data.on_finish = [this] {
            viewing_mode_ = false;
            album_sprite_->activate();
        };
        move.start_move(std::move(data));
        duration = 15;
        SpriteOmoriAlbumBigPicture* vp = viewing_picture_.get();
        move.start_move(ObjectMovement::tween(vp, {{prop_y(vp), 480 + (pixi_height(vp) / 2), duration}, {prop_opacity(vp), 0, duration}}, ease_in_circ));
        duration = 25;
        tween_legend(-6, 255, 255, duration);
        duration = 15;
        tween_box_x(left_box_.get(), -left_box_->width(), 0, duration);
        tween_box_x(right_box_.get(), (640 + right_box_->width()), 0, duration);
        duration = 10;
        g2d::Sprite* obj = flip_box_.get();
        move.start_move(ObjectMovement::tween(obj, {{prop_opacity(obj), 0, duration}}, ease_in_circ));
    }

    void update_viewing_mode() {
        if (!move.is_moving() && !viewing_picture_->is_flipping()) {
            if (input::is_triggered(input::SHIFT)) {
                int page_max = album_sprite_->max_page_pictures();
                int pg = (int)std::floor(viewing_index_ / (double)page_max);
                album_sprite_->page = pg;
                album_sprite_->index = viewing_index_ % page_max;
                const AlbumItem* p = album_sprite_->picture();
                if (p && has_comment(p)) start_message(p->text);
                return;
            }
            if (input::is_repeated(input::LEFT)) {
                show_prev_viewing_picture();
                return;
            }
            if (input::is_repeated(input::RIGHT)) {
                show_next_viewing_picture();
                return;
            }
            if (input::is_repeated(input::UP)) return;
            if (input::is_repeated(input::DOWN)) return;
            if (input::is_triggered(input::CANCEL)) {
                end_viewing_picture();
                return;
            }
        }
    }

    void update_shift_to_read(const AlbumItem* p) {
        g2d::Sprite* obj = flip_box_.get();
        if (has_comment(p)) move.start_move(ObjectMovement::tween(obj, {{prop_opacity(obj), 255, 25}}, ease_in_circ));
        else move.start_move(ObjectMovement::tween(obj, {{prop_opacity(obj), 0, 10}}, ease_in_circ));
    }

    void update_side_boxes() {
        if (next_viewing_picture_exists()) tween_box_x(right_box_.get(), (640 - right_box_->width()) - 80, 255, 15);
        else tween_box_x(right_box_.get(), (640 + right_box_->width()), 0, 15);
        if (previous_viewing_picture_exists()) tween_box_x(left_box_.get(), 80, 255, 15);
        else tween_box_x(left_box_.get(), -left_box_->width(), 0, 15);
    }

    void slide_in_finish(const AlbumItem* p, int duration) {
        SpriteOmoriAlbumBigPicture* sp = viewing_picture_slide_.get();
        ObjectMovement::Move data = ObjectMovement::tween(sp, {{prop_x(sp), 640 / 2.0, duration}, {prop_opacity(sp), 255, duration}}, ease_out_circ);
        data.on_finish = [this] { on_picture_show_finish(); };
        move.start_move(std::move(data));
    }

    void show_next_viewing_picture() {
        const AlbumItem* next_picture = nullptr;
        int max_per_page = album_sprite_->max_page_pictures();
        for (int i = viewing_index_ + 1; i <= album_data_.max_images; i++) {
            int page_index = i % max_per_page;
            int pg = (int)std::floor(i / (double)max_per_page);
            const AlbumItem* p = album_sprite_->picture(page_index, pg);
            if (p) {
                next_picture = p;
                viewing_index_ = i;
                break;
            }
        }
        update_side_boxes();
        if (next_picture) {
            play_turn_page();
            int duration = 25;
            SpriteOmoriAlbumBigPicture* vp = viewing_picture_.get();
            move.start_move(ObjectMovement::tween(vp, {{prop_x(vp), 640 / 2.0, duration}, {prop_opacity(vp), 0, duration}}, ease_in_circ));
            viewing_picture_slide_->x = 640 + (pixi_width(viewing_picture_.get()) / 2);
            viewing_picture_slide_->y = 480 / 2.0;
            viewing_picture_slide_->setup_bitmaps(picture_bitmap(next_picture->graphics_name), picture_bitmap(next_picture->back_graphics_name));
            update_shift_to_read(next_picture);
            slide_in_finish(next_picture, duration);
        }
    }

    void show_prev_viewing_picture() {
        const AlbumItem* prev_picture = nullptr;
        int max_per_page = album_sprite_->max_page_pictures();
        for (int i = viewing_index_ - 1; i >= 0; i--) {
            int page_index = i % max_per_page;
            int pg = (int)std::floor(i / (double)max_per_page);
            const AlbumItem* p = album_sprite_->picture(page_index, pg);
            if (p) {
                prev_picture = p;
                viewing_index_ = i;
                break;
            }
        }
        update_side_boxes();
        if (prev_picture) {
            play_turn_page();
            int duration = 25;
            SpriteOmoriAlbumBigPicture* vp = viewing_picture_.get();
            move.start_move(ObjectMovement::tween(vp, {{prop_x(vp), 640 / 2.0, duration}, {prop_opacity(vp), 0, duration}}, ease_in_circ));
            viewing_picture_slide_->x = -(640 + (pixi_width(viewing_picture_.get()) / 2));
            viewing_picture_slide_->y = 480 / 2.0;
            update_shift_to_read(prev_picture);
            viewing_picture_slide_->setup_bitmaps(picture_bitmap(prev_picture->graphics_name), picture_bitmap(prev_picture->back_graphics_name));
            slide_in_finish(prev_picture, duration);
        }
    }

    void on_picture_show_finish() {
        viewing_picture_->facing = 0;
        viewing_picture_->setup_bitmaps(viewing_picture_slide_->front_bitmap, viewing_picture_slide_->back_bitmap);
        viewing_picture_->set_opacity(255);
        viewing_picture_slide_->set_opacity(0);
    }

    const AlbumItem* next_viewing_picture_exists() {
        int max_per_page = album_sprite_->max_page_pictures();
        for (int i = viewing_index_ + 1; i <= album_data_.max_images; i++) {
            const AlbumItem* p = album_sprite_->picture(i % max_per_page, (int)std::floor(i / (double)max_per_page));
            if (p) return p;
        }
        return nullptr;
    }

    const AlbumItem* previous_viewing_picture_exists() {
        int max_per_page = album_sprite_->max_page_pictures();
        for (int i = viewing_index_ - 1; i >= 0; i--) {
            const AlbumItem* p = album_sprite_->picture(i % max_per_page, (int)std::floor(i / (double)max_per_page));
            if (p) return p;
        }
        return nullptr;
    }

    std::shared_ptr<WindowOmoriAlbumLegend> legend_window() const { return legend_window_; }

private:
    AlbumData album_data_;
    int interface_mode_ = 0;
    bool viewing_mode_ = false;
    bool placing_mode_ = false;
    bool browsing_mode_ = false;
    bool viewing_message_ = false;
    int viewing_index_ = 0;
    bool block_all_input_ = false;
    int flip_show_count_ = 0;
    int flip_show_delay_ = 100;
    int required_pictures_ = 0;
    int starting_page_ = 0;
    std::shared_ptr<g2d::TilingSprite> background_parallax_;
    g2d::SpritePtr background_sprite_;
    std::shared_ptr<WindowOmoriAlbumLegend> legend_window_;
    std::shared_ptr<SpriteOmoriPictureAlbum> album_sprite_;
    g2d::SpritePtr closed_album_sprite_;
    std::shared_ptr<WindowOmoriPictureList> list_window_;
    std::shared_ptr<SpriteOmoriAlbumBigPicture> viewing_picture_;
    std::shared_ptr<SpriteOmoriAlbumBigPicture> viewing_picture_slide_;
    g2d::SpritePtr left_box_;
    g2d::SpritePtr right_box_;
    g2d::SpritePtr flip_box_;
    std::shared_ptr<WindowMessage> message_window_;
    std::shared_ptr<SpriteOmoriAlbumPictureCursor> picture_cursor_;
};

void SpriteOmoriPictureAlbum::update_cursor_position() {
    if (index >= 0) {
        const auto& pos = picture_positions_[(size_t)(index % max_page_pictures())];
        cursor_sprite_->x = pos[0] - 10;
        cursor_sprite_->y = pos[1] + 55;
        cursor_sprite_->visible = use_picture_cursor ? false : true;
        if (picture_cursor) {
            int duration = 15;
            SpriteOmoriAlbumPictureCursor* obj = picture_cursor;
            move.start_move(ObjectMovement::tween(obj, {{prop_x(obj), home_x_ + pos[0], duration}, {prop_y(obj), home_y_ + pos[1], duration}}, ease_out_circ));
        }
    }
    update_page_cursor_visibility();
    cursor_sprite_->active_ = active_;
    cursor_sprite_->set_color_tone(active_ ? kToneClear : kToneInactive);
    if (scene) {
        const AlbumItem* p = picture();
        scene->legend_window()->refresh(scene->has_comment(p));
    }
}

}

rpg::SceneFactory photo_album_scene(int item_id, int interface_mode) {
    return rpg::SceneFactory{"Scene_OmoriPhotoAlbum", [item_id, interface_mode] {
                                 auto* scene = new menu::SceneOmoriPhotoAlbum();
                                 scene->prepare(rpg::ItemRef::item(item_id), interface_mode);
                                 return std::unique_ptr<rpg::Scene>(scene);
                             }};
}

}
