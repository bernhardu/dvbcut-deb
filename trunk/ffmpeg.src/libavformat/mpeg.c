/*
 * MPEG1/2 mux/demux
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"
#include "bitstream.h"

#define MAX_PAYLOAD_SIZE 4096
//#define DEBUG_SEEK

#undef NDEBUG
#include <assert.h>

typedef struct PacketDesc {
    int64_t pts;
    int64_t dts;
    int size;
    int unwritten_size;
    int flags;
    struct PacketDesc *next;
} PacketDesc;

typedef struct {
    FifoBuffer fifo;
    uint8_t id;
    int max_buffer_size; /* in bytes */
    int buffer_index;
    PacketDesc *predecode_packet;
    PacketDesc *premux_packet;
    PacketDesc **next_packet;
    int packet_number;
    uint8_t lpcm_header[3];
    int lpcm_align;
    uint8_t *fifo_iframe_ptr;
    int align_iframe;
    int64_t vobu_start_pts;
} StreamInfo;

typedef struct {
    int packet_size; /* required packet size */
    int packet_number;
    int pack_header_freq;     /* frequency (in packets^-1) at which we send pack headers */
    int system_header_freq;
    int system_header_size;
    int mux_rate; /* bitrate in units of 50 bytes/s */
    /* stream info */
    int audio_bound;
    int video_bound;
    int is_mpeg2;
    int is_vcd;
    int is_svcd;
    int is_dvd;
    int64_t last_scr; /* current system clock */

    double vcd_padding_bitrate; //FIXME floats
    int64_t vcd_padding_bytes_written;

} MpegMuxContext;

#define PACK_START_CODE             ((unsigned int)0x000001ba)
#define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
#define SEQUENCE_END_CODE           ((unsigned int)0x000001b7)
#define PACKET_START_CODE_MASK      ((unsigned int)0xffffff00)
#define PACKET_START_CODE_PREFIX    ((unsigned int)0x00000100)
#define ISO_11172_END_CODE          ((unsigned int)0x000001b9)
  
/* mpeg2 */
#define PROGRAM_STREAM_MAP 0x1bc
#define PRIVATE_STREAM_1   0x1bd
#define PADDING_STREAM     0x1be
#define PRIVATE_STREAM_2   0x1bf


#define AUDIO_ID 0xc0
#define VIDEO_ID 0xe0
#define AC3_ID   0x80
#define DTS_ID   0x8a
#define LPCM_ID  0xa0
#define SUB_ID   0x20

#define STREAM_TYPE_VIDEO_MPEG1     0x01
#define STREAM_TYPE_VIDEO_MPEG2     0x02
#define STREAM_TYPE_AUDIO_MPEG1     0x03
#define STREAM_TYPE_AUDIO_MPEG2     0x04
#define STREAM_TYPE_PRIVATE_SECTION 0x05
#define STREAM_TYPE_PRIVATE_DATA    0x06
#define STREAM_TYPE_AUDIO_AAC       0x0f
#define STREAM_TYPE_VIDEO_MPEG4     0x10
#define STREAM_TYPE_VIDEO_H264      0x1b

#define STREAM_TYPE_AUDIO_AC3       0x81
#define STREAM_TYPE_AUDIO_DTS       0x8a

static const int lpcm_freq_tab[4] = { 48000, 96000, 44100, 32000 };

#ifdef CONFIG_ENCODERS
static AVOutputFormat mpeg1system_mux;
static AVOutputFormat mpeg1vcd_mux;
static AVOutputFormat mpeg2vob_mux;
static AVOutputFormat mpeg2svcd_mux;
static AVOutputFormat mpeg2dvd_mux;

static int put_pack_header(AVFormatContext *ctx, 
                           uint8_t *buf, int64_t timestamp)
{
    MpegMuxContext *s = ctx->priv_data;
    PutBitContext pb;
    
    init_put_bits(&pb, buf, 128);

    put_bits(&pb, 32, PACK_START_CODE);
    if (s->is_mpeg2) {
        put_bits(&pb, 2, 0x1);
    } else {
        put_bits(&pb, 4, 0x2);
    }
    put_bits(&pb, 3, (uint32_t)((timestamp >> 30) & 0x07));
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (uint32_t)((timestamp >> 15) & 0x7fff));
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (uint32_t)((timestamp) & 0x7fff));
    put_bits(&pb, 1, 1);
    if (s->is_mpeg2) {
        /* clock extension */
        put_bits(&pb, 9, 0);
    }
    put_bits(&pb, 1, 1);
    put_bits(&pb, 22, s->mux_rate);
    put_bits(&pb, 1, 1);
    if (s->is_mpeg2) {
        put_bits(&pb, 1, 1);
        put_bits(&pb, 5, 0x1f); /* reserved */
        put_bits(&pb, 3, 0); /* stuffing length */
    }
    flush_put_bits(&pb);
    return pbBufPtr(&pb) - pb.buf;
}

