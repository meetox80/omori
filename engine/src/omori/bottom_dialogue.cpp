#include "bottom_dialogue.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>

#include "../core/image.h"
#include "../gfx2d/bitmap.h"
#include "../gfx2d/text.h"
#include "../platform/platform.h"
#include "../rpg/game.h"
#include "../rpg/map.h"
#include "../rpg/party.h"
#include "../rpg/scene.h"
#include "../rpg/state.h"
#include "../rpg/system.h"
#include "message.h"
#include "ui_util.h"
#include "window_base.h"
#include "window_selectable.h"

namespace ui {
namespace dialogue {

namespace {

const char ESC = '\x1b';

constexpr double WIN_X = 10;
constexpr double WIN_Y = 10;
constexpr double WIN_W = 620;
constexpr double WIN_H = 460;
constexpr double MARGIN = 20;
constexpr double TEXT_X = WIN_X + MARGIN;
constexpr double TEXT_Y = WIN_Y + MARGIN;
constexpr double TEXT_W = WIN_W - MARGIN * 2;
constexpr double TEXT_H = WIN_H - MARGIN * 2;
constexpr int TEXT_SIZE = 48;
constexpr int NAME_SIZE = 40;
constexpr double NAME_BOX_H = 50;
constexpr double NAME_PAD = 14;
constexpr double NAME_H = NAME_BOX_H + 6;
constexpr double BORDER = 2;
constexpr double ENTRY_GAP = 20;
constexpr double PAGE_GAP = 12;
constexpr double CHOICE_GAP = 12;
constexpr double CHOICE_INDENT = 60;
constexpr double DRAG_SLOP = 8;
constexpr double DIM = 0.55;
constexpr size_t MAX_LINES = 160;
constexpr int FADE_STEP = 51;

class PanelWindow : public WindowBase {
public:
    int standard_padding() override { return 12; }
    double contents_width() override { return 0; }
    double contents_height() override { return 0; }
    void refresh_pause_sign() override {
        pause_sign_->set_bitmap(load_system("cursor_menu"));
        pause_sign_->anchor_x = 0.5;
        pause_sign_->anchor_y = 1;
        pause_sign_->alpha = 0;
        pause_sign_->move(width_ - 40, height_ - 4);
    }
    void update_pause_sign() override {
        if (!pause) pause_sign_->alpha = 0;
        else if (pause_sign_->alpha < 1) pause_sign_->alpha = std::min(pause_sign_->alpha + 0.1, 1.0);
        pause_sign_->visible = is_open();
        pause_sign_->anchor_x = 0.5 + std::sin(animation_count * 0.1) * 0.1;
    }
};

enum class Kind : uint8_t { Text, Name, Choice };

struct Run {
    double x = 0, end = 0;
    int size = 0;
    std::string color;
    std::string text;
    int icon = -1;
    std::string key;
    double scale = 1;
};

struct Line {
    Kind kind = Kind::Text;
    int entry = 0;
    int choice = -1;
    double y = 0, h = 0, left = 0, pen = 0;
    bool enabled = true;
    int version = 0;
    std::vector<Run> runs;
};

struct Shown {
    long id = 0;
    g2d::SpritePtr sprite;
    int version = -1;
};

struct Panel {
    std::shared_ptr<g2d::Sprite> root;
    std::shared_ptr<PanelWindow> window;
    std::shared_ptr<g2d::Sprite> clip;
    std::shared_ptr<SpriteWindowCustomCursor> cursor;
    std::vector<Shown> shown;
};

struct State {
    WindowMessage* source = nullptr;
    rpg::Scene* scene = nullptr;
    std::deque<Line> lines;
    long first_id = 0;
    int entry = 0;
    Kind kind = Kind::Text;
    double left = 0;
    int choice = -1;
    bool pending_space = false;
    bool in_word = false;
    bool force_break = false;
    double gap = 0;
    std::string last_char;
    long choice_first = -1;
    int selected = -1;
    double scroll = 0, velocity = 0;
    bool follow = true;
    bool touching = false;
    bool dragging = false;
    bool swallow = false;
    double touch_start = 0, touch_last = 0;
    bool active = false;
    int alpha = 0;
    int version = 0;
    std::vector<double> sig;
    bool dirty = true;
    std::unique_ptr<Panel> panel;
};

State g;

std::string face() { return rpg::game_system().msg_font_name; }

int base_size() { return std::max(1, rpg::game_system().msg_font_size); }

double icon_scale() { return (double)TEXT_SIZE / base_size(); }

double width_of(int size, const std::string& s) { return g2d::measure_text(face(), size, s); }

double line_h(int size) { return size + 8; }

double snap_up(double v) { return std::ceil(v * g2d::S - 0.001) / g2d::S; }

double snap(double v) { return std::round(v * g2d::S) / g2d::S; }

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

double word_width(const std::string& src, size_t at, int size) {
    std::string w;
    size_t i = at;
    while (i < src.size()) {
        char c = src[i];
        if (c == ' ' || c == '\n' || c == '\f') break;
        if (c == ESC) {
            i = skip_escape(src, i);
            continue;
        }
        size_t j = i;
        g2d::decode_utf8(src, j);
        w.append(src, i, j - i);
        i = j;
    }
    return width_of(size, w);
}

bool ends_sentence(const std::string& c) {
    static const char* const marks[] = {".", "!", "?", ":", "~", ")", "\"", "'", "\xE2\x80\xA6", "\xE2\x80\x9D", "\xE2\x80\x99",
                                        "\xE3\x80\x82", "\xEF\xBC\x81", "\xEF\xBC\x9F", "\xE3\x80\x8D", "\xE3\x80\x8F"};
    for (const char* m : marks) {
        if (c == m) return true;
    }
    return false;
}

double next_y() { return g.lines.empty() ? 0 : g.lines.back().y + g.lines.back().h; }

long last_id() { return g.first_id + (long)g.lines.size() - 1; }

void trim() {
    while (g.lines.size() > MAX_LINES) {
        int oldest = g.lines.front().entry;
        if (oldest == g.entry) return;
        while (g.lines.size() > 1 && g.lines.front().entry == oldest) {
            g.lines.pop_front();
            g.first_id++;
        }
        double dy = g.lines.front().y;
        for (Line& l : g.lines) l.y -= dy;
        g.scroll = std::max(0.0, g.scroll - dy);
    }
}

Line& push_line(Kind kind, double left, double h) {
    Line l;
    l.kind = kind;
    l.entry = g.entry;
    l.choice = g.choice;
    l.left = left;
    l.pen = left;
    l.h = snap_up(h);
    l.y = next_y() + g.gap;
    g.gap = 0;
    g.lines.push_back(std::move(l));
    g.version++;
    trim();
    return g.lines.back();
}

bool line_has_content() {
    if (g.lines.empty()) return false;
    const Line& b = g.lines.back();
    return b.kind == g.kind && b.entry == g.entry && b.choice == g.choice && !b.runs.empty();
}

Line& content_line() {
    if (!g.lines.empty()) {
        Line& b = g.lines.back();
        bool same = b.kind == g.kind && b.entry == g.entry && b.choice == g.choice;
        if (same && !(g.force_break && !b.runs.empty())) {
            g.force_break = false;
            g.gap = 0;
            return b;
        }
    }
    g.force_break = false;
    g.pending_space = false;
    return push_line(g.kind, g.left, line_h(TEXT_SIZE));
}

Line& wrap_line() {
    g.gap = 0;
    g.pending_space = false;
    return push_line(g.kind, g.left, line_h(TEXT_SIZE));
}

void grow(Line& l, double h) {
    l.h = std::max(l.h, snap_up(h));
    l.version++;
    g.version++;
}

void append_text(Line& l, const std::string& s, const std::string& color, int size, double w) {
    Run* last = l.runs.empty() ? nullptr : &l.runs.back();
    if (last && last->icon < 0 && last->key.empty() && last->size == size && last->color == color && std::fabs(last->end - l.pen) < 0.001) {
        last->text += s;
        last->end += w;
    } else {
        Run r;
        r.x = l.pen;
        r.end = l.pen + w;
        r.size = size;
        r.color = color;
        r.text = s;
        l.runs.push_back(std::move(r));
    }
    l.pen += w;
    grow(l, line_h(size));
}

void space_char() {
    if (line_has_content()) g.pending_space = true;
    g.in_word = false;
}

void place(const std::string& src, size_t at, const std::string& ch, const std::string& color, int size) {
    Line* l = &content_line();
    double cw = width_of(size, ch);
    if (!g.in_word) {
        double ww = std::max(cw, word_width(src, at, size));
        double sw = g.pending_space && !l->runs.empty() ? width_of(size, " ") : 0;
        if (!l->runs.empty() && l->pen + sw + ww > TEXT_W) {
            l = &wrap_line();
            sw = 0;
        }
        if (sw > 0) append_text(*l, " ", color, size, sw);
        g.in_word = true;
    } else if (l->pen + cw > TEXT_W && l->pen > l->left) {
        l = &wrap_line();
    }
    g.pending_space = false;
    append_text(*l, ch, color, size, cw);
    g.last_char = ch;
}

Line& place_box(double w) {
    Line* l = &content_line();
    double sw = g.pending_space && !l->runs.empty() ? width_of(TEXT_SIZE, " ") : 0;
    if (!l->runs.empty() && l->pen + sw + w > TEXT_W) {
        l = &wrap_line();
        sw = 0;
    }
    l->pen += sw;
    g.pending_space = false;
    g.in_word = false;
    return *l;
}

void clear_shown() {
    if (!g.panel) return;
    for (Shown& s : g.panel->shown) g.panel->clip->remove_child(s.sprite.get());
    g.panel->shown.clear();
}

void reset_log() {
    clear_shown();
    g.first_id += (long)g.lines.size();
    g.lines.clear();
    g.kind = Kind::Text;
    g.left = 0;
    g.choice = -1;
    g.pending_space = g.in_word = g.force_break = false;
    g.gap = 0;
    g.last_char.clear();
    g.choice_first = -1;
    g.selected = -1;
    g.scroll = 0;
    g.velocity = 0;
    g.follow = true;
    g.version++;
}

void end_choices() {
    if (g.choice_first < 0) return;
    while (!g.lines.empty() && last_id() >= g.choice_first) g.lines.pop_back();
    g.choice_first = -1;
    g.selected = -1;
    g.kind = Kind::Text;
    g.left = 0;
    g.choice = -1;
    g.pending_space = g.in_word = g.force_break = false;
    g.gap = 0;
    g.version++;
}

void adopt(WindowMessage* source) {
    rpg::Scene* scene = rpg::scene_manager::scene();
    if (source != g.source || scene != g.scene) reset_log();
    g.source = source;
    g.scene = scene;
}

void ensure_panel() {
    if (g.panel) return;
    g.panel.reset(new Panel());
    Panel& p = *g.panel;
    p.root = std::make_shared<g2d::Sprite>();
    p.window = WindowBase::make<PanelWindow>(WIN_X, WIN_Y, WIN_W, WIN_H);
    p.root->add_child(p.window);
    p.clip = std::make_shared<g2d::Sprite>();
    p.clip->x = TEXT_X;
    p.clip->y = TEXT_Y;
    p.clip->mask_rect = std::make_shared<g2d::Rect>(0, 0, TEXT_W, TEXT_H);
    p.root->add_child(p.clip);
    p.cursor = std::make_shared<SpriteWindowCustomCursor>(-1, "cursor_menu");
    p.cursor->visible = false;
    p.clip->add_child(p.cursor);
}

std::string normal_color() { return g.panel ? g.panel->window->normal_color() : std::string("#ffffff"); }

void feed_markup(const std::string& s) {
    std::string color = normal_color();
    int size = TEXT_SIZE;
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == ESC) {
            size_t j = i + 1;
            std::string code;
            if (j < s.size() && !std::isalpha((unsigned char)s[j])) code = s[j];
            while (j < s.size() && std::isalpha((unsigned char)s[j])) code += (char)std::toupper((unsigned char)s[j++]);
            int param = 0;
            if (j < s.size() && s[j] == '[') param = std::atoi(s.c_str() + j + 1);
            if (code == "C" && g.panel) color = g.panel->window->text_color(param);
            else if (code == "I") icon(param);
            else if (code == "{") size = g2d::font_atlas_size(size + 12);
            else if (code == "}") size = g2d::font_atlas_size(size - 12);
            i = skip_escape(s, i);
            continue;
        }
        if (c == '\n') {
            line_break();
            i++;
            continue;
        }
        size_t j = i;
        g2d::decode_utf8(s, j);
        if (j - i == 1 && c == ' ') space_char();
        else place(s, i, s.substr(i, j - i), color, size);
        i = j;
    }
}

bool wanted() {
    if (!g.source || g.lines.empty() || !rpg::game_objects_exist()) return false;
    if (rpg::scene_manager::scene() != g.scene) return false;
    g2d::Window* w = message_window_as_window(g.source);
    if (!w->is_closed() || message_window_is_any_sub_window_active(g.source)) return true;
    if (rpg::game_message().is_busy()) return true;
    if (rpg::party_in_battle()) return rpg::game_troop().is_event_running();
    return rpg::game_map().is_event_running();
}

double max_scroll() { return std::max(0.0, next_y() - TEXT_H); }

const Line* choice_line(int choice) {
    for (const Line& l : g.lines) {
        if (l.kind == Kind::Choice && l.choice == choice) return &l;
    }
    return nullptr;
}

double follow_target() {
    double t = max_scroll();
    if (g.choice_first >= 0 && g.selected >= 0) {
        const Line* first = choice_line(g.selected);
        if (first && first->y < t) t = std::max(0.0, first->y - 4);
    }
    return t;
}

void track_choices() {
    if (g.choice_first < 0) return;
    int index = message_window_choice_index(g.source);
    if (index < 0) {
        end_choices();
        return;
    }
    if (index != g.selected) {
        g.selected = index;
        g.follow = true;
        g.version++;
    }
}

void handle_touch() {
    int tx = 0, ty = 0;
    bool now = plat::touch(tx, ty);
    double ly = ty / images::SCALE;
    if (g.swallow) {
        if (!now) g.swallow = false;
        return;
    }
    if (now && !g.touching) {
        g.touching = true;
        g.dragging = false;
        g.touch_start = g.touch_last = ly;
        g.velocity = 0;
    } else if (now && g.touching) {
        if (!g.dragging && std::fabs(ly - g.touch_start) > DRAG_SLOP) {
            g.dragging = true;
            g.follow = false;
            g.touch_last = g.touch_start;
        }
        if (g.dragging) {
            double dy = ly - g.touch_last;
            g.scroll -= dy;
            g.velocity = g.velocity * 0.5 - dy * 0.5;
        }
        g.touch_last = ly;
    } else if (!now && g.touching) {
        g.touching = false;
        if (!g.dragging) g.velocity = 0;
        g.dragging = false;
    }
}

void move_scroll() {
    double top = max_scroll();
    if (!g.touching) {
        if (std::fabs(g.velocity) > 0.25) {
            g.scroll += g.velocity;
            g.velocity *= 0.92;
        } else {
            g.velocity = 0;
        }
    }
    if (g.scroll <= 0) {
        g.scroll = 0;
        if (g.velocity < 0) g.velocity = 0;
    }
    if (g.scroll >= top) {
        g.scroll = top;
        if (g.velocity > 0) g.velocity = 0;
    }
    if (!g.touching && g.velocity == 0 && g.scroll >= top - 1) g.follow = true;
    if (g.follow && !g.touching) {
        double target = follow_target();
        double d = target - g.scroll;
        g.scroll = std::fabs(d) < 1 ? target : g.scroll + d * 0.3;
    }
}

void draw_line(g2d::Bitmap& b, const Line& l) {
    b.clear();
    b.font_face = face();
    b.outline_width = 0;
    b.paint_opacity = l.enabled ? 255 : 160;
    if (l.kind == Kind::Name) {
        if (l.runs.empty()) return;
        const Run& r = l.runs.front();
        double w = std::min(TEXT_W, snap_up(r.end - r.x + NAME_PAD * 2));
        b.fill_rect(0, 0, w, BORDER, "#ffffff");
        b.fill_rect(0, NAME_BOX_H - BORDER, w, BORDER, "#ffffff");
        b.fill_rect(0, 0, BORDER, NAME_BOX_H, "#ffffff");
        b.fill_rect(w - BORDER, 0, BORDER, NAME_BOX_H, "#ffffff");
        b.font_size = r.size;
        b.text_color = normal_color();
        b.draw_text(r.text, NAME_PAD, 0, w - NAME_PAD * 2, NAME_BOX_H, "center");
        return;
    }
    int biggest = 0;
    for (const Run& r : l.runs) {
        if (r.icon < 0 && r.key.empty()) biggest = std::max(biggest, r.size);
    }
    if (biggest == 0) biggest = TEXT_SIZE;
    double baseline = l.h / 2 + biggest * 0.35;
    for (const Run& r : l.runs) {
        if (r.icon >= 0) {
            g2d::BitmapPtr set = load_system("IconSet");
            double side = WindowBase::kIconWidth * r.scale;
            b.blt(set.get(), (r.icon % 16) * WindowBase::kIconWidth, std::floor(r.icon / 16.0) * WindowBase::kIconHeight, WindowBase::kIconWidth,
                  WindowBase::kIconHeight, r.x, snap((l.h - side) / 2), side, side);
        } else if (!r.key.empty()) {
            IconRect up, down;
            key_icon_rects(r.key, up, down);
            g2d::BitmapPtr src = input_icon_bitmap();
            double w = up.w * r.scale, h = up.h * r.scale;
            b.blt(src.get(), up.x, up.y, up.w, up.h, r.x, snap((l.h - h) / 2), w, h);
        } else {
            b.font_size = r.size;
            b.text_color = r.color;
            double y = baseline - r.size * 0.85 - 4;
            b.draw_text(r.text, r.x, y, TEXT_W * 4, r.size + 8, "left");
        }
    }
}

void sync() {
    Panel& p = *g.panel;
    double top = snap(g.scroll);
    std::vector<Shown> keep;
    keep.reserve(16);
    for (size_t i = 0; i < g.lines.size(); i++) {
        const Line& l = g.lines[i];
        if (l.y + l.h <= top || l.y >= top + TEXT_H) continue;
        long id = g.first_id + (long)i;
        Shown s;
        auto it = std::find_if(p.shown.begin(), p.shown.end(), [id](const Shown& x) { return x.id == id; });
        if (it != p.shown.end()) {
            s = *it;
            p.shown.erase(it);
        } else {
            s.id = id;
            s.sprite = std::make_shared<g2d::Sprite>();
            p.clip->add_child_at(s.sprite, 0);
        }
        if (s.version != l.version) {
            g2d::BitmapPtr bmp = s.sprite->bitmap();
            int h = (int)std::ceil(l.h);
            if (!bmp || bmp->height() != h) {
                bmp = g2d::Bitmap::create((int)TEXT_W, h);
                s.sprite->set_bitmap(bmp);
            }
            draw_line(*bmp, l);
            s.version = l.version;
        }
        s.sprite->x = 0;
        s.sprite->y = l.y - top;
        s.sprite->alpha = (l.entry == g.entry || l.kind == Kind::Choice) ? 1.0 : DIM;
        keep.push_back(s);
    }
    for (Shown& s : p.shown) p.clip->remove_child(s.sprite.get());
    p.shown.swap(keep);

    const Line* sel = g.choice_first >= 0 && g.selected >= 0 ? choice_line(g.selected) : nullptr;
    p.cursor->visible = sel != nullptr;
    if (sel) {
        p.cursor->x = CHOICE_INDENT - 30;
        p.cursor->y = sel->y - top + sel->h / 2;
    }
    double limit = max_scroll();
    p.window->up_arrow_visible = top > 0.5;
    p.window->down_arrow_visible = top < limit - 0.5;
    g2d::Window* src = message_window_as_window(g.source);
    p.window->pause = src->pause && g.choice_first < 0;
    p.root->alpha = g.alpha / 255.0;
}

std::vector<double> signature() {
    Panel& p = *g.panel;
    g2d::Sprite& cur = *p.cursor;
    g2d::Sprite& sign = *p.window->pause_sign_sprite();
    return {snap(g.scroll), (double)g.version, (double)g.alpha, (double)cur.visible, cur.x, cur.y, cur.anchor_x, (double)sign.visible, sign.alpha,
            sign.anchor_x, (double)p.window->up_arrow_visible, (double)p.window->down_arrow_visible, (double)g.entry};
}

}

