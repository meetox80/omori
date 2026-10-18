#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef USE_TREMOR
#include <tremor/ivorbisfile.h>
#else
#include <vorbis/vorbisfile.h>
#endif

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "../../third_party/minimp3_ex.h"

#include "../platform/audio_out.h"
#include "../platform/platform.h"
#include "fs.h"

namespace audio {

static size_t cb_read(void* ptr, size_t size, size_t nmemb, void* src) {
    if (!size) return 0;
    return fs::data_read((fs::DataFile*)src, ptr, size * nmemb) / size;
}
static int cb_seek(void* src, ogg_int64_t off, int whence) { return fs::data_seek((fs::DataFile*)src, (int64_t)off, whence); }
static int cb_close(void* src) {
    fs::data_close((fs::DataFile*)src);
    return 0;
}
static long cb_tell(void* src) { return (long)fs::data_tell((fs::DataFile*)src); }

static long decode(OggVorbis_File* vf, char* buf, int len) {
    int bs = 0;
#ifdef USE_TREMOR
    return ov_read(vf, buf, len, &bs);
#else
    return ov_read(vf, buf, len, 0, 2, 1, &bs);
#endif
}

struct Pcm {
    std::vector<int16_t> data;
    int rate = 44100;
    int channels = 2;
    size_t frames = 0;
};

struct Stream {
    OggVorbis_File vf;
    bool mp3 = false;
    bool wav = false;
    int64_t wav_data = 0;
    int wav_bits = 16;
    mp3dec_ex_t mp3d;
    mp3dec_io_t mp3io;
    fs::DataFile* file = nullptr;
    bool open = false;
    int rate = 44100;
    int channels = 2;
    int64_t loop_start = 0;
    int64_t loop_end = -1;
    int64_t total = 0;
    int64_t next_frame = 0;
    int64_t bytes = 0;
    bool loop = false;
    bool eof = false;
    ~Stream() {
        if (!open) return;
        if (wav) {
            fs::data_close(file);
        } else if (mp3) {
            mp3dec_ex_close(&mp3d);
            fs::data_close(file);
        } else {
            ov_clear(&vf);
        }
    }
};

static size_t mp3_read_cb(void* buf, size_t size, void* user) { return fs::data_read((fs::DataFile*)user, buf, size); }
static int mp3_seek_cb(uint64_t pos, void* user) { return fs::data_seek((fs::DataFile*)user, (int64_t)pos, SEEK_SET); }

static int stream_seek(Stream* s, int64_t frame) {
    if (s->wav) {
        if (frame < 0 || frame > s->total) return -1;
        return fs::data_seek(s->file, s->wav_data + frame * s->channels * (s->wav_bits / 8), SEEK_SET);
    }
    if (s->mp3) return mp3dec_ex_seek(&s->mp3d, (uint64_t)frame * (uint64_t)s->channels) == 0 ? 0 : -1;
    return ov_pcm_seek(&s->vf, frame);
}

static long stream_decode(Stream* s, char* buf, int len) {
    if (s->wav) {
        int bytes_per = s->wav_bits / 8;
        int frames = len / (2 * s->channels);
        int64_t pos = (fs::data_tell(s->file) - s->wav_data) / (bytes_per * s->channels);
        if (pos + frames > s->total) frames = (int)(s->total - pos);
        if (frames <= 0) return 0;
        if (bytes_per == 2) {
            size_t got = fs::data_read(s->file, buf, (size_t)frames * s->channels * 2);
            return (long)got;
        }
        std::vector<uint8_t> raw((size_t)frames * s->channels);
        size_t got = fs::data_read(s->file, raw.data(), raw.size());
        int16_t* out = (int16_t*)buf;
        for (size_t i = 0; i < got; i++) out[i] = (int16_t)(((int)raw[i] - 128) << 8);
        return (long)(got * 2);
    }
    if (s->mp3) {
        size_t samples = (size_t)len / sizeof(int16_t);
        size_t got = mp3dec_ex_read(&s->mp3d, (mp3d_sample_t*)buf, samples);
        return (long)(got * sizeof(int16_t));
    }
    return decode(&s->vf, buf, len);
}

struct Voice {
    int kind = 0;
    std::string folder;
    std::string name;
    bool loop = false;
    bool active = false;
    std::unique_ptr<Stream> stream;
    std::shared_ptr<Pcm> pcm;
    std::shared_ptr<Pcm> capture;
    bool capture_done = false;
    int16_t buf[4096 * 2];
    int buf_len = 0;
    int buf_pos = 0;
    int64_t buf_start = 0;
    uint32_t frac = 0;
    uint32_t step = 65536;
    int32_t prev_l = 0, prev_r = 0, cur_l = 0, cur_r = 0;
    bool primed = false;
    int32_t vol_q = 32768;
    int32_t pan_l = 32768, pan_r = 32768;
    int32_t gain = 1 << 24;
    int32_t gain_target = 1 << 24;
    int32_t gain_step = 0;
    bool stop_after_fade = false;
    int64_t played = 0;
    uint32_t pending = 0;
    Track pending_track;
    double pending_pos = 0;
    double pending_fade_in = -1;
    int32_t pending_gain = 1 << 24;
};

enum Kind { K_BGM = 0, K_BGS = 1, K_ME = 2, K_SE = 3 };

constexpr int MAX_SE = 12;

static Voice g_bgm, g_bgs, g_me, g_danger;
static Voice g_extra[EXTRA_BUFFERS];
static std::string g_extra_folder[EXTRA_BUFFERS];
static bool g_extra_once[EXTRA_BUFFERS];
static Track g_cur_danger;
static bool g_danger_valid = false;
static bool g_bgm_once = false;
static bool g_bgm_once_finished = false;
static Voice g_se[MAX_SE];
static Voice g_movie[MOVIE_VOICES];
static volatile uint32_t g_movie_played[MOVIE_VOICES];
static volatile int g_movie_rate[MOVIE_VOICES];
static int g_vol[4] = {100, 100, 100, 100};
static Track g_cur_bgm, g_cur_bgs, g_cur_me;
static bool g_bgm_valid = false, g_bgs_valid = false, g_me_valid = false;
static bool g_bgm_held = false;
static std::unordered_map<std::string, std::shared_ptr<Pcm>> g_se_cache;
static std::vector<std::string> g_se_order;
static size_t g_se_bytes = 0;
static bool g_inited = false;

static std::string rel_for(const char* folder, const std::string& name) {
    std::string rel = std::string("audio/") + folder + "/" + name + ".ogg";
    for (auto& c : rel) {
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    }
    return rel;
}

static int64_t comment_int(OggVorbis_File* vf, const char* key) {
    vorbis_comment* vc = ov_comment(vf, -1);
    if (!vc) return -1;
    size_t kl = std::strlen(key);
    for (int i = 0; i < vc->comments; i++) {
        const char* c = vc->user_comments[i];
        if ((size_t)vc->comment_lengths[i] > kl && strncasecmp(c, key, kl) == 0 && c[kl] == '=') return std::atoll(c + kl + 1);
    }
    return -1;
}

static std::unique_ptr<Stream> open_stream_rel(const std::string& rel, bool loop) {
    fs::DataFile* f = fs::open_data_memory(rel, 64 * 1024);
    if (!f) {
        plat::log("missing audio %s", rel.c_str());
        return nullptr;
    }
    uint8_t magic[4] = {0, 0, 0, 0};
    fs::data_read(f, magic, 4);
    fs::data_seek(f, 0, SEEK_SET);
    std::unique_ptr<Stream> s(new Stream());
    s->loop = loop;
    s->bytes = fs::data_size(f);
    if (std::memcmp(magic, "RIFF", 4) == 0) {
        uint8_t hdr[12];
        fs::data_read(f, hdr, 12);
        bool have_fmt = false;
        for (;;) {
            uint8_t ch[8];
            if (fs::data_read(f, ch, 8) != 8) break;
            uint32_t size = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
            if (!std::memcmp(ch, "fmt ", 4)) {
                uint8_t fmt[16];
                if (size < 16 || fs::data_read(f, fmt, 16) != 16) break;
                s->channels = fmt[2] | (fmt[3] << 8);
                s->rate = (int)(fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24));
                s->wav_bits = fmt[14] | (fmt[15] << 8);
                have_fmt = (fmt[0] | (fmt[1] << 8)) == 1 && (s->wav_bits == 16 || s->wav_bits == 8) && s->channels >= 1 && s->channels <= 2;
                fs::data_seek(f, (int64_t)size - 16 + (size & 1), SEEK_CUR);
            } else if (!std::memcmp(ch, "data", 4)) {
                if (!have_fmt) break;
                s->wav = true;
                s->file = f;
                s->wav_data = fs::data_tell(f);
                s->total = size / (s->channels * (s->wav_bits / 8));
                s->open = true;
                return s;
            } else {
                fs::data_seek(f, (int64_t)size + (size & 1), SEEK_CUR);
            }
        }
        fs::data_close(f);
        plat::log("bad wav %s", rel.c_str());
        return nullptr;
    }
    if (std::memcmp(magic, "OggS", 4) != 0) {
        s->mp3 = true;
        s->file = f;
        s->mp3io.read = mp3_read_cb;
        s->mp3io.read_data = f;
        s->mp3io.seek = mp3_seek_cb;
        s->mp3io.seek_data = f;
        if (mp3dec_ex_open_cb(&s->mp3d, &s->mp3io, MP3D_SEEK_TO_SAMPLE) != 0) {
            fs::data_close(f);
            plat::log("bad mp3 %s", rel.c_str());
            return nullptr;
        }
        s->open = true;
        s->rate = s->mp3d.info.hz ? s->mp3d.info.hz : 44100;
        s->channels = s->mp3d.info.channels ? s->mp3d.info.channels : 2;
        s->total = (int64_t)(s->mp3d.samples / (uint64_t)s->channels);
        return s;
    }
    ov_callbacks cb{cb_read, cb_seek, cb_close, cb_tell};
    if (ov_open_callbacks(f, &s->vf, nullptr, 0, cb) != 0) {
        fs::data_close(f);
        return nullptr;
    }
    s->open = true;
    vorbis_info* vi = ov_info(&s->vf, -1);
    s->rate = vi ? (int)vi->rate : 44100;
    s->channels = vi ? vi->channels : 2;
    s->total = (int64_t)ov_pcm_total(&s->vf, -1);
    int64_t ls = comment_int(&s->vf, "LOOPSTART");
    int64_t ll = comment_int(&s->vf, "LOOPLENGTH");
    if (ls >= 0 && ll > 0) {
        s->loop_start = ls;
        s->loop_end = ls + ll;
    }
    return s;
}

