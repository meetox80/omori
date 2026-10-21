#include "video.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../core/audio.h"
#include "../core/fs.h"
#include "../core/image.h"
#include "../platform/gfx.h"
#include "../platform/movie_decode.h"
#include "../platform/platform.h"
#include "../rpg/hooks.h"

namespace {

constexpr int SLOTS = 3;
constexpr size_t HEADER_SIZE = 40;
constexpr size_t ENTRY_SIZE = 12;
constexpr double AUDIO_EXTRAPOLATE = 0.1;

enum SlotState { SLOT_FREE, SLOT_DECODING, SLOT_READY, SLOT_SHOWN };

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

struct Header {
    int src_w = 0, src_h = 0;
    int w = 0, h = 0;
    int coded_w = 0, coded_h = 0;
    uint32_t frames = 0;
    uint32_t duration_us = 0;
    uint32_t max_bytes = 0;
    uint32_t table_off = 0;
    uint32_t flags = 0;
};

bool read_header(fs::DataFile* f, Header& h) {
    uint8_t b[HEADER_SIZE];
    if (fs::data_read(f, b, HEADER_SIZE) != HEADER_SIZE || std::memcmp(b, "OMJV", 4) != 0 || rd16(b + 4) != 1) return false;
    h.src_w = rd16(b + 8);
    h.src_h = rd16(b + 10);
    h.w = rd16(b + 12);
    h.h = rd16(b + 14);
    h.coded_w = rd16(b + 16);
    h.coded_h = rd16(b + 18);
    h.frames = rd32(b + 20);
    h.duration_us = rd32(b + 24);
    h.max_bytes = rd32(b + 28);
    h.table_off = rd32(b + 32);
    h.flags = rd32(b + 36);
    return h.frames > 0 && h.w > 0 && h.h > 0 && h.coded_w >= h.w && h.coded_h >= h.h && h.coded_w <= profile::MOVIE_MAX && h.coded_h <= profile::MOVIE_MAX && h.src_w > 0 &&
           h.src_h > 0;
}

struct Slot {
    uint32_t* px = nullptr;
    gfx::Texture* tex = nullptr;
    int state = SLOT_FREE;
    int frame = -1;
    int gen = 0;
};

struct Player {
    Header hdr;
    fs::DataFile* file = nullptr;
    std::vector<uint32_t> pts;
    std::vector<uint32_t> off;
    std::vector<uint32_t> size;
    int stride = 0;
    int rows = 0;
    Slot slots[SLOTS];
    int gen = 0;
    int next_decode = 0;
    int shown = -1;
    int shown_frame = -1;
    bool busy = false;
    bool dead = false;
    ~Player() {
        for (auto& s : slots) {
            if (s.tex) gfx::texture_destroy(s.tex);
            gfx::release(s.px);
        }
        if (file) fs::data_close(file);
    }
};

struct Source;
struct VSprite;
class VideoLayer;

struct State {
    std::vector<Player*> players;
    std::vector<Player*> graveyard;
    std::vector<std::shared_ptr<Source>> playing;
    std::vector<std::shared_ptr<Source>> settling;
    std::map<std::string, std::shared_ptr<Source>> cache;
    std::map<std::string, std::shared_ptr<VSprite>> sprites;
    std::vector<std::shared_ptr<VSprite>> display;
    std::shared_ptr<Source> movie;
    std::vector<VideoLayer*> layers;
};

State& st() {
    static State* s = new State();
    return *s;
}

plat::Mutex* g_lock = nullptr;
bool g_worker = false;
size_t g_rr = 0;
uint8_t* g_buf = nullptr;
size_t g_buf_cap = 0;

void lock() { plat::mutex_lock(g_lock); }
void unlock() { plat::mutex_unlock(g_lock); }

bool run_job() {
    Player* p = nullptr;
    int slot = -1, frame = -1;
    lock();
    size_t n = st().players.size();
    for (size_t k = 0; k < n && !p; k++) {
        Player* c = st().players[(g_rr + k) % n];
        if (c->dead || c->next_decode >= (int)c->pts.size()) continue;
        int free_slot = -1;
        for (int s = 0; s < SLOTS; s++) {
            if (c->slots[s].state == SLOT_FREE) {
                free_slot = s;
                break;
            }
        }
        if (free_slot < 0) continue;
        p = c;
        slot = free_slot;
        frame = c->next_decode++;
        c->slots[slot].state = SLOT_DECODING;
        c->slots[slot].frame = frame;
        c->slots[slot].gen = c->gen;
        c->busy = true;
        g_rr = (g_rr + k + 1) % n;
    }
    unlock();
    if (!p) return false;
    uint32_t size = p->size[(size_t)frame];
    bool ok = size > 0;
    if (ok && size > g_buf_cap) {
        gfx::release(g_buf);
        g_buf_cap = ((size_t)size + 63) & ~(size_t)63;
        g_buf = (uint8_t*)gfx::alloc(g_buf_cap);
        if (!g_buf) g_buf_cap = 0;
        ok = g_buf != nullptr;
    }
    ok = ok && fs::data_seek(p->file, p->off[(size_t)frame], SEEK_SET) == 0 && fs::data_read(p->file, g_buf, size) == size;
    ok = ok && movie_decode::decode(g_buf, size, p->slots[slot].px, p->stride, p->rows);
    lock();
    Slot& s = p->slots[slot];
    s.state = ok && s.gen == p->gen && !p->dead ? SLOT_READY : SLOT_FREE;
    p->busy = false;
    unlock();
    return true;
}

void worker() {
    for (;;) {
        if (!run_job()) plat::sleep_ms(3);
    }
}

void ensure_worker() {
    if (!g_lock) g_lock = plat::mutex_create();
    if (g_worker) return;
    g_worker = movie_decode::start_thread(worker);
    if (!g_worker) {
        static bool logged = false;
        if (!logged) plat::log("movie: decoder thread unavailable, decoding on the main thread");
        logged = true;
    }
}

int frame_index(const Player* p, double t) {
    double us = t * 1e6;
    if (us < 0) return 0;
    uint32_t key = us >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)us;
    auto it = std::upper_bound(p->pts.begin(), p->pts.end(), key);
    int i = (int)(it - p->pts.begin()) - 1;
    return i < 0 ? 0 : i;
}

