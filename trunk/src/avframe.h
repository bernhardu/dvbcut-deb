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

#ifndef _DVBCUT_AVFRAME_H
#define _DVBCUT_AVFRAME_H

extern "C" {
#include <avcodec.h>
#ifdef HAVE_LIB_SWSCALE
#include <swscale.h>
#endif
}

class QImage;

/**
@author Sven Over
*/
class avframe
  {
protected:
  AVFrame *f;
  void *tobefreed;
  int w,h,dw;
  enum PixelFormat pix_fmt;
#ifdef HAVE_LIB_SWSCALE
  struct SwsContext *img_convert_ctx;
#endif

public:
  avframe();
  avframe(AVFrame *src, AVCodecContext *ctx);
  ~avframe();

  operator AVFrame*()
    {
    return f;
    }
  AVFrame *operator->()
    {
    return f;
    }
  int getwidth() const
    {
    return w;
    }
  int getheight() const
    {
    return h;
    }
  int getdisplaywidth() const
    {
    return dw;
    }
  enum PixelFormat getpixfmt() const
    {
    return pix_fmt;
    }
  QImage getqimage(bool scaled=true, double viewscalefactor=1.0);
  };

#endif