static int put_system_header(AVFormatContext *ctx, uint8_t *buf,int only_for_stream_id)
{
    MpegMuxContext *s = ctx->priv_data;
    int size, i, private_stream_coded, id;
    PutBitContext pb;

    init_put_bits(&pb, buf, 128);

    put_bits(&pb, 32, SYSTEM_HEADER_START_CODE);
    put_bits(&pb, 16, 0);
    put_bits(&pb, 1, 1);
    
    put_bits(&pb, 22, s->mux_rate); /* maximum bit rate of the multiplexed stream */
    put_bits(&pb, 1, 1); /* marker */
    if (s->is_vcd && only_for_stream_id==VIDEO_ID) {
        /* This header applies only to the video stream (see VCD standard p. IV-7)*/
        put_bits(&pb, 6, 0);
    } else
        put_bits(&pb, 6, s->audio_bound);

    if (s->is_vcd) {
        /* see VCD standard, p. IV-7*/
        put_bits(&pb, 1, 0); 
        put_bits(&pb, 1, 1);
    } else {
        put_bits(&pb, 1, 0); /* variable bitrate*/
        put_bits(&pb, 1, 0); /* non constrainted bit stream */
    }
    
    if (s->is_vcd || s->is_dvd) {
        /* see VCD standard p IV-7 */
        put_bits(&pb, 1, 1); /* audio locked */
        put_bits(&pb, 1, 1); /* video locked */
    } else {
        put_bits(&pb, 1, 0); /* audio locked */
        put_bits(&pb, 1, 0); /* video locked */
    }

    put_bits(&pb, 1, 1); /* marker */

    if (s->is_vcd && only_for_stream_id==AUDIO_ID) {
        /* This header applies only to the audio stream (see VCD standard p. IV-7)*/
        put_bits(&pb, 5, 0);
    } else
        put_bits(&pb, 5, s->video_bound);
    
    if (s->is_dvd) {
        put_bits(&pb, 1, 0);    /* packet_rate_restriction_flag */
        put_bits(&pb, 7, 0x7f); /* reserved byte */
    } else
        put_bits(&pb, 8, 0xff); /* reserved byte */
    
    /* DVD-Video Stream_bound entries
    id (0xB9) video, maximum P-STD for stream 0xE0. (P-STD_buffer_bound_scale = 1) 
    id (0xB8) audio, maximum P-STD for any MPEG audio (0xC0 to 0xC7) streams. If there are none set to 4096 (32x128). (P-STD_buffer_bound_scale = 0) 
    id (0xBD) private stream 1 (audio other than MPEG and subpictures). (P-STD_buffer_bound_scale = 1) 
    id (0xBF) private stream 2, NAV packs, set to 2x1024. */
    if (s->is_dvd) {
        
        int P_STD_max_video = 0;
        int P_STD_max_mpeg_audio = 0;
        int P_STD_max_mpeg_PS1 = 0;
        
        for(i=0;i<ctx->nb_streams;i++) {
            StreamInfo *stream = ctx->streams[i]->priv_data;

            id = stream->id;
            if (id == 0xbd && stream->max_buffer_size > P_STD_max_mpeg_PS1) {
                P_STD_max_mpeg_PS1 = stream->max_buffer_size;
            } else if (id >= 0xc0 && id <= 0xc7 && stream->max_buffer_size > P_STD_max_mpeg_audio) {
                P_STD_max_mpeg_audio = stream->max_buffer_size;
            } else if (id == 0xe0 && stream->max_buffer_size > P_STD_max_video) {
                P_STD_max_video = stream->max_buffer_size;
            }
        }

        /* video */
        put_bits(&pb, 8, 0xb9); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 1);
        put_bits(&pb, 13, P_STD_max_video / 1024);

        /* audio */
        if (P_STD_max_mpeg_audio == 0)
            P_STD_max_mpeg_audio = 4096;
        put_bits(&pb, 8, 0xb8); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 0);
        put_bits(&pb, 13, P_STD_max_mpeg_audio / 128);

        /* private stream 1 */
        put_bits(&pb, 8, 0xbd); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 0);
        put_bits(&pb, 13, P_STD_max_mpeg_PS1 / 128);

        /* private stream 2 */
        put_bits(&pb, 8, 0xbf); /* stream ID */
        put_bits(&pb, 2, 3);
        put_bits(&pb, 1, 1);
        put_bits(&pb, 13, 2);
    }
    else {
        /* audio stream info */
        private_stream_coded = 0;
        for(i=0;i<ctx->nb_streams;i++) {
            StreamInfo *stream = ctx->streams[i]->priv_data;
            

            /* For VCDs, only include the stream info for the stream
            that the pack which contains this system belongs to.
            (see VCD standard p. IV-7) */
            if ( !s->is_vcd || stream->id==only_for_stream_id
                || only_for_stream_id==0) {

                id = stream->id;
                if (id < 0xc0) {
                    /* special case for private streams (AC3 use that) */
                    if (private_stream_coded)
                        continue;
                    private_stream_coded = 1;
                    id = 0xbd;
                }
                put_bits(&pb, 8, id); /* stream ID */
                put_bits(&pb, 2, 3);
                if (id < 0xe0) {
                    /* audio */
                    put_bits(&pb, 1, 0);
                    put_bits(&pb, 13, stream->max_buffer_size / 128);
                } else {
                    /* video */
                    put_bits(&pb, 1, 1);
                    put_bits(&pb, 13, stream->max_buffer_size / 1024);
                }
            }
        }
    }

    flush_put_bits(&pb);
    size = pbBufPtr(&pb) - pb.buf;
    /* patch packet size */
    buf[4] = (size - 6) >> 8;
    buf[5] = (size - 6) & 0xff;

    return size;
}

static int get_system_header_size(AVFormatContext *ctx)
{
    int buf_index, i, private_stream_coded;
    StreamInfo *stream;
    MpegMuxContext *s = ctx->priv_data;

    if (s->is_dvd)
       return 18; // DVD-Video system headers are 18 bytes fixed length.

    buf_index = 12;
    private_stream_coded = 0;
    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;
        if (stream->id < 0xc0) {
            if (private_stream_coded)
                continue;
            private_stream_coded = 1;
        }
        buf_index += 3;
    }
    return buf_index;
}

static int mpeg_mux_init(AVFormatContext *ctx)
{
    MpegMuxContext *s = ctx->priv_data;
    int bitrate, i, mpa_id, mpv_id, mps_id, ac3_id, dts_id, lpcm_id, j;
    AVStream *st;
    StreamInfo *stream;
    int audio_bitrate;
    int video_bitrate;

    s->packet_number = 0;
    s->is_vcd = (ctx->oformat == &mpeg1vcd_mux);
    s->is_svcd = (ctx->oformat == &mpeg2svcd_mux);
    s->is_mpeg2 = (ctx->oformat == &mpeg2vob_mux || ctx->oformat == &mpeg2svcd_mux || ctx->oformat == &mpeg2dvd_mux);
    s->is_dvd = (ctx->oformat == &mpeg2dvd_mux);
    
    if(ctx->packet_size)
        s->packet_size = ctx->packet_size;
    else
        s->packet_size = 2048;
       
    s->vcd_padding_bytes_written = 0;
    s->vcd_padding_bitrate=0;
        
    s->audio_bound = 0;
    s->video_bound = 0;
    mpa_id = AUDIO_ID;
    ac3_id = AC3_ID;
    dts_id = DTS_ID;
    mpv_id = VIDEO_ID;
    mps_id = SUB_ID;
    lpcm_id = LPCM_ID;
    for(i=0;i<ctx->nb_streams;i++) {
        st = ctx->streams[i];
        stream = av_mallocz(sizeof(StreamInfo));
        if (!stream)
            goto fail;
        st->priv_data = stream;

        av_set_pts_info(st, 64, 1, 90000);

        switch(st->codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            if (st->codec->codec_id == CODEC_ID_AC3) {
                stream->id = ac3_id++;
            } else if (st->codec->codec_id == CODEC_ID_DTS) {
                stream->id = dts_id++;
            } else if (st->codec->codec_id == CODEC_ID_PCM_S16BE) {
                stream->id = lpcm_id++;
                for(j = 0; j < 4; j++) {
                    if (lpcm_freq_tab[j] == st->codec->sample_rate)
                        break;
                }
                if (j == 4)
                    goto fail;
                if (st->codec->channels > 8)
                    return -1;
                stream->lpcm_header[0] = 0x0c;
                stream->lpcm_header[1] = (st->codec->channels - 1) | (j << 4);
                stream->lpcm_header[2] = 0x80;
                stream->lpcm_align = st->codec->channels * 2;
            } else {
                stream->id = mpa_id++;
            }

            /* This value HAS to be used for VCD (see VCD standard, p. IV-7).
               Right now it is also used for everything else.*/
            stream->max_buffer_size = 4 * 1024; 
            s->audio_bound++;
            break;
        case CODEC_TYPE_VIDEO:
            stream->id = mpv_id++;
            if (st->codec->rc_buffer_size)
                stream->max_buffer_size = 6*1024 + st->codec->rc_buffer_size/8;
            else
                stream->max_buffer_size = 230*1024; //FIXME this is probably too small as default
#if 0
                /* see VCD standard, p. IV-7*/
                stream->max_buffer_size = 46 * 1024; 
            else
                /* This value HAS to be used for SVCD (see SVCD standard, p. 26 V.2.3.2).
                   Right now it is also used for everything else.*/
                stream->max_buffer_size = 230 * 1024; 
#endif
            s->video_bound++;
            break;
        case CODEC_TYPE_SUBTITLE:
            stream->id = mps_id++;
            stream->max_buffer_size = 16 * 1024;
            break;
        default:
            return -1;
        }
        fifo_init(&stream->fifo, 16);
    }
    bitrate = 0;
    audio_bitrate = 0;
    video_bitrate = 0;
    for(i=0;i<ctx->nb_streams;i++) {
        int codec_rate;
        st = ctx->streams[i];
        stream = (StreamInfo*) st->priv_data;

        if(st->codec->rc_max_rate || stream->id==VIDEO_ID)
            codec_rate= st->codec->rc_max_rate;
        else
            codec_rate= st->codec->bit_rate;
                
        if(!codec_rate)
            codec_rate= (1<<21)*8*50/ctx->nb_streams;
            
        bitrate += codec_rate;

        if (stream->id==AUDIO_ID)
            audio_bitrate += codec_rate;
        else if (stream->id==VIDEO_ID)
            video_bitrate += codec_rate;
    }
    
    if(ctx->mux_rate){
        s->mux_rate= (ctx->mux_rate + (8 * 50) - 1) / (8 * 50);
    } else {
        /* we increase slightly the bitrate to take into account the
           headers. XXX: compute it exactly */
        bitrate += bitrate*5/100;
        bitrate += 10000;
        s->mux_rate = (bitrate + (8 * 50) - 1) / (8 * 50);
    }

    if (s->is_vcd) {
        double overhead_rate;

        /* The VCD standard mandates that the mux_rate field is 3528
           (see standard p. IV-6).
           The value is actually "wrong", i.e. if you calculate
           it using the normal formula and the 75 sectors per second transfer
           rate you get a different value because the real pack size is 2324,
           not 2352. But the standard explicitly specifies that the mux_rate
           field in the header must have this value.*/
//        s->mux_rate=2352 * 75 / 50;    /* = 3528*/

        /* The VCD standard states that the muxed stream must be
           exactly 75 packs / second (the data rate of a single speed cdrom).
           Since the video bitrate (probably 1150000 bits/sec) will be below
           the theoretical maximum we have to add some padding packets
           to make up for the lower data rate.
           (cf. VCD standard p. IV-6 )*/

        /* Add the header overhead to the data rate.
           2279 data bytes per audio pack, 2294 data bytes per video pack*/
        overhead_rate = ((audio_bitrate / 8.0) / 2279) * (2324 - 2279);
        overhead_rate += ((video_bitrate / 8.0) / 2294) * (2324 - 2294);
        overhead_rate *= 8;
        
        /* Add padding so that the full bitrate is 2324*75 bytes/sec */
        s->vcd_padding_bitrate = 2324 * 75 * 8 - (bitrate + overhead_rate);
    }
    
    if (s->is_vcd || s->is_mpeg2)
        /* every packet */
        s->pack_header_freq = 1;
    else
        /* every 2 seconds */
        s->pack_header_freq = 2 * bitrate / s->packet_size / 8;

    /* the above seems to make pack_header_freq zero sometimes */
    if (s->pack_header_freq == 0)
       s->pack_header_freq = 1;
    
    if (s->is_mpeg2)
        /* every 200 packets. Need to look at the spec.  */
        s->system_header_freq = s->pack_header_freq * 40;
    else if (s->is_vcd)
        /* the standard mandates that there are only two system headers
           in the whole file: one in the first packet of each stream.
           (see standard p. IV-7 and IV-8) */
        s->system_header_freq = 0x7fffffff;
    else
        s->system_header_freq = s->pack_header_freq * 5;
    
    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;
        stream->packet_number = 0;
    }
    s->system_header_size = get_system_header_size(ctx);
    s->last_scr = 0;
    return 0;
 fail:
    for(i=0;i<ctx->nb_streams;i++) {
        av_free(ctx->streams[i]->priv_data);
    }
    return -ENOMEM;
}

