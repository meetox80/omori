#include "bottom_battle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../gfx2d/bitmap.h"
#include "../rpg/game.h"
#include "../rpg/scene.h"
#include "battle_windows.h"
#include "menu_common.h"
#include "ui_util.h"
#include "window_base.h"

namespace ui {
namespace bottom {
namespace battle {

namespace {

constexpr int SLOTS = 4;
constexpr double CARD_W = 312;
constexpr double CARD_H = 230;
constexpr double EDGE_X = 5;
constexpr double EDGE_Y = 5;
constexpr double LINE = 4;
constexpr double FACE_X = 14;
constexpr double FACE_Y = 14;
constexpr double FACE = 100;
constexpr double FACE_INSET = (WindowBase::kFaceWidth - FACE) / 2;
constexpr double SIDE_X = FACE_X + FACE + 14;
constexpr double SIDE_W = CARD_W - SIDE_X - 12;
constexpr double NAME_Y = 12;
constexpr double NAME_H = 48;
constexpr int NAME_SIZE = 32;
constexpr double HEADER_H = 32;
constexpr double HEADER_Y = FACE_Y + FACE - HEADER_H;
constexpr double ROW_H = 44;
constexpr double HP_Y = 130;
constexpr double MP_Y = 180;
constexpr double ICON_W = 36;
constexpr double ICON_H = 32;
constexpr double SLOT_X = 58;
constexpr double SLOT_W = CARD_W - SLOT_X - 12;
constexpr double BAR_INSET = 4;
constexpr double NUMBER_PAD = 10;
constexpr double ICON_SRC_X = 7;
constexpr double ICON_SRC_W = 18;
constexpr double ICON_SRC_H = 16;
constexpr double HEART_SRC_Y = 54;
constexpr double DROP_SRC_Y = 82;
constexpr double BAR_SRC_W = 116;
constexpr double BAR_SRC_H = 14;
constexpr double HP_BAR_SRC_Y = 109;
constexpr double MP_BAR_SRC_Y = 123;
constexpr int NUMBER_SIZES[] = {40, 36, 32, 28};
constexpr int FADE_STEP = 51;

const char* const FILL = "#dedede";
const char* const LIGHT = "#fbfcfb";
const char* const SHADE = "#a7a7a7";
const char* const INK = "#000000";

struct Card {
    std::shared_ptr<g2d::Sprite> body, back, face, header;
    std::vector<double> key;
};

struct Panel {
    std::shared_ptr<g2d::Sprite> root;
    Card cards[SLOTS];
};

std::unique_ptr<Panel> g_panel;
int g_alpha = 0;
bool g_active = false;
bool g_dirty = true;
std::vector<double> g_sig;

btl::SceneBattle* battle_scene() {
    if (!rpg::game_objects_exist()) return nullptr;
    rpg::Scene* s = rpg::scene_manager::scene();
    return s ? dynamic_cast<btl::SceneBattle*>(s) : nullptr;
}

double card_x(int slot) { return EDGE_X + (slot % 2) * (CARD_W + (640 - EDGE_X * 2 - CARD_W * 2)); }

double card_y(int slot) { return EDGE_Y + (double)(slot / 2) * (CARD_H + (480 - EDGE_Y * 2 - CARD_H * 2)); }

void bevel(g2d::Bitmap& b, double x, double y, double w, double h) {
    b.fill_rect(x, y, w, h, FILL);
    b.fill_rect(x, y, w, 2, LIGHT);
    b.fill_rect(x, y + h - 2, w, 2, SHADE);
}

int number_size(g2d::Bitmap& b, const std::string& text, double width) {
    for (int size : NUMBER_SIZES) {
        b.font_size = size;
        if (b.measure_text_width(text) <= width) return size;
    }
    return NUMBER_SIZES[sizeof(NUMBER_SIZES) / sizeof(NUMBER_SIZES[0]) - 1];
}

void separator(g2d::Bitmap& b, double y) { b.fill_rect(LINE, std::floor(y) - 1, CARD_W - LINE * 2, 2, SHADE); }

void draw_row(g2d::Bitmap& b, double y, double icon_y, double bar_y, double value, double max, double rate) {
    g2d::BitmapPtr art = load_system("newtagmenud");
    b.blt(art.get(), ICON_SRC_X, icon_y, ICON_SRC_W, ICON_SRC_H, FACE_X, y + (ROW_H - ICON_H) / 2, ICON_W, ICON_H);
    b.fill_rect(SLOT_X, y, SLOT_W, ROW_H, INK);
    double inner = SLOT_W - BAR_INSET * 2;
    double fill = std::floor(inner * std::clamp(rate, 0.0, 1.0));
    if (fill > 0) b.blt(art.get(), 0, bar_y, BAR_SRC_W, BAR_SRC_H, SLOT_X + BAR_INSET, y + BAR_INSET, fill, ROW_H - BAR_INSET * 2);
    std::string text = menu::num_str(value) + "/" + menu::num_str(max);
    double room = SLOT_W - NUMBER_PAD * 2;
    b.font_size = number_size(b, text, room);
    b.text_color = "#ffffff";
    b.outline_color = "rgba(0, 0, 0, 1)";
    b.outline_width = 4;
    b.draw_text(text, SLOT_X + NUMBER_PAD, y - 2, room, ROW_H, "right");
}

void draw_card(Card& card, rpg::GameActor* a) {
    g2d::Bitmap& b = *card.body->bitmap();
    b.clear();
    b.fill_rect(0, 0, CARD_W, CARD_H, INK);
    b.fill_rect(LINE, LINE, CARD_W - LINE * 2, CARD_H - LINE * 2, FILL);
    b.fill_rect(FACE_X - LINE, FACE_Y - LINE, FACE + LINE * 2, FACE + LINE * 2, INK);
    bevel(b, SIDE_X, NAME_Y, SIDE_W, NAME_H);
    b.fill_rect(SIDE_X, HEADER_Y, SIDE_W, HEADER_H, INK);
    separator(b, (FACE_Y + FACE + LINE + HP_Y) / 2);
    separator(b, (HP_Y + ROW_H + MP_Y) / 2);
    b.font_face = yparam_str("DefaultFont");
    b.font_size = NAME_SIZE;
    b.text_color = INK;
    b.outline_width = 0;
    b.draw_text(a->name(), SIDE_X + 10, NAME_Y, SIDE_W - 20, NAME_H, "left");
    draw_row(b, HP_Y, HEART_SRC_Y, HP_BAR_SRC_Y, a->hp(), a->mhp(), a->hp_rate());
    draw_row(b, MP_Y, DROP_SRC_Y, MP_BAR_SRC_Y, a->mp(), a->mmp(), a->mp_rate());
}

void build() {
    g_panel.reset(new Panel());
    Panel& p = *g_panel;
    p.root = std::make_shared<g2d::Sprite>();
    for (int i = 0; i < SLOTS; i++) {
        Card& card = p.cards[i];
        double x = card_x(i), y = card_y(i);
        card.body = std::make_shared<g2d::Sprite>(g2d::Bitmap::create((int)CARD_W, (int)CARD_H));
        card.body->x = x;
        card.body->y = y;
        p.root->add_child(card.body);
        card.back = std::make_shared<g2d::Sprite>(load_system("faceset_states"));
        card.back->x = x + FACE_X;
        card.back->y = y + FACE_Y;
        p.root->add_child(card.back);
        card.face = std::make_shared<g2d::Sprite>();
        card.face->x = x + FACE_X - FACE_INSET;
        card.face->y = y + FACE_Y - FACE_INSET;
        card.face->mask_rect = std::make_shared<g2d::Rect>(FACE_INSET, FACE_INSET, FACE, FACE);
        p.root->add_child(card.face);
        card.header = std::make_shared<g2d::Sprite>(load_system("statelist"));
        card.header->x = x + SIDE_X + (SIDE_W - 134) / 2;
        card.header->y = y + HEADER_Y + (HEADER_H - 24) / 2;
        p.root->add_child(card.header);
    }
}

std::vector<double> card_key(rpg::GameActor* a) {
    if (!a) return {};
    return {(double)a->actor_id(), a->hp(), a->mhp(), a->mp(), a->mmp(), (double)std::hash<std::string>()(a->name())};
}

void sync(btl::SceneBattle& scene) {
    Panel& p = *g_panel;
    for (int i = 0; i < SLOTS; i++) {
        Card& card = p.cards[i];
        btl::WindowOmoriBattleActorStatus* status = i < (int)scene.face_windows.size() ? scene.face_windows[(size_t)i].get() : nullptr;
        rpg::GameActor* a = status ? status->actor() : nullptr;
        bool shown = a != nullptr;
        card.body->visible = card.back->visible = card.face->visible = card.header->visible = shown;
        if (!shown) {
            card.key.clear();
            continue;
        }
        std::vector<double> k = card_key(a);
        if (k != card.key) {
            card.key = k;
            draw_card(card, a);
        }
        int back = a->status_back_index();
        card.back->set_frame((back % 4) * FACE, std::floor(back / 4.0) * FACE, FACE, FACE);
        card.header->set_frame(0, a->status_list_index() * 24, 134, 24);
        g2d::Sprite* top = status->face_sprite.get();
        if (top && top->bitmap()) {
            if (card.face->bitmap() != top->bitmap()) card.face->set_bitmap(top->bitmap());
            const g2d::Rect& f = top->frame();
            card.face->set_frame(f.x, f.y, f.width, f.height);
        }
    }
    p.root->alpha = g_alpha / 255.0;
}

void push_sprite(std::vector<double>& sig, const g2d::Sprite* s) {
    const g2d::Rect& f = s->frame();
    sig.insert(sig.end(), {(double)(uintptr_t)s->bitmap().get(), f.x, f.y, f.width, f.height, (double)s->visible});
}

std::vector<double> signature() {
    Panel& p = *g_panel;
    std::vector<double> sig = {(double)g_alpha};
    for (const Card& card : p.cards) {
        sig.insert(sig.end(), card.key.begin(), card.key.end());
        sig.push_back((double)card.key.size());
        push_sprite(sig, card.back.get());
        push_sprite(sig, card.face.get());
        push_sprite(sig, card.header.get());
    }
    return sig;
}

}

bool update() {
    btl::SceneBattle* scene = battle_scene();
    if (!scene) {
        if (g_active) reset();
        return false;
    }
    if (!g_active) {
        build();
        g_active = true;
        g_alpha = 0;
        g_sig.clear();
        g_dirty = true;
    }
    if (g_alpha < 255) g_alpha = std::min(255, g_alpha + FADE_STEP);
    sync(*scene);
    std::vector<double> sig = signature();
    if (sig != g_sig) {
        g_sig.swap(sig);
        g_dirty = true;
    }
    return true;
}

bool changed() {
    bool d = g_dirty;
    g_dirty = false;
    return d;
}

void render(const g2d::RenderState& rs) {
    if (g_panel && g_active) g_panel->root->render(rs);
}

void reset() {
    g_panel.reset();
    g_active = false;
    g_sig.clear();
    g_dirty = true;
}

}
}
}
