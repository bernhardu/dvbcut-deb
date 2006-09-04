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
#include <string.h>
#include <utility>
#include <list>
#include "avframe.h"
#include "lavfmuxer.h"

#include <stdio.h>

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
      s->codec=mpg.getavcc(astr);
      }

  if ((av_set_parameters(avfc, NULL) < 0) || (!(fmt->flags & AVFMT_NOFILE)&&(url_fopen(&avfc->pb, filename, URL_WRONLY) < 0))) {
    av_free(avfc);
    avfc=0;
    return;
    }
  avfc->preload= (int)(.5*AV_TIME_BASE);
  avfc->max_delay= (int)(.7*AV_TIME_BASE);
  avfc->mux_rate=10080000;
  s->codec->rc_buffer_size = 224*1024*8;


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