static inline void put_timestamp(ByteIOContext *pb, int id, int64_t timestamp)
{
    put_byte(pb, 
             (id << 4) | 
             (((timestamp >> 30) & 0x07) << 1) | 
             1);
    put_be16(pb, (uint16_t)((((timestamp >> 15) & 0x7fff) << 1) | 1));
    put_be16(pb, (uint16_t)((((timestamp) & 0x7fff) << 1) | 1));
}


/* return the number of padding bytes that should be inserted into
   the multiplexed stream.*/
static int get_vcd_padding_size(AVFormatContext *ctx, int64_t pts)
{
    MpegMuxContext *s = ctx->priv_data;
    int pad_bytes = 0;

    if (s->vcd_padding_bitrate > 0 && pts!=AV_NOPTS_VALUE)
    {
        int64_t full_pad_bytes;
        
        full_pad_bytes = (int64_t)((s->vcd_padding_bitrate * (pts / 90000.0)) / 8.0); //FIXME this is wrong
        pad_bytes = (int) (full_pad_bytes - s->vcd_padding_bytes_written);

        if (pad_bytes<0)
            /* might happen if we have already padded to a later timestamp. This
               can occur if another stream has already advanced further.*/
            pad_bytes=0;
    }

    return pad_bytes;
}


#if 0 /* unused, remove? */
/* return the exact available payload size for the next packet for
   stream 'stream_index'. 'pts' and 'dts' are only used to know if
   timestamps are needed in the packet header. */
static int get_packet_payload_size(AVFormatContext *ctx, int stream_index,
                                   int64_t pts, int64_t dts)
{
    MpegMuxContext *s = ctx->priv_data;
    int buf_index;
    StreamInfo *stream;

    stream = ctx->streams[stream_index]->priv_data;

    buf_index = 0;
    if (((s->packet_number % s->pack_header_freq) == 0)) {
        /* pack header size */
        if (s->is_mpeg2) 
            buf_index += 14;
        else
            buf_index += 12;
        
        if (s->is_vcd) {
            /* there is exactly one system header for each stream in a VCD MPEG,
               One in the very first video packet and one in the very first
               audio packet (see VCD standard p. IV-7 and IV-8).*/
            
            if (stream->packet_number==0)
                /* The system headers refer only to the stream they occur in,
                   so they have a constant size.*/
                buf_index += 15;

        } else {            
            if ((s->packet_number % s->system_header_freq) == 0)
                buf_index += s->system_header_size;
        }
    }

    if ((s->is_vcd && stream->packet_number==0)
        || (s->is_svcd && s->packet_number==0))
        /* the first pack of each stream contains only the pack header,
           the system header and some padding (see VCD standard p. IV-6) 
           Add the padding size, so that the actual payload becomes 0.*/
        buf_index += s->packet_size - buf_index;
    else {
        /* packet header size */
        buf_index += 6;
        if (s->is_mpeg2) {
            buf_index += 3;
            if (stream->packet_number==0)
                buf_index += 3; /* PES extension */
            buf_index += 1;    /* obligatory stuffing byte */
        }
        if (pts != AV_NOPTS_VALUE) {
            if (dts != pts)
                buf_index += 5 + 5;
            else
                buf_index += 5;

        } else {
            if (!s->is_mpeg2)
                buf_index++;
        }
    
        if (stream->id < 0xc0) {
            /* AC3/LPCM private data header */
            buf_index += 4;
            if (stream->id >= 0xa0) {
                int n;
                buf_index += 3;
                /* NOTE: we round the payload size to an integer number of
                   LPCM samples */
                n = (s->packet_size - buf_index) % stream->lpcm_align;
                if (n)
                    buf_index += (stream->lpcm_align - n);
            }
        }

        if (s->is_vcd && stream->id == AUDIO_ID)
            /* The VCD standard demands that 20 zero bytes follow
               each audio packet (see standard p. IV-8).*/
            buf_index+=20;
    }
    return s->packet_size - buf_index; 
}
#endif

/* Write an MPEG padding packet header. */
static void put_padding_packet(AVFormatContext *ctx, ByteIOContext *pb,int packet_bytes)
{
    MpegMuxContext *s = ctx->priv_data;
    int i;
    
    put_be32(pb, PADDING_STREAM);
    put_be16(pb, packet_bytes - 6);
    if (!s->is_mpeg2) {
        put_byte(pb, 0x0f);
        packet_bytes -= 7;
    } else
        packet_bytes -= 6;

    for(i=0;i<packet_bytes;i++)
        put_byte(pb, 0xff);
}

static int get_nb_frames(AVFormatContext *ctx, StreamInfo *stream, int len){
    int nb_frames=0;
    PacketDesc *pkt_desc= stream->premux_packet;

    while(len>0){ 
        if(pkt_desc->size == pkt_desc->unwritten_size)
            nb_frames++;
        len -= pkt_desc->unwritten_size;
        pkt_desc= pkt_desc->next;
    }

    return nb_frames;
}