static std::unique_ptr<Stream> open_stream(const char* folder, const std::string& name, bool loop) { return open_stream_rel(rel_for(folder, name), loop); }

static std::shared_ptr<Pcm> decode_pcm(std::unique_ptr<Stream> s) {
    if (!s) return nullptr;
    auto pcm = std::make_shared<Pcm>();
    pcm->rate = s->rate;
    pcm->channels = s->channels;
    if (s->total > 0) pcm->data.reserve((size_t)s->total * s->channels);
    char tmp[8192];
    for (;;) {
        long got = stream_decode(s.get(), tmp, sizeof(tmp));
        if (got <= 0) break;
        size_t n = (size_t)got / 2;
        size_t old = pcm->data.size();
        pcm->data.resize(old + n);
        std::memcpy(&pcm->data[old], tmp, n * 2);
    }
    pcm->frames = pcm->data.size() / std::max(1, pcm->channels);
    return pcm;
}

static void cache_se(const std::string& name, const std::shared_ptr<Pcm>& pcm) {
    if (!pcm || g_se_cache.count(name)) return;
    g_se_cache[name] = pcm;
    g_se_order.push_back(name);
    g_se_bytes += pcm->data.size() * 2;
    while (g_se_bytes > (2u << 20) && g_se_order.size() > 1) {
        std::string victim = g_se_order.front();
        g_se_order.erase(g_se_order.begin());
        auto v = g_se_cache.find(victim);
        if (v != g_se_cache.end()) {
            g_se_bytes -= v->second->data.size() * 2;
            g_se_cache.erase(v);
        }
    }
}

