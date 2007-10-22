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

// stuff to identify proprietary (topfield) headers and find bookmarks
// ==> magic (&version) number, position of bookmarks, length of header
#define TF5XXXPVR_MAGIC (0x54467263)
#define TF5XXXPVR_LEN (20*TSPACKETSIZE)
#define TF5000PVR_VERSION (0x5000)
#define TF5000PVR_POS (1400)
#define TF5010PVR_VERSION (0x5010)
#define TF5010PVR_POS (1404)
#define TF4000PVR_LEN (3*TSPACKETSIZE)
#define TF4000PVR_POS (216)
#define MAX_BOOKMARKS (64)

/**
@author Sven Over
*/

class tsfile : public mpgfile
  {
protected:
  struct tspacket {
    uint8_t data[TSPACKETSIZE];

    int pid() const {
      return ((data[1]&0x1f)<<8) | data[2];
    }
    bool transport_error_indicator() const {
      return data[1] & 0x80;
    }
    bool payload_unit_start_indicator() const {
      return data[1] & 0x40;
    }
    bool transport_priority() const {
      return data[1] & 0x20;
    }
    int transport_scrambling_control() const {
      return (data[3]>>6)&0x03;
    }
    bool contains_adaptation_field() const {
      return data[3]&0x20;
    }
    bool contains_payload() const {
      return data[3]&0x10;
    }
    int continuity_counter() const {
      return data[3]&0x0f;
    }
    const void *adaptation_field() const {
      return contains_adaptation_field()?&data[4]:0;
    }
    int adaptation_field_length() const {
      return contains_adaptation_field()?(1+data[4]):0;
    }
    const void *payload() const {
      return contains_payload()?(void*) &data[4+adaptation_field_length()]:0;
    }
    int payload_length() const {
      return contains_payload()?(184-adaptation_field_length()):0;
    }
    int sid() const {
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

  bool check_si_tables();
  size_t get_si_table(uint8_t*, size_t,  size_t&, int, int);

  int isTOPFIELD(const uint8_t*, int);  
  // indicates type of read bookmarks (frame numbers or byte positions)
  bool bytes;
  // to store the bookmarks in byte positions (terminated by a zero-bookmark)
  dvbcut_off_t byte_bookmarks[MAX_BOOKMARKS+1];
  // to store the bookmarks in frame numbers (terminated by a zero-bookmark)
  int pic_bookmarks[MAX_BOOKMARKS+1];

public:
  tsfile(inbuffer &b, int initial_offset);

  ~tsfile();
  int streamreader(class streamhandle &s);
  static int probe(inbuffer &buf);
  virtual int mplayeraudioid(int astr) {
    return s[audiostream(astr)].id;
  }
  virtual bool istransportstream() {
    return true;
  }
  virtual int *getbookmarks() {
    if(bytes) {
      int pic, nbp=0,nbb=0;
      while(byte_bookmarks[nbb])
        if((pic = getpictureatposition(byte_bookmarks[nbb++])) >= 0) 
          pic_bookmarks[nbp++] = pic;
      pic_bookmarks[nbp] = 0;
      bytes = false;
    } 
    return pic_bookmarks;
  }

};

#endif