/* flush the packet on stream stream_index */
static int flush_packet(AVFormatContext *ctx, int stream_index, 
                         int64_t pts, int64_t dts, int64_t scr, int trailer_size)
{
    MpegMuxContext *s = ctx->priv_data;
    StreamInfo *stream = ctx->streams[stream_index]->priv_data;
    uint8_t *buf_ptr;
    int size, payload_size, startcode, id, stuffing_size, i, header_len;
    int packet_size;
    uint8_t buffer[128];
    int zero_trail_bytes = 0;
    int pad_packet_bytes = 0;
    int pes_flags;
    int general_pack = 0;  /*"general" pack without data specific to one stream?*/
    int nb_frames;
    
    id = stream->id;
    
#if 0
    printf("packet ID=%2x PTS=%0.3f\n", 
           id, pts / 90000.0);
#endif

    buf_ptr = buffer;

    if ((s->packet_number % s->pack_header_freq) == 0 || s->last_scr != scr) {
        /* output pack and systems header if needed */
        size = put_pack_header(ctx, buf_ptr, scr);
        buf_ptr += size;
        s->last_scr= scr;

        if (s->is_vcd) {
            /* there is exactly one system header for each stream in a VCD MPEG,
               One in the very first video packet and one in the very first
               audio packet (see VCD standard p. IV-7 and IV-8).*/
            
            if (stream->packet_number==0) {
                size = put_system_header(ctx, buf_ptr, id);
                buf_ptr += size;
            }
        } else if (s->is_dvd) {
            if (stream->align_iframe || s->packet_number == 0){
                int bytes_to_iframe;
                int PES_bytes_to_fill;
                if (stream->fifo_iframe_ptr >= stream->fifo.rptr) {
                    bytes_to_iframe = stream->fifo_iframe_ptr - stream->fifo.rptr;
                } else {
                    bytes_to_iframe = (stream->fifo.end - stream->fifo.rptr) + (stream->fifo_iframe_ptr - stream->fifo.buffer);
                }
                PES_bytes_to_fill = s->packet_size - size - 10;

                if (pts != AV_NOPTS_VALUE) {
                    if (dts != pts)
                        PES_bytes_to_fill -= 5 + 5;
                    else
                        PES_bytes_to_fill -= 5;
                }

                if (bytes_to_iframe == 0 || s->packet_number == 0) {
                    size = put_system_header(ctx, buf_ptr, 0);
                    buf_ptr += size;
                    size = buf_ptr - buffer;
                    put_buffer(&ctx->pb, buffer, size);

                    put_be32(&ctx->pb, PRIVATE_STREAM_2);
                    put_be16(&ctx->pb, 0x03d4);         // length
                    put_byte(&ctx->pb, 0x00);           // substream ID, 00=PCI
                    for (i = 0; i < 979; i++)
                        put_byte(&ctx->pb, 0x00);

                    put_be32(&ctx->pb, PRIVATE_STREAM_2);
                    put_be16(&ctx->pb, 0x03fa);         // length
                    put_byte(&ctx->pb, 0x01);           // substream ID, 01=DSI
                    for (i = 0; i < 1017; i++)
                        put_byte(&ctx->pb, 0x00);

                    memset(buffer, 0, 128);
                    buf_ptr = buffer;
                    s->packet_number++;
                    stream->align_iframe = 0;
                    scr += s->packet_size*90000LL / (s->mux_rate*50LL); //FIXME rounding and first few bytes of each packet
                    size = put_pack_header(ctx, buf_ptr, scr);
                    s->last_scr= scr;
                    buf_ptr += size;
                    /* GOP Start */
                } else if (bytes_to_iframe < PES_bytes_to_fill) {
                    pad_packet_bytes = PES_bytes_to_fill - bytes_to_iframe;
                }
            }
        } else {
            if ((s->packet_number % s->system_header_freq) == 0) {
                size = put_system_header(ctx, buf_ptr, 0);
                buf_ptr += size;
            }
        }
    }
    size = buf_ptr - buffer;
    put_buffer(&ctx->pb, buffer, size);

    packet_size = s->packet_size - size;

    if (s->is_vcd && id == AUDIO_ID)
        /* The VCD standard demands that 20 zero bytes follow
           each audio pack (see standard p. IV-8).*/
        zero_trail_bytes += 20;
            
    if ((s->is_vcd && stream->packet_number==0)
        || (s->is_svcd && s->packet_number==0)) {
        /* for VCD the first pack of each stream contains only the pack header,
           the system header and lots of padding (see VCD standard p. IV-6).
           In the case of an audio pack, 20 zero bytes are also added at
           the end.*/
        /* For SVCD we fill the very first pack to increase compatibility with
           some DVD players. Not mandated by the standard.*/
        if (s->is_svcd)
            general_pack = 1;    /* the system header refers to both streams and no stream data*/
        pad_packet_bytes = packet_size - zero_trail_bytes;
    }

    packet_size -= pad_packet_bytes + zero_trail_bytes;

    if (packet_size > 0) {

        /* packet header size */
        packet_size -= 6;
        
        /* packet header */
        if (s->is_mpeg2) {
            header_len = 3;
            if (stream->packet_number==0)
                header_len += 3; /* PES extension */
            header_len += 1; /* obligatory stuffing byte */
        } else {
            header_len = 0;
        }
        if (pts != AV_NOPTS_VALUE) {
            if (dts != pts)
                header_len += 5 + 5;
            else
                header_len += 5;
        } else {
            if (!s->is_mpeg2)
                header_len++;
        }

        payload_size = packet_size - header_len;
        if (id < 0xc0) {
            startcode = PRIVATE_STREAM_1;
            payload_size -= 1;
            if (id >= 0x40) {
                payload_size -= 3;
                if (id >= 0xa0)
                    payload_size -= 3;
            }
        } else {
            startcode = 0x100 + id;
        }

        stuffing_size = payload_size - fifo_size(&stream->fifo, stream->fifo.rptr);

        // first byte doesnt fit -> reset pts/dts + stuffing
        if(payload_size <= trailer_size && pts != AV_NOPTS_VALUE){
            int timestamp_len=0;
            if(dts != pts) 
                timestamp_len += 5;
            if(pts != AV_NOPTS_VALUE)
                timestamp_len += s->is_mpeg2 ? 5 : 4;
            pts=dts= AV_NOPTS_VALUE;
            header_len -= timestamp_len;
            if (s->is_dvd && stream->align_iframe) {
                pad_packet_bytes += timestamp_len;
                packet_size -= timestamp_len;
            } else {
                payload_size += timestamp_len;
            }
            stuffing_size += timestamp_len;
            if(payload_size > trailer_size)
                stuffing_size += payload_size - trailer_size;
        }

        if (pad_packet_bytes > 0 && pad_packet_bytes <= 7) { // can't use padding, so use stuffing
            packet_size += pad_packet_bytes;
            payload_size += pad_packet_bytes; // undo the previous adjustment
            if (stuffing_size < 0) {
                stuffing_size = pad_packet_bytes;
            } else {
                stuffing_size += pad_packet_bytes;
            }
            pad_packet_bytes = 0;
        }

        if (stuffing_size < 0)
            stuffing_size = 0;
        if (stuffing_size > 16) {    /*<=16 for MPEG-1, <=32 for MPEG-2*/
            pad_packet_bytes += stuffing_size;
            packet_size -= stuffing_size;
            payload_size -= stuffing_size;
            stuffing_size = 0;
        }
        
        nb_frames= get_nb_frames(ctx, stream, payload_size - stuffing_size);

        put_be32(&ctx->pb, startcode);

        put_be16(&ctx->pb, packet_size);
        
        if (!s->is_mpeg2)
            for(i=0;i<stuffing_size;i++)
                put_byte(&ctx->pb, 0xff);

        if (s->is_mpeg2) {
            put_byte(&ctx->pb, 0x80); /* mpeg2 id */

            pes_flags=0;

            if (pts != AV_NOPTS_VALUE) {
                pes_flags |= 0x80;
                if (dts != pts)
                    pes_flags |= 0x40;
            }

            /* Both the MPEG-2 and the SVCD standards demand that the
               P-STD_buffer_size field be included in the first packet of
               every stream. (see SVCD standard p. 26 V.2.3.1 and V.2.3.2
               and MPEG-2 standard 2.7.7) */
            if (stream->packet_number == 0)
                pes_flags |= 0x01;

            put_byte(&ctx->pb, pes_flags); /* flags */
            put_byte(&ctx->pb, header_len - 3 + stuffing_size);

            if (pes_flags & 0x80)  /*write pts*/
                put_timestamp(&ctx->pb, (pes_flags & 0x40) ? 0x03 : 0x02, pts);
            if (pes_flags & 0x40)  /*write dts*/
                put_timestamp(&ctx->pb, 0x01, dts);
            
            if (pes_flags & 0x01) {  /*write pes extension*/
                put_byte(&ctx->pb, 0x10); /* flags */

                /* P-STD buffer info */                
                if (id == AUDIO_ID)
                    put_be16(&ctx->pb, 0x4000 | stream->max_buffer_size/128);
                else
                    put_be16(&ctx->pb, 0x6000 | stream->max_buffer_size/1024);
            }

        } else {
            if (pts != AV_NOPTS_VALUE) {
                if (dts != pts) {
                    put_timestamp(&ctx->pb, 0x03, pts);
                    put_timestamp(&ctx->pb, 0x01, dts);
                } else {
                    put_timestamp(&ctx->pb, 0x02, pts);
                }
            } else {
                put_byte(&ctx->pb, 0x0f);
            }
        }

        if (s->is_mpeg2) {
            /* special stuffing byte that is always written
               to prevent accidental generation of start codes. */
            put_byte(&ctx->pb, 0xff);

            for(i=0;i<stuffing_size;i++)
                put_byte(&ctx->pb, 0xff);
        }

        if (startcode == PRIVATE_STREAM_1) {
            put_byte(&ctx->pb, id);
            if (id >= 0xa0) {
                /* LPCM (XXX: check nb_frames) */
                put_byte(&ctx->pb, 7);
                put_be16(&ctx->pb, 4); /* skip 3 header bytes */
                put_byte(&ctx->pb, stream->lpcm_header[0]);
                put_byte(&ctx->pb, stream->lpcm_header[1]);
                put_byte(&ctx->pb, stream->lpcm_header[2]);
            } else if (id >= 0x40) {
                /* AC3 */
                put_byte(&ctx->pb, nb_frames);
                put_be16(&ctx->pb, trailer_size+1);
            }
        }

        /* output data */
        if(put_fifo(&ctx->pb, &stream->fifo, payload_size - stuffing_size, &stream->fifo.rptr) < 0)
            return -1;
    }else{
        payload_size=
        stuffing_size= 0;
    }

    if (pad_packet_bytes > 0)
        put_padding_packet(ctx,&ctx->pb, pad_packet_bytes);    

    for(i=0;i<zero_trail_bytes;i++)
        put_byte(&ctx->pb, 0x00);
        
    put_flush_packet(&ctx->pb);
    
    s->packet_number++;

    /* only increase the stream packet number if this pack actually contains
       something that is specific to this stream! I.e. a dedicated header
       or some data.*/
    if (!general_pack)
        stream->packet_number++;
    
    return payload_size - stuffing_size;
}

