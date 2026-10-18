#include <algorithm>
#include <cmath>

#include "../js/vm.h"
#include "../rpg/audio_manager.h"
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

js::Val shop_data() {
    js::Val* v = rpg::game_temp().expando.find(js::atom("_shopData"));
    return v ? *v : js::Val::undef();
}

js::Val shop_get(const char* key) {
    js::Val d = shop_data();
    if (!d.is_obj()) return js::Val::undef();
    return js::vm().get(d, js::atom(key));
}

int shop_type() { return shop_get("shopType").to_int(); }

std::string shop_text(const char* key) {
    js::Val texts = shop_get("texts");
    if (!texts.is_obj()) return "undefined";
    return js::vm().get(texts, js::atom(key)).to_str();
}

void play_cursor_se() { play_se("sys_cursor1", 90, 100, 0); }

g2d::BitmapPtr& shop_snap_slot() {
    static g2d::BitmapPtr b;
    return b;
}

}

void snap_for_shop() { shop_snap_slot() = rpg::scene_manager::snap(); }

class WindowOmoriShopHeader : public WindowBase {
public:
    void initialize() {
        WindowBase::initialize(0, 0, 250, 45);
        set_openness(0);
        refresh();
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 4; }
    int standard_font_size() override { return 24; }
    void refresh() {
        c().clear();
        c().draw_text(shop_get("name").to_str(), 5, -5, c().width() - 10, c().height(), "left");
    }
};

class WindowOmoriShopTotal : public WindowBase {
public:
    void initialize() {
        amount = 0;
        price_ = 0;
        max_amount_ = 0;
        WindowBase::initialize(0, 0, 215, 55);
        create_arrow_sprites();
        refresh();
        deactivate();
        set_openness(0);
        sync_arrow_openness();
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 4; }
    int standard_font_size() override { return 24; }
    double total_price() const { return price_ * amount; }

    void set_handlers(std::function<void()> ok, std::function<void()> cancel) {
        ok_handler_ = std::move(ok);
        cancel_handler_ = std::move(cancel);
    }

    void set_price(double price, double max_amount) {
        price_ = price;
        amount = 1;
        max_amount_ = max_amount;
        draw_shopping_values();
        update_arrow_visibility();
    }

    void create_arrow_sprites() {
        double p = 24, q = p / 2, sx = 96 + p, sy = 0 + p;
        left_arrow_ = std::make_shared<g2d::Sprite>(load_system("Window"));
        left_arrow_->x = width() - 67;
        left_arrow_->y = 5;
        left_arrow_->set_frame(sx - q, sy + q, p, p);
        add_child(left_arrow_);
        right_arrow_ = std::make_shared<g2d::Sprite>(load_system("Window"));
        right_arrow_->x = width() - 35;
        right_arrow_->y = 5;
        right_arrow_->set_frame(sx + p, sy + q, p, p);
        add_child(right_arrow_);
        update_arrow_visibility();
    }

    void refresh() {
        c().clear();
        const Value& loc = blue("Omori_Item_Shop");
        c().draw_text(data::js_string(loc["total_price"]), 5, 20, c().width() - 10, 20, "left");
        c().draw_text(data::js_string(loc["amount"]), 5, 0, c().width() - 10, 20, "left");
        draw_shopping_values();
    }

    void draw_shopping_values() {
        c().clear_rect(c().width() - 90, 0, 90, c().height());
        c().font_size = standard_font_size();
        c().draw_text(js_fmt("%1 %2", {group_digits(total_price()), world_currency_unit()}), 0, 20, c().width() - 5, 20, "right");
        c().draw_text(num_str(amount), c().width() - 55, 0, 50, 20, "center");
    }

    void update() override {
        WindowBase::update();
        sync_arrow_openness();
        update_amount_input();
    }

