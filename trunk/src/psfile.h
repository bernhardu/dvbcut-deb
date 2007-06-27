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

#ifndef _DVBCUT_PSFILE_H
#define _DVBCUT_PSFILE_H

#include "mpgfile.h"

/**
@author Sven Over
*/
class psfile : public mpgfile
  {
protected:
  int streamnumber[0x300]; // PS stream ids are 0..0xff plus possibly 2x256 private streams

public:
  psfile(inbuffer &b, int initial_offset);

  ~psfile();
  int streamreader(class streamhandle &s);
  static int probe(inbuffer &buf);
  virtual int mplayeraudioid(int astr)
    {
    int sid=s[audiostream(astr)].id;
    if (sid>=0xc0 && sid<0xe0)
      return sid-0xc0;
    if (sid>=0x180 && sid<0x1a0)
      return sid-0x100;
    return 0;
    }
  };

#endif
