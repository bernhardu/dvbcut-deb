/*
 * Register all the formats and protocols
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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
#include "avformat.h"

/* If you do not call this function, then you can select exactly which
   formats you want to support */

/**
 * Initialize libavcodec and register all the codecs and formats.
 */
void av_register_all(void)
{
    static int inited = 0;
    
    if (inited != 0)
        return;
    inited = 1;

    avcodec_init();
    avcodec_register_all();

    mpegps_init();
    mpegts_init();
    
    /* file protocols */
    register_protocol(&file_protocol);
    register_protocol(&pipe_protocol);
}
