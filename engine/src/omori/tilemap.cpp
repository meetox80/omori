#include "../rpg/perf.h"
#include "tilemap.h"

#include <algorithm>
#include <cmath>

#include "../core/image.h"
#include "../platform/gfx.h"
#include "../rpg/game.h"
#include "../rpg/map.h"
#include "../rpg/params.h"

namespace ui {

namespace {

int rd16(const uint8_t* p) { return (int16_t)(uint16_t)(p[0] | (p[1] << 8)); }
int rdu16(const uint8_t* p) { return p[0] | (p[1] << 8); }

int mod(int a, int b) { return b ? ((a % b) + b) % b : 0; }

}

void TileLayerSprite::render(const g2d::RenderState& rs) {
    const TiledTilemap::Layer& l = owner_->layers()[index_];
    if (owner_->is_priority_layer(l) || !l.cells) return;
    int cols = owner_->tile_cols(), rows = owner_->tile_rows();
    int sx = owner_->start_x(), sy = owner_->start_y();
    int mw = owner_->map_width(), mh = owner_->map_height();
    double tw = owner_->tile_width(), th = owner_->tile_height();
    const g2d::Mat& m = rs.world;
    bool fast = m.axis_aligned() && m.a > 0 && m.d > 0;
    float scale_x = (float)(m.a * images::SCALE), scale_y = (float)(m.d * images::SCALE);
    float origin_x = (float)(m.tx * images::SCALE + images::OFFSET_X), origin_y = (float)(m.ty * images::SCALE + images::OFFSET_Y);
    bool unscaled = std::fabs(m.a - 1.0) < 1e-6 && std::fabs(m.d - 1.0) < 1e-6;
    if (unscaled) {
        origin_x = std::floor(origin_x + 0.5f);
        origin_y = std::floor(origin_y + 0.5f);
    }
    float step_x = (float)tw * scale_x, step_y = (float)th * scale_y;
    int a8 = std::clamp((int)std::lround(rs.alpha * 255.0), 0, 255);
    uint32_t color = gfx::rgba(a8, a8, a8, a8);
    if (a8 <= 0) return;
    for (int y = 0; y < rows; y++) {
        float py = origin_y + y * step_y;
        for (int x = 0; x < cols; x++) {
            int mx = x + sx, my = y + sy;
            if (owner_->horizontal_wrap) mx = mod(mx, mw);
            if (owner_->vertical_wrap) my = mod(my, mh);
            if (mx < 0 || mx >= mw || my < 0 || my >= mh) continue;
            size_t pos = (size_t)mx + (size_t)my * (size_t)mw;
            if (pos >= l.cell_count) continue;
            int v = rdu16(l.cells + pos * 2);
            if (!v) continue;
            int rect = owner_->cell_rect(v);
            const TiledTilemap::FastRect* fr = fast ? owner_->fast_rect(rect) : nullptr;
            if (!fr) {
                owner_->draw_rect(rect, x * tw, y * th, rs);
                continue;
            }
            float px = origin_x + x * step_x;
            gfx::sprite(fr->tex, fr->u0, fr->v0, fr->u1, fr->v1, px, py, px + fr->ow * scale_x, py + fr->oh * scale_y, color, gfx::Blend::Normal,
                        !unscaled);
        }
    }
}

void PriorityTileSprite::render(const g2d::RenderState& rs) {
    const g2d::Mat& m = rs.world;
    const TiledTilemap::FastRect* fr = m.axis_aligned() && m.a > 0 && m.d > 0 ? owner_->fast_rect(rect) : nullptr;
    if (!fr) {
        owner_->draw_rect(rect, -width / 2, -height, rs);
        return;
    }
    int a8 = std::clamp((int)std::lround(rs.alpha * 255.0), 0, 255);
    if (a8 <= 0) return;
    float scale_x = (float)(m.a * images::SCALE), scale_y = (float)(m.d * images::SCALE);
    float x0 = (float)((m.tx - m.a * width / 2) * images::SCALE + images::OFFSET_X);
    float y0 = (float)((m.ty - m.d * height) * images::SCALE + images::OFFSET_Y);
    bool unscaled = std::fabs(m.a - 1.0) < 1e-6 && std::fabs(m.d - 1.0) < 1e-6;
    if (unscaled) {
        x0 = std::floor(x0 + 0.5f);
        y0 = std::floor(y0 + 0.5f);
    }
    gfx::sprite(fr->tex, fr->u0, fr->v0, fr->u1, fr->v1, x0, y0, x0 + fr->ow * scale_x, y0 + fr->oh * scale_y, gfx::rgba(a8, a8, a8, a8),
                gfx::Blend::Normal, !unscaled);
}

void TiledTilemap::build_fast_rects() {
    fast_.assign(rects_.size(), FastRect());
    fast_img_ = frame_img_;
    fast_generation_ = images::generation();
    if (!frame_img_) return;
    for (size_t i = 0; i < rects_.size(); i++) {
        const Rect& r = rects_[i];
        int ax = r.x, ay = r.page * 512 + r.y;
        for (size_t ci = 0; ci < frame_img_->chunks.size(); ci++) {
            const ImageChunk& c = frame_img_->chunks[ci];
            if (ax >= c.x && ay >= c.y && ax + r.w <= c.x + c.w && ay + r.h <= c.y + c.h) {
                FastRect& f = fast_[i];
                f.tex = images::chunk_texture(frame_img_, ci);
                f.u0 = (float)(ax - c.x);
                f.v0 = (float)(ay - c.y);
                f.u1 = f.u0 + (float)r.w;
                f.v1 = f.v0 + (float)r.h;
                f.ow = (float)r.ow;
                f.oh = (float)r.oh;
                break;
            }
        }
    }
}

TiledTilemap::TiledTilemap() {
    comparator = [](const g2d::DisplayPtr& a, const g2d::DisplayPtr& b) {
        if (a->z != b->z) return a->z < b->z;
        if (a->y != b->y) return a->y < b->y;
        if (a->priority != b->priority) return a->priority < b->priority;
        return a->sprite_id < b->sprite_id;
    };
}

void TiledTilemap::setup() {
    rpg::GameMap& m = rpg::game_map();
    doc_ = m.doc;
    const Value& t = m.tiled();
    if (!t.is_obj()) return;
    tiled_ = &t;
    tile_width_ = t["tw"].num(32);
    tile_height_ = t["th"].num(32);
    map_width_ = t["w"].as_int();
    map_height_ = t["h"].as_int();
    double width = 640 + margin_ * 2, height = 480 + margin_ * 2;
    tile_cols_ = (int)std::ceil(width / tile_width_) + 1;
    tile_rows_ = (int)std::ceil(height / tile_height_) + 1;
    player_z_ = rpg::params().tiled_z_player;
    char key[32];
    std::snprintf(key, sizeof key, "maps/map%03d", m.map_id_);
    atlas_ = g2d::Bitmap::load(key);
    const Value& rb = t["rects"];
    for (size_t i = 0; i + 14 <= rb.size(); i += 14) {
        const uint8_t* p = rb.bytes + i;
        Rect r;
        r.page = rd16(p);
        r.x = rd16(p + 2);
        r.y = rd16(p + 4);
        r.w = rd16(p + 6);
        r.h = rd16(p + 8);
        r.ow = rd16(p + 10);
        r.oh = rd16(p + 12);
        rects_.push_back(r);
    }
    const Value& tb = t["tiles"];
    for (size_t i = 0; i + 4 <= tb.size(); i += 4) table_.emplace_back(rd16(tb.bytes + i), rd16(tb.bytes + i + 2));
    const Value& an = t["anims"];
    for (size_t i = 0; i < an.size(); i++) {
        const Value& e = an[i];
        Anim a;
        a.local = e[1].as_int();
        for (size_t f = 0; f < e[2].size(); f++) a.durations.push_back(e[2][f].num());
        for (size_t f = 0; f < e[3].size(); f++) a.rects.push_back(e[3][f].as_int(-1));
        anims_.push_back(a);
    }
    for (const Anim& a : anims_) {
        anim_frame_[a.local] = 0;
        anim_duration_[a.local] = a.durations.empty() ? 0 : a.durations[0];
    }
    const Value& ls = t["layers"];
    for (size_t i = 0; i < ls.size(); i++) {
        const Value& e = ls[i];
        Layer l;
        l.li = e[0].as_int();
        l.z = e[1].as_int();
        l.priority = e[2].as_int();
        l.hide = e[3].as_int(-1);
        l.offx = e[4].num();
        l.offy = e[5].num();
        l.cells = e[6].bytes;
        l.cell_count = e[6].size() / 2;
        layer_offsets_[l.li] = {l.offx, l.offy};
        layers_.push_back(l);
    }
    for (size_t i = 0; i < layers_.size(); i++) {
        auto s = std::make_shared<TileLayerSprite>(this, i);
        s->z = layers_[i].z;
        layers_[i].sprite = s;
        add_child(s);
    }
    const Value& os = t["objects"];
    for (size_t i = 0; i < os.size(); i++) {
        const Value& e = os[i];
        Object o;
        o.li = e[0].as_int();
        o.z = e[1].as_int();
        o.priority = e[2].as_int();
        o.table = e[3].as_int();
        o.x = e[4].num();
        o.y = e[5].num();
        o.offx = e.size() > 6 ? e[6].num() : 0;
        o.offy = e.size() > 7 ? e[7].num() : 0;
        layer_offsets_[o.li] = {o.offx, o.offy};
        objects_.push_back(o);
    }
    for (int i = 0; i < 256; i++) {
        auto s = std::make_shared<PriorityTileSprite>(this);
        s->z = player_z_;
        s->layer_id = -1;
        s->hide();
        add_child(s);
        priority_tiles_.push_back(s);
    }
}

double TiledTilemap::layer_offset_x(int li) const {
    auto it = layer_offsets_.find(li);
    return it == layer_offsets_.end() ? 0 : it->second.first;
}

double TiledTilemap::layer_offset_y(int li) const {
    auto it = layer_offsets_.find(li);
    return it == layer_offsets_.end() ? 0 : it->second.second;
}

int TiledTilemap::cell_rect(int cell_value) const {
    size_t ti = (size_t)(cell_value - 1);
    if (ti >= table_.size()) return -1;
    int rect = table_[ti].first;
    int a = table_[ti].second;
    if (a >= 0 && (size_t)a < anims_.size()) {
        const Anim& an = anims_[(size_t)a];
        auto it = anim_frame_.find(an.local);
        int frame = it == anim_frame_.end() ? 0 : it->second;
        if (frame > 0) rect = (size_t)frame < an.rects.size() ? an.rects[(size_t)frame] : -1;
    }
    return rect;
}

void TiledTilemap::draw_rect(int index, double dx, double dy, const g2d::RenderState& rs) const {
    const Rect* r = rect_at(index);
    if (!r || r->w <= 0 || r->h <= 0) return;
    Image* img = frame_img_;
    if (!img) return;
    g2d::Mat t;
    t.tx = dx;
    t.ty = dy;
    t.a = (double)r->ow / r->w;
    t.d = (double)r->oh / r->h;
    g2d::Mat m = rs.world.mul(t);
    g2d::draw_image(img, r->x, r->page * 512 + r->y, r->w, r->h, m, rs.alpha, 0, false, rs);
}

void TiledTilemap::update() {
    rpg::perf::Scope ps(rpg::perf::Tilemap);
    animation_count++;
    animation_frame = animation_count / 30;
    g2d::Container::update();
    if (tiled_) update_anim();
}

void TiledTilemap::update_anim() {
    bool need_refresh = false;
    for (auto& kv : anim_duration_) {
        kv.second -= 1;
        if (kv.second <= 0) {
            anim_frame_[kv.first] += 1;
            need_refresh = true;
        }
    }
    if (need_refresh) {
        update_anim_frames();
        refresh();
    }
}

void TiledTilemap::update_anim_frames() {
    for (const Anim& a : anims_) {
        int& frame = anim_frame_[a.local];
        if (frame < 0 || (size_t)frame >= a.durations.size()) frame = 0;
        double duration = a.durations.empty() ? 0 : a.durations[(size_t)frame];
        if (anim_duration_[a.local] <= 0) anim_duration_[a.local] = duration;
    }
}

void TiledTilemap::update_layer_positions(int sx, int sy) {
    double ox = std::floor(origin_x), oy = std::floor(origin_y);
    for (Layer& l : layers_) {
        l.sprite->x = sx * tile_width_ - ox + l.offx;
        l.sprite->y = sy * tile_height_ - oy + l.offy;
    }
    for (auto& s : priority_tiles_) {
        double offx = s->layer_id >= 0 ? layer_offset_x(s->layer_id) : 0;
        double offy = s->layer_id >= 0 ? layer_offset_y(s->layer_id) : 0;
        s->x = s->orig_x + sx * tile_width_ - ox + offx + s->width / 2;
        s->y = s->orig_y + sy * tile_height_ - oy + offy + s->height;
    }
}

void TiledTilemap::paint_priority_tile(int li, int table, int sx, int sy, double dx, double dy) {
    if (priority_count_ >= priority_tiles_.size()) return;
    int rect = cell_rect(table + 1);
    const Rect* base = (size_t)table < table_.size() ? rect_at(table_[(size_t)table].first) : nullptr;
    double w = base ? base->ow : tile_width_, h = base ? base->oh : tile_height_;
    double ox = std::floor(origin_x), oy = std::floor(origin_y);
    auto& s = priority_tiles_[priority_count_];
    s->layer_id = li;
    s->orig_x = dx;
    s->orig_y = dy;
    s->width = w;
    s->height = h;
    s->x = dx + sx * tile_width_ - ox + layer_offset_x(li) + w / 2;
    s->y = dy + sy * tile_height_ - oy + layer_offset_y(li) + h;
    s->rect = rect;
    int pr = 0, z = 0;
    for (const Layer& l : layers_) {
        if (l.li == li) {
            pr = l.priority;
            z = l.z;
        }
    }
    for (const Object& o : objects_) {
        if (o.li == li) {
            pr = o.priority;
            z = o.z;
        }
    }
    s->priority = pr;
    s->z = z;
    s->show();
    priority_count_++;
}

void TiledTilemap::paint_all_tiles(int sx, int sy) {
    priority_count_ = 0;
    for (const Layer& l : layers_) {
        if (!is_priority_layer(l) || !l.cells) continue;
        for (int y = 0; y < tile_rows_; y++) {
            for (int x = 0; x < tile_cols_; x++) {
                int mx = x + sx, my = y + sy;
                if (horizontal_wrap) mx = mod(mx, map_width_);
                if (vertical_wrap) my = mod(my, map_height_);
                if (mx < 0 || mx >= map_width_ || my < 0 || my >= map_height_) continue;
                size_t pos = (size_t)mx + (size_t)my * (size_t)map_width_;
                if (pos >= l.cell_count) continue;
                int v = rdu16(l.cells + pos * 2);
                if (!v) continue;
                paint_priority_tile(l.li, v - 1, sx, sy, x * tile_width_, y * tile_height_);
            }
        }
    }
    for (const Object& o : objects_) paint_priority_tile(o.li, o.table, sx, sy, o.x - sx * tile_width_, o.y - sy * tile_height_);
    while (priority_count_ < priority_tiles_.size()) {
        auto& s = priority_tiles_[priority_count_];
        s->hide();
        s->layer_id = -1;
        priority_count_++;
    }
}

void TiledTilemap::hide_on_level(int level) {
    std::vector<int> hidden;
    for (Layer& l : layers_) {
        if (l.hide == -1) continue;
        if (l.hide != level) {
            add_child(l.sprite);
            continue;
        }
        hidden.push_back(l.li);
        remove_child(l.sprite.get());
    }
    for (auto& s : priority_tiles_) {
        if (std::find(hidden.begin(), hidden.end(), s->layer_id) == hidden.end()) continue;
        s->visible = false;
    }
}

void TiledTilemap::render(const g2d::RenderState& rs) {
    frame_img_ = atlas_ ? atlas_->image() : nullptr;
    if (frame_img_) atlas_->touch();
    if (frame_img_ != fast_img_ || fast_generation_ != images::generation()) build_fast_rects();
    if (tiled_) {
        double ox = std::floor(origin_x), oy = std::floor(origin_y);
        int sx = (int)std::floor((ox - margin_) / tile_width_);
        int sy = (int)std::floor((oy - margin_) / tile_height_);
        start_x_ = sx;
        start_y_ = sy;
        update_layer_positions(sx, sy);
        if (needs_repaint_ || last_start_x_ != sx || last_start_y_ != sy) {
            last_start_x_ = sx;
            last_start_y_ = sy;
            paint_all_tiles(sx, sy);
            needs_repaint_ = false;
        }
    }
    g2d::Container::render(rs);
}

}