Player* player_open(const std::string& rel, const Header& hdr) {
    fs::DataFile* f = fs::open_data(rel);
    if (!f) return nullptr;
    std::vector<uint8_t> table((size_t)hdr.frames * ENTRY_SIZE);
    if (fs::data_seek(f, hdr.table_off, SEEK_SET) != 0 || fs::data_read(f, table.data(), table.size()) != table.size()) {
        fs::data_close(f);
        return nullptr;
    }
    Player* p = new Player();
    p->hdr = hdr;
    p->file = f;
    uint32_t prev_off = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < hdr.frames; i++) {
        const uint8_t* e = &table[(size_t)i * ENTRY_SIZE];
        uint32_t off = rd32(e + 4);
        if (off == prev_off) continue;
        prev_off = off;
        p->pts.push_back(rd32(e));
        p->off.push_back(off);
        p->size.push_back(rd32(e + 8));
    }
    p->stride = 8;
    while (p->stride < hdr.coded_w) p->stride <<= 1;
    p->rows = hdr.coded_h;
    size_t bytes = (size_t)p->stride * p->rows * 4;
    for (auto& s : p->slots) {
        s.px = (uint32_t*)gfx::alloc(bytes);
        if (!s.px) {
            delete p;
            return nullptr;
        }
        s.tex = gfx::texture_wrap(hdr.w, hdr.h, p->stride, s.px);
    }
    return p;
}

void player_register(Player* p) {
    ensure_worker();
    lock();
    st().players.push_back(p);
    unlock();
}

