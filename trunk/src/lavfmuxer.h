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

#ifndef _DVBCUT_LAVFMUXER_H
#define _DVBCUT_LAVFMUXER_H

extern "C" {
#include <ffmpeg/avformat.h>
}

#include "mpgfile.h"
#include "muxer.h"

/**
@author Sven Over
*/
class lavfmuxer : public muxer
  {
protected:
  struct stream
    {
    AVStream *avstr;
    int stream_index;

    stream() : avstr(0), stream_index(0)
      {}
    ~stream()
      {
      if (avstr)
        av_free(avstr);
      }
    };

  AVOutputFormat *fmt;
  AVFormatContext *avfc;

  stream st[MAXAVSTREAMS];

  bool fileopened;

public:
  lavfmuxer(const char *format, uint32_t audiostreammask, mpgfile &mpg, const char *filename);

  ~lavfmuxer();

  bool putpacket(int str, const void *data, int len, pts_t pts, pts_t dts, uint32_t flags=0)
    {
    if (len<=0)
      return 0;

    AVPacket avp;
    av_init_packet(&avp);
    avp.data=(uint8_t*) data;
    avp.size=len;
    avp.pts=pts;
    avp.dts=dts;
    avp.stream_index=st[str].stream_index;
    if (flags & MUXER_FLAG_KEY)
      avp.flags |= PKT_FLAG_KEY;

    int rv=av_interleaved_write_frame(avfc,&avp);

    return rv>=0;
    }

  virtual bool ready()
    {
    return bool(avfc);
    }
  virtual void finish()
    {
    return;
    }
  };

#endif
