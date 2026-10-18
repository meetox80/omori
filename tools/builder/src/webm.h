#ifndef TB_WEBM_H
#define TB_WEBM_H

#include <stddef.h>
#include <stdint.h>

enum { WEBM_VIDEO = 1, WEBM_AUDIO = 2 };

typedef struct {
    int number;
    int type;
    char codec[32];
    const uint8_t* priv;
    size_t priv_len;
    uint64_t default_duration_ns;
    uint64_t codec_delay_ns;
    uint64_t seek_preroll_ns;
    int width;
    int height;
    double rate;
    int channels;
    int encoded;
} WebmTrack;

typedef struct {
    int track;
    int64_t ts_ns;
    int64_t duration_ns;
    int64_t discard_ns;
    int key;
    const uint8_t* data;
    size_t len;
} WebmPacket;

typedef struct {
    WebmTrack* tracks;
    int ntracks;
    WebmPacket* packets;
    int npackets;
    int cap;
    uint64_t timecode_scale;
    double duration_ns;
} Webm;

int webm_parse(const uint8_t* data, size_t len, Webm* out, char* err, size_t errlen);
void webm_free(Webm* w);
const WebmTrack* webm_find_track(const Webm* w, int type);

#endif