    void update_amount_input() {
        if (!active || rpg::game_message().has_text()) return;
        if (input::is_triggered(input::OK)) {
            deactivate();
            close();
            if (ok_handler_) ok_handler_();
            return;
        }
        if (input::is_triggered(input::CANCEL)) {
            deactivate();
            close();
            if (cancel_handler_) cancel_handler_();
            return;
        }
        if (input::is_repeated(input::RIGHT)) {
            double gold = rpg::game_party().gold();
            double next_price = shop_type() == 0 ? (amount + 1) * price_ : 0;
            if (amount < max_amount_ && gold >= next_price) {
                play_cursor_se();
                amount++;
                draw_shopping_values();
                update_arrow_visibility();
            }
            return;
        }
        if (input::is_repeated(input::UP)) {
            double gold = rpg::game_party().gold();
            double max_price = shop_type() == 0 ? std::min(std::floor(gold / price_), max_amount_) : max_amount_;
            if (amount < max_amount_) {
                play_cursor_se();
                amount = std::min(amount + 10, max_price);
                draw_shopping_values();
                update_arrow_visibility();
            }
            return;
        }
        if (input::is_repeated(input::LEFT)) {
            if (amount > 1) {
                play_cursor_se();
                amount--;
                draw_shopping_values();
                update_arrow_visibility();
            }
            return;
        }
        if (input::is_repeated(input::DOWN)) {
            if (amount > 0) {
                play_cursor_se();
                amount = std::max(amount - 10, 1.0);
                draw_shopping_values();
                update_arrow_visibility();
            }
            return;
        }
    }

    void update_arrow_visibility() {
        bool buying = shop_type() == 0;
        double total = buying ? total_price() : 0;
        double next_price = buying ? (amount + 1) * price_ : -1;
        double gold = rpg::game_party().gold();
        if (total >= gold || gold < next_price || amount >= max_amount_) right_arrow_->set_opacity(160);
        else right_arrow_->set_opacity(255);
        if (amount <= 1) left_arrow_->set_opacity(160);
        else left_arrow_->set_opacity(255);
    }

    void sync_arrow_openness() {
        if (left_arrow_) left_arrow_->visible = openness() == 255;
        if (right_arrow_) right_arrow_->visible = openness() == 255;
    }

    double amount = 0;

private:
    double price_ = 0;
    double max_amount_ = 0;
    std::function<void()> ok_handler_, cancel_handler_;
    g2d::SpritePtr left_arrow_, right_arrow_;
};

class WindowItemShopMessage;

class WindowOmoriShopItemList : public WindowItemList {
public:
    void initialize() {
        WindowItemList::initialize(0, 0, 250, 290 - 37);
        refresh();
        set_openness(0);
        select(0);
        activate();
    }
    using WindowItemList::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    bool includes(const rpg::ItemRef&) override { return true; }
    int max_cols() override { return 1; }
    double line_height() override { return 27; }
    int spacing() override { return 0; }
    int standard_padding() override { return 10; }
    double custom_cursor_rect_x_offset() override { return 8; }

    void make_item_list() override {
        data_.clear();
        prices_.clear();
        has_data_ = true;
        if (shop_type() == 0) {
            js::Val goods = shop_get("goods");
            int n = goods.is_obj() ? js::vm().get(goods, js::atom("length")).to_int() : 0;
            for (int i = 0; i < n; i++) {
                js::Val g = js::vm().get(goods, js::Val::num(i));
                int kind = js::vm().get(g, js::Val::num(0)).to_int();
                int id = js::vm().get(g, js::Val::num(1)).to_int();
                rpg::ItemRef item;
                switch (kind) {
                case 0: item = rpg::ItemRef::item(id); break;
                case 1: item = rpg::ItemRef::weapon(id); break;
                case 2: item = rpg::ItemRef::armor(id); break;
                }
                if (item.valid()) {
                    double price = js::vm().get(g, js::Val::num(2)).to_num() == 0 ? item.data()["price"].num() : js::vm().get(g, js::Val::num(3)).to_num();
                    data_.push_back(item);
                    prices_.push_back(price);
                }
            }
        } else {
            for (const rpg::ItemRef& item : rpg::game_party().all_items()) {
                if (item.valid() && item.data()["price"].num() > 0) {
                    data_.push_back(item);
                    prices_.push_back(item.data()["price"].num());
                }
            }
        }
    }

    rpg::ItemRef item() override {
        if (data_.empty()) return rpg::ItemRef();
        int idx = index();
        if (idx >= (int)data_.size() || idx < 0) return rpg::ItemRef();
        return data_[(size_t)idx];
    }

    double price(int idx) {
        double p = (idx >= 0 && idx < (int)prices_.size()) ? prices_[(size_t)idx] : 0;
        if (shop_type() == 1) p /= 2;
        return std::floor(p + 0.5);
    }
    double price() { return price(index()); }

    bool is_enabled(const rpg::ItemRef& item) override { return item.valid(); }

    ItemRect item_rect(int index) override {
        ItemRect r = WindowSelectable::item_rect(index);
        r.y += 15;
        return r;
    }