static int read_stream(Stream* s, int16_t* out, int want) {
    int ch = s->channels;
    int len = 0;
    char tmp[1024 * 2 * 2];
    while (len < want) {
        if (s->loop && s->loop_end > 0 && s->next_frame >= s->loop_end) {
            stream_seek(s, s->loop_start);
            s->next_frame = s->loop_start;
        }
        int64_t limit = (s->loop && s->loop_end > 0) ? s->loop_end - s->next_frame : want;
        int frames = (int)std::min<int64_t>(want - len, std::max<int64_t>(limit, 1));
        frames = std::min(frames, (int)(sizeof(tmp) / (2 * std::max(1, ch))));
        long got = stream_decode(s, tmp, frames * ch * 2);
        if (got <= 0) {
            if (s->loop) {
                int64_t target = s->loop_end > 0 ? s->loop_start : 0;
                if (stream_seek(s, target) != 0) return len;
                s->next_frame = target;
                if (s->total <= 0) return len;
                continue;
            }
            return len;
        }
        int n = (int)(got / (2 * ch));
        const int16_t* src = (const int16_t*)tmp;
        for (int i = 0; i < n; i++) {
            out[(len + i) * 2] = src[i * ch];
            out[(len + i) * 2 + 1] = ch > 1 ? src[i * ch + 1] : src[i * ch];
        }
        len += n;
        s->next_frame += n;
    }
    return len;
}

static bool refill(Voice& v) {
    if (v.pcm) {
        if (v.buf_start + v.buf_len >= (int64_t)v.pcm->frames) return false;
        v.buf_start += v.buf_len;
        int64_t remain = (int64_t)v.pcm->frames - v.buf_start;
        int n = (int)std::min<int64_t>(remain, 4096);
        int ch = v.pcm->channels;
        for (int i = 0; i < n; i++) {
            int16_t l = v.pcm->data[(size_t)(v.buf_start + i) * ch];
            int16_t r = ch > 1 ? v.pcm->data[(size_t)(v.buf_start + i) * ch + 1] : l;
            v.buf[i * 2] = l;
            v.buf[i * 2 + 1] = r;
        }
        v.buf_len = n;
        v.buf_pos = 0;
        return n > 0;
    }
    Stream* s = v.stream.get();
    if (!s) return false;
    v.buf_start += v.buf_len;
    v.buf_pos = 0;
    v.buf_len = read_stream(s, v.buf, 1024);
    if (v.capture && !v.capture_done) {
        if (v.buf_len > 0) v.capture->data.insert(v.capture->data.end(), v.buf, v.buf + v.buf_len * 2);
        else v.capture_done = true;
    }
    return v.buf_len > 0;
}

static inline bool next_frame(Voice& v, int32_t& l, int32_t& r) {
    if (v.buf_pos >= v.buf_len) {
        if (!refill(v)) return false;
    }
    l = v.buf[v.buf_pos * 2];
    r = v.buf[v.buf_pos * 2 + 1];
    v.buf_pos++;
    return true;
}

static void mix_voice(Voice& v, int32_t* acc, int frames, int kind_vol) {
    if (!v.active) return;
    int32_t base = (int32_t)(((int64_t)v.vol_q * std::clamp(kind_vol, 0, 100)) / 100);
    int32_t kl = (int32_t)(((int64_t)base * v.pan_l) >> 15);
    int32_t kr = (int32_t)(((int64_t)base * v.pan_r) >> 15);
    if (!v.primed) {
        if (!next_frame(v, v.prev_l, v.prev_r) || !next_frame(v, v.cur_l, v.cur_r)) {
            v.active = false;
            return;
        }
        v.primed = true;
        v.frac = 0;
    }
    for (int i = 0; i < frames; i++) {
        int32_t t = (int32_t)(v.frac >> 2);
        int32_t l = v.prev_l + (((v.cur_l - v.prev_l) * t) >> 14);
        int32_t r = v.prev_r + (((v.cur_r - v.prev_r) * t) >> 14);
        int32_t g = v.gain >> 9;
        int32_t gl = (int32_t)(((int64_t)kl * g) >> 15);
        int32_t gr = (int32_t)(((int64_t)kr * g) >> 15);
        acc[i * 2] += (int32_t)(((int64_t)l * gl) >> 15);
        acc[i * 2 + 1] += (int32_t)(((int64_t)r * gr) >> 15);
        if (v.gain_step != 0) {
            v.gain += v.gain_step;
            if ((v.gain_step > 0 && v.gain >= v.gain_target) || (v.gain_step < 0 && v.gain <= v.gain_target)) {
                v.gain = v.gain_target;
                v.gain_step = 0;
                if (v.stop_after_fade) {
                    v.active = false;
                    return;
                }
            }
        }
        v.frac += v.step;
        while (v.frac >= 65536) {
            v.frac -= 65536;
            v.prev_l = v.cur_l;
            v.prev_r = v.cur_r;
            v.played++;
            if (!next_frame(v, v.cur_l, v.cur_r)) {
                v.active = false;
                return;
            }
        }
    }
}

