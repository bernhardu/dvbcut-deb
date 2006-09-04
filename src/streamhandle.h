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

#ifndef _DVBCUT_STREAMHANDLE_H
#define _DVBCUT_STREAMHANDLE_H

#include "mpgfile.h"
#include "streamdata.h"

struct streamhandle
  {
  off_t fileposition;
  streamdata *stream[MAXAVSTREAMS];

  streamhandle(off_t filepos=0) : fileposition(filepos), stream()
    {}
  ~streamhandle()
    {
    for(unsigned int i=0;i<MAXAVSTREAMS;++i)
      if (stream[i])
        delete stream[i];
    }
  streamdata *newstream(int streamnumber, streamtype::type t, bool transportstream)
    {
    if (stream[streamnumber])
      delete stream[streamnumber];
    return stream[streamnumber]=new streamdata(t,transportstream);
    }
  void delstream(int streamnumber)
    {
    if (stream[streamnumber]) {
      delete stream[streamnumber];
      stream[streamnumber]=0;
      }
    }
  //const streamdata &getstream(int streamnumber) { return stream[streamnumber]; }
  };


#endif