void player_release(Player* p) {
    if (!p) return;
    lock();
    p->dead = true;
    st().players.erase(std::remove(st().players.begin(), st().players.end(), p), st().players.end());
    bool busy = p->busy;
    if (busy) st().graveyard.push_back(p);
    unlock();
    if (!busy) delete p;
}

void player_seek(Player* p, double t) {
    lock();
    p->gen++;
    p->next_decode = frame_index(p, t);
    p->shown_frame = -1;
    for (auto& s : p->slots) {
        if (s.state == SLOT_READY) s.state = SLOT_FREE;
    }
    unlock();
}

void player_advance(Player* p, double t) {
    lock();
    int target = frame_index(p, t);
    int best = -1, best_frame = -1;
    for (int i = 0; i < SLOTS; i++) {
        const Slot& s = p->slots[i];
        if (s.state == SLOT_READY && s.gen == p->gen && s.frame <= target && s.frame > best_frame) {
            best = i;
            best_frame = s.frame;
        }
    }
    if (best >= 0 && best_frame > p->shown_frame) {
        if (p->shown >= 0 && p->shown != best) p->slots[p->shown].state = SLOT_FREE;
        p->slots[best].state = SLOT_SHOWN;
        gfx::texture_touch(p->slots[best].tex);
        p->shown = best;
        p->shown_frame = best_frame;
    }
    for (auto& s : p->slots) {
        if (s.state == SLOT_READY && (s.gen != p->gen || s.frame <= p->shown_frame)) s.state = SLOT_FREE;
    }
    if (p->next_decode < target) p->next_decode = target;
    unlock();
}

gfx::Texture* black_texture() {
    static gfx::Texture* tex = nullptr;
    if (!tex) {
        uint32_t* px = (uint32_t*)gfx::alloc(8 * 8 * 4);
        for (int i = 0; i < 64; i++) px[i] = 0xFF000000u;
        tex = gfx::texture_wrap(8, 8, 8, px);
        gfx::texture_touch(tex);
    }
    return tex;
}

void draw_tex(gfx::Texture* tex, float tw, float th, const float* xy, bool axis, bool smooth, uint32_t color, gfx::Blend blend) {
    if (axis) {
        gfx::sprite(tex, 0, 0, tw, th, xy[0], xy[1], xy[4], xy[5], color, blend, smooth);
        return;
    }
    float uv[8] = {0, 0, tw, 0, tw, th, 0, th};
    gfx::draw_quad(tex, uv, xy, color, blend, smooth);
}

bool player_settled(Player* p, double t) {
    lock();
    int target = frame_index(p, t);
    bool done = p->shown_frame == target;
    if (!done) {
        bool pending = p->next_decode <= target;
        for (const auto& sl : p->slots) {
            if (sl.gen == p->gen && (sl.state == SLOT_DECODING || (sl.state == SLOT_READY && sl.frame <= target))) pending = true;
        }
        done = !pending;
    }
    unlock();
    return done;
}

void draw_player(Player* p, const g2d::Mat& m, double lw, double lh, double alpha) {
    if (!p || p->shown < 0) return;
    const Slot& s = p->slots[p->shown];
    int a = std::clamp((int)std::lround(alpha * 255.0), 0, 255);
    if (a <= 0) return;
    float tw = (float)p->hdr.w, th = (float)p->hdr.h;
    float xy[8];
    const double lx[4] = {0, lw, lw, 0}, ly[4] = {0, 0, lh, lh};
    for (int k = 0; k < 4; k++) g2d::screen_point(m, lx[k], ly[k], xy[k * 2], xy[k * 2 + 1]);
    bool axis = m.axis_aligned();
    bool smooth = true;
    if (axis && std::fabs(std::fabs(xy[4] - xy[0]) - tw) < 0.05f && std::fabs(std::fabs(xy[5] - xy[1]) - th) < 0.05f) {
        float dx = std::round(xy[0]) - xy[0], dy = std::round(xy[1]) - xy[1];
        for (int k = 0; k < 4; k++) {
            xy[k * 2] += dx;
            xy[k * 2 + 1] += dy;
        }
        smooth = false;
    }
    if (a >= 255) {
        draw_tex(s.tex, tw, th, xy, axis, smooth, gfx::rgba(255, 255, 255, 255), gfx::Blend::Copy);
        return;
    }
    draw_tex(black_texture(), 8, 8, xy, axis, false, gfx::rgba(a, a, a, a), gfx::Blend::Normal);
    draw_tex(s.tex, tw, th, xy, axis, smooth, gfx::rgba(a, a, a, 255), gfx::Blend::Add);
}