static double g_mix_time = 0;
static double g_mix_max = 0;
static int g_mix_count = 0;

double take_mix_stats(double& max_us) {
    double avg = g_mix_count ? g_mix_time / g_mix_count : 0;
    max_us = g_mix_max;
    g_mix_time = 0;
    g_mix_max = 0;
    g_mix_count = 0;
    return avg;
}

static void mix_inner(int16_t* out, int frames);

static std::vector<int16_t> g_dump;
static size_t g_dump_cap = 0;

void write_dump(const std::string& path) {
    if (g_dump.empty()) return;
    fs::write_file(path, g_dump.data(), g_dump.size() * sizeof(int16_t));
}

static void mix(int16_t* out, int frames) {
    bool perf = plat::options().perf;
    double t0 = perf ? plat::time_seconds() : 0;
    mix_inner(out, frames);
    if (g_dump_cap && g_dump.size() + (size_t)frames * 2 <= g_dump_cap) g_dump.insert(g_dump.end(), out, out + frames * 2);
    if (perf) {
        double us = (plat::time_seconds() - t0) * 1e6;
        g_mix_time += us;
        if (us > g_mix_max) g_mix_max = us;
        g_mix_count++;
    }
}

static void mix_inner(int16_t* out, int frames) {
    static int32_t acc[audio_out::BLOCK * 2 * 2];
    int n = std::min(frames, audio_out::BLOCK * 2);
    std::memset(acc, 0, sizeof(int32_t) * n * 2);
    mix_voice(g_bgm, acc, n, g_vol[K_BGM]);
    mix_voice(g_bgs, acc, n, g_vol[K_BGS]);
    mix_voice(g_danger, acc, n, g_vol[K_BGS]);
    mix_voice(g_me, acc, n, g_vol[K_ME]);
    for (int i = 0; i < EXTRA_BUFFERS; i++) {
        const std::string& f = g_extra_folder[i];
        int k = f == "bgm" ? K_BGM : f == "bgs" ? K_BGS : f == "me" ? K_ME : K_SE;
        mix_voice(g_extra[i], acc, n, g_vol[k]);
    }
    for (auto& v : g_se) mix_voice(v, acc, n, g_vol[K_SE]);
    for (int i = 0; i < MOVIE_VOICES; i++) {
        mix_voice(g_movie[i], acc, n, g_vol[K_BGM]);
        g_movie_played[i] = (uint32_t)g_movie[i].played;
    }
    for (int i = 0; i < n * 2; i++) out[i] = (int16_t)std::clamp<int32_t>(acc[i], -32768, 32767);
    for (int i = n * 2; i < frames * 2; i++) out[i] = 0;
}

static void reset_voice(Voice& v) {
    v.pending = 0;
    v.active = false;
    v.stream.reset();
    v.pcm.reset();
    v.capture.reset();
    v.capture_done = false;
    v.buf_len = v.buf_pos = 0;
    v.buf_start = 0;
    v.primed = false;
    v.gain = 1 << 24;
    v.gain_target = 1 << 24;
    v.gain_step = 0;
    v.stop_after_fade = false;
    v.played = 0;
}

static void apply_params(Voice& v, const Track& t, int rate) {
    v.vol_q = std::clamp(t.volume, 0, 100) * 32768 / 100;
    int pan = std::clamp(t.pan, -100, 100);
    v.pan_l = pan > 0 ? (100 - pan) * 32768 / 100 : 32768;
    v.pan_r = pan < 0 ? (100 + pan) * 32768 / 100 : 32768;
    v.step = (uint32_t)((double)rate / audio_out::RATE * (std::max(10, t.pitch) / 100.0) * 65536.0 + 0.5);
    if (v.step == 0) v.step = 1;
}

struct LoadJob {
    uint32_t token = 0;
    Voice* voice = nullptr;
    std::string rel;
    std::string se_name;
    bool loop = false;
    double pos = 0;
};

struct LoadResult {
    uint32_t token = 0;
    Voice* voice = nullptr;
    std::string se_name;
    std::unique_ptr<Stream> stream;
    std::shared_ptr<Pcm> pcm;
    int64_t start_frame = 0;
    std::vector<int16_t> pre;
    int pre_len = 0;
};

static plat::Mutex* g_load_mutex = nullptr;
static std::vector<LoadJob> g_load_jobs;
static std::vector<LoadResult> g_load_results;
static uint32_t g_load_token = 0;
static std::unordered_map<std::string, std::vector<Track>> g_se_waiting;

constexpr int64_t SE_DECODE_BYTES = 48 * 1024;
constexpr size_t SE_CAPTURE_BYTES = 512 * 1024;

static void open_job(const LoadJob& job, LoadResult& r, std::unique_ptr<Stream> s) {
    if (!s) return;
    if (job.pos > 0) {
        int64_t frame = (int64_t)(job.pos * s->rate);
        if (s->total > 0) {
            if (s->loop_end > 0 && frame >= s->loop_end) frame = s->loop_start + (frame - s->loop_start) % std::max<int64_t>(1, s->loop_end - s->loop_start);
            frame %= s->total;
        }
        if (stream_seek(s.get(), frame) == 0) {
            s->next_frame = frame;
            r.start_frame = frame;
        }
    }
    r.pre.resize(1024 * 2);
    r.pre_len = read_stream(s.get(), r.pre.data(), 1024);
    r.stream = std::move(s);
}

