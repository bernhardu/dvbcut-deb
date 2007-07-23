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

#ifndef _DVBCUT_PTS_H_
#define _DVBCUT_PTS_H_

#include <stdint.h>
#include <string>


typedef int64_t pts_t;

#define mplayer_ptsreference(x,y) (ptsreference(x,y))
#define PTSMASK (0x0ffffffffll)
#define PTSMSB (1ll<<31)
#define PTS_T_MAX (0x7fffffffffffffffll)
static inline pts_t ptsreference(pts_t t, pts_t reference)
  {
  t&=PTSMASK;
  return t| ((reference+((t<((reference&PTSMASK)^PTSMSB))?(1ll<<32):0)-((reference&PTSMSB)?0:(1ll<<32)))&~PTSMASK);
  }

std::string ptsstring(pts_t pts);

#endif