void begin(WindowMessage* source, const std::string& speaker) {
    if (!enabled()) return;
    adopt(source);
    end_choices();
    ensure_panel();
    g.entry++;
    g.kind = Kind::Text;
    g.left = 0;
    g.choice = -1;
    g.pending_space = g.in_word = g.force_break = false;
    g.last_char.clear();
    g.gap = g.lines.empty() ? 0 : ENTRY_GAP;
    std::string name = plain(speaker);
    if (!name.empty()) {
        Line& l = push_line(Kind::Name, 0, NAME_H);
        Run r;
        r.size = NAME_SIZE;
        r.text = name;
        r.end = width_of(NAME_SIZE, name);
        l.runs.push_back(std::move(r));
        l.version++;
    }
    g.follow = true;
    g.version++;
}

void page() {
    if (!enabled() || !g.source) return;
    g.in_word = false;
    if (!line_has_content()) return;
    g.force_break = true;
    g.pending_space = false;
    g.gap = std::max(g.gap, PAGE_GAP);
}

void glyph(const std::string& src, size_t at, size_t end, const std::string& color, double ratio) {
    if (!enabled() || !g.source || end <= at) return;
    if (end - at == 1 && src[at] == ' ') {
        space_char();
        return;
    }
    place(src, at, src.substr(at, end - at), color, g2d::font_atlas_size((int)std::lround(TEXT_SIZE * ratio)));
}