static void loader() {
    for (;;) {
        plat::mutex_lock(g_load_mutex);
        if (g_load_jobs.empty()) {
            plat::mutex_unlock(g_load_mutex);
            plat::sleep_ms(4);
            continue;
        }
        LoadJob job = std::move(g_load_jobs.front());
        g_load_jobs.erase(g_load_jobs.begin());
        plat::mutex_unlock(g_load_mutex);
        LoadResult r;
        r.token = job.token;
        r.voice = job.voice;
        r.se_name = job.se_name;
        try {
            if (job.voice) {
                open_job(job, r, open_stream_rel(job.rel, job.loop));
            } else {
                std::unique_ptr<Stream> s = open_stream_rel(job.rel, false);
                if (s && s->bytes > SE_DECODE_BYTES) open_job(job, r, std::move(s));
                else r.pcm = decode_pcm(std::move(s));
            }
        } catch (const std::bad_alloc&) {
            r.stream.reset();
            r.pcm.reset();
            r.pre.clear();
            r.pre_len = 0;
            plat::log("audio %s skipped: out of memory", job.rel.c_str());
        }
        plat::mutex_lock(g_load_mutex);
        g_load_results.push_back(std::move(r));
        plat::mutex_unlock(g_load_mutex);
    }
}

static void enqueue(LoadJob job) {
    if (!g_load_mutex) {
        g_load_mutex = plat::mutex_create();
        plat::start_thread(loader);
    }
    plat::mutex_lock(g_load_mutex);
    g_load_jobs.push_back(std::move(job));
    plat::mutex_unlock(g_load_mutex);
}

static bool busy(const Voice& v) { return v.active || v.pending != 0; }

static void start_stream(Voice& v, int kind, const char* folder, const Track& t, double pos, bool loop) {
    audio_out::lock();
    reset_voice(v);
    v.kind = kind;
    v.folder = folder;
    v.name = t.name;
    v.loop = loop;
    audio_out::unlock();
    v.pending = ++g_load_token;
    v.pending_track = t;
    v.pending_pos = pos;
    v.pending_fade_in = -1;
    v.pending_gain = 1 << 24;
    LoadJob job;
    job.token = v.pending;
    job.voice = &v;
    job.rel = rel_for(folder, t.name);
    job.loop = loop;
    job.pos = pos;
    enqueue(std::move(job));
}

static void fade(Voice& v, double seconds, float target_f, bool stop);

static void install(LoadResult& r) {
    Voice& v = *r.voice;
    if (v.pending != r.token) return;
    v.pending = 0;
    if (!r.stream) return;
    audio_out::lock();
    v.buf_start = r.start_frame;
    v.played = r.start_frame;
    std::memcpy(v.buf, r.pre.data(), sizeof(int16_t) * 2 * r.pre_len);
    v.buf_len = r.pre_len;
    v.buf_pos = 0;
    apply_params(v, v.pending_track, r.stream->rate);
    v.gain = v.pending_fade_in >= 0 ? 0 : v.pending_gain;
    v.stream = std::move(r.stream);
    v.active = r.pre_len > 0;
    audio_out::unlock();
    if (v.pending_fade_in >= 0 && v.active) fade(v, v.pending_fade_in, 1, false);
}

static void fade(Voice& v, double seconds, float target_f, bool stop) {
    int32_t target = (int32_t)(target_f * (1 << 24));
    audio_out::lock();
    if (seconds <= 0) {
        v.gain = target;
        v.gain_step = 0;
        if (stop) v.active = false;
    } else {
        v.gain_target = target;
        double samples = seconds * audio_out::RATE;
        int32_t step = (int32_t)((double)(target - v.gain) / (samples > 1 ? samples : 1));
        if (step == 0) step = target > v.gain ? 1 : -1;
        if (target == v.gain) step = 0;
        v.gain_step = step;
        v.stop_after_fade = stop;
        if (v.gain_step == 0 && stop) v.active = false;
    }
    audio_out::unlock();
}

static double voice_pos(const Voice& v) {
    if (v.pending) return v.pending_pos;
    if (!v.stream) return 0;
    double p = (double)v.played / v.stream->rate;
    return p;
}

void init() {
    if (g_inited) return;
    g_inited = true;
    if (plat::options().dump_audio > 0) {
        g_dump_cap = (size_t)plat::options().dump_audio * audio_out::RATE * 2;
        g_dump.reserve(g_dump_cap);
    }
    audio_out::start(mix);
}

void shutdown() {
    audio_out::stop();
    reset_voice(g_bgm);
    reset_voice(g_bgs);
    reset_voice(g_me);
    reset_voice(g_danger);
    for (auto& v : g_extra) reset_voice(v);
    for (auto& v : g_movie) reset_voice(v);
    for (auto& v : g_se) reset_voice(v);
    g_se_cache.clear();
}

void set_volumes(int bgm, int bgs, int me, int se) {
    audio_out::lock();
    g_vol[K_BGM] = bgm;
    g_vol[K_BGS] = bgs;
    g_vol[K_ME] = me;
    g_vol[K_SE] = se;
    audio_out::unlock();
}

void play_bgm(const Track& t, double pos) {
    if (g_bgm_valid && g_cur_bgm.name == t.name && !t.name.empty() && (busy(g_bgm) || g_bgm_held)) {
        if (g_bgm.active) {
            audio_out::lock();
            int rate = g_bgm.stream ? g_bgm.stream->rate : 44100;
            apply_params(g_bgm, t, rate);
            audio_out::unlock();
        } else if (g_bgm.pending) {
            g_bgm.pending_track = t;
        }
    } else {
        stop_bgm();
        if (!t.name.empty()) {
            if (g_me_valid) g_bgm_held = true;
            else start_stream(g_bgm, K_BGM, "bgm", t, pos, true);
        }
    }
    g_cur_bgm = t;
    g_cur_bgm.pos = pos;
    g_bgm_valid = !t.name.empty();
}

