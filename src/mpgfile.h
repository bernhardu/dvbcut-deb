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

#ifndef _DVBCUT_MPGFILE_H
#define _DVBCUT_MPGFILE_H

#include <string>
#include <vector>
#include <list>

#include "port.h"
#include "buffer.h"
#include "types.h"
#include "index.h"
#include "pts.h"
#include "defines.h"
#include "stream.h"

/**
@author Sven Over
*/
class avframe;
class muxer;
class logoutput;

class mpgfile
  {
public:

protected:
  inbuffer &buf;
  stream s[MAXAVSTREAMS];
  int videostreams, audiostreams;
  int initialoffset;
  index::index idx;
  int pictures;

  mpgfile(inbuffer &b, int initial_offset);


public:
  virtual ~mpgfile();

  static mpgfile *open(inbuffer &b, std::string *errormessage = 0);
  virtual int streamreader(class streamhandle &s)=0;
  virtual int mplayeraudioid(int audiostream)=0;
  virtual bool istransportstream()
    {
    return false;
    }

  int getinitialoffset() const
    {
    return initialoffset;
    }
  int getpictures() const
    {
    return pictures;
    }
  int getaudiostreams() const
    {
    return audiostreams;
    }
  streamtype::type getstreamtype(int str) const
    {
    return s[str].type;
    }
  const std::string &getstreaminfo(int str) const
    {
    return s[str].getinfo();
    }
  const index::picture &operator[](unsigned int i) const
    {
    return idx[idx.indexnr(i)];
    }
  const int lastseqheader(int i) const
    {
    while (i>0 && !idx[idx.indexnr(i)].getseqheader())
      --i;
    return i;
    }
  const int lastiframe(int i) const
    {
    while (i>0 && !idx[idx.indexnr(i)].isiframe())
      --i;
    return i;
    }
  const int nextseqheader(int i) const
    {
    while (i+1<pictures && !idx[idx.indexnr(i)].getseqheader())
      ++i;
    return i;
    }
  const int nextiframe(int i) const
    {
    while (i+1<pictures && !idx[idx.indexnr(i)].isiframe())
      ++i;
    return i;
    }
  const int nextaspectdiscontinuity(int i) const
    {
    if (i >= 0 && i < pictures)
      {
      int aspect = idx[idx.indexnr(i)].getaspectratio();
      while (++i < pictures)
	{
	if (aspect != idx[idx.indexnr(i)].getaspectratio())
	  return i;
	}
      }
    return -1;
    }
  AVCodecContext *getavcc(int str)
    {
    return s[str].avcc;
    }
  void setvideoencodingparameters()
    {
    s[videostream()].setvideoencodingparameters();
    }

  int generateindex(const char *savefilename=0, std::string *errorstring=0, logoutput *log=0)
    {
    int rv=idx.generate(savefilename,errorstring,log);
    pictures=(rv>0)?rv:0;
    return rv;
    }
  int loadindex(const char *filename, std::string *errorstring=0)
    {
    int rv=idx.load(filename,errorstring);
    pictures=(rv>0)?rv:0;
    return rv;
    }
  int saveindex(const char *filename, std::string *errorstring=0)
    {
    return idx.save(filename,errorstring);
    }

  void decodegop(int start, int stop, std::list<avframe*> &framelist);
  void initaudiocodeccontext(int aud);
  void initcodeccontexts(int vid);
  void playaudio(int aud, int picture, int ms);
  void savempg(muxer &mux, int start, int stop, int progresspics=0,
               int progresstotal=0, logoutput *log=0);
  void recodevideo(muxer &mux, int start, int stop, pts_t offset,
                   int progresspics=0, int progresstotal=0, logoutput *log=0);
  void fixtimecode(uint8_t *buf, int len, pts_t pts);

  dvbcut_off_t getfilesize()
    {
    return buf.getfilesize();
    }
  dvbcut_off_t getfilepos() const
    {
    return buf.getfilepos();
    }

  static pts_t char2pts(const unsigned char *h)
    {
    return pts_t((uint32_t(h[4] & 0xfe) >> 1) |
                 (uint32_t(h[3]) << 7) |
                 (uint32_t(h[2] & 0xfe) << 14) |
                 (uint32_t(h[1]) << 22)) | pts_t((uint32_t) (h[0] & 0x0e) << 29);
    }
  static pts_t char2pts(const unsigned char *h, pts_t reference)
    {
    return ptsreference(char2pts(h),reference);
    }
  static const int frameratescr[16];
  };

#endif