static void put_vcd_padding_sector(AVFormatContext *ctx)
{
    /* There are two ways to do this padding: writing a sector/pack
       of 0 values, or writing an MPEG padding pack. Both seem to
       work with most decoders, BUT the VCD standard only allows a 0-sector
       (see standard p. IV-4, IV-5).
       So a 0-sector it is...*/

    MpegMuxContext *s = ctx->priv_data;
    int i;

    for(i=0;i<s->packet_size;i++)
        put_byte(&ctx->pb, 0);

    s->vcd_padding_bytes_written += s->packet_size;
        
    put_flush_packet(&ctx->pb);
    
    /* increasing the packet number is correct. The SCR of the following packs
       is calculated from the packet_number and it has to include the padding
       sector (it represents the sector index, not the MPEG pack index)
       (see VCD standard p. IV-6)*/
    s->packet_number++;
}

#if 0 /* unused, remove? */
static int64_t get_vcd_scr(AVFormatContext *ctx,int stream_index,int64_t pts)
{
    MpegMuxContext *s = ctx->priv_data;
    int64_t scr;

        /* Since the data delivery rate is constant, SCR is computed
           using the formula C + i * 1200 where C is the start constant
           and i is the pack index.
           It is recommended that SCR 0 is at the beginning of the VCD front
           margin (a sequence of empty Form 2 sectors on the CD).
           It is recommended that the front margin is 30 sectors long, so
           we use C = 30*1200 = 36000
           (Note that even if the front margin is not 30 sectors the file
           will still be correct according to the standard. It just won't have
           the "recommended" value).*/
        scr = 36000 + s->packet_number * 1200;

    return scr;
}    
#endif

static int remove_decoded_packets(AVFormatContext *ctx, int64_t scr){
//    MpegMuxContext *s = ctx->priv_data;
    int i;

    for(i=0; i<ctx->nb_streams; i++){
        AVStream *st = ctx->streams[i];
        StreamInfo *stream = st->priv_data;
        PacketDesc *pkt_desc= stream->predecode_packet;
        
        while(pkt_desc && scr > pkt_desc->dts){ //FIXME > vs >=
            if(stream->buffer_index < pkt_desc->size || 
               stream->predecode_packet == stream->premux_packet){
                av_log(ctx, AV_LOG_ERROR, "buffer underflow\n");
                break;
            }
            stream->buffer_index -= pkt_desc->size;

            stream->predecode_packet= pkt_desc->next;
            av_freep(&pkt_desc);
        }
    }
    
    return 0;
}