void stop_bgm() {
    g_bgm_once = false;
    audio_out::lock();
    reset_voice(g_bgm);
    audio_out::unlock();
    g_bgm_held = false;
    g_bgm_valid = false;
    g_cur_bgm = Track();
}

void fadeout_bgm(double seconds) {
    if (g_bgm_valid && (busy(g_bgm) || g_bgm_held)) {
        if (g_bgm.active) fade(g_bgm, seconds, 0, true);
        else if (g_bgm.pending) g_bgm.pending = 0;
        g_bgm_held = false;
        g_bgm_valid = false;
        g_cur_bgm = Track();
    }
}

void fadein_bgm(double seconds) {
    if (g_bgm.pending) {
        g_bgm.pending_fade_in = seconds;
        return;
    }
    if (!g_bgm.active) return;
    audio_out::lock();
    g_bgm.gain = 0;
    audio_out::unlock();
    fade(g_bgm, seconds, 1, false);
}

bool bgm_playing() { return g_bgm.active; }

Track current_bgm() { return g_bgm_valid ? g_cur_bgm : Track(); }

Track save_bgm() {
    if (!g_bgm_valid) {
        Track t;
        t.volume = 0;
        return t;
    }
    Track t = g_cur_bgm;
    if (!g_bgm_held) t.pos = voice_pos(g_bgm);
    return t;
}

void replay_bgm(const Track& t) {
    if (g_bgm_valid && g_cur_bgm.name == t.name) {
        play_bgm(t, g_cur_bgm.pos);
        return;
    }
    play_bgm(t, t.pos);
    if (busy(g_bgm)) fadein_bgm(0.5);
}

void play_bgs(const Track& t, double pos) {
    if (g_bgs_valid && g_cur_bgs.name == t.name && !t.name.empty() && busy(g_bgs)) {
        if (g_bgs.pending) {
            g_bgs.pending_track = t;
        } else {
            audio_out::lock();
            int rate = g_bgs.stream ? g_bgs.stream->rate : 44100;
            apply_params(g_bgs, t, rate);
            audio_out::unlock();
        }
    } else {
        stop_bgs();
        if (!t.name.empty()) start_stream(g_bgs, K_BGS, "bgs", t, pos, true);
    }
    g_cur_bgs = t;
    g_bgs_valid = !t.name.empty();
}

void stop_bgs() {
    audio_out::lock();
    reset_voice(g_bgs);
    audio_out::unlock();
    g_bgs_valid = false;
    g_cur_bgs = Track();
}

void fadeout_bgs(double seconds) {
    if (busy(g_bgs) && g_bgs_valid) {
        if (g_bgs.pending) g_bgs.pending = 0;
        else fade(g_bgs, seconds, 0, true);
        g_bgs_valid = false;
        g_cur_bgs = Track();
    }
}

Track current_bgs() { return g_bgs_valid ? g_cur_bgs : Track(); }

static void end_me(bool resume) {
    audio_out::lock();
    reset_voice(g_me);
    audio_out::unlock();
    if (g_me_valid && resume && g_bgm_held && g_bgm_valid) {
        g_bgm_held = false;
        start_stream(g_bgm, K_BGM, "bgm", g_cur_bgm, g_cur_bgm.pos, true);
        fadein_bgm(0.5);
    }
    g_me_valid = false;
}

void play_me(const Track& t) {
    end_me(t.name.empty());
    if (t.name.empty()) return;
    if (g_bgm_valid && busy(g_bgm)) {
        g_cur_bgm.pos = voice_pos(g_bgm);
        audio_out::lock();
        reset_voice(g_bgm);
        audio_out::unlock();
        g_bgm_held = true;
    }
    start_stream(g_me, K_ME, "me", t, 0, false);
    g_cur_me = t;
    g_me_valid = true;
}

void stop_me() { end_me(true); }

void fadeout_me(double seconds) {
    if (!g_me_valid) return;
    if (g_me.pending) g_me.pending = 0;
    else if (g_me.active) fade(g_me, seconds, 0, true);
}

bool me_playing() { return busy(g_me); }

static Voice* pick_se_slot() {
    for (auto& v : g_se) {
        if (!v.active) return &v;
    }
    Voice* slot = &g_se[0];
    for (auto& v : g_se) {
        if (v.played > slot->played) slot = &v;
    }
    return slot;
}

static void start_se_stream(LoadResult& r, const Track& t, int volume) {
    audio_out::lock();
    Voice* slot = pick_se_slot();
    reset_voice(*slot);
    slot->kind = K_SE;
    slot->name = t.name;
    std::memcpy(slot->buf, r.pre.data(), sizeof(int16_t) * 2 * r.pre_len);
    slot->buf_start = 0;
    slot->buf_len = r.pre_len;
    slot->buf_pos = 0;
    apply_params(*slot, t, r.stream->rate);
    slot->vol_q = std::max(0, volume) * 32768 / 100;
    if (r.stream->total > 0 && (size_t)r.stream->total * 4 <= SE_CAPTURE_BYTES) {
        slot->capture = std::make_shared<Pcm>();
        slot->capture->rate = r.stream->rate;
        slot->capture->channels = 2;
        slot->capture->data.reserve((size_t)r.stream->total * 2);
        slot->capture->data.assign(r.pre.begin(), r.pre.begin() + r.pre_len * 2);
    }
    slot->capture_done = false;
    slot->stream = std::move(r.stream);
    slot->active = r.pre_len > 0;
    audio_out::unlock();
}

