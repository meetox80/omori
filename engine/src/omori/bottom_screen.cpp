#include "bottom_screen.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/image.h"
#include "../platform/gfx.h"
#include "../platform/platform.h"
#include "../rpg/action.h"
#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/scene.h"
#include "menu_common.h"
#include "ui_util.h"

namespace ui {
namespace bottom {

namespace {

enum class Mode { Off, Hidden, Play };

constexpr int TAB_COUNT = 4;
constexpr double TAB_Y = 6;
constexpr double INFO_Y = 72;
constexpr double LIST_Y = 68;
constexpr double LIST_H = 308;
constexpr double HELP_Y = 384;
constexpr double CARD_W = 155;
constexpr double CARD_H = 109;
constexpr double FACE_H = 106;
constexpr double CARD_Y = 480 - CARD_H;
constexpr double CARD_HIDDEN_Y = 480 + FACE_H + 8;
constexpr double HELP_HIDDEN_Y = 480 + 8;
constexpr double LIFT = 8;
constexpr double POINTER_RISE = 170;
constexpr double BACKDROP_EDGE = 32;
constexpr double BACKDROP_H = POINTER_RISE + BACKDROP_EDGE + 12 + CARD_H + 8;
constexpr double DRAG_SLOP = 10;

const char* const CATEGORIES[3] = {"consumables", "toys", "important"};

const Value& main_menu_text(const char* key) { return menu::plugin_data("mainMenu", key); }

std::string party_command_text() {
    const Value& text = main_menu_text("commands")[0];
    if (rpg::game_switches().value(4)) return data::js_string(text[2]);
    if (rpg::game_switches().value(17)) return data::js_string(text[1]);
    return data::js_string(text[0]);
}

bool party_command_enabled() { return rpg::game_switches().value(5); }

bool category_enabled(int category) {
    rpg::GameParty& party = rpg::game_party();
    if (category == 0) return party.has_consumable_items();
    if (category == 1) return party.has_toy_items();
    return party.has_key_items();
}

class TabWindow : public WindowCommand {
public:
    void initialize(double x, double y) override {
        WindowCommand::initialize(x, y);
        if (custom_cursor_sprite_) custom_cursor_sprite_->init_cursor_animation(0);
    }
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double window_width() override { return 620; }
    int num_visible_rows() override { return 1; }
    int max_cols() override { return TAB_COUNT; }
    int spacing() override { return 12; }
    int standard_padding() override { return 15; }
    double line_height() override { return 24; }
    int standard_font_size() override { return menu::blue("Window_MenuCommand")["standardFontSize"].as_int(); }
    double custom_cursor_rect_y_offset() override { return 1; }
    double custom_cursor_rect_text_x_offset() override { return 20; }
    ItemRect item_rect_for_text(int index) override {
        ItemRect r = WindowCommand::item_rect_for_text(index);
        r.y -= 3;
        return r;
    }
    void make_command_list() override {
        add_command(party_command_text(), "party", party_command_enabled());
        const Value& text = menu::plugin_data("itemMenu", "categories");
        for (int i = 0; i < 3; i++) add_command(data::js_string(text[i]), CATEGORIES[i], category_enabled(i));
    }
    void draw_item(int index) override {
        if (index == 0 && rpg::game_switches().value(4)) change_text_color(text_color(9));
        else reset_text_color();
        ItemRect r = item_rect_for_text(index);
        change_paint_opacity(is_command_enabled(index));
        draw_text(command_name(index), r.x, r.y, r.width, item_text_align());
        change_paint_opacity(true);
    }
    void process_cursor_move() override {}
    void process_handling() override {}
};

class InfoWindow : public WindowBase {
public:
    void initialize(double x, double y, double w, double h) override {
        WindowBase::initialize(x, y, w, h);
        refresh(std::string());
    }
    int standard_padding() override { return 8; }
    double line_height() override { return 24; }
    int standard_font_size() override { return menu::blue("Window_MenuCommand")["standardFontSize"].as_int(); }
    void refresh(const std::string& text) {
        text_ = text;
        c().clear();
        draw_text(text_, 4, (contents_height() - line_height()) / 2, contents_width() - 8);
    }
    const std::string& text() const { return text_; }

private:
    std::string text_;
};

class ItemListWindow : public menu::WindowItemList {
public:
    void initialize(double x, double y, double w, double h) override {
        menu::WindowItemList::initialize(x, y, w, h);
        if (custom_cursor_sprite_) custom_cursor_sprite_->init_cursor_animation(0);
        activate();
    }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 8; }
    int max_cols() override { return 2; }
    int spacing() override { return 24; }
    double item_height() override { return 28; }
    double custom_cursor_rect_x_offset() override { return 6; }
    double custom_cursor_rect_text_x_offset() override { return 24; }
    bool is_enabled(const rpg::ItemRef&) override { return true; }
    bool includes(const rpg::ItemRef& item) override { return menu::pocket_includes(item, category()); }
    void make_item_list() override {
        menu::WindowItemList::make_item_list();
        menu::sort_pocket_items(data(), category());
    }
    void draw_item(int index) override {
        if (index < 0 || index >= (int)data().size()) return;
        const rpg::ItemRef& it = data()[(size_t)index];
        if (!it.valid()) return;
        ItemRect r = item_rect_for_text(index);
        r.width -= custom_cursor_rect_text_x_offset();
        c().font_size = menu::blue("Window_OmoMenuItemList")["drawItem_contents_fontsize"].as_int();
        std::string count = "x" + menu::num_str(rpg::game_party().num_items(it));
        double count_w = text_width(count) + 8;
        c().draw_text(menu::item_name(it), r.x, r.y, r.width - count_w, r.height, "left");
        c().draw_text(count, r.x, r.y, r.width - 4, r.height, "right");
        reset_font_settings();
    }
    void update_help() override {}
    void process_cursor_move() override {}
    void process_handling() override {}
    void update_transform_hook() override {
        double w = width(), h = height(), q = 12;
        down_arrow_sprite()->move(w - q, h - q);
        up_arrow_sprite()->move(w - q, q);
    }
    int hit(double lx, double ly) {
        if (!visible || openness() < 255) return -1;
        double px = lx - x - padding(), py = ly - y - padding();
        if (px < 0 || py < 0 || px >= contents_width() || py >= contents_height()) return -1;
        int top = top_index(), last = std::min(max_items(), top + max_page_items());
        for (int i = top; i < last; i++) {
            ItemRect r = item_rect(i);
            if (px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height) return i < (int)data().size() ? i : -1;
        }
        return -1;
    }
};

struct Card {
    std::shared_ptr<menu::WindowOmoMainMenuActorStatus> window;
    std::vector<double> key;
    double target_y = CARD_HIDDEN_Y;
};

struct Ui {
    std::shared_ptr<g2d::Sprite> root;
    std::shared_ptr<TabWindow> tabs;
    std::shared_ptr<InfoWindow> info;
    std::shared_ptr<menu::WindowGold> gold;
    std::shared_ptr<ItemListWindow> list;
    std::shared_ptr<menu::WindowOmoMenuHelp> help;
    std::shared_ptr<g2d::Sprite> backdrop;
    std::shared_ptr<menu::WindowOmoWindowIndexCursor> pointer;
    std::vector<Card> cards;
    std::vector<int> members;
    int world = -1;
    std::vector<double> tabs_key, list_key;
    double gold_value = -1;
    double help_target_y = HELP_Y;
    rpg::ItemRef help_item;
    bool help_has_item = false;
    int fade = 255;
};

enum class Hit { None, Tab, Card, Item };

struct Press {
    Hit hit = Hit::None;
    int index = -1;
    double x = 0, y = 0, last_x = 0, last_y = 0;
    double scroll = 0;
    bool dragged = false;
};

std::unique_ptr<Ui> g_ui;
Mode g_mode = Mode::Off;
int g_page = 0;
bool g_targeting = false;
bool g_dirty = true;
bool g_down = false;
Press g_press;
std::vector<double> g_sig;

Mode current_mode() {
    if (!rpg::game_objects_exist()) return Mode::Off;
    rpg::Scene* s = rpg::scene_manager::scene();
    if (!s || rpg::scene_manager::is_scene_changing() || !rpg::scene_manager::is_current_scene_started()) return g_mode;
    std::string name = s->name();
    if (name == "Scene_Boot" || name == "Scene_SplashScreens" || name == "Scene_OmoriTitleScreen" || name == "Scene_OmoriFile" ||
        name == "Scene_Gameover")
        return Mode::Off;
    if (rpg::game_party().size() == 0) return Mode::Off;
    return name == "Scene_Map" ? Mode::Play : Mode::Hidden;
}

bool can_act() {
    if (g_mode != Mode::Play || rpg::scene_manager::is_scene_changing()) return false;
    if (rpg::game_message().is_busy() || rpg::game_map().is_event_running()) return false;
    if (rpg::map_character_tag_active() || rpg::game_player().is_transferring()) return false;
    return true;
}

std::vector<int> member_ids() {
    std::vector<int> ids;
    for (rpg::GameActor* a : rpg::game_party().members()) ids.push_back(a ? a->actor_id() : 0);
    return ids;
}

std::vector<double> card_key(rpg::GameActor* a) {
    std::vector<double> k;
    if (!a) return k;
    k = {(double)a->actor_id(), a->hp(), a->mp(), a->mhp(), a->mmp(), (double)a->level, a->current_exp(), (double)std::hash<std::string>()(a->name())};
    for (const Value* st : a->states()) k.push_back(st ? (*st)["id"].num() : 0);
    return k;
}

std::vector<double> list_key(const std::string& category) {
    std::vector<double> k;
    rpg::GameParty& party = rpg::game_party();
    for (const rpg::ItemRef& it : party.all_items()) {
        if (!menu::pocket_includes(it, category)) continue;
        k.push_back(it.id);
        k.push_back(party.num_items(it));
    }
    return k;
}

std::vector<double> tabs_key() {
    rpg::GameSwitches& sw = rpg::game_switches();
    return {(double)sw.value(4), (double)sw.value(5), (double)sw.value(17), (double)category_enabled(0), (double)category_enabled(1),
            (double)category_enabled(2)};
}

void build() {
    g_ui.reset(new Ui());
    Ui& ui = *g_ui;
    ui.world = current_world();
    ui.members = member_ids();
    ui.root = std::make_shared<g2d::Sprite>();

    ui.info = WindowBase::make<InfoWindow>(10, INFO_Y, 400, 48);
    ui.root->add_child(ui.info);

    ui.gold = std::shared_ptr<menu::WindowGold>(new menu::WindowGold());
    ui.gold->initialize(0, 0);
    ui.gold->x = 640 - 10 - ui.gold->width();
    ui.gold->y = INFO_Y + (48 - ui.gold->height()) / 2;
    ui.gold->set_openness(255);
    ui.root->add_child(ui.gold);

    ui.list = WindowBase::make<ItemListWindow>(10, LIST_Y, 620, LIST_H);
    ui.root->add_child(ui.list);

    ui.help = std::shared_ptr<menu::WindowOmoMenuHelp>(new menu::WindowOmoMenuHelp());
    ui.help->initialize();
    ui.help->x = (640 - ui.help->width()) / 2;
    ui.help->y = HELP_Y;
    ui.root->add_child(ui.help);

    g2d::BitmapPtr shade = g2d::Bitmap::create(640, (int)BACKDROP_H);
    shade->gradient_fill_rect(0, 0, 640, BACKDROP_EDGE, "rgba(0, 0, 0, 0)", "rgba(0, 0, 0, 1)", true);
    shade->fill_rect(0, BACKDROP_EDGE, 640, BACKDROP_H - BACKDROP_EDGE, "rgba(0, 0, 0, 1)");
    ui.backdrop = std::make_shared<g2d::Sprite>(shade);
    ui.backdrop->visible = false;
    ui.root->add_child(ui.backdrop);

    std::vector<rpg::GameActor*> members = rpg::game_party().members();
    double row_w = CARD_W * members.size();
    for (size_t i = 0; i < members.size(); i++) {
        Card card;
        card.window = std::shared_ptr<menu::WindowOmoMainMenuActorStatus>(new menu::WindowOmoMainMenuActorStatus());
        card.window->initialize((int)i);
        card.window->x = std::floor((640 - row_w) / 2 + CARD_W * i);
        card.window->y = g_page == 0 ? CARD_Y : CARD_HIDDEN_Y;
        card.target_y = card.window->y;
        card.key = card_key(members[i]);
        ui.root->add_child(card.window);
        ui.cards.push_back(card);
    }

    ui.pointer = WindowBase::make<menu::WindowOmoWindowIndexCursor>();
    ui.pointer->visible = false;
    ui.root->add_child(ui.pointer);

    ui.tabs = std::shared_ptr<TabWindow>(new TabWindow());
    ui.tabs->initialize(10, TAB_Y);
    ui.tabs->select(g_page);
    ui.root->add_child(ui.tabs);
    ui.tabs_key = tabs_key();

    if (g_page > 0) {
        ui.list->set_category(CATEGORIES[g_page - 1]);
        ui.list_key = list_key(ui.list->category());
        if (ui.list->max_items() > 0 && ui.list->item().valid()) ui.list->select(0);
    }
    g_targeting = false;
    g_dirty = true;
}

bool needs_rebuild() { return !g_ui || g_ui->world != current_world() || g_ui->members != member_ids(); }

rpg::ItemRef selected_item() { return g_ui && g_page > 0 ? g_ui->list->item() : rpg::ItemRef(); }

std::vector<rpg::GameBattler*> item_targets(rpg::GameBattler* user, const rpg::ItemRef& item, rpg::GameActor* chosen) {
    std::vector<rpg::GameBattler*> out;
    rpg::GameAction action(user);
    action.set_item_object(item);
    if (!action.is_for_friend()) return out;
    if (action.is_for_all()) {
        for (rpg::GameActor* a : rpg::game_party().members()) out.push_back(a);
    } else if (chosen) {
        out.push_back(chosen);
    }
    return out;
}

bool effects_valid(rpg::GameBattler* user, const rpg::ItemRef& item, const std::vector<rpg::GameBattler*>& targets) {
    rpg::GameAction action(user);
    action.set_item_object(item);
    for (rpg::GameBattler* t : targets) {
        if (t && action.test_apply(t)) return true;
    }
    return false;
}

bool item_for_all(const rpg::ItemRef& item) {
    rpg::GameAction action(menu::pocket_item_user());
    action.set_item_object(item);
    return action.is_for_all();
}

bool item_usable(const rpg::ItemRef& item) {
    if (!item.valid() || g_page != 1 || !can_act() || !rpg::game_system().menu_enabled) return false;
    rpg::GameBattler* user = menu::pocket_item_user();
    if (!user || !user->can_use(item)) return false;
    std::vector<rpg::GameBattler*> members;
    for (rpg::GameActor* a : rpg::game_party().members()) members.push_back(a);
    return effects_valid(user, item, members);
}

void use_item_on(rpg::GameActor* actor) {
    rpg::ItemRef item = selected_item();
    rpg::GameBattler* user = menu::pocket_item_user();
    if (!item_usable(item) || !user) {
        play_buzzer();
        return;
    }
    std::vector<rpg::GameBattler*> targets = item_targets(user, item, actor);
    if (!effects_valid(user, item, targets)) {
        play_buzzer();
        return;
    }
    rpg::audio_manager::play_system_sound(22);
    user->use_item(item);
    rpg::GameAction action(user);
    action.set_item_object(item);
    for (rpg::GameBattler* target : targets) {
        if (!target) continue;
        for (int i = 0; i < action.num_repeats(); i++) action.apply(target);
    }
    action.apply_global();
    menu::play_item_heal_se(item);
    rpg::GameTemp& temp = rpg::game_temp();
    if (temp.is_common_event_reserved() && temp.is_common_event_for_battle_only()) temp.clear_common_event();
    ItemListWindow& list = *g_ui->list;
    int index = list.index();
    list.refresh();
    g_ui->list_key = list_key(list.category());
    if (rpg::game_party().num_items(item) <= 0) {
        g_targeting = false;
        int count = (int)list.data().size();
        list.select(count > 0 ? std::min(index, count - 1) : -1);
    }
    menu::goto_gameover_if_dead();
}

void tag_actor(int index) {
    if (!can_act()) {
        play_buzzer();
        return;
    }
    if (rpg::game_switches().value(4)) {
        if (index != 0) {
            play_buzzer();
            return;
        }
        play_ok();
        rpg::game_temp().reserve_common_event(34);
        return;
    }
    std::vector<rpg::GameActor*> members = rpg::game_party().members();
    if (!party_command_enabled() || index <= 0 || index >= (int)members.size() || !rpg::map_can_use_character_tag()) {
        play_buzzer();
        return;
    }
    play_ok();
    rpg::game_variables().set_number(rpg::params().tag_selected_variable, members[(size_t)index]->actor_id());
    rpg::game_temp().reserve_common_event(rpg::params().tag_common_event);
}

void show_page(int page) {
    Ui& ui = *g_ui;
    g_page = page;
    g_targeting = false;
    ui.tabs->select(page);
    if (page > 0) {
        ui.list->set_category(CATEGORIES[page - 1]);
        ui.list->reset_scroll();
        ui.list_key = list_key(ui.list->category());
        ui.list->select(ui.list->data().empty() ? -1 : 0);
    }
    ui.fade = 0;
}

int card_hit(double lx, double ly) {
    for (size_t i = 0; i < g_ui->cards.size(); i++) {
        menu::WindowOmoMainMenuActorStatus& w = *g_ui->cards[i].window;
        if (!w.visible) continue;
        if (lx >= w.x && lx < w.x + CARD_W && ly >= w.y - FACE_H && ly < w.y + CARD_H) return (int)i;
    }
    return -1;
}

int tab_hit(double lx, double ly) {
    TabWindow& t = *g_ui->tabs;
    double px = lx - t.x - t.padding(), py = ly - t.y;
    if (py < 0 || py >= t.height() || px < -t.padding() || px >= t.contents_width() + t.padding()) return -1;
    for (int i = 0; i < t.max_items(); i++) {
        ItemRect r = t.item_rect(i);
        if (px >= r.x - t.spacing() / 2.0 && px < r.x + r.width + t.spacing() / 2.0) return i;
    }
    return -1;
}

bool cards_shown() { return g_page == 0 || g_targeting; }

Press hit_test(double lx, double ly) {
    Press p;
    p.x = p.last_x = lx;
    p.y = p.last_y = ly;
    int i = tab_hit(lx, ly);
    if (i >= 0) {
        p.hit = Hit::Tab;
        p.index = i;
        return p;
    }
    if (cards_shown()) {
        i = card_hit(lx, ly);
        if (i >= 0) {
            p.hit = Hit::Card;
            p.index = i;
            return p;
        }
    }
    if (g_page > 0) {
        i = g_ui->list->hit(lx, ly);
        if (i >= 0 || (lx >= g_ui->list->x && lx < g_ui->list->x + g_ui->list->width() && ly >= g_ui->list->y &&
                       ly < g_ui->list->y + g_ui->list->height())) {
            p.hit = Hit::Item;
            p.index = i;
        }
    }
    return p;
}

void tap_tab(int index) {
    if (index == g_page && !g_targeting) return;
    bool enabled = index == 0 || g_ui->tabs->is_command_enabled(index);
    if (!enabled) {
        play_buzzer();
        return;
    }
    play_cursor();
    show_page(index);
}

void tap_card(int index) {
    if (g_page == 0) {
        tag_actor(index);
        return;
    }
    if (g_targeting) use_item_on(rpg::game_party().members()[(size_t)index]);
}

void tap_item(int index) {
    ItemListWindow& list = *g_ui->list;
    if (index < 0) {
        if (g_targeting) {
            g_targeting = false;
            play_cancel();
        }
        return;
    }
    if (index != list.index()) {
        list.select(index);
        g_targeting = false;
        play_cursor();
        return;
    }
    if (g_targeting) {
        g_targeting = false;
        play_cancel();
        return;
    }
    if (!item_usable(list.item())) {
        play_buzzer();
        return;
    }
    play_ok();
    g_targeting = true;
    double clear_y = CARD_Y - POINTER_RISE - BACKDROP_EDGE - list.y - list.padding();
    int rows_above = std::max(1, (int)std::floor(clear_y / list.item_height()));
    if (list.row() - list.top_row() >= rows_above) list.set_top_row(list.row() - rows_above + 1);
}

void tap_empty() {
    if (!g_targeting) return;
    g_targeting = false;
    play_cancel();
}

void handle_touch() {
    int tx = 0, ty = 0;
    bool now = plat::touch(tx, ty);
    double lx = tx / images::SCALE, ly = ty / images::SCALE;
    if (now && !g_down) {
        g_press = hit_test(lx, ly);
    } else if (now && g_down) {
        if (g_press.hit == Hit::Item && std::fabs(ly - g_press.y) > DRAG_SLOP) g_press.dragged = true;
        if (g_press.dragged) {
            ItemListWindow& list = *g_ui->list;
            g_press.scroll += g_press.last_y - ly;
            double step = list.item_height();
            while (g_press.scroll >= step) {
                list.set_top_row(list.top_row() + 1);
                g_press.scroll -= step;
            }
            while (g_press.scroll <= -step) {
                list.set_top_row(list.top_row() - 1);
                g_press.scroll += step;
            }
        }
        g_press.last_x = lx;
        g_press.last_y = ly;
    } else if (!now && g_down) {
        Press start = g_press;
        g_press = Press();
        Press end = hit_test(start.last_x, start.last_y);
        if (!start.dragged && end.hit == start.hit && end.index == start.index) {
            switch (end.hit) {
                case Hit::Tab: tap_tab(end.index); break;
                case Hit::Card: tap_card(end.index); break;
                case Hit::Item: tap_item(end.index); break;
                case Hit::None: tap_empty(); break;
            }
        }
    }
    g_down = now;
}

double approach(double v, double target) {
    double d = target - v;
    if (std::fabs(d) < 0.75) return target;
    return v + d * 0.35;
}

void sync() {
    Ui& ui = *g_ui;
    bool party = g_page == 0;
    std::vector<double> tk = tabs_key();
    if (tk != ui.tabs_key) {
        ui.tabs_key = tk;
        ui.tabs->refresh();
        ui.tabs->select(g_page);
    }
    std::string map_name = rpg::game_map().display_name();
    if (map_name != ui.info->text()) ui.info->refresh(map_name);
    ui.info->visible = party && !map_name.empty();
    double gold = rpg::game_party().gold();
    if (gold != ui.gold_value) {
        ui.gold_value = gold;
        ui.gold->refresh();
    }
    ui.gold->visible = party;

    ui.list->visible = !party;
    if (!party) {
        std::vector<double> lk = list_key(ui.list->category());
        if (lk != ui.list_key) {
            ui.list_key = lk;
            int index = ui.list->index();
            ui.list->refresh();
            int count = (int)ui.list->data().size();
            ui.list->select(count > 0 ? std::clamp(index, 0, count - 1) : -1);
        }
        if (g_targeting && !item_usable(ui.list->item())) g_targeting = false;
    }
    ui.list->set_contents_opacity(ui.fade);
    ui.help->visible = !party;
    rpg::ItemRef item = party ? rpg::ItemRef() : ui.list->item();
    if (item.valid() != ui.help_has_item || (item.valid() && item != ui.help_item)) {
        ui.help_item = item;
        ui.help_has_item = item.valid();
        ui.help->set_item(item);
    }
    ui.help_target_y = g_targeting ? HELP_HIDDEN_Y : HELP_Y;
    ui.help->y = approach(ui.help->y, ui.help_target_y);
    ui.help->set_contents_opacity(ui.fade);

    std::vector<rpg::GameActor*> members = rpg::game_party().members();
    bool for_all = g_targeting && item_for_all(item);
    for (size_t i = 0; i < ui.cards.size(); i++) {
        Card& card = ui.cards[i];
        rpg::GameActor* a = i < members.size() ? members[i] : nullptr;
        std::vector<double> k = card_key(a);
        if (k != card.key) {
            card.key = k;
            card.window->refresh();
        }
        bool pressed = g_down && g_press.hit == Hit::Card && g_press.index == (int)i;
        card.target_y = (cards_shown() ? CARD_Y : CARD_HIDDEN_Y) - (pressed ? LIFT : 0);
        card.window->y = approach(card.window->y, card.target_y);
        bool lively = party ? i == 0 : (for_all || pressed);
        card.window->animate_face(lively);
        card.window->set_contents_opacity_all(party ? ui.fade : 255);
    }

    menu::WindowOmoWindowIndexCursor& pointer = *ui.pointer;
    std::string text;
    double px = 0, py = 0;
    if (party && !ui.cards.empty()) {
        const Value& status = main_menu_text("status");
        text = rpg::game_switches().value(4) ? data::js_string(status["stab"]) : data::js_string(menu::blue("Map_Character_Tag")["leader"]);
        menu::WindowOmoMainMenuActorStatus& w = *ui.cards[0].window;
        px = w.x + (w.width() - pointer.width()) / 2;
        py = w.y - POINTER_RISE;
    } else if (g_targeting && !ui.cards.empty()) {
        text = data::js_string(main_menu_text("status")["select"]);
        px = (640 - pointer.width()) / 2;
        py = ui.cards[0].window->y - POINTER_RISE;
    }
    if (!ui.cards.empty()) {
        ui.backdrop->y = std::floor(ui.cards[0].window->y - POINTER_RISE - BACKDROP_EDGE - 12);
        ui.backdrop->visible = !party && ui.cards[0].window->y < 480;
    }
    if (!text.empty() && text != pointer.text()) pointer.set_text(text);
    pointer.x = std::floor(px);
    pointer.y = std::floor(py);
    pointer.visible = !text.empty();
    pointer.set_contents_opacity(party ? ui.fade : 255);
    if (pointer.index_sprite) pointer.index_sprite->set_opacity(party ? ui.fade : 255);

    if (ui.fade < 255) ui.fade = std::min(255, ui.fade + 51);
}

void push_sprite(std::vector<double>& sig, const g2d::Sprite* s) {
    if (!s) return;
    const g2d::Rect& f = s->frame();
    sig.insert(sig.end(), {(double)(uintptr_t)s->bitmap().get(), f.x, f.y, f.width, f.height, (double)s->visible, s->alpha});
}

std::vector<double> signature() {
    Ui& ui = *g_ui;
    std::vector<double> sig = {(double)g_page, (double)g_targeting, (double)ui.fade, (double)ui.list->index(), (double)ui.list->top_row(), ui.help->y,
                               (double)ui.pointer->visible, ui.pointer->x, ui.pointer->y, (double)std::hash<std::string>()(ui.pointer->text()),
                               (double)ui.tabs->index(), ui.gold_value, (double)std::hash<std::string>()(ui.info->text()), ui.backdrop->y,
                               (double)ui.backdrop->visible};
    sig.insert(sig.end(), ui.tabs_key.begin(), ui.tabs_key.end());
    sig.insert(sig.end(), ui.list_key.begin(), ui.list_key.end());
    for (const Card& card : ui.cards) {
        sig.push_back(card.window->y);
        sig.insert(sig.end(), card.key.begin(), card.key.end());
        push_sprite(sig, card.window->face_sprite.get());
    }
    for (g2d::Window* w : {(g2d::Window*)ui.tabs.get(), (g2d::Window*)ui.info.get(), (g2d::Window*)ui.gold.get(), (g2d::Window*)ui.list.get(),
                           (g2d::Window*)ui.help.get()}) {
        sig.push_back(w->visible);
        sig.push_back(w->openness());
    }
    return sig;
}

void update_bottom() {
    Mode mode = current_mode();
    if (mode != g_mode) {
        g_mode = mode;
        g_dirty = true;
        if (mode == Mode::Off) {
            g_ui.reset();
            g_targeting = false;
            g_down = false;
            g_press = Press();
            g_sig.clear();
        } else if (mode == Mode::Hidden) {
            g_targeting = false;
            g_down = false;
            g_press = Press();
            g_sig.clear();
        } else if (g_ui) {
            g_ui->fade = 0;
        }
    }
    if (g_mode != Mode::Play) return;
    if (needs_rebuild()) build();
    handle_touch();
    g_ui->root->update();
    sync();
    std::vector<double> sig = signature();
    if (sig != g_sig) {
        g_sig.swap(sig);
        g_dirty = true;
    }
}

void render_bottom() {
    if (!g_dirty) return;
    g_dirty = false;
    gfx::Surface keep = gfx::current_target();
    gfx::set_target(gfx::Surface::Bottom);
    if (g_ui && g_mode == Mode::Play) {
        g2d::RenderState rs;
        rs.clip = true;
        rs.cx0 = (int)images::OFFSET_X;
        rs.cy0 = (int)images::OFFSET_Y;
        rs.cx1 = (int)images::OFFSET_X + (int)std::lround(640 * images::SCALE);
        rs.cy1 = (int)images::OFFSET_Y + (int)std::lround(480 * images::SCALE);
        gfx::set_scissor(rs.cx0, rs.cy0, rs.cx1 - rs.cx0, rs.cy1 - rs.cy0);
        g_ui->root->render(rs);
        gfx::reset_scissor();
    }
    gfx::set_target(keep);
}

}

void update() {
    if constexpr (profile::BOTTOM_SCREEN) update_bottom();
}

void render() {
    if constexpr (profile::BOTTOM_SCREEN) render_bottom();
}

}
}
