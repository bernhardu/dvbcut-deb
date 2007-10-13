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

#include <qimage.h>
#include <cstdlib>
#include <cstdio>
#include "avframe.h"

#ifdef HAVE_LIB_SWSCALE
avframe::avframe() : tobefreed(0),w(0),h(0),dw(0),pix_fmt(),img_convert_ctx(0)
#else
avframe::avframe() : tobefreed(0),w(0),h(0),dw(0),pix_fmt()
#endif
  {
  f=avcodec_alloc_frame();
  }

avframe::avframe(AVFrame *src, AVCodecContext *ctx) : f(0),tobefreed(0)
  {
  f=avcodec_alloc_frame();
  tobefreed=malloc(avpicture_get_size(ctx->pix_fmt, ctx->width, ctx->height));

  avpicture_fill((AVPicture *)f,
                 (u_int8_t*)tobefreed,
                 ctx->pix_fmt,ctx->width,ctx->height);

  img_copy((AVPicture *)f, (const AVPicture *) src,
           ctx->pix_fmt, ctx->width, ctx->height);

  f->pict_type              = src->pict_type;
  f->quality                = src->quality;
  f->coded_picture_number   = src->coded_picture_number;
  f->display_picture_number = src->display_picture_number;
  f->pts                    = src->pts;
  f->interlaced_frame       = src->interlaced_frame;
  f->top_field_first        = src->top_field_first;
  f->repeat_pict            = src->repeat_pict;
  f->quality                = src->quality;

  w=ctx->width;
  h=ctx->height;
  pix_fmt=ctx->pix_fmt;
  dw=w*ctx->sample_aspect_ratio.num/ctx->sample_aspect_ratio.den;
#ifdef HAVE_LIB_SWSCALE
  img_convert_ctx=sws_getContext(w, h, pix_fmt, 
                                 w, h, PIX_FMT_BGR24, SWS_BICUBIC, 
                                 NULL, NULL, NULL);
#endif
  }

avframe::~avframe()
  {
  if (tobefreed)
    free(tobefreed);
  if (f)
    av_free(f);
#ifdef HAVE_LIB_SWSCALE
  if (img_convert_ctx)
    sws_freeContext(img_convert_ctx);
#endif
  }

QImage avframe::getqimage(bool scaled, int viewscalefactor)
  {
#ifdef HAVE_LIB_SWSCALE
  if (w<=0 || h<=0 || img_convert_ctx==NULL)
#else
  if (w<=0 || h<=0)
#endif
    return QImage();

  uint8_t *rgbbuffer=(uint8_t*)malloc(avpicture_get_size(PIX_FMT_RGB24, w, h)+64);
  int headerlen=sprintf((char *) rgbbuffer, "P6\n%d %d\n255\n", w, h);

  AVFrame *avframergb=avcodec_alloc_frame();

  avpicture_fill((AVPicture*)avframergb,
                 rgbbuffer+headerlen,
                 PIX_FMT_RGB24,w,h);

#ifdef HAVE_LIB_SWSCALE
  sws_scale(img_convert_ctx, f->data, f->linesize, 0, h, 
              avframergb->data, avframergb->linesize);
#else
  img_convert((AVPicture *)avframergb, PIX_FMT_RGB24, (AVPicture*)f, pix_fmt, w, h);
#endif

  QImage im;
  im.loadFromData(rgbbuffer, headerlen+w*h*3, "PPM");

#ifdef HAVE_LIB_SWSCALE
  im = im.swapRGB();
#endif

  if ((scaled && w!=dw)||(viewscalefactor!=1)) {
#ifdef SMOOTHSCALE
    im = im.smoothScale((scaled?dw:w)/viewscalefactor, h/viewscalefactor);
#else
    im = im.scale((scaled?dw:w)/viewscalefactor, h/viewscalefactor);
#endif
    }

  free(rgbbuffer);
  av_free(avframergb);
  return (im);
  }