static void harvest_se_captures() {
    for (auto& v : g_se) {
        std::shared_ptr<Pcm> done;
        std::string name;
        audio_out::lock();
        if (v.capture && v.capture_done) {
            done = std::move(v.capture);
            v.capture.reset();
            name = v.name;
        }
        audio_out::unlock();
        if (!done) continue;
        done->frames = done->data.size() / 2;
        cache_se(name, done);
    }
}

static void start_se(const std::shared_ptr<Pcm>& pcm, const Track& t) {
    audio_out::lock();
    Voice* slot = pick_se_slot();
    reset_voice(*slot);
    slot->kind = K_SE;
    slot->name = t.name;
    slot->pcm = pcm;
    slot->buf_start = 0;
    slot->buf_len = 0;
    apply_params(*slot, t, pcm->rate);
    {
        int n = (int)std::min<size_t>(pcm->frames, 4096);
        int ch = pcm->channels;
        for (int i = 0; i < n; i++) {
            int16_t l = pcm->data[(size_t)i * ch];
            slot->buf[i * 2] = l;
            slot->buf[i * 2 + 1] = ch > 1 ? pcm->data[(size_t)i * ch + 1] : l;
        }
        slot->buf_len = n;
        slot->buf_pos = 0;
    }
    slot->active = pcm->frames > 0;
    audio_out::unlock();
}

void play_se(const Track& t) {
    if (t.name.empty()) return;
    auto it = g_se_cache.find(t.name);
    if (it != g_se_cache.end()) {
        start_se(it->second, t);
        return;
    }
    auto w = g_se_waiting.find(t.name);
    if (w != g_se_waiting.end()) {
        w->second.push_back(t);
        return;
    }
    g_se_waiting[t.name].push_back(t);
    LoadJob job;
    job.se_name = t.name;
    job.rel = rel_for("se", t.name);
    enqueue(std::move(job));
}

bool se_playing() {
    for (auto& kv : g_se_waiting) {
        if (!kv.second.empty()) return true;
    }
    for (auto& v : g_se) {
        if (v.active) return true;
    }
    return false;
}

double bgm_position() {
    if (!g_bgm_valid || !busy(g_bgm)) return 0;
    return voice_pos(g_bgm);
}

void stop_se() {
    audio_out::lock();
    for (auto& v : g_se) reset_voice(v);
    audio_out::unlock();
    g_se_waiting.clear();
}

void stop_all() {
    stop_bgm();
    stop_bgs();
    stop_danger_bgs();
    clear_extra();
    stop_me();
    stop_se();
}

static void reopen(Voice& v) {
    if (!v.active || !v.stream) return;
    std::unique_ptr<Stream> s = v.folder.empty() ? open_stream_rel(v.name, v.loop) : open_stream(v.folder.c_str(), v.name, v.loop);
    if (!s) return;
    int64_t frame = v.buf_start + v.buf_pos;
    if (s->total > 0) frame %= s->total;
    if (stream_seek(s.get(), frame) != 0) frame = 0;
    s->next_frame = frame;
    audio_out::lock();
    v.stream = std::move(s);
    v.buf_start = frame;
    v.buf_len = 0;
    v.buf_pos = 0;
    audio_out::unlock();
}

void on_resume() {
    reopen(g_bgm);
    reopen(g_bgs);
    reopen(g_me);
    reopen(g_danger);
    for (auto& v : g_extra) reopen(v);
    for (auto& v : g_movie) reopen(v);
}

void loop_current_me() {
    if (!g_me_valid || !busy(g_me)) return;
    double pos = voice_pos(g_me);
    start_stream(g_me, K_ME, "me", g_cur_me, pos, true);
}

void play_current_bgm_once() {
    if (!g_bgm_valid || !busy(g_bgm)) return;
    double pos = voice_pos(g_bgm);
    int32_t gain = g_bgm.pending ? g_bgm.pending_gain : g_bgm.gain;
    double fade_in = g_bgm.pending ? g_bgm.pending_fade_in : -1;
    start_stream(g_bgm, K_BGM, "bgm", g_cur_bgm, pos, false);
    g_bgm.pending_gain = gain;
    g_bgm.pending_fade_in = fade_in;
    g_bgm_once = true;
    g_bgm_once_finished = false;
}

bool take_bgm_once_finished() {
    bool f = g_bgm_once_finished;
    g_bgm_once_finished = false;
    return f;
}

static void fade_to(Voice& v, int kind, const Track& t, double volume, double seconds) {
    if (!busy(v)) return;
    double base = std::clamp(t.volume, 0, 100) / 100.0 * std::clamp(g_vol[kind], 0, 100) / 100.0;
    double target = base > 0 ? (volume / 100.0) / base : 0;
    target = std::clamp(target, 0.0, 64.0);
    if (v.pending) {
        v.pending_gain = (int32_t)(target * (1 << 24));
        v.pending_fade_in = -1;
        return;
    }
    fade(v, seconds, (float)target, false);
}

void fade_bgm_to(double volume, double seconds) {
    if (g_bgm_valid) fade_to(g_bgm, K_BGM, g_cur_bgm, volume, seconds);
}

void fade_bgs_to(double volume, double seconds) {
    if (g_bgs_valid) fade_to(g_bgs, K_BGS, g_cur_bgs, volume, seconds);
}

void play_danger_bgs(const Track& t, double pos) {
    if (g_danger_valid && g_cur_danger.name == t.name && busy(g_danger)) {
        if (g_danger.pending) {
            g_danger.pending_track = t;
        } else {
            audio_out::lock();
            int rate = g_danger.stream ? g_danger.stream->rate : 44100;
            apply_params(g_danger, t, rate);
            audio_out::unlock();
        }
    } else {
        stop_danger_bgs();
        if (!t.name.empty()) start_stream(g_danger, K_BGS, "bgs", t, pos, true);
    }
    g_cur_danger = t;
    g_cur_danger.pos = pos;
    g_danger_valid = !t.name.empty();
}

