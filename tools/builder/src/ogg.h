#ifndef TB_OGG_H
#define TB_OGG_H

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

typedef struct {
    Buf* out;
    uint32_t serial;
    uint32_t seq;
    Buf body;
    uint8_t lace[255];
    int nlace;
    int64_t granule;
    int completed;
    int continued;
    int first;
} OggWriter;

void ogg_writer_init(OggWriter* w, Buf* out, uint32_t serial);
void ogg_writer_packet(OggWriter* w, const uint8_t* data, size_t len, int64_t granule, int eos, int flush);
void ogg_writer_free(OggWriter* w);
uint32_t ogg_crc(const uint8_t* data, size_t len);

typedef struct {
    int channels;
    int rate;
    int bs0;
    int bs1;
    int mode_count;
    uint8_t mode_block[64];
} VorbisInfo;

int vorbis_split_private(const uint8_t* priv, size_t len, const uint8_t* hdr[3], size_t hlen[3]);
int vorbis_parse_info(const uint8_t* id, size_t idlen, const uint8_t* setup, size_t setuplen, VorbisInfo* vi);
int vorbis_packet_blocksize(const VorbisInfo* vi, const uint8_t* pkt, size_t len);
int ogg_vorbis_remux(const uint8_t* priv, size_t priv_len, const uint8_t* const* pkts, const size_t* lens, int n, uint32_t serial, Buf* out,
                     int64_t* samples_out, char* err, size_t errlen);

#endif
