/*  dvbcut
    Copyright (c) 2005 Sven Over <svenover@svenover.de>
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* $Id$ */

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}
#include <cstring>
#include <utility>
#include <list>
#include "avframe.h"
#include "streamhandle.h"
#include "lavfmuxer.h"

#include <stdio.h>

lavfmuxer::lavfmuxer(const char *format, uint32_t audiostreammask, mpgfile &mpg, const char *filename)
    : muxer(), avfc(0), fileopened(false)
  {
  auto fmt_ = av_guess_format(format, NULL, NULL);
  if (!fmt_) {
    return;
    }
  fmt = fmt_;

  if (avformat_alloc_output_context2(&avfc, fmt_, NULL, av_strdup(filename ? filename : "")) < 0)
    return;

  av_opt_set_int(avfc, "preload", (int)(.5 * AV_TIME_BASE), AV_OPT_SEARCH_CHILDREN);
  av_opt_set_int(avfc, "muxrate", 10080000, AV_OPT_SEARCH_CHILDREN);
  avfc->max_delay= (int)(.7*AV_TIME_BASE);

  int id=0;

  st[VIDEOSTREAM].stream_index=id;

  strpres[VIDEOSTREAM]=true;
  mpg.setvideoencodingparameters();
  AVCodecContext* codec = mpg.getavcc(VIDEOSTREAM);
  codec->rc_buffer_size = 224*1024*8;
  AVStream *s = st[VIDEOSTREAM].avstr = avformat_new_stream(avfc, codec->codec);
  s->id = id++;
  s->sample_aspect_ratio = codec->sample_aspect_ratio;
  s->time_base = codec->time_base;

  int avlogflags = av_log_get_flags();

  for (int i=0;i<mpg.getaudiostreams();++i)
    if (audiostreammask & (1u<<i)) {
      int astr=audiostream(i);
      st[astr].stream_index=id;
      codec = avcodec_alloc_context3(NULL);
      codec->codec_type = AVMEDIA_TYPE_AUDIO;
      codec->codec_id = (mpg.getstreamtype(astr)==streamtype::ac3audio) ? AV_CODEC_ID_AC3 : AV_CODEC_ID_MP2;
      codec->rc_buffer_size = 224*1024*8;
      s = st[astr].avstr = avformat_new_stream(avfc, codec->codec);
      s->id = id++;
      strpres[astr]=true;

      // Must read some packets to get codec parameters
      streamhandle sh(mpg.getinitialoffset());
      streamdata *sd=sh.newstream(astr,mpg.getstreamtype(astr),mpg.istransportstream());

      int srerror = 0;
      while (sh.fileposition < mpg.getinitialoffset()+(8<<20)) {
	if (mpg.streamreader(sh)<=0)
	  break;

	if (sd->getitemlistsize() > 1) {
	  if (!avcodec_open2(codec,
			     avcodec_find_decoder(codec->codec_id), NULL)) {
            AVFrame *frame = av_frame_alloc();
            AVPacket* pkt = av_packet_alloc();

            pkt->data = (uint8_t*)sd->getdata();
            pkt->size = sd->inbytes();

            avcodec_send_packet(codec, pkt);
            avcodec_receive_frame(codec, frame);

            av_packet_free(&pkt);
            av_frame_free(&frame);
            avcodec_close(codec);
          }
          if (codec->sample_rate == 0) {
            ++srerror;
            if (srerror == 1) {
              fprintf(stderr,"Error, could not determine sample rate\n");
              av_log_set_flags(AV_LOG_SKIP_REPEATED);
            }
            sd->pop();
          }
          else {
            fprintf(stdout,"Sample rate found after %d errors\n", srerror);
            break;
          }
        }
      }
      s->time_base = codec->time_base;
    }

  av_log_set_flags(avlogflags);

  if (!(fmt->flags & AVFMT_NOFILE)&&(avio_open(&avfc->pb, filename, AVIO_FLAG_WRITE) < 0)) {
    av_free(avfc);
    avfc=0;
    return;
    }

  av_opt_set_int(avfc, "preload", (int)(.5 * AV_TIME_BASE), AV_OPT_SEARCH_CHILDREN);
  av_opt_set_int(avfc, "muxrate", 10080000, AV_OPT_SEARCH_CHILDREN);
  avfc->max_delay= (int)(.7*AV_TIME_BASE);

  av_dump_format(avfc, 0, filename, 1);
  int ret = avformat_write_header(avfc, NULL);
  if (ret < 0) {
      fprintf(stderr, "avformat_write_header failed ret[%d]\n", ret);
      return;
  }

  fileopened = true;
  }


lavfmuxer::~lavfmuxer()
  {
  if (avfc) {
    if (fileopened) {
      av_write_trailer(avfc);
      if (!(fmt->flags & AVFMT_NOFILE))
        avio_close(avfc->pb);
      }

    av_free(avfc);
    }
  }

