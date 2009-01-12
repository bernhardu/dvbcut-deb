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

#ifndef DVBCUT_STREAM_H
#define DVBCUT_STREAM_H

#include <string>

extern "C" {
#include <avcodec.h>
}

class stream
  {
protected:
  int id; //avfid;
  streamtype::type type;
  std::string infostring;
  AVCodec *dec,*enc;
  AVCodecContext *avcc;
  stream() : id(-1),type(streamtype::unknown),dec(0),enc(0),avcc(0)
    {}
  ~stream()
    {
    if (avcc)
      av_free(avcc);
    }
  void freeavcc()
    {
    if (avcc)
      av_free(avcc);
    avcc=0;
    }
  void allocavcc()
    {
    if (avcc)
      av_free(avcc);
    avcc=avcodec_alloc_context();
    avcodec_get_context_defaults(avcc);
    }
  void setvideoencodingparameters(bool interlaced=true)
    {
    avcc->bit_rate=9500000;
    avcc->rc_min_rate=9500000;
    avcc->rc_max_rate=9500000;
    avcc->rc_buffer_size=224*1024*8;
    avcc->rc_buffer_aggressivity=1.0;
    avcc->rc_initial_buffer_occupancy = avcc->rc_buffer_size*3/4;
    avcc->qmax=2;
    avcc->mb_lmax= FF_QP2LAMBDA * 2;
    avcc->lmax= FF_QP2LAMBDA * 2;
    if (interlaced)
      avcc->flags |= CODEC_FLAG_INTERLACED_DCT|CODEC_FLAG_INTERLACED_ME;
    }

  friend class mpgfile;
  friend class tsfile;
  friend class psfile;
public:
  const std::string &getinfo() const
    {
    return infostring;
    }
  };

#endif