    void draw_item(int index) override {
        if (index < 0 || index >= (int)data_.size()) return;
        rpg::ItemRef it = data_[(size_t)index];
        c().font_size = blue("Window_OmoriShopItemList")["itemFontSize"].as_int();
        if (!it.valid()) return;
        ItemRect r = item_rect(index);
        r.x += 32;
        r.width -= 40;
        change_paint_opacity(is_enabled(it));
        c().draw_text(item_name(it), r.x, r.y, r.width - 50, r.height, "left");
        c().draw_text(js_fmt("%1 %2", {num_str(price(index)), world_currency_unit()}), r.x, r.y, r.width, r.height, "right");
        change_paint_opacity(true);
    }

    void refresh() override {
        WindowItemList::refresh();
        c().font_size = blue("Window_OmoriShopItemList")["transactionHeaderFontSize"].as_int();
        c().draw_text(shop_get("transactionHeader").to_str(), 0, -5, c().width() - 10, 20, "left");
        int mi = max_items();
        if (index() >= mi) index_ = std::max(0, mi - 1);
    }

    void select(int index) override {
        WindowItemList::select(index);
        notify_message_window();
    }

    void notify_message_window();

    WindowItemShopMessage* message_window = nullptr;

protected:
    void update_transform_hook() override {
        double w = width(), h = height();
        double p = 24, q = p / 2;
        down_arrow_sprite()->move(w - q, h - q);
        up_arrow_sprite()->move(w - q, q + 1);
    }

private:
    std::vector<double> prices_;
};


class SceneOmoriItemShop;

class WindowItemShopMessage {
public:
    void create(rpg::Scene& scene, SceneOmoriItemShop* parent);
    void draw_item(const rpg::ItemRef& item);
    void open() {
        if (WindowBase* b = base()) b->open();
    }
    void close() {
        if (WindowBase* b = base()) b->close();
    }
    bool is_closing() { return message_window_is_closing(window.get()); }
    void terminate_message() { message_window_terminate(window.get()); }
    WindowBase* base() { return message_window_base(window.get()); }
    void release() {
        if (window) {
            set_message_hooks(window.get(), MessageHooks());
            forget_message_window(window.get());
        }
    }

    std::shared_ptr<WindowMessage> window;
    WindowOmoriShopItemList* item_list_window = nullptr;
    WindowOmoriShopTotal* total_window = nullptr;

private:
    SceneOmoriItemShop* parent_ = nullptr;
    bool drawing_item_text_ = false;
    std::string convert(const std::string& text);
};

void WindowOmoriShopItemList::notify_message_window() {
    if (message_window) message_window->draw_item(item());
}

class SceneOmoriItemShop : public SceneBaseEX {
public:
    const char* name() const override { return "Scene_OmoriItemShop"; }

    void prepare(bool purchase_only) {
        purchase_only_ = purchase_only;
    }

    bool is_busy() override {
        if (message_.is_closing()) return true;
        return rpg::Scene::is_busy();
    }

    void start() override {
        SceneBaseEX::start();
        item_list_window_->select(0);
        message_.open();
        gold_window_->open();
        item_list_window_->open();
        shop_header_window_->open();
        g2d::Sprite* obj = shop_keeper_sprite_.get();
        move.start_move(ObjectMovement::tween(obj, {{prop_opacity(obj), 255, 10}}, linear_tween));
    }

    void terminate() override {
        SceneBaseEX::terminate();
        message_.release();
        rpg::game_temp().clear_shop_data();
    }

    void create() override {
        SceneBaseEX::create();
        create_background();
        create_shop_keeper_sprite();
        create_gold_window();
        create_shop_header_window();
        create_item_list_window();
        create_total_window();
        create_message_window();
        g2d::Window* mw = message_window_as_window(message_.window.get());
        shop_keeper_sprite_->x = (mw->x + mw->width()) - shop_keeper_sprite_->width() - 50;
        shop_keeper_sprite_->y = mw->y - shop_keeper_sprite_->height() + 30;
    }

    void create_background() {
        background_sprite_ = std::make_shared<g2d::Sprite>(shop_snap_slot());
        add_child(background_sprite_);
        shop_snap_slot().reset();
    }

    void create_shop_keeper_sprite() {
        shop_keeper_sprite_ = std::make_shared<g2d::Sprite>(load_system("mailbox"));
        shop_keeper_sprite_->set_frame(0, 0, 251, 344);
        shop_keeper_sprite_->visible = shop_get("showMailboxShopkeeper").truthy();
        shop_keeper_sprite_->set_opacity(0);
        add_child(shop_keeper_sprite_);
        shop_keepers_face_sprite_ = std::make_shared<g2d::Sprite>(load_system("mailbox"));
        shop_keepers_face_sprite_->x = 50;
        shop_keepers_face_sprite_->y = 65;
        shop_keeper_sprite_->add_child(shop_keepers_face_sprite_);
        set_shop_keeper_face(0);
    }

