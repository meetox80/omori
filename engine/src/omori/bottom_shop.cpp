#include "bottom_shop.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/image.h"
#include "../gfx2d/bitmap.h"
#include "../gfx2d/text.h"
#include "../platform/platform.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/party.h"
#include "menu_common.h"
#include "ui_util.h"
#include "window_base.h"
#include "window_selectable.h"

namespace ui {
namespace bottom {
namespace shop {

namespace {

const char ESC = '\x1b';

constexpr double WIN_X = 10;
constexpr double WIN_W = 620;
constexpr int PAD = 12;
constexpr double HEAD_Y = 10;
constexpr double HEAD_H = 60;
constexpr double LIST_Y = 76;
constexpr double LIST_H = 248;
constexpr double INFO_Y = 330;
constexpr double INFO_H = 140;
constexpr double ROW_H = 44;
constexpr double TEXT_X = 8;
constexpr double NAME_X = 58;
constexpr double CURSOR_X = 28;
constexpr double GAP = 24;
constexpr int HEAD_SIZE = 32;
constexpr int ROW_SIZES[] = {36, 32, 28};
constexpr int INFO_SIZE = 30;
constexpr double INFO_LINE = 36;
constexpr int COUNT_SIZE = 36;
constexpr double COUNT_LINE = 40;
constexpr double DRAG_SLOP = 10;
constexpr int FADE_STEP = 51;

class PanelWindow : public WindowBase {
public:
    int standard_padding() override { return PAD; }
};

struct Panel {
    std::shared_ptr<g2d::Sprite> root;
    std::shared_ptr<PanelWindow> head, list, info;
    std::shared_ptr<SpriteWindowCustomCursor> cursor;
};

struct State {
    std::unique_ptr<Panel> panel;
    menu::ShopView view;
    bool active = false;
    int alpha = 0;
    int top = 0;
    int followed = -2;
    std::vector<double> head_key, list_key, info_key;
    bool down = false;
    bool swallow = false;
    bool dragged = false;
    int press_row = -1;
    double press_x = 0, press_y = 0, last_x = 0, last_y = 0, drag = 0;
    std::vector<double> sig;
    bool dirty = true;
};

State g;

int rows() { return (int)std::floor((LIST_H - PAD * 2) / ROW_H); }

int item_count() { return (int)g.view.items.size(); }

std::string money(double v) { return menu::js_fmt("%1 %2", {group_digits(v), g.view.unit}); }

size_t hash_of(const std::string& s) { return std::hash<std::string>()(s); }

size_t skip_escape(const std::string& s, size_t i) {
    i++;
    if (i >= s.size()) return i;
    if (s[i] && std::strchr("$.|^!><{}\\", s[i])) return i + 1;
    while (i < s.size() && std::isalpha((unsigned char)s[i])) i++;
    if (i < s.size() && (s[i] == '[' || s[i] == '<')) {
        size_t close = s.find(s[i] == '[' ? ']' : '>', i);
        if (close != std::string::npos) return close + 1;
    }
    return i;
}

std::string plain(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == ESC) {
            i = skip_escape(s, i);
            continue;
        }
        out += s[i++];
    }
    return out;
}

std::string trim_right(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::vector<std::string> paragraphs(const std::string& text) {
    std::vector<std::string> out(1);
    for (char ch : text) {
        if (ch == '\n') out.emplace_back();
        else if (ch != '\r') out.back() += ch;
    }
    return out;
}

std::vector<std::string> wrap(WindowBase& w, const std::string& text, double width) {
    g2d::Bitmap& b = w.c();
    std::vector<std::string> lines;
    for (const std::string& p : paragraphs(text)) {
        std::string line;
        for (size_t i = 0; i < p.size();) {
            size_t j = i;
            uint32_t cp = g2d::decode_utf8(p, j);
            if (cp != ' ' && cp < 0x2E80) {
                while (j < p.size()) {
                    size_t k = j;
                    uint32_t next = g2d::decode_utf8(p, k);
                    if (next == ' ' || next >= 0x2E80) break;
                    j = k;
                }
            }
            std::string token = p.substr(i, j - i);
            i = j;
            if (line.empty() && token == " ") continue;
            if (line.empty() || w.can_line_break(0, token) || b.measure_text_width(trim_right(line + token)) <= width) {
                line += token;
            } else {
                lines.push_back(trim_right(line));
                line = token == " " ? std::string() : token;
            }
        }
        lines.push_back(trim_right(line));
    }
    return lines;
}

void build() {
    g.panel.reset(new Panel());
    Panel& p = *g.panel;
    p.root = std::make_shared<g2d::Sprite>();
    p.head = WindowBase::make<PanelWindow>(WIN_X, HEAD_Y, WIN_W, HEAD_H);
    p.root->add_child(p.head);
    p.list = WindowBase::make<PanelWindow>(WIN_X, LIST_Y, WIN_W, LIST_H);
    p.root->add_child(p.list);
    p.info = WindowBase::make<PanelWindow>(WIN_X, INFO_Y, WIN_W, INFO_H);
    p.root->add_child(p.info);
    p.cursor = std::make_shared<SpriteWindowCustomCursor>(-1, "cursor_menu");
    p.cursor->visible = false;
    p.root->add_child(p.cursor);
    g.head_key.clear();
    g.list_key.clear();
    g.info_key.clear();
    g.top = 0;
    g.followed = -2;
}

void draw_head(PanelWindow& w) {
    g2d::Bitmap& b = w.c();
    b.clear();
    w.reset_font_settings();
    b.font_size = HEAD_SIZE;
    double cw = w.contents_width(), ch = w.contents_height();
    std::string gold = money(g.view.gold);
    double gw = b.measure_text_width(gold);
    b.draw_text(gold, 0, 0, cw - TEXT_X, ch, "right");
    b.draw_text(g.view.name, TEXT_X, 0, cw - TEXT_X * 2 - gw - GAP, ch, "left");
}

void draw_list(PanelWindow& w) {
    g2d::Bitmap& b = w.c();
    b.clear();
    w.reset_font_settings();
    double cw = w.contents_width();
    int last = std::min(item_count(), g.top + rows());
    for (int i = g.top; i < last; i++) {
        std::string name = menu::item_name(g.view.items[(size_t)i]);
        std::string price = money(g.view.prices[(size_t)i]);
        double room = cw - NAME_X - TEXT_X;
        int size = ROW_SIZES[sizeof(ROW_SIZES) / sizeof(ROW_SIZES[0]) - 1];
        for (int s : ROW_SIZES) {
            b.font_size = s;
            if (b.measure_text_width(name) + b.measure_text_width(price) + GAP <= room) {
                size = s;
                break;
            }
        }
        b.font_size = size;
        double y = (i - g.top) * ROW_H;
        double pw = b.measure_text_width(price);
        b.draw_text(price, 0, y, cw - TEXT_X, ROW_H, "right");
        b.draw_text(name, NAME_X, y, room - pw - GAP, ROW_H, "left");
    }
}

void draw_info(PanelWindow& w) {
    g2d::Bitmap& b = w.c();
    b.clear();
    w.reset_font_settings();
    int index = g.view.index;
    if (index < 0 || index >= item_count()) return;
    const rpg::ItemRef& it = g.view.items[(size_t)index];
    double cw = w.contents_width(), ch = w.contents_height();
    std::string owned = menu::js_fmt("(%1: %2)", {data::js_string(menu::plugin_data("itemShopMenu", "ownedText")), menu::num_str(rpg::game_party().num_items(it))});
    b.font_size = INFO_SIZE;
    double ow = b.measure_text_width(owned);
    b.draw_text(owned, 0, 0, cw - TEXT_X, INFO_LINE, "right");
    b.draw_text(menu::item_name(it), TEXT_X, 0, cw - TEXT_X * 2 - ow - GAP, INFO_LINE, "left");
    if (g.view.counting) {
        const Value& loc = menu::blue("Omori_Item_Shop");
        b.font_size = COUNT_SIZE;
        double y = INFO_LINE;
        b.draw_text(data::js_string(loc["amount"]), TEXT_X, y, cw - TEXT_X * 2, COUNT_LINE, "left");
        b.draw_text(menu::num_str(g.view.amount), 0, y, cw - TEXT_X, COUNT_LINE, "right");
        y += COUNT_LINE;
        b.draw_text(data::js_string(loc["total_price"]), TEXT_X, y, cw - TEXT_X * 2, COUNT_LINE, "left");
        b.draw_text(money(g.view.total), 0, y, cw - TEXT_X, COUNT_LINE, "right");
        return;
    }
    std::string text = plain(w.convert_escape_characters(w.set_word_wrap(menu::item_description(it))));
    std::vector<std::string> lines = wrap(w, text, cw - TEXT_X * 2);
    double y = INFO_LINE;
    for (const std::string& line : lines) {
        if (y + INFO_LINE > ch + 1) break;
        b.draw_text(line, TEXT_X, y, cw - TEXT_X * 2, INFO_LINE, "left");
        y += INFO_LINE;
    }
}

void scroll(int d) { g.top = std::clamp(g.top + d, 0, std::max(0, item_count() - rows())); }

void follow() {
    if (g.view.index == g.followed) {
        scroll(0);
        return;
    }
    g.followed = g.view.index;
    if (g.view.index < 0) {
        scroll(0);
        return;
    }
    if (g.view.index < g.top) g.top = g.view.index;
    if (g.view.index >= g.top + rows()) g.top = g.view.index - rows() + 1;
    scroll(0);
}

bool in_list(double lx, double ly) {
    const PanelWindow& w = *g.panel->list;
    return lx >= w.x && lx < w.x + w.width() && ly >= w.y && ly < w.y + w.height();
}

int row_at(double lx, double ly) {
    PanelWindow& w = *g.panel->list;
    double px = lx - w.x - PAD, py = ly - w.y - PAD;
    if (px < 0 || px >= w.contents_width() || py < 0 || py >= rows() * ROW_H) return -1;
    int i = g.top + (int)(py / ROW_H);
    return i < item_count() ? i : -1;
}

void handle_touch() {
    int tx = 0, ty = 0;
    bool now = plat::touch(tx, ty);
    double lx = tx / images::SCALE, ly = ty / images::SCALE;
    if (g.swallow) {
        if (!now) g.swallow = false;
        g.down = now;
        return;
    }
    if (now && !g.down) {
        g.press_row = in_list(lx, ly) ? row_at(lx, ly) : -1;
        g.dragged = false;
        g.drag = 0;
        g.press_x = g.last_x = lx;
        g.press_y = g.last_y = ly;
    } else if (now && g.down) {
        if (in_list(g.press_x, g.press_y) && std::fabs(ly - g.press_y) > DRAG_SLOP) g.dragged = true;
        if (g.dragged) {
            g.drag += g.last_y - ly;
            while (g.drag >= ROW_H) {
                scroll(1);
                g.drag -= ROW_H;
            }
            while (g.drag <= -ROW_H) {
                scroll(-1);
                g.drag += ROW_H;
            }
        }
        g.last_x = lx;
        g.last_y = ly;
    } else if (!now && g.down) {
        if (!g.dragged && g.press_row >= 0 && row_at(g.last_x, g.last_y) == g.press_row) menu::shop_tap(g.press_row);
        g.press_row = -1;
    }
    g.down = now;
}

std::vector<double> head_key() { return {(double)hash_of(g.view.name), g.view.gold, (double)hash_of(g.view.unit)}; }

std::vector<double> list_key() {
    std::vector<double> k = {(double)g.top, (double)item_count(), (double)hash_of(g.view.unit)};
    for (size_t i = 0; i < g.view.items.size(); i++) k.insert(k.end(), {(double)g.view.items[i].kind, (double)g.view.items[i].id, g.view.prices[i]});
    return k;
}

std::vector<double> info_key() {
    std::vector<double> k = {(double)g.view.index, (double)g.view.counting};
    int index = g.view.index;
    if (index >= 0 && index < item_count()) {
        const rpg::ItemRef& it = g.view.items[(size_t)index];
        k.insert(k.end(), {(double)it.kind, (double)it.id, rpg::game_party().num_items(it)});
    }
    if (g.view.counting) k.insert(k.end(), {g.view.amount, g.view.total, (double)hash_of(g.view.unit)});
    return k;
}

void sync() {
    Panel& p = *g.panel;
    follow();
    std::vector<double> hk = head_key(), lk = list_key(), ik = info_key();
    if (hk != g.head_key) {
        g.head_key.swap(hk);
        draw_head(*p.head);
    }
    if (lk != g.list_key) {
        g.list_key.swap(lk);
        draw_list(*p.list);
    }
    if (ik != g.info_key) {
        g.info_key.swap(ik);
        draw_info(*p.info);
    }
    p.list->up_arrow_visible = g.top > 0;
    p.list->down_arrow_visible = g.top + rows() < item_count();
    int row = g.view.index - g.top;
    p.cursor->visible = g.view.index >= 0 && row >= 0 && row < rows();
    p.cursor->x = p.list->x + PAD + CURSOR_X;
    p.cursor->y = p.list->y + PAD + row * ROW_H + ROW_H / 2;
    p.root->alpha = g.alpha / 255.0;
}

std::vector<double> signature() {
    Panel& p = *g.panel;
    g2d::Sprite& cur = *p.cursor;
    std::vector<double> sig = {(double)g.alpha, (double)cur.visible, cur.x, cur.y, cur.anchor_x, cur.anchor_y,
                               (double)p.list->up_arrow_visible, (double)p.list->down_arrow_visible};
    sig.insert(sig.end(), g.head_key.begin(), g.head_key.end());
    sig.insert(sig.end(), g.list_key.begin(), g.list_key.end());
    sig.insert(sig.end(), g.info_key.begin(), g.info_key.end());
    return sig;
}

}

bool counting() {
    menu::ShopView v;
    return menu::shop_view(v) && v.counting;
}

bool update() {
    if (!menu::shop_view(g.view)) {
        if (g.active) reset();
        return false;
    }
    if (!g.active) {
        build();
        g.active = true;
        g.alpha = 0;
        hold_touch();
        g.sig.clear();
        g.dirty = true;
    }
    if (g.alpha < 255) g.alpha = std::min(255, g.alpha + FADE_STEP);
    handle_touch();
    g.panel->root->update();
    sync();
    std::vector<double> sig = signature();
    if (sig != g.sig) {
        g.sig.swap(sig);
        g.dirty = true;
    }
    return true;
}

bool changed() {
    bool d = g.dirty;
    g.dirty = false;
    return d;
}

void render(const g2d::RenderState& rs) {
    if (g.panel && g.active) g.panel->root->render(rs);
}

void hold_touch() {
    int tx = 0, ty = 0;
    g.swallow = plat::touch(tx, ty);
    g.down = g.swallow;
    g.dragged = false;
    g.press_row = -1;
}

void reset() {
    g.panel.reset();
    g.active = false;
    g.view = menu::ShopView();
    g.sig.clear();
    g.dirty = true;
    g.down = g.swallow = g.dragged = false;
    g.press_row = -1;
}

}
}
}
