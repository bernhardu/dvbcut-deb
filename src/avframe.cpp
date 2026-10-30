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
extern "C" {
#include <libavutil/imgutils.h>
}
#include "avframe.h"

#ifdef HAVE_LIB_SWSCALE
avframe::avframe() : tobefreed(0),w(0),h(0),dw(0),pix_fmt(),img_convert_ctx(0)
#else
avframe::avframe() : tobefreed(0),w(0),h(0),dw(0),pix_fmt()
#endif
  {
  f=av_frame_alloc();
  }

avframe::avframe(AVFrame *src, AVCodecContext *ctx) : f(0),tobefreed(0)
  {
  f=av_frame_alloc();

  tobefreed = (uint8_t *)malloc(av_image_get_buffer_size(ctx->pix_fmt, ctx->width, ctx->height, 1));

  av_image_fill_arrays(f->data, f->linesize,
                       tobefreed,
                       ctx->pix_fmt, ctx->width, ctx->height, 1);

  av_image_copy(f->data, f->linesize, (const uint8_t**)src->data, src->linesize,
                ctx->pix_fmt, ctx->width, ctx->height);

  f->pict_type              = src->pict_type;
  f->quality                = src->quality;
  //f->coded_picture_number   = src->coded_picture_number;
  //f->display_picture_number = src->display_picture_number;
  f->pts                    = src->pts;
  //f->interlaced_frame       = src->interlaced_frame;
  //f->top_field_first        = src->top_field_first;
  f->repeat_pict            = src->repeat_pict;
  f->quality                = src->quality;
  f->format                 = src->format;
  f->width                  = src->width;
  f->height                 = src->height;

  // Modernes Handling für Interlaced und Top-Field-First via flags (FFmpeg 6.x/7.x+)
  f->flags            = src->flags;

  w=ctx->width;
  h=ctx->height;
  pix_fmt=ctx->pix_fmt;
  dw=w*ctx->sample_aspect_ratio.num/ctx->sample_aspect_ratio.den;
#ifdef HAVE_LIB_SWSCALE
  img_convert_ctx=sws_getContext(w, h, pix_fmt, 
                                 w, h, AV_PIX_FMT_RGB24, SWS_BICUBIC,
                                 NULL, NULL, NULL);
#endif
  }

avframe::~avframe()
  {
  if (tobefreed)
    free(tobefreed);
  if (f)
    av_frame_free(&f);
#ifdef HAVE_LIB_SWSCALE
  if (img_convert_ctx)
    sws_freeContext(img_convert_ctx);
#endif
  }

QImage avframe::getqimage(bool scaled, double viewscalefactor)
  {
  if (w<=0 || h<=0 || img_convert_ctx==NULL)
    return QImage();

  uint8_t *rgbbuffer = (uint8_t*)malloc(av_image_get_buffer_size(AV_PIX_FMT_RGB24, w, h, 1));

  AVFrame *avframergb=av_frame_alloc();

  av_image_fill_arrays(avframergb->data, avframergb->linesize,
                       rgbbuffer,
                       AV_PIX_FMT_RGB24, w, h, 1);

#ifdef HAVE_LIB_SWSCALE
  sws_scale(img_convert_ctx, f->data, f->linesize, 0, h,
              avframergb->data, avframergb->linesize);
#else
  // img_convert wurde vor Jahren entfernt. Falls kein swscale aktiv ist,
  // sollte hier ein Fehler geworfen oder swscale erzwungen werden.
#endif

  QImage im(rgbbuffer, w, h, 3*w, QImage::Format_RGB888, ::free, rgbbuffer);

  if ((scaled && w!=dw)||(viewscalefactor!=1.0)) {
#ifdef SMOOTHSCALE
    im = im.scaled(int((scaled?dw:w)/viewscalefactor+0.5), int(h/viewscalefactor+0.5), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
#else
    im = im.scaled(int((scaled?dw:w)/viewscalefactor+0.5), int(h/viewscalefactor+0.5));
#endif
    }

  av_frame_free(&avframergb);
  return (im);
  }