void soft_space() {
    if (!enabled() || !g.source) return;
    space_char();
}

void line_break() {
    if (!enabled() || !g.source) return;
    g.in_word = false;
    if (!line_has_content()) return;
    if (g.force_break) {
        g.gap += line_h(TEXT_SIZE) / 2;
        return;
    }
    if (ends_sentence(g.last_char)) {
        g.force_break = true;
        g.pending_space = false;
    } else {
        g.pending_space = true;
    }
}

void icon(int index) {
    if (!enabled() || !g.source) return;
    double k = icon_scale();
    Line& l = place_box((WindowBase::kIconWidth + 4) * k);
    Run r;
    r.icon = index;
    r.scale = k;
    r.x = l.pen + 2 * k;
    l.pen += (WindowBase::kIconWidth + 4) * k;
    r.end = l.pen;
    l.runs.push_back(std::move(r));
    grow(l, WindowBase::kIconHeight * k + 2);
}

void key_icon(const std::string& key) {
    if (!enabled() || !g.source) return;
    double k = icon_scale();
    IconRect up, down;
    key_icon_rects(key, up, down);
    Line& l = place_box((up.w + 8) * k);
    Run r;
    r.key = key;
    r.scale = k;
    r.x = l.pen + 4 * k;
    l.pen += (up.w + 8) * k;
    r.end = l.pen;
    l.runs.push_back(std::move(r));
    grow(l, up.h * k + 2);
}

