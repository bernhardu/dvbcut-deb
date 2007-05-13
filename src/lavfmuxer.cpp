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

#include <ffmpeg/avformat.h>
#include <ffmpeg/avcodec.h>
#include <string.h>
#include <utility>
#include <list>
#include "avframe.h"
#include "streamhandle.h"
#include "lavfmuxer.h"

#include <stdio.h>

#if LIBAVCODEC_VERSION_INT < (51 << 16)
#define liba52_decoder ac3_decoder
#endif

lavfmuxer::lavfmuxer(const char *format, uint32_t audiostreammask, mpgfile &mpg, const char *filename)
    : muxer(), avfc(0), fileopened(false)
  {
  fmt = guess_format(format, NULL, NULL);
  if (!fmt) {
    return;
    }

  avfc=av_alloc_format_context();
  if (!avfc)
    return;

  avfc->preload= (int)(.5*AV_TIME_BASE);
  avfc->max_delay= (int)(.7*AV_TIME_BASE);
  avfc->mux_rate=10080000;

  avfc->oformat=fmt;
  strncpy(avfc->filename, filename, sizeof(avfc->filename));

  int id=0;

  st[VIDEOSTREAM].stream_index=id;
  AVStream *s=st[VIDEOSTREAM].avstr=av_new_stream(avfc,id++);
  strpres[VIDEOSTREAM]=true;
  av_free(s->codec);
  mpg.setvideoencodingparameters();
  s->codec=mpg.getavcc(VIDEOSTREAM);
  s->codec->rc_buffer_size = 224*1024*8;

  for (int i=0;i<mpg.getaudiostreams();++i)
    if (audiostreammask & (1u<<i)) {
      int astr=audiostream(i);
      st[astr].stream_index=id;
      s=st[astr].avstr=av_new_stream(avfc,id++);
      strpres[astr]=true;
      if (s->codec)
        av_free(s->codec);
      s->codec = avcodec_alloc_context();
      avcodec_get_context_defaults(s->codec);
      s->codec->codec_type=CODEC_TYPE_AUDIO;
      s->codec->codec_id = (mpg.getstreamtype(astr)==streamtype::ac3audio) ?
	CODEC_ID_AC3 : CODEC_ID_MP2;
      s->codec->rc_buffer_size = 224*1024*8;

      // Must read some packets to get codec parameters
      streamhandle sh(mpg.getinitialoffset());
      streamdata *sd=sh.newstream(astr,mpg.getstreamtype(astr),mpg.istransportstream());

      while (sh.fileposition < (mpg.getinitialoffset()+4<<20)) {
	if (mpg.streamreader(sh)<=0)
	  break;

	if (sd->getitemlistsize() > 1) {
	  if (!avcodec_open(s->codec,
			    (mpg.getstreamtype(astr)==streamtype::ac3audio) ?
			    &liba52_decoder : &mp2_decoder)) {
	    int16_t samples[6*1536]; // must be enough for 6 AC-3 channels --mr
	    int frame_size=sizeof(samples);
	    //fprintf(stderr, "** decode audio size=%d\n", sd->inbytes());
	    avcodec_decode_audio(s->codec,samples,&frame_size,
				 (uint8_t*) sd->getdata(),sd->inbytes());
	    avcodec_close(s->codec);
	  }
	  break;
	}
      }
    }

  if ((av_set_parameters(avfc, NULL) < 0) || (!(fmt->flags & AVFMT_NOFILE)&&(url_fopen(&avfc->pb, filename, URL_WRONLY) < 0))) {
    av_free(avfc);
    avfc=0;
    return;
    }
  avfc->preload= (int)(.5*AV_TIME_BASE);
  avfc->max_delay= (int)(.7*AV_TIME_BASE);
  avfc->mux_rate=10080000;


  dump_format(avfc, 0, filename, 1);
  fileopened=true;
  av_write_header(avfc);
  }


lavfmuxer::~lavfmuxer()
  {
  if (avfc) {
    if (fileopened) {
      av_write_trailer(avfc);
      if (!(fmt->flags & AVFMT_NOFILE))
        url_fclose(&avfc->pb);
      }

    av_free(avfc);
    }
  }

