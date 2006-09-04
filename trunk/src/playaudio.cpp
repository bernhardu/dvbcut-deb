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

#ifdef HAVE_LIB_AO
#include <stdio.h>
#include <ao/ao.h>
#include <ffmpeg/avcodec.h>
#include "playaudio.h"

#define MIN_BUFFER_SAMPLES (1536*6)

void playaudio(const void *data, uint32_t len, AVCodecContext *avcc, AVCodec *dec)
  {
  if (!avcc)
    return;

  if (int rv=avcodec_open(avcc, dec)) {
    fprintf(stderr,"avcodec_open returned %d\n",rv);
    return;
    }

  ao_device *device=0;
  const uint8_t *d=(const uint8_t*)data;

  while (len>0) {
    int16_t samples[MIN_BUFFER_SAMPLES >? avcc->frame_size];
    int frame_size;

    int bytesDecoded=avcodec_decode_audio(avcc,samples,&frame_size,(uint8_t*)d,len);

    if (bytesDecoded<0) {
      fprintf(stderr,"avcodec_decode_audio returned %d\n",bytesDecoded);
      break;
      }

    len-=bytesDecoded;
    d+=bytesDecoded;

    if (frame_size<=0) {
      if (bytesDecoded==0)
        break;
      else
        continue;
      }

    if (!device) {
      ao_sample_format format;

      format.bits = 16;
      format.channels = avcc->channels;
      format.rate = avcc->sample_rate;
      format.byte_format = AO_FMT_LITTLE;

      device = ao_open_live(ao_default_driver_id(), &format, NULL /* no options */ );
      if (!device)
        break;
      }

    ao_play(device, (char*) samples, frame_size);
    }

  if (device)
    ao_close(device);
  avcodec_close(avcc);
  }

#endif // HAVE_LIB_AO
