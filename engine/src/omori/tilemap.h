#pragma once
#include <climits>
#include <map>
#include <memory>
#include <vector>

#include "../core/value.h"
#include "../gfx2d/display.h"

namespace ui {

class TiledTilemap;

class TileLayerSprite : public g2d::DisplayObject {
public:
    TileLayerSprite(TiledTilemap* owner, size_t index) : owner_(owner), index_(index) {}
    void render(const g2d::RenderState& rs) override;
    size_t index() const { return index_; }

private:
    TiledTilemap* owner_;
    size_t index_;
};

class PriorityTileSprite : public g2d::DisplayObject {
public:
    explicit PriorityTileSprite(TiledTilemap* owner) : owner_(owner) {}
    void render(const g2d::RenderState& rs) override;
    int layer_id = -1;
    int rect = -1;
    double orig_x = 0, orig_y = 0;
    double width = 0, height = 0;
    void show() { visible = true; }
    void hide() { visible = false; }

private:
    TiledTilemap* owner_;
};

class TiledTilemap : public g2d::Container {
public:
    struct Rect {
        int page = 0, x = 0, y = 0, w = 0, h = 0, ow = 0, oh = 0;
    };
    struct Anim {
        int local = 0;
        std::vector<double> durations;
        std::vector<int> rects;
    };
    struct Layer {
        int li = 0, z = 0, priority = 0, hide = -1;
        double offx = 0, offy = 0;
        const uint8_t* cells = nullptr;
        size_t cell_count = 0;
        std::shared_ptr<TileLayerSprite> sprite;
        bool removed = false;
    };
    struct Object {
        int li = 0, z = 0, priority = 0, table = 0;
        double x = 0, y = 0, offx = 0, offy = 0;
    };

    TiledTilemap();
    void setup();
    bool is_tiled() const { return tiled_ != nullptr; }
    void update() override;
    void render(const g2d::RenderState& rs) override;
    void hide_on_level(int level);
    void refresh() { needs_repaint_ = true; }

    double origin_x = 0, origin_y = 0;
    bool horizontal_wrap = false, vertical_wrap = false;
    int animation_count = 0;
    int animation_frame = 0;

    void draw_rect(int rect, double dx, double dy, const g2d::RenderState& rs) const;
    struct FastRect {
        gfx::Texture* tex = nullptr;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float ow = 0, oh = 0;
    };
    const FastRect* fast_rect(int index) const {
        return index >= 0 && (size_t)index < fast_.size() && fast_[(size_t)index].tex ? &fast_[(size_t)index] : nullptr;
    }
    void build_fast_rects();
    int cell_rect(int cell_value) const;
    const Rect* rect_at(int index) const { return index >= 0 && (size_t)index < rects_.size() ? &rects_[(size_t)index] : nullptr; }
    const std::vector<Layer>& layers() const { return layers_; }
    int start_x() const { return start_x_; }
    int start_y() const { return start_y_; }
    double tile_width() const { return tile_width_; }
    double tile_height() const { return tile_height_; }
    int map_width() const { return map_width_; }
    int map_height() const { return map_height_; }
    int tile_cols() const { return tile_cols_; }
    int tile_rows() const { return tile_rows_; }
    bool is_priority_layer(const Layer& l) const { return l.priority > 0 && l.z == player_z_; }
    double layer_offset_x(int li) const;
    double layer_offset_y(int li) const;

private:
    std::shared_ptr<Document> doc_;
    const Value* tiled_ = nullptr;
    g2d::BitmapPtr atlas_;
    Image* frame_img_ = nullptr;
    Image* fast_img_ = nullptr;
    uint64_t fast_generation_ = 0;
    std::vector<FastRect> fast_;
    std::vector<Rect> rects_;
    std::vector<std::pair<int, int>> table_;
    std::vector<Anim> anims_;
    std::map<int, int> anim_frame_;
    std::map<int, double> anim_duration_;
    std::vector<Layer> layers_;
    std::vector<Object> objects_;
    std::vector<std::shared_ptr<PriorityTileSprite>> priority_tiles_;
    std::map<int, std::pair<double, double>> layer_offsets_;
    size_t priority_count_ = 0;
    int last_start_x_ = INT_MIN, last_start_y_ = INT_MIN;
    int start_x_ = 0, start_y_ = 0;
    bool needs_repaint_ = true;
    bool positions_valid_ = false;
    int positioned_sx_ = 0, positioned_sy_ = 0;
    double positioned_ox_ = 0, positioned_oy_ = 0;
    double tile_width_ = 32, tile_height_ = 32;
    int map_width_ = 0, map_height_ = 0;
    int tile_cols_ = 0, tile_rows_ = 0;
    int player_z_ = 3;
    double margin_ = 20;

    void update_anim();
    void update_anim_frames();
    void update_layer_positions(int sx, int sy);
    void paint_all_tiles(int sx, int sy);
    void paint_priority_tile(int li, int table, int sx, int sy, double dx, double dy);
};

}
