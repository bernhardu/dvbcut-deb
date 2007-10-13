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

#ifndef _DVBCUT_STREAMDATA_H
#define _DVBCUT_STREAMDATA_H

#include <string>
#include <list>
extern "C" {
#include <ffmpeg/avformat.h>
}

#include "port.h"
#include "tsfile.h"
#include "psfile.h"
#include "buffer.h"
#include "types.h"
#include "pts.h"

/* obsolete, do not use --mr
#define STREAM_ITEM_FLAG_DATA_ALIGNMENT_INDICATOR (1<<0)
*/
#define STREAM_ITEM_FLAG_HAS_PTS                  (1<<1)
#define STREAM_ITEM_FLAG_FRAME                    (1<<2)

/**
@author Sven Over
*/
class streamdata
  {
public:
  struct item
    {
    filepos_t fileposition;

    uint32_t bufferposition;
    uint32_t flags;
    pts_t pts;

    item(filepos_t fp, uint32_t fl, pts_t p, uint32_t bp) : fileposition(fp), bufferposition(bp), flags(fl), pts(p)
      {
      if (!(flags&STREAM_ITEM_FLAG_HAS_PTS))
        pts=(pts_t)AV_NOPTS_VALUE;
      }
    ~item()
    {}

    void setpts(pts_t p)
      {
      if (p!=(pts_t)AV_NOPTS_VALUE)
        flags |= STREAM_ITEM_FLAG_HAS_PTS;
      else
        flags &=~ STREAM_ITEM_FLAG_HAS_PTS;
      pts=p;
      }
    bool headerhaspts() const
      {
      return flags & STREAM_ITEM_FLAG_HAS_PTS;
      }
    pts_t headerpts() const
      {
      return pts;
      }
    pts_t headerpts(pts_t reference) const
      {
      if (flags & STREAM_ITEM_FLAG_HAS_PTS)
        return ptsreference(pts,reference);
      return AV_NOPTS_VALUE;
      }
    void clearpts()
      {
      flags &=~ STREAM_ITEM_FLAG_HAS_PTS;
      pts=AV_NOPTS_VALUE;
      }
    bool is_frame() const
      {
      return flags & STREAM_ITEM_FLAG_FRAME;
      }
    }
  ;
  typedef std::list<item> itemlisttype;

protected:
  itemlisttype items;
  unsigned int itemlistsize;
  buffer data;
  uint32_t offset;
  std::string header;
  filepos_t curpos;
  const streamtype::type type;
  bool transportstream;
  dvbcut_off_t nextfilepos;

public:
  streamdata(streamtype::type t, bool transport_stream, uint32_t buffersize=256<<10) :
      itemlistsize(0), data(buffersize), offset(0), curpos(0), type(t), transportstream(transport_stream)
  {}
  ~streamdata()
    {}

  const std::list<item> &itemlist() const
    {
    return items;
    }
  bool empty() const
    {
    return items.empty();
    }
  void pop()
    {
    items.pop_front();
    --itemlistsize;
    if (items.empty()) {
      itemlistsize=0; // ...should be redundant
      offset=0;
      curpos+=data.inbytes();
      data.clear();
      } else {
      data.discard(items.front().bufferposition-offset);
      offset=items.front().bufferposition;
      curpos=items.front().fileposition;
      if (offset & (1ul<<31)) {
        for(std::list<item>::iterator it=items.begin();it!=items.end();++it)
          it->bufferposition-=offset;
        offset=0;
        }
      }
    }
  void pop(unsigned int number)
    {
    if (number==0)
      return;
    if (number>=itemlistsize) {
      itemlistsize=0;
      offset=0;
      curpos+=data.inbytes();
      data.clear();
      return;
      }

    --number;
    for (unsigned int i=0;i<number;++i)
      items.pop_front();
    itemlistsize-=number;

    pop();
    }
  void discard(uint32_t bytes)
    {
    if (bytes==0)
      return;
    if (bytes>=data.inbytes()) {
      curpos+=data.inbytes();
      data.clear();
      items.clear();
      itemlistsize=0;
      offset=0;
      return;
      }
    data.discard(bytes);
    offset+=bytes;
    curpos+=bytes;
    if (items.empty())
      return;
    std::list<item>::iterator it=items.begin();
    std::list<item>::iterator nx=it;
    ++nx;
    while(nx!=items.end()) {
      if (nx->bufferposition>offset)
        break;
      ++nx;
      it=items.erase(it);
      --itemlistsize;
      }
    if (it->bufferposition<offset) {
      it->fileposition += offset-it->bufferposition;
      it->bufferposition=offset;
      }
    curpos=items.front().fileposition;
    if (offset & (1ul<<31)) {
      for(std::list<item>::iterator it=items.begin();it!=items.end();++it)
	it->bufferposition-=offset;
      offset=0;
      }
    }
  void append(const void *d, int s)
    {
    data.putdata(d,s,true);
    }
  void appenditem(filepos_t fp, const std::string &header, const void *d, int s);
  void appenditem(filepos_t fp, int flags, pts_t pts, const void *d, int s)
    {
    if (items.empty())
      curpos=fp;
    items.push_back(item(fp,flags,pts,offset+data.inbytes()));
    ++itemlistsize;
    data.putdata(d,s,true);
    }
  const void *getdata() const
    {
    return data.data();
    }
  void *getdata()
    {
    return data.data();
    }
  const void *getdata(uint32_t bufferpos) const
    {
    return (char*)data.data()+(bufferpos-offset);
    }
  void *getdata(uint32_t bufferpos)
    {
    return (char*)data.data()+(bufferpos-offset);
    }
  const buffer &getbuffer() const
    {
    return data;
    }
  uint32_t getoffset() const
    {
    return offset;
    }
  unsigned int inbytes() const
    {
    return data.inbytes();
    }
  void discardheader()
    {
    if (!items.empty())
      items.front().clearpts();
    }
  const filepos_t &getcurpos() const
    {
    return curpos;
    }
  unsigned int getitemlistsize() const
    {
    return itemlistsize;
    }
  void audio_addpts(uint32_t startbufferpos=0, bool onepacket=false);
  uint32_t ptsbufferpos(pts_t pts);
  uint32_t closestptsbufferpos(pts_t pts);
  uint32_t fileposbufferpos(filepos_t pos)
    {
    for (itemlisttype::iterator it=items.begin();it!=items.end();++it)
      if (it->fileposition.packetposition() > pos.packetposition())
        return it->bufferposition;
      else if (it->fileposition.packetposition() == pos.packetposition())
        return it->bufferposition+(pos.packetoffset()-it->fileposition.packetoffset());
    return offset+inbytes();
    }
  const item &filepositem(filepos_t pos)
    {
    for (itemlisttype::iterator it=items.begin();it!=items.end();++it)
      if (it->fileposition.packetposition() >= pos.packetposition())
        return *it;
    return items.back();
    }
  const item &bufferpositem(uint32_t bpos) const
    {
    const item *i=&items.front();
    for (itemlisttype::const_iterator it=++items.begin();it!=items.end();++it)
      if (it->bufferposition > bpos)
        break;
      else
        i=&(*it);
    return *i;
    }

  friend int tsfile::streamreader(class streamhandle &s);
  friend int psfile::streamreader(class streamhandle &s);
  };

#endif