    void create_gold_window() {
        gold_window_ = std::shared_ptr<WindowGold>(new WindowGold());
        gold_window_->initialize(0.0, 0.0);
        gold_window_->x = (640 - gold_window_->width()) - 16;
        gold_window_->y = 10;
        gold_window_->set_openness(0);
        add_child(gold_window_);
    }

    void create_shop_header_window() {
        shop_header_window_ = WindowBase::make<WindowOmoriShopHeader>();
        shop_header_window_->x = 16;
        shop_header_window_->y = 10;
        add_child(shop_header_window_);
    }

    void create_item_list_window() {
        item_list_window_ = WindowBase::make<WindowOmoriShopItemList>();
        item_list_window_->x = 16;
        item_list_window_->y = shop_header_window_->y + shop_header_window_->height();
        item_list_window_->set_handler("ok", [this] { on_item_list_ok(); });
        item_list_window_->set_handler("cancel", [this] { shop_pop_scene(); });
        item_list_window_->call_update_help();
        add_child(item_list_window_);
    }

    void create_total_window() {
        total_window_ = WindowBase::make<WindowOmoriShopTotal>();
        total_window_->x = (640 - total_window_->width()) - 16;
        total_window_->y = (480 - total_window_->height()) - 124;
        total_window_->set_handlers([this] { on_total_window_ok(); }, [this] { on_total_window_cancel(); });
        add_child(total_window_);
    }

    void create_message_window() {
        create_window_layer();
        message_.item_list_window = item_list_window_.get();
        message_.total_window = total_window_.get();
        message_.create(*this, this);
        item_list_window_->message_window = &message_;
    }

    void set_shop_keeper_face(double index) { shop_keepers_face_sprite_->set_frame(251, index * 28, 79, 28); }

    bool wait_for_message() { return rpg::game_message().is_busy(); }

    void shop_pop_scene() {
        queue([this] {
            gold_window_->close();
            item_list_window_->close();
            shop_header_window_->close();
            message_.close();
            g2d::Sprite* obj = shop_keeper_sprite_.get();
            move.start_move(ObjectMovement::tween(obj, {{prop_opacity(obj), 0, 10}}, linear_tween));
        });
        queue_wait(10);
        queue([this] { pop_scene(); });
    }

    void on_item_list_ok() {
        rpg::ItemRef item = item_list_window_->item();
        double price = item_list_window_->price();
        bool buying = shop_type() == 0;
        double max_items = buying ? 99 - rpg::game_party().num_items(item) : rpg::game_party().num_items(item);
        bool can_buy = max_items > 0;
        bool has_gold = price <= rpg::game_party().gold();
        if (buying && (!can_buy || !has_gold)) {
            queue([can_buy, has_gold] {
                std::string message;
                if (!can_buy) message = shop_text("maxItemMessage");
                if (!has_gold) message = shop_text("notEnoughMoneyMessage");
                rpg::game_message().show_language_message(message);
            });
            queue([this] { start_function_wait([this] { return wait_for_message(); }); });
            queue([this] { item_list_window_->activate(); });
            return;
        }
        total_window_->open();
        total_window_->set_price(price, max_items);
        total_window_->activate();
        if (buying) rpg::game_message().show_language_message(shop_text("onItemListBuyOkMessage"));
        else rpg::game_message().show_language_message(shop_text("onItemListSellOkMessage"));
    }

    void on_total_window_ok() {
        rpg::game_message().choice_callback = [this](int n) { on_purchase_choice(n); };
        if (shop_type() == 0) rpg::game_message().show_language_message(shop_text("itemBuyingPromptMessage"));
        else rpg::game_message().show_language_message(shop_text("itemSellingPromptMessage"));
        const Value& text = plugin_data("itemShopMenu", "buyOptions");
        rpg::game_message().set_choices({data::js_string(text[0]), data::js_string(text[1])}, 0, 1);
    }

    void on_total_window_cancel() {
        message_.terminate_message();
        item_list_window_->activate();
    }