void stop_danger_bgs() {
    audio_out::lock();
    reset_voice(g_danger);
    audio_out::unlock();
    g_danger_valid = false;
    g_cur_danger = Track();
}

void fadeout_danger_bgs(double seconds) {
    if (busy(g_danger) && g_danger_valid) {
        if (g_danger.pending) g_danger.pending = 0;
        else fade(g_danger, seconds, 0, true);
        g_danger_valid = false;
        g_cur_danger = Track();
    }
}

Track current_danger_bgs() { return g_danger_valid ? g_cur_danger : Track(); }

void play_extra(int index, const std::string& folder, const Track& t, double pos, bool loop) {
    if (index < 0 || index >= EXTRA_BUFFERS) return;
    Voice& v = g_extra[index];
    if (busy(v) && g_extra_folder[index] == folder) {
        if (v.pending) {
            v.pending_track = t;
            return;
        }
        audio_out::lock();
        int rate = v.stream ? v.stream->rate : 44100;
        apply_params(v, t, rate);
        audio_out::unlock();
        return;
    }
    stop_extra(index);
    if (t.name.empty()) return;
    g_extra_folder[index] = folder;
    start_stream(v, K_BGS, folder.c_str(), t, pos, loop);
    g_extra_once[index] = !loop;
}

void stop_extra(int index) {
    if (index < 0 || index >= EXTRA_BUFFERS) return;
    audio_out::lock();
    reset_voice(g_extra[index]);
    audio_out::unlock();
    g_extra_folder[index].clear();
    g_extra_once[index] = false;
}

void fadeout_extra(int index, double seconds) {
    if (index < 0 || index >= EXTRA_BUFFERS || !busy(g_extra[index])) return;
    if (g_extra[index].pending) {
        stop_extra(index);
        return;
    }
    fade(g_extra[index], seconds, 0, true);
}

void clear_extra() {
    for (int i = 0; i < EXTRA_BUFFERS; i++) stop_extra(i);
}

static void collect_loads() {
    if (!g_load_mutex) return;
    std::vector<LoadResult> ready;
    plat::mutex_lock(g_load_mutex);
    ready.swap(g_load_results);
    plat::mutex_unlock(g_load_mutex);
    for (LoadResult& r : ready) {
        if (r.voice) {
            install(r);
            continue;
        }
        cache_se(r.se_name, r.pcm);
        auto w = g_se_waiting.find(r.se_name);
        if (w == g_se_waiting.end()) continue;
        std::vector<Track> tracks = std::move(w->second);
        g_se_waiting.erase(w);
        if (r.pcm) {
            for (const Track& t : tracks) start_se(r.pcm, t);
        } else if (r.stream && !tracks.empty()) {
            const Track& first = tracks[0];
            int volume = 0;
            std::vector<Track> rest;
            for (const Track& t : tracks) {
                if (t.pitch == first.pitch && t.pan == first.pan) volume += std::clamp(t.volume, 0, 100);
                else rest.push_back(t);
            }
            start_se_stream(r, first, volume);
            for (const Track& t : rest) play_se(t);
        }
    }
}

void release_memory() {
    g_se_cache.clear();
    g_se_order.clear();
    g_se_bytes = 0;
}

void update() {
    collect_loads();
    harvest_se_captures();
    if (g_bgm_once && !busy(g_bgm)) {
        g_bgm_once = false;
        g_bgm_once_finished = true;
    }
    for (int i = 0; i < EXTRA_BUFFERS; i++) {
        if (g_extra_once[i] && !busy(g_extra[i])) stop_extra(i);
    }
    if (g_me_valid && !busy(g_me)) {
        stop_me();
    }
}

bool play_movie_audio(int slot, const std::string& rel, double pos) {
    if (slot < 0 || slot >= MOVIE_VOICES) return false;
    std::unique_ptr<Stream> s = open_stream_rel(rel, false);
    static int16_t pre[1024 * 2];
    int pre_len = 0;
    int64_t start = 0;
    if (s) {
        if (pos > 0) {
            int64_t frame = (int64_t)(pos * s->rate);
            if (s->total > 0 && frame > s->total) frame = s->total;
            if (stream_seek(s.get(), frame) == 0) {
                s->next_frame = frame;
                start = frame;
            }
        }
        pre_len = read_stream(s.get(), pre, 1024);
    }
    audio_out::lock();
    Voice& v = g_movie[slot];
    reset_voice(v);
    v.kind = K_BGM;
    v.folder.clear();
    v.name = rel;
    v.loop = false;
    bool ok = false;
    if (s) {
        v.buf_start = start;
        v.played = start;
        std::memcpy(v.buf, pre, sizeof(int16_t) * 2 * pre_len);
        v.buf_len = pre_len;
        v.buf_pos = 0;
        apply_params(v, Track(), s->rate);
        v.vol_q = 32768;
        g_movie_rate[slot] = s->rate;
        v.stream = std::move(s);
        v.active = pre_len > 0;
        ok = v.active;
    }
    g_movie_played[slot] = (uint32_t)start;
    audio_out::unlock();
    return ok;
}

void stop_movie_audio(int slot) {
    if (slot < 0 || slot >= MOVIE_VOICES) return;
    audio_out::lock();
    reset_voice(g_movie[slot]);
    audio_out::unlock();
}

bool movie_audio_active(int slot) { return slot >= 0 && slot < MOVIE_VOICES && g_movie[slot].active; }

double movie_audio_position(int slot) {
    if (slot < 0 || slot >= MOVIE_VOICES || g_movie_rate[slot] <= 0) return 0;
    return (double)g_movie_played[slot] / g_movie_rate[slot];
}

}
