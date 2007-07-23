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

#ifndef _DVBCUT_TSFILE_H
#define _DVBCUT_TSFILE_H

#include <stdint.h>
#include "mpgfile.h"
#define TSPACKETSIZE (188)
#define TSSYNCBYTE (0x47)

/**
@author Sven Over
*/

class tsfile : public mpgfile
  {
protected:
  struct tspacket
    {
    uint8_t data[TSPACKETSIZE];

    int pid() const
      {
      return ((data[1]&0x1f)<<8) | data[2];
      }
    bool transport_error_indicator() const
      {
      return data[1] & 0x80;
      }
    bool payload_unit_start_indicator() const
      {
      return data[1] & 0x40;
      }
    bool transport_priority() const
      {
      return data[1] & 0x20;
      }
    int transport_scrambling_control() const
      {
      return (data[3]>>6)&0x03;
      }
    bool contains_adaptation_field() const
      {
      return data[3]&0x20;
      }
    bool contains_payload() const
      {
      return data[3]&0x10;
      }
    int continuity_counter() const
      {
      return data[3]&0x0f;
      }
    const void *adaptation_field() const
      {
      return contains_adaptation_field()?&data[4]:0;
      }
    int adaptation_field_length() const
      {
      return contains_adaptation_field()?(1+data[4]):0;
      }
    const void *payload() const
      {
      return contains_payload()?(void*) &data[4+adaptation_field_length()]:0;
      }
    int payload_length() const
      {
      return contains_payload()?(184-adaptation_field_length()):0;
      }
    int sid() const
      {
      if (!(payload_unit_start_indicator()&&contains_payload()))
        return -1;
      int afl=adaptation_field_length();
      if (afl>180)
        return -1; // no space for four bytes of payload
      const uint8_t *d=data+4+afl;
      if (d[0]==0 && d[1]==0 && d[2]==1)
        return d[3];
      return -1;
      }
    };

  int streamnumber[8192]; // TS pids are 0..8191

public:
  tsfile(inbuffer &b, int initial_offset);

  ~tsfile();
  int streamreader(class streamhandle &s);
  static int probe(inbuffer &buf);
  virtual int mplayeraudioid(int astr)
    {
    return s[audiostream(astr)].id;
    }
  virtual bool istransportstream()
    {
    return true;
    }

  };

#endif