static int output_packet(AVFormatContext *ctx, int flush){
    MpegMuxContext *s = ctx->priv_data;
    AVStream *st;
    StreamInfo *stream;
    int i, avail_space, es_size, trailer_size;
    int best_i= -1;
    int best_score= INT_MIN;
    int ignore_constraints=0;
    int64_t scr= s->last_scr;
    PacketDesc *timestamp_packet;
    const int64_t max_delay= av_rescale(ctx->max_delay, 90000, AV_TIME_BASE);

retry:
    for(i=0; i<ctx->nb_streams; i++){
        AVStream *st = ctx->streams[i];
        StreamInfo *stream = st->priv_data;
        const int avail_data=  fifo_size(&stream->fifo, stream->fifo.rptr);
        const int space= stream->max_buffer_size - stream->buffer_index;
        int rel_space= 1024*space / stream->max_buffer_size;
        PacketDesc *next_pkt= stream->premux_packet;

        /* for subtitle, a single PES packet must be generated,
           so we flush after every single subtitle packet */
        if(s->packet_size > avail_data && !flush
           && st->codec->codec_type != CODEC_TYPE_SUBTITLE)
            return 0;
        if(avail_data==0)
            continue;
        assert(avail_data>0);

        if(space < s->packet_size && !ignore_constraints)
            continue;
            
        if(next_pkt && next_pkt->dts - scr > max_delay)
            continue;
            
        if(rel_space > best_score){
            best_score= rel_space;
            best_i = i;
            avail_space= space;
        }
    }
    
    if(best_i < 0){
        int64_t best_dts= INT64_MAX;

        for(i=0; i<ctx->nb_streams; i++){
            AVStream *st = ctx->streams[i];
            StreamInfo *stream = st->priv_data;
            PacketDesc *pkt_desc= stream->predecode_packet;
            if(pkt_desc && pkt_desc->dts < best_dts)
                best_dts= pkt_desc->dts;
        }

#if 0
        av_log(ctx, AV_LOG_DEBUG, "bumping scr, scr:%f, dts:%f\n", 
               scr/90000.0, best_dts/90000.0);
#endif
        if(best_dts == INT64_MAX)
            return 0;

        if(scr >= best_dts+1 && !ignore_constraints){
            av_log(ctx, AV_LOG_ERROR, "packet too large, ignoring buffer limits to mux it\n");
            ignore_constraints= 1;
        }
        scr= FFMAX(best_dts+1, scr);
        if(remove_decoded_packets(ctx, scr) < 0)
            return -1;
        goto retry;
    }

    assert(best_i >= 0);
    
    st = ctx->streams[best_i];
    stream = st->priv_data;
    
    assert(fifo_size(&stream->fifo, stream->fifo.rptr) > 0);

    assert(avail_space >= s->packet_size || ignore_constraints);
    
    timestamp_packet= stream->premux_packet;
    if(timestamp_packet->unwritten_size == timestamp_packet->size){
        trailer_size= 0;
    }else{
        trailer_size= timestamp_packet->unwritten_size;
        timestamp_packet= timestamp_packet->next;
    }

    if(timestamp_packet){
//av_log(ctx, AV_LOG_DEBUG, "dts:%f pts:%f scr:%f stream:%d\n", timestamp_packet->dts/90000.0, timestamp_packet->pts/90000.0, scr/90000.0, best_i);
        es_size= flush_packet(ctx, best_i, timestamp_packet->pts, timestamp_packet->dts, scr, trailer_size);
    }else{
        assert(fifo_size(&stream->fifo, stream->fifo.rptr) == trailer_size);
        es_size= flush_packet(ctx, best_i, AV_NOPTS_VALUE, AV_NOPTS_VALUE, scr, trailer_size);
    }

    if (s->is_vcd) {
        /* Write one or more padding sectors, if necessary, to reach
           the constant overall bitrate.*/
        int vcd_pad_bytes;

        while((vcd_pad_bytes = get_vcd_padding_size(ctx,stream->premux_packet->pts) ) >= s->packet_size){ //FIXME pts cannot be correct here
            put_vcd_padding_sector(ctx);
            s->last_scr += s->packet_size*90000LL / (s->mux_rate*50LL); //FIXME rounding and first few bytes of each packet
        }
    }
    
    stream->buffer_index += es_size;
    s->last_scr += s->packet_size*90000LL / (s->mux_rate*50LL); //FIXME rounding and first few bytes of each packet
    
    while(stream->premux_packet && stream->premux_packet->unwritten_size <= es_size){
        es_size -= stream->premux_packet->unwritten_size;
        stream->premux_packet= stream->premux_packet->next;
    }
    if(es_size)
        stream->premux_packet->unwritten_size -= es_size;
    
    if(remove_decoded_packets(ctx, s->last_scr) < 0)
        return -1;

    return 1;
}

static int mpeg_mux_write_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    MpegMuxContext *s = ctx->priv_data;
    int stream_index= pkt->stream_index;
    int size= pkt->size;
    uint8_t *buf= pkt->data;
    AVStream *st = ctx->streams[stream_index];
    StreamInfo *stream = st->priv_data;
    int64_t pts, dts;
    PacketDesc *pkt_desc;
    const int preload= av_rescale(ctx->preload, 90000, AV_TIME_BASE);
    const int is_iframe = st->codec->codec_type == CODEC_TYPE_VIDEO && (pkt->flags & PKT_FLAG_KEY);
    
    pts= pkt->pts;
    dts= pkt->dts;

    if(pts != AV_NOPTS_VALUE) pts += preload;
    if(dts != AV_NOPTS_VALUE) dts += preload;

//av_log(ctx, AV_LOG_DEBUG, "dts:%f pts:%f flags:%d stream:%d nopts:%d\n", dts/90000.0, pts/90000.0, pkt->flags, pkt->stream_index, pts != AV_NOPTS_VALUE);
    if (!stream->premux_packet)
        stream->next_packet = &stream->premux_packet;
    *stream->next_packet=
    pkt_desc= av_mallocz(sizeof(PacketDesc));
    pkt_desc->pts= pts;
    pkt_desc->dts= dts;
    pkt_desc->unwritten_size=
    pkt_desc->size= size;
    if(!stream->predecode_packet)
        stream->predecode_packet= pkt_desc;
    stream->next_packet= &pkt_desc->next;

    fifo_realloc(&stream->fifo, fifo_size(&stream->fifo, NULL) + size + 1);

    if (s->is_dvd){
        if (is_iframe && (s->packet_number == 0 || (pts - stream->vobu_start_pts >= 36000))) { // min VOBU length 0.4 seconds (mpucoder)
            stream->fifo_iframe_ptr = stream->fifo.wptr;
            stream->align_iframe = 1;
            stream->vobu_start_pts = pts;
        } else {
            stream->align_iframe = 0;
        }
    }

    fifo_write(&stream->fifo, buf, size, &stream->fifo.wptr);

    for(;;){
        int ret= output_packet(ctx, 0);
        if(ret<=0) 
            return ret;
    }
}

static int mpeg_mux_end(AVFormatContext *ctx)
{
//    MpegMuxContext *s = ctx->priv_data;
    StreamInfo *stream;
    int i;
    
    for(;;){
        int ret= output_packet(ctx, 1);
        if(ret<0) 
            return ret;
        else if(ret==0)
            break;
    }

    /* End header according to MPEG1 systems standard. We do not write
       it as it is usually not needed by decoders and because it
       complicates MPEG stream concatenation. */
    //put_be32(&ctx->pb, ISO_11172_END_CODE);
    //put_flush_packet(&ctx->pb);

    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;

        assert(fifo_size(&stream->fifo, stream->fifo.rptr) == 0);
        fifo_free(&stream->fifo);
    }
    return 0;
}
#endif //CONFIG_ENCODERS

/*********************************************/
/* demux code */

#define MAX_SYNC_SIZE 100000

static int mpegps_probe(AVProbeData *p)
{
    int i;
    int size= FFMIN(20, p->buf_size);
    uint32_t code=0xFF;

    /* we search the first start code. If it is a packet start code,
       then we decide it is mpeg ps. We do not send highest value to
       give a chance to mpegts */
    /* NOTE: the search range was restricted to avoid too many false
       detections */

    for (i = 0; i < size; i++) {
        code = (code << 8) | p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            if (code == PACK_START_CODE ||
                code == SYSTEM_HEADER_START_CODE ||
                (code >= 0x1e0 && code <= 0x1ef) ||
                (code >= 0x1c0 && code <= 0x1df) ||
                code == PRIVATE_STREAM_2 ||
                code == PROGRAM_STREAM_MAP ||
                code == PRIVATE_STREAM_1 ||
                code == PADDING_STREAM)
                return AVPROBE_SCORE_MAX - 2;
            else
                return 0;
        }
    }
    return 0;
}


typedef struct MpegDemuxContext {
    int header_state;
    unsigned char psm_es_type[256];
} MpegDemuxContext;