void choices(WindowMessage* source, const std::vector<std::string>& names, const std::vector<bool>& enabled_flags) {
    if (!enabled()) return;
    adopt(source);
    end_choices();
    ensure_panel();
    if (g.lines.empty()) g.entry++;
    g.choice_first = last_id() + 1;
    g.gap = g.lines.empty() ? 0 : CHOICE_GAP;
    for (size_t i = 0; i < names.size(); i++) {
        g.kind = Kind::Choice;
        g.left = CHOICE_INDENT;
        g.choice = (int)i;
        g.pending_space = g.in_word = g.force_break = false;
        g.last_char.clear();
        feed_markup(names[i]);
        content_line();
        bool on = i >= enabled_flags.size() || enabled_flags[i];
        for (Line& l : g.lines) {
            if (l.kind == Kind::Choice && l.choice == (int)i && l.enabled != on) {
                l.enabled = on;
                l.version++;
            }
        }
    }
    g.kind = Kind::Text;
    g.left = 0;
    g.choice = -1;
    g.pending_space = g.in_word = g.force_break = false;
    g.selected = -1;
    g.follow = true;
    g.version++;
}

void forget(WindowMessage* source) {
    if (!enabled() || g.source != source) return;
    reset_log();
    g.source = nullptr;
    g.scene = nullptr;
}

bool update() {
    if (!enabled()) return false;
    bool want = wanted();
    if (want && !g.active) {
        ensure_panel();
        g.active = true;
        g.alpha = 0;
        int tx = 0, ty = 0;
        g.swallow = plat::touch(tx, ty);
        g.touching = false;
        g.dragging = false;
        g.velocity = 0;
        g.sig.clear();
        g.dirty = true;
    } else if (!want && g.active) {
        g.active = false;
        reset_log();
        g.panel.reset();
        g.sig.clear();
        g.dirty = true;
    }
    if (!g.active) return false;
    track_choices();
    if (g.alpha < 255) g.alpha = std::min(255, g.alpha + FADE_STEP);
    g.panel->root->update();
    handle_touch();
    move_scroll();
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

}
}
