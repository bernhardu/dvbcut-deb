
/*
 * Utils for libavcodec
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file allcodecs.c
 * Utils for libavcodec.
 */

#include "avcodec.h"

/* If you do not call this function, then you can select exactly which
   formats you want to support */

/**
 * simple call to register all the codecs. 
 */
void avcodec_register_all(void)
{
    static int inited = 0;
    
    if (inited != 0)
	return;
    inited = 1;

    register_avcodec(&mpeg2video_encoder);
    register_avcodec(&mpeg2video_decoder);
    register_avcodec(&mp2_encoder);
    register_avcodec(&mp2_decoder);
    register_avcodec(&mp3_decoder);

#ifdef CONFIG_AC3
    register_avcodec(&ac3_encoder);
    register_avcodec(&ac3_decoder);
#endif

#ifdef CONFIG_DTS
    register_avcodec(&dts_decoder);
#endif
    
    av_register_codec_parser(&mpegaudio_parser);
//     av_register_codec_parser(&ac3_parser);
}