static int mpegps_read_header(AVFormatContext *s,
                              AVFormatParameters *ap)
{
    MpegDemuxContext *m = s->priv_data;
    m->header_state = 0xff;
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    /* no need to do more */
    return 0;
}

static int64_t get_pts(ByteIOContext *pb, int c)
{
    int64_t pts;
    int val;

    if (c < 0)
        c = get_byte(pb);
    pts = (int64_t)((c >> 1) & 0x07) << 30;
    val = get_be16(pb);
    pts |= (int64_t)(val >> 1) << 15;
    val = get_be16(pb);
    pts |= (int64_t)(val >> 1);
    return pts;
}

static int find_next_start_code(ByteIOContext *pb, int *size_ptr, 
                                uint32_t *header_state)
{
    unsigned int state, v;
    int val, n;

    state = *header_state;
    n = *size_ptr;
    while (n > 0) {
        if (url_feof(pb))
            break;
        v = get_byte(pb);
        n--;
        if (state == 0x000001) {
            state = ((state << 8) | v) & 0xffffff;
            val = state;
            goto found;
        }
        state = ((state << 8) | v) & 0xffffff;
    }
    val = -1;
 found:
    *header_state = state;
    *size_ptr = n;
    return val;
}

#if 0 /* unused, remove? */
/* XXX: optimize */
static int find_prev_start_code(ByteIOContext *pb, int *size_ptr)
{
    int64_t pos, pos_start;
    int max_size, start_code;

    max_size = *size_ptr;
    pos_start = url_ftell(pb);

    /* in order to go faster, we fill the buffer */
    pos = pos_start - 16386;
    if (pos < 0)
        pos = 0;
    url_fseek(pb, pos, SEEK_SET);
    get_byte(pb);

    pos = pos_start;
    for(;;) {
        pos--;
        if (pos < 0 || (pos_start - pos) >= max_size) {
            start_code = -1;
            goto the_end;
        }
        url_fseek(pb, pos, SEEK_SET);
        start_code = get_be32(pb);
        if ((start_code & 0xffffff00) == 0x100)
            break;
    }
 the_end:
    *size_ptr = pos_start - pos;
    return start_code;
}
#endif

/**
 * Extracts stream types from a program stream map
 * According to ISO/IEC 13818-1 ('MPEG-2 Systems') table 2-35
 * 
 * @return number of bytes occupied by PSM in the bitstream
 */
static long mpegps_psm_parse(MpegDemuxContext *m, ByteIOContext *pb)
{
    int psm_length, ps_info_length, es_map_length;

    psm_length = get_be16(pb);
    get_byte(pb);
    get_byte(pb);
    ps_info_length = get_be16(pb);

    /* skip program_stream_info */
    url_fskip(pb, ps_info_length);
    es_map_length = get_be16(pb);

    /* at least one es available? */
    while (es_map_length >= 4){
        unsigned char type = get_byte(pb);
        unsigned char es_id = get_byte(pb);
        uint16_t es_info_length = get_be16(pb);
        /* remember mapping from stream id to stream type */
        m->psm_es_type[es_id] = type;
        /* skip program_stream_info */
        url_fskip(pb, es_info_length);
        es_map_length -= 4 + es_info_length;
    }
    get_be32(pb); /* crc32 */
    return 2 + psm_length;
}

/* read the next PES header. Return its position in ppos 
   (if not NULL), and its start code, pts and dts.
 */
static int mpegps_read_pes_header(AVFormatContext *s,
                                  int64_t *ppos, int *pstart_code, 
                                  int64_t *ppts, int64_t *pdts)
{
    MpegDemuxContext *m = s->priv_data;
    int len, size, startcode, c, flags, header_len;
    int64_t pts, dts, last_pos;

    last_pos = -1;
 redo:
        /* next start code (should be immediately after) */
        m->header_state = 0xff;
        size = MAX_SYNC_SIZE;
        startcode = find_next_start_code(&s->pb, &size, &m->header_state);
    //printf("startcode=%x pos=0x%Lx\n", startcode, url_ftell(&s->pb));
    if (startcode < 0)
        return AVERROR_IO;
    if (startcode == PACK_START_CODE)
        goto redo;
    if (startcode == SYSTEM_HEADER_START_CODE)
        goto redo;
    if (startcode == PADDING_STREAM ||
        startcode == PRIVATE_STREAM_2) {
        /* skip them */
        len = get_be16(&s->pb);
        url_fskip(&s->pb, len);
        goto redo;
    }
    if (startcode == PROGRAM_STREAM_MAP) {
        mpegps_psm_parse(m, &s->pb);
        goto redo;
    }
    
    /* find matching stream */
    if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||
          (startcode >= 0x1e0 && startcode <= 0x1ef) ||
          (startcode == 0x1bd)))
        goto redo;
    if (ppos) {
        *ppos = url_ftell(&s->pb) - 4;
    }
    len = get_be16(&s->pb);
    pts = AV_NOPTS_VALUE;
    dts = AV_NOPTS_VALUE;
    /* stuffing */
    for(;;) {
        if (len < 1)
            goto redo;
        c = get_byte(&s->pb);
        len--;
        /* XXX: for mpeg1, should test only bit 7 */
        if (c != 0xff) 
            break;
    }
    if ((c & 0xc0) == 0x40) {
        /* buffer scale & size */
        if (len < 2)
            goto redo;
        get_byte(&s->pb);
        c = get_byte(&s->pb);
        len -= 2;
    }
    if ((c & 0xf0) == 0x20) {
        if (len < 4)
            goto redo;
        dts = pts = get_pts(&s->pb, c);
        len -= 4;
    } else if ((c & 0xf0) == 0x30) {
        if (len < 9)
            goto redo;
        pts = get_pts(&s->pb, c);
        dts = get_pts(&s->pb, -1);
        len -= 9;
    } else if ((c & 0xc0) == 0x80) {
        /* mpeg 2 PES */
        if ((c & 0x30) != 0) {
            /* Encrypted multiplex not handled */
            goto redo;
        }
        flags = get_byte(&s->pb);
        header_len = get_byte(&s->pb);
        len -= 2;
        if (header_len > len)
            goto redo;
        if ((flags & 0xc0) == 0x80) {
            dts = pts = get_pts(&s->pb, -1);
            if (header_len < 5)
                goto redo;
            header_len -= 5;
            len -= 5;
        } if ((flags & 0xc0) == 0xc0) {
            pts = get_pts(&s->pb, -1);
            dts = get_pts(&s->pb, -1);
            if (header_len < 10)
                goto redo;
            header_len -= 10;
            len -= 10;
        }
        len -= header_len;
        while (header_len > 0) {
            get_byte(&s->pb);
            header_len--;
        }
    }
    else if( c!= 0xf )
        goto redo;

    if (startcode == PRIVATE_STREAM_1 && !m->psm_es_type[startcode & 0xff]) {
        if (len < 1)
            goto redo;
        startcode = get_byte(&s->pb);
        len--;
        if (startcode >= 0x80 && startcode <= 0xbf) {
            /* audio: skip header */
            if (len < 3)
                goto redo;
            get_byte(&s->pb);
            get_byte(&s->pb);
            get_byte(&s->pb);
            len -= 3;
        }
    }
    if(dts != AV_NOPTS_VALUE && ppos){
        int i;
        for(i=0; i<s->nb_streams; i++){
            if(startcode == s->streams[i]->id) {
                av_add_index_entry(s->streams[i], *ppos, dts, 0, AVINDEX_KEYFRAME /* FIXME keyframe? */);
            }
        }
    }
    
    *pstart_code = startcode;
    *ppts = pts;
    *pdts = dts;
    return len;
}

