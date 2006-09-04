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

#ifndef _DVBCUT_MUXER_H
#define _DVBCUT_MUXER_H

#include <stdint.h>
#include "pts.h"
#include "defines.h"

/**
@author Sven Over
*/
class muxer
  {
protected:
  pts_t pts[MAXAVSTREAMS];
  pts_t dts[MAXAVSTREAMS];
  bool strpres[MAXAVSTREAMS];
  bool empty;

public:
  muxer() : pts(), dts(), strpres(), empty(true)
    {}
  virtual ~muxer()
    {}

  pts_t getpts(int str) const
    {
    return pts[str];
    }
  void setpts(int str, pts_t p)
    {
    pts[str]=p;
    }
  pts_t getdts(int str) const
    {
    return dts[str];
    }
  void setdts(int str, pts_t d)
    {
    dts[str]=d;
    }
  bool streampresent(int str) const
    {
    return strpres[str];
    }
  void unsetempty()
    {
    empty=false;
    }
  bool isempty()
    {
    return empty;
    }

  virtual bool putpacket(int str, const void *data, int len, pts_t pts, pts_t dts, uint32_t flags=0)=0;
  virtual bool ready()
    {
    return false;
    }
  virtual void finish()
    {
    return;
    }
  };

#endif
