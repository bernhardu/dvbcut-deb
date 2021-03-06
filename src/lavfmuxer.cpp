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
  fmt = av_guess_format(format, NULL, NULL);
  if (!fmt) {
    return;
    }

  avfc=avformat_alloc_context();
  if (!avfc)
    return;

  av_opt_set_int(avfc, "preload", (int)(.5 * AV_TIME_BASE), AV_OPT_SEARCH_CHILDREN);
  av_opt_set_int(avfc, "muxrate", 10080000, AV_OPT_SEARCH_CHILDREN);
  avfc->max_delay= (int)(.7*AV_TIME_BASE);

  avfc->oformat=fmt;
  avfc->url = av_strdup(filename ? filename : "");

  int id=0;

  st[VIDEOSTREAM].stream_index=id;
  AVStream *s=st[VIDEOSTREAM].avstr=avformat_new_stream(avfc, NULL);
  s->id = id++;
  strpres[VIDEOSTREAM]=true;
  av_free(s->codec);
  mpg.setvideoencodingparameters();
  s->codec=mpg.getavcc(VIDEOSTREAM);
  s->codec->rc_buffer_size = 224*1024*8;
  s->sample_aspect_ratio = s->codec->sample_aspect_ratio;
  s->time_base = s->codec->time_base;

  for (int i=0;i<mpg.getaudiostreams();++i)
    if (audiostreammask & (1u<<i)) {
      int astr=audiostream(i);
      st[astr].stream_index=id;
      s=st[astr].avstr=avformat_new_stream(avfc, NULL);
      s->id = id++;
      strpres[astr]=true;
      if (s->codec)
        av_free(s->codec);
      s->codec = avcodec_alloc_context3(NULL);
      avcodec_get_context_defaults3(s->codec, NULL);
      s->codec->codec_type=AVMEDIA_TYPE_AUDIO;
      s->codec->codec_id = (mpg.getstreamtype(astr)==streamtype::ac3audio) ?
	AV_CODEC_ID_AC3 : AV_CODEC_ID_MP2;
      s->codec->rc_buffer_size = 224*1024*8;

      // Must read some packets to get codec parameters
      streamhandle sh(mpg.getinitialoffset());
      streamdata *sd=sh.newstream(astr,mpg.getstreamtype(astr),mpg.istransportstream());

      while (sh.fileposition < mpg.getinitialoffset()+(4<<20)) {
	if (mpg.streamreader(sh)<=0)
	  break;

	if (sd->getitemlistsize() > 1) {
	  if (!avcodec_open2(s->codec,
			     avcodec_find_decoder(s->codec->codec_id), NULL)) {
            AVFrame *frame = av_frame_alloc();
	    AVPacket pkt;
            int got_output;

 	    av_init_packet( &pkt );
	    pkt.data = (uint8_t*) sd->getdata();
	    pkt.size = sd->inbytes();

            avcodec_decode_audio4(s->codec, frame, &got_output, &pkt);

            av_frame_free(&frame);
	    avcodec_close(s->codec);
	  }
	  break;
	}
      }
      s->time_base = s->codec->time_base;
    }

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

