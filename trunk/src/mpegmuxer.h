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

#ifndef _DVBCUT_MPEGMUXER_H
#define _DVBCUT_MPEGMUXER_H

#define MINPACKSIZE 64
#define DTSMAXDELAY 90000

#include <list>
#include "defines.h"
#include "muxer.h"
#include "mpgfile.h"

typedef int64_t scr_t;
#define pts2scr(x) (((scr_t)(x))*300)
#define scr2pts(x) ((pts_t)((x)/300))

/**
@author Sven Over
*/
class mpegmuxer : public muxer
  {
public:
  class bufferremoval
    {
    scr_t _scr;
    int _bytes;
  public:
    bufferremoval(scr_t scr, int bytes) : _scr(scr),_bytes(bytes)
      {}
    int bytes()
      {
      return _bytes;
      }
    scr_t scr()
      {
      return _scr;
      }
    };

  class au
    {
  protected:
    void *data;
    int size;
    pts_t pts,dts;
    int flags;
    int pos;
  public:
    au(const void *_data,int _size, pts_t _pts, pts_t _dts, int _flags);
    ~au();
    const void *getdata() const
      {
      return data;
      }
    void *getdata()
      {
      return (void*)((char*)data+pos);
      }
    int getsize()
      {
      return size-pos;
      }
    bool incomplete()
      {
      return pos;
      }
    int getpos()
      {
      return pos;
      }
    void addpos(int p)
      {
      pos+=p;
      if (pos>size)
        pos=size;
      }
    pts_t getpts()
      {
      return pts;
      }
    pts_t getdts()
      {
      return dts;
      }
    int getflags()
      {
      return flags;
      }
    void addflags(int fl)
      {
      flags|=fl;
      }
    void unsetflags(int fl)
      {
      flags &= ~fl;
      }
    };

  class pack
    {
  protected:
    void *data;
    int size;
    scr_t minscr,maxscr;
    pts_t dts;
    int payloadpos;
    int payloadlen;
    int aupayloadlen;
    friend class mpegmuxer;

  public:
    pack(int packsize, int payloadsize, int muxrate, pts_t _dts);
    ~pack();

    const void *getdata() const
      {
      return data;
      }
    int getsize() const
      {
      return size;
      }
    void *getpayload()
      {
      return (void*)((char*)data+payloadpos);
      }
    int getpayloadlen() const
      {
      return payloadlen;
      }
    void addaupayload(int p)
      {
      aupayloadlen+=p;
      }
    int getaupayloadlen() const
      {
      return aupayloadlen;
      }
    void setscr(scr_t scr);
    void setlastdts(pts_t dts)
      {
      setminscr(pts2scr(dts-DTSMAXDELAY));
      }
    void setminscr(scr_t scr)
      {
      if (scr>minscr)
	minscr=scr;
      }
    void setmaxscr(scr_t scr)
      {
      if (scr<maxscr)
        maxscr=scr;
      }
    scr_t getminscr() const
      {
      return minscr;
      }
    scr_t getmaxscr() const
      {
      return maxscr;
      }
    pts_t getdts() const
      {
      return dts;
      }
    void nopayload()
      {
      payloadlen=0;
      }

    static int maxpayload(int packsize)
      {
      return packsize-14;
      }

    bool operator<(const pack &p)
      {
      return maxscr<p.maxscr;
      }

    bool write(int fd);
    };

  class stream
    {
  protected:
    std::list<au*> aulist;
    std::list<pack*> packlist;
    std::list<bufferremoval> bufferremovals;
    streamtype::type type;
    int id;
    int bufsize;
    int filllevel;
    int packet;
    uint32_t startcode;
    uint16_t pstdbuffer;
    friend class mpegmuxer;

  public:
    stream(streamtype::type _type, int _id, int _bufsize, int _pstdbuffer, bool bufferscale=true) :
        type(_type), id(_id&0x3ff), bufsize(_bufsize), filllevel(0), packet(0)
      {
      if (bufferscale)
        pstdbuffer=htom16(0x6000|((_pstdbuffer/1024)&0x1fff));
      else
        pstdbuffer=htom16(0x4000|((_pstdbuffer/128)&0x1fff));

      if ( (id&~0xff)==0x100 )
        startcode=mbo32(0x1bd);
      else if ( (id&~0xff)==0x200 )
        startcode=mbo32(0x1bf);
      else
        startcode=htom32(0x100|id);
      }
    ~stream()
      {
      for(std::list<au*>::iterator it=aulist.begin();it!=aulist.end();++it)
        delete *it;
      for(std::list<pack*>::iterator it=packlist.begin();it!=packlist.end();++it)
        delete *it;
      }
    int getbufsize() const
      {
      return bufsize;
      }
    int getbuffree() const
      {
      return bufsize-filllevel;
      }
    uint32_t getstartcode() const
      {
      return startcode;
      }
    uint16_t getpstdbuffer() const
      {
      return pstdbuffer;
      }
    int getid() const
      {
      return id;
      }
    void fill(int bytes)
      {
      filllevel+=bytes;
      }
    void unfill(int bytes)
      {
      filllevel-=bytes;
      if (filllevel<0)
        filllevel=0;
      }
    void zerofill()
      {
      filllevel=0;
      }
    int getfill() const
      {
      return filllevel;
      }
    bool operator<(const stream &s) const
      {
      if (packlist.empty())
        return false;
      if (s.packlist.empty())
        return true;
      return packlist.front()->getmaxscr()<s.packlist.front()->getmaxscr();
      }
    bool operator<(const scr_t &s) const
      {
      if (packlist.empty())
        return false;
      return packlist.front()->getmaxscr()<s;
      }
    };

protected:
  int fd;
  stream *st[MAXAVSTREAMS];
  int muxrate;
  int packsize;
  pts_t ptsoffset;
  int aucounter;
  void *systemhdr;
  int systemhdrlen;
  bool pespacket_setlength;
  scr_t scr;
  int scrpack;

  bool flush(bool flushall);
  void packetizer(int str, pts_t maxdts);

public:
  mpegmuxer(uint32_t audiostreammask, mpgfile &mpg, const char *filename, bool dvdnavpackets=true, int packsize_bytes=2048, int muxrate_bitsps=10080000);
  ~mpegmuxer();

  virtual bool putpacket(int str, const void *data, int len, pts_t pts, pts_t dts, uint32_t flags=0);
  virtual bool ready()
    {
    return fd>=0;
    }
  virtual void finish()
    {
    flush(true);
    }
  };

#endif