static int mpegps_read_packet(AVFormatContext *s,
                              AVPacket *pkt)
{
    MpegDemuxContext *m = s->priv_data;
    AVStream *st;
    int len, startcode, i, type, codec_id = 0, es_type;
    int64_t pts, dts, dummy_pos; //dummy_pos is needed for the index building to work

 redo:
    len = mpegps_read_pes_header(s, &dummy_pos, &startcode, &pts, &dts);
    if (len < 0)
        return len;
    
    /* now find stream */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st->id == startcode)
            goto found;
    }

    es_type = m->psm_es_type[startcode & 0xff];
    if(es_type > 0){
        if(es_type == STREAM_TYPE_VIDEO_MPEG1){
            codec_id = CODEC_ID_MPEG2VIDEO;
            type = CODEC_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_VIDEO_MPEG2){
            codec_id = CODEC_ID_MPEG2VIDEO;
            type = CODEC_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_AUDIO_MPEG1 ||
                  es_type == STREAM_TYPE_AUDIO_MPEG2){
            codec_id = CODEC_ID_MP3;
            type = CODEC_TYPE_AUDIO;
        } else if(es_type == STREAM_TYPE_AUDIO_AAC){
            codec_id = CODEC_ID_AAC;
            type = CODEC_TYPE_AUDIO;
        } else if(es_type == STREAM_TYPE_VIDEO_MPEG4){
            codec_id = CODEC_ID_MPEG4;
            type = CODEC_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_VIDEO_H264){
            codec_id = CODEC_ID_H264;
            type = CODEC_TYPE_VIDEO;
        } else if(es_type == STREAM_TYPE_AUDIO_AC3){
            codec_id = CODEC_ID_AC3;
            type = CODEC_TYPE_AUDIO;
        } else {
            goto skip;
        }
    } else if (startcode >= 0x1e0 && startcode <= 0x1ef) {
        type = CODEC_TYPE_VIDEO;
        codec_id = CODEC_ID_MPEG2VIDEO;
    } else if (startcode >= 0x1c0 && startcode <= 0x1df) {
        type = CODEC_TYPE_AUDIO;
        codec_id = CODEC_ID_MP2;
    } else if (startcode >= 0x80 && startcode <= 0x87) {
        type = CODEC_TYPE_AUDIO;
        codec_id = CODEC_ID_AC3;
    } else if (startcode >= 0x88 && startcode <= 0x9f) {
        type = CODEC_TYPE_AUDIO;
        codec_id = CODEC_ID_DTS;
    } else if (startcode >= 0xa0 && startcode <= 0xbf) {
        type = CODEC_TYPE_AUDIO;
        codec_id = CODEC_ID_PCM_S16BE;
    } else if (startcode >= 0x20 && startcode <= 0x3f) {
        type = CODEC_TYPE_SUBTITLE;
        codec_id = CODEC_ID_DVD_SUBTITLE;
    } else {
    skip:
        /* skip packet */
        url_fskip(&s->pb, len);
        goto redo;
    }
    /* no stream found: add a new stream */
    st = av_new_stream(s, startcode);
    if (!st) 
        goto skip;
    st->codec->codec_type = type;
    st->codec->codec_id = codec_id;
    if (codec_id != CODEC_ID_PCM_S16BE)
        st->need_parsing = 1;
 found:
    if(st->discard >= AVDISCARD_ALL)
        goto skip;
    if (startcode >= 0xa0 && startcode <= 0xbf) {
        int b1, freq;

        /* for LPCM, we just skip the header and consider it is raw
           audio data */
        if (len <= 3)
            goto skip;
        get_byte(&s->pb); /* emphasis (1), muse(1), reserved(1), frame number(5) */
        b1 = get_byte(&s->pb); /* quant (2), freq(2), reserved(1), channels(3) */
        get_byte(&s->pb); /* dynamic range control (0x80 = off) */
        len -= 3;
        freq = (b1 >> 4) & 3;
        st->codec->sample_rate = lpcm_freq_tab[freq];
        st->codec->channels = 1 + (b1 & 7);
        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate * 2;
    }
    av_new_packet(pkt, len);
    get_buffer(&s->pb, pkt->data, pkt->size);
    pkt->pts = pts;
    pkt->dts = dts;
    pkt->stream_index = st->index;
#if 0
    av_log(s, AV_LOG_DEBUG, "%d: pts=%0.3f dts=%0.3f size=%d\n",
           pkt->stream_index, pkt->pts / 90000.0, pkt->dts / 90000.0, pkt->size);
#endif

    return 0;
}

static int mpegps_read_close(AVFormatContext *s)
{
    return 0;
}

static int64_t mpegps_read_dts(AVFormatContext *s, int stream_index, 
                               int64_t *ppos, int64_t pos_limit)
{
    int len, startcode;
    int64_t pos, pts, dts;

    pos = *ppos;
#ifdef DEBUG_SEEK
    printf("read_dts: pos=0x%llx next=%d -> ", pos, find_next);
#endif
    url_fseek(&s->pb, pos, SEEK_SET);
    for(;;) {
        len = mpegps_read_pes_header(s, &pos, &startcode, &pts, &dts);
        if (len < 0) {
#ifdef DEBUG_SEEK
            printf("none (ret=%d)\n", len);
#endif
            return AV_NOPTS_VALUE;
        }
        if (startcode == s->streams[stream_index]->id && 
            dts != AV_NOPTS_VALUE) {
            break;
        }
        url_fskip(&s->pb, len);
    }
#ifdef DEBUG_SEEK
    printf("pos=0x%llx dts=0x%llx %0.3f\n", pos, dts, dts / 90000.0);
#endif
    *ppos = pos;
    return dts;
}

#ifdef CONFIG_ENCODERS
static AVOutputFormat mpeg1system_mux = {
    "mpeg",
    "MPEG1 System format",
    "video/mpeg",
    "mpg,mpeg",
    sizeof(MpegMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG1VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};

static AVOutputFormat mpeg1vcd_mux = {
    "vcd",
    "MPEG1 System format (VCD)",
    "video/mpeg",
    NULL,
    sizeof(MpegMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG1VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};

static AVOutputFormat mpeg2vob_mux = {
    "vob",
    "MPEG2 PS format (VOB)",
    "video/mpeg",
    "vob",
    sizeof(MpegMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG2VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};

/* Same as mpeg2vob_mux except that the pack size is 2324 */
static AVOutputFormat mpeg2svcd_mux = {
    "svcd",
    "MPEG2 PS format (VOB)",
    "video/mpeg",
    "vob",
    sizeof(MpegMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG2VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};

/*  Same as mpeg2vob_mux except the 'is_dvd' flag is set to produce NAV pkts */
static AVOutputFormat mpeg2dvd_mux = {
    "dvd",
    "MPEG2 PS format (DVD VOB)",
    "video/mpeg",
    "dvd",
    sizeof(MpegMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG2VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};

#endif //CONFIG_ENCODERS

AVInputFormat mpegps_demux = {
    "mpeg",
    "MPEG PS format",
    sizeof(MpegDemuxContext),
    mpegps_probe,
    mpegps_read_header,
    mpegps_read_packet,
    mpegps_read_close,
    NULL, //mpegps_read_seek,
    mpegps_read_dts,
    .flags = AVFMT_SHOW_IDS,
};

int mpegps_init(void)
{
#ifdef CONFIG_ENCODERS
    av_register_output_format(&mpeg1system_mux);
    av_register_output_format(&mpeg1vcd_mux);
    av_register_output_format(&mpeg2vob_mux);
    av_register_output_format(&mpeg2svcd_mux);
    av_register_output_format(&mpeg2dvd_mux);
#endif //CONFIG_ENCODERS
    av_register_input_format(&mpegps_demux);
    return 0;
}