std::string key_for(const std::string& name) {
    std::string base = name;
    size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        std::string ext = images::lower(base.substr(dot));
        if (ext == ".webm" || ext == ".mp4") base = base.substr(0, dot);
    }
    return "movies/" + images::lower(base);
}

Source* g_audio_owner[audio::MOVIE_VOICES] = {};

struct Source {
    std::string name;
    std::string key;
    bool ok = false;
    Header hdr;
    double duration = 0;
    std::string audio_rel;
    bool paused = true;
    bool loop = false;
    bool ended = false;
    double pos = 0;
    double clock_base = 0;
    double wall_base = 0;
    double audio_last = -1;
    double audio_wall = 0;
    int audio_slot = -1;
    Player* player = nullptr;
    int refs = 0;
    ~Source();
};

void audio_stop(Source& s) {
    if (s.audio_slot < 0) return;
    audio::stop_movie_audio(s.audio_slot);
    g_audio_owner[s.audio_slot] = nullptr;
    s.audio_slot = -1;
}

void audio_start(Source& s, double pos) {
    audio_stop(s);
    if (s.audio_rel.empty()) return;
    int slot = -1;
    for (int i = 0; i < audio::MOVIE_VOICES; i++) {
        if (!g_audio_owner[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        plat::log("movie: no free audio voice for %s", s.name.c_str());
        return;
    }
    if (audio::play_movie_audio(slot, s.audio_rel, pos)) {
        g_audio_owner[slot] = &s;
        s.audio_slot = slot;
        s.audio_last = -1;
    }
}

Source::~Source() {
    audio_stop(*this);
    player_release(player);
    player = nullptr;
}

std::shared_ptr<Source> make_source(const std::string& name) {
    auto s = std::make_shared<Source>();
    s->name = name;
    s->key = key_for(name);
    fs::DataFile* f = fs::open_data(s->key + ".mjv");
    if (f) {
        s->ok = read_header(f, s->hdr);
        fs::data_close(f);
    }
    if (!s->ok) {
        plat::log("movie: %s not available", s->key.c_str());
        return s;
    }
    s->duration = s->hdr.duration_us / 1e6;
    if (fs::data_exists(s->key + ".ogg")) s->audio_rel = s->key + ".ogg";
    else if (fs::data_exists(s->key + ".wav")) s->audio_rel = s->key + ".wav";
    return s;
}

double current_time(Source& s) {
    if (s.paused) return s.pos;
    double now = plat::time_seconds();
    if (s.audio_slot >= 0 && audio::movie_audio_active(s.audio_slot)) {
        double a = audio::movie_audio_position(s.audio_slot);
        if (a != s.audio_last) {
            s.audio_last = a;
            s.audio_wall = now;
        }
        double t = a + std::min(now - s.audio_wall, AUDIO_EXTRAPOLATE);
        s.clock_base = t;
        s.wall_base = now;
        return t;
    }
    return s.clock_base + (now - s.wall_base);
}

void ensure_player(Source& s, double pos) {
    if (!s.player) {
        s.player = player_open(s.key + ".mjv", s.hdr);
        if (!s.player) {
            plat::log("movie: cannot open %s", s.key.c_str());
            return;
        }
        player_register(s.player);
    }
    player_seek(s.player, pos);
}

void src_play(const std::shared_ptr<Source>& s) {
    if (!s->ok || !s->paused) return;
    bool restart = s->ended || s->pos >= s->duration;
    if (restart) {
        s->pos = 0;
        s->ended = false;
    }
    s->paused = false;
    s->clock_base = s->pos;
    s->wall_base = plat::time_seconds();
    audio_start(*s, s->pos);
    if (!s->player || restart || s->player->shown < 0) ensure_player(*s, s->pos);
    if (std::find(st().playing.begin(), st().playing.end(), s) == st().playing.end()) st().playing.push_back(s);
}

void settle(const std::shared_ptr<Source>& s) {
    if (s->player && std::find(st().settling.begin(), st().settling.end(), s) == st().settling.end()) st().settling.push_back(s);
}

void src_pause(const std::shared_ptr<Source>& s) {
    if (s->paused) return;
    s->pos = std::min(current_time(*s), s->duration);
    s->paused = true;
    audio_stop(*s);
    st().playing.erase(std::remove(st().playing.begin(), st().playing.end(), s), st().playing.end());
    settle(s);
}

void src_drop_player_if_idle(Source& s) {
    if (s.paused && s.refs <= 0 && s.player) {
        player_release(s.player);
        s.player = nullptr;
    }
}

struct VSprite {
    std::shared_ptr<Source> src;
    uint64_t layer = 0;
    explicit VSprite(std::shared_ptr<Source> s) : src(std::move(s)) { src->refs++; }
    ~VSprite() {
        src->refs--;
        src_drop_player_if_idle(*src);
    }
};

uint64_t g_layer_serial = 0;

class VideoLayer : public g2d::Container {
public:
    uint64_t serial;
    VideoLayer() : serial(++g_layer_serial) { st().layers.push_back(this); }
    ~VideoLayer() override { st().layers.erase(std::remove(st().layers.begin(), st().layers.end(), this), st().layers.end()); }
    void render(const g2d::RenderState& rs) override {
        std::vector<std::shared_ptr<VSprite>> list = st().display;
        for (auto& sp : list) {
            if (sp->layer != serial || !sp->src->player) continue;
            draw_player(sp->src->player, rs.world, sp->src->hdr.src_w, sp->src->hdr.src_h, rs.alpha);
        }
        render_children(rs);
    }
};

uint64_t current_layer() { return st().layers.empty() ? 0 : st().layers.back()->serial; }

std::shared_ptr<Source> cached(const std::string& name) {
    auto it = st().cache.find(name);
    if (it != st().cache.end()) return it->second;
    auto s = make_source(name);
    st().cache[name] = s;
    return s;
}

std::shared_ptr<VSprite> sprite_of(const std::string& id) {
    auto it = st().sprites.find(id);
    return it == st().sprites.end() ? nullptr : it->second;
}

bool finished(Source& s) {
    if (!s.ok) return true;
    if (s.loop && !s.paused) return false;
    return current_time(s) >= s.duration;
}

void purge_graveyard() {
    std::vector<Player*> done;
    lock();
    for (size_t i = 0; i < st().graveyard.size();) {
        if (!st().graveyard[i]->busy) {
            done.push_back(st().graveyard[i]);
            st().graveyard.erase(st().graveyard.begin() + (long)i);
        } else {
            i++;
        }
    }
    unlock();
    for (Player* p : done) delete p;
}

void tick_source(const std::shared_ptr<Source>& s) {
    double t = current_time(*s);
    if (t >= s->duration) {
        if (s->loop && s->duration > 0) {
            t = std::fmod(t, s->duration);
            s->pos = t;
            s->clock_base = t;
            s->wall_base = plat::time_seconds();
            audio_start(*s, t);
            if (s->player) player_seek(s->player, t);
        } else {
            s->pos = s->duration;
            s->ended = true;
            s->paused = true;
            audio_stop(*s);
            st().playing.erase(std::remove(st().playing.begin(), st().playing.end(), s), st().playing.end());
            src_drop_player_if_idle(*s);
            settle(s);
            return;
        }
    } else {
        s->pos = t;
    }
    if (s->player) player_advance(s->player, t);
}

}

namespace ui {

std::shared_ptr<g2d::Container> video_container() { return std::make_shared<VideoLayer>(); }

void update_videos() {
    if (!g_lock) return;
    purge_graveyard();
    std::vector<std::shared_ptr<Source>> list = st().playing;
    for (auto& s : list) tick_source(s);
    auto& pending = st().settling;
    for (size_t i = 0; i < pending.size();) {
        auto s = pending[i];
        if (!s->paused || !s->player) {
            pending.erase(pending.begin() + (long)i);
            continue;
        }
        player_advance(s->player, s->pos);
        if (player_settled(s->player, s->pos)) pending.erase(pending.begin() + (long)i);
        else i++;
    }
    if (st().movie && st().movie->ended) st().movie.reset();
    if (!g_worker) {
        for (int i = 0; i < 2 && run_job(); i++) {
        }
    }
}

bool movie_covers_screen() { return st().movie && st().movie->player && st().movie->player->shown >= 0; }

void render_movie_overlay() {
    if (!movie_covers_screen()) return;
    int x0 = (int)images::OFFSET_X, y0 = (int)images::OFFSET_Y;
    int w = (int)std::lround(640 * images::SCALE), h = (int)std::lround(480 * images::SCALE);
    gfx::set_scissor(x0, y0, w, h);
    gfx::fill((float)x0, (float)y0, (float)(x0 + w), (float)(y0 + h), 0xFF000000u, gfx::Blend::Copy);
    const Header& hd = st().movie->hdr;
    double scale = std::min(640.0 / hd.src_w, 480.0 / hd.src_h);
    g2d::Mat m;
    m.a = scale;
    m.d = scale;
    m.tx = (640.0 - hd.src_w * scale) / 2;
    m.ty = (480.0 - hd.src_h * scale) / 2;
    draw_player(st().movie->player, m, hd.src_w, hd.src_h, 1.0);
    gfx::reset_scissor();
}

}

namespace rpg {

namespace scenes {

void play_movie(const std::string& name) {
    st().movie.reset();
    auto s = make_source(name);
    if (!s->ok) return;
    if (!g_lock) g_lock = plat::mutex_create();
    st().movie = s;
    src_play(s);
}

bool is_movie_playing() { return st().movie && !st().movie->ended; }

}

namespace video {

void load(const std::string& name) {
    if (!g_lock) g_lock = plat::mutex_create();
    cached(name);
}

void release(const std::string& name) { st().cache.erase(name); }

void create(const std::string& name, const std::string& id) {
    if (!g_lock) g_lock = plat::mutex_create();
    auto src = cached(name);
    src->loop = false;
    st().sprites[id] = std::make_shared<VSprite>(src);
}

void play(const std::string& id) {
    auto sp = sprite_of(id);
    if (!sp) return;
    st().display.erase(std::remove(st().display.begin(), st().display.end(), sp), st().display.end());
    sp->layer = current_layer();
    st().display.push_back(sp);
    src_play(sp->src);
}

void stop(const std::string& id) {
    auto sp = sprite_of(id);
    if (!sp) return;
    st().display.erase(std::remove(st().display.begin(), st().display.end(), sp), st().display.end());
    src_pause(sp->src);
    st().sprites.erase(id);
}

void set_loop(const std::string& id) {
    auto sp = sprite_of(id);
    if (sp) sp->src->loop = true;
}

bool is_ready() { return true; }

bool has_finished(const std::string& id) {
    auto sp = sprite_of(id);
    return !sp || finished(*sp->src);
}

bool exists(const std::string& id) { return st().sprites.count(id) != 0; }

void pause_all() {
    for (auto& kv : st().sprites) src_pause(kv.second->src);
}

void play_all() {
    for (auto& kv : st().sprites) {
        auto& src = kv.second->src;
        if (src->paused && !finished(*src)) src_play(src);
    }
}

}

}