    void on_purchase_choice(int n) {
        input::clear();
        std::string message;
        if (n == 0) {
            rpg::ItemRef item = item_list_window_->item();
            double total = total_window_->total_price();
            if (shop_type() == 0) {
                play_se("SYS_cha_ching", 90, 100, 0);
                rpg::game_party().lose_gold(total);
                rpg::game_party().gain_item(item, total_window_->amount);
                message = shop_text("itemBuyingConfirmationMessage");
            } else {
                rpg::game_party().gain_gold(total);
                play_se("SYS_cha_ching", 90, 100, 0);
                rpg::game_party().lose_item(item, total_window_->amount);
                message = shop_text("itemSellingConfirmationMessage");
                item_list_window_->refresh();
            }
            gold_window_->refresh();
        } else {
            if (shop_type() == 0) message = shop_text("itemBuyingCancelMessage");
            else message = shop_text("itemSellingCancelMessage");
        }
        if (!message.empty()) {
            queue([message] {
                rpg::game_message().clear();
                rpg::game_message().show_language_message(message);
            });
        }
        queue([this] { start_function_wait([this] { return wait_for_message(); }); });
        queue([this] {
            rpg::game_message().clear();
            item_list_window_->activate();
        });
    }

private:
    bool purchase_only_ = false;
    std::shared_ptr<g2d::Sprite> background_sprite_;
    std::shared_ptr<g2d::Sprite> shop_keeper_sprite_;
    std::shared_ptr<g2d::Sprite> shop_keepers_face_sprite_;
    std::shared_ptr<WindowGold> gold_window_;
    std::shared_ptr<WindowOmoriShopHeader> shop_header_window_;
    std::shared_ptr<WindowOmoriShopItemList> item_list_window_;
    std::shared_ptr<WindowOmoriShopTotal> total_window_;
    WindowItemShopMessage message_;
};

void WindowItemShopMessage::create(rpg::Scene& scene, SceneOmoriItemShop* parent) {
    parent_ = parent;
    window = create_message_window(scene);
    MessageHooks hooks;
    hooks.after_terminate_message = [this] {
        open();
        message_window_reopen_name_box(window.get());
    };
    hooks.new_line_x = [this](double& v) {
        if (!drawing_item_text_) return false;
        v = 110;
        return true;
    };
    hooks.convert_escape_characters = [this](const std::string& text) { return convert(text); };
    hooks.process_escape_character = [this](const std::string& code, TextState& ts) {
        if (code == "SHOPFACE") parent_->set_shop_keeper_face(base()->obtain_escape_param(ts).to_num());
    };
    set_message_hooks(window.get(), std::move(hooks));
}

std::string WindowItemShopMessage::convert(const std::string& text) {
    auto replace_ci = [](std::string s, const std::string& tag, const std::string& with) {
        std::string lower = rpg::to_lower(s);
        std::string out;
        size_t from = 0;
        size_t pos = lower.find(tag);
        while (pos != std::string::npos) {
            out += s.substr(from, pos - from);
            out += with;
            from = pos + tag.size();
            pos = lower.find(tag, from);
        }
        out += s.substr(from);
        return out;
    };
    rpg::ItemRef item = item_list_window ? item_list_window->item() : rpg::ItemRef();
    std::string out = replace_ci(text, "<shopitem>", item.valid() ? item_name(item) : std::string());
    out = replace_ci(out, "<shopamount>", num_str(total_window ? total_window->amount : 0));
    out = replace_ci(out, "<shoptotal>", num_str(total_window ? total_window->total_price() : 0));
    return out;
}

void WindowItemShopMessage::draw_item(const rpg::ItemRef& item) {
    if (!item.valid() || !window) return;
    WindowBase* w = base();
    std::string item_text = item_description(item);
    drawing_item_text_ = true;
    message_window_draw_text_page(window.get(), item_text, 110, 24);
    w->draw_item_icon(item, 0, -5, 1);
    w->c().draw_text(item_name(item), 106, 0, w->c().width() - 106, 24, "left");
    std::string owned = js_fmt("(%1: %2)", {data::js_string(plugin_data("itemShopMenu", "ownedText")), num_str(rpg::game_party().num_items(item))});
    w->c().draw_text(owned, 0, 0, w->c().width() - 10, 24, "right");
    drawing_item_text_ = false;
}

}

rpg::SceneFactory item_shop_scene(const Value* goods, std::shared_ptr<const void> keep, bool purchase_only) {
    return rpg::SceneFactory{"Scene_OmoriItemShop", [keep, purchase_only] {
                                 auto* scene = new menu::SceneOmoriItemShop();
                                 scene->prepare(purchase_only);
                                 return std::unique_ptr<rpg::Scene>(scene);
                             }};
}

static SceneRegistrar r_scene_omori_item_shop("Scene_OmoriItemShop", item_shop_scene(nullptr, nullptr, false));

}
