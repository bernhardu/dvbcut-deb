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

#ifndef _DVBCUT_BUFFER_H_
#define _DVBCUT_BUFFER_H_

#include "port.h"

class buffer
  {
protected:
  void *d;
  unsigned int size, readpos, writepos;
  unsigned long long wrtot;

  void relax();

public:
  buffer(unsigned int _size);
  ~buffer();

  void writeposition(int v)
    {
    if (v<-(signed)writepos)
      writepos=0;
    else {
      writepos+=v;
      if (writepos>size)
        writepos=size;
      }
    }
  unsigned int inbytes() const
    {
    return writepos-readpos;
    }
  unsigned int freebytes() const
    {
    return size-writepos+readpos;
    }
  bool empty() const
    {
    return writepos==readpos;
    }
  bool full() const
    {
    return (readpos==0)&&(writepos==size);
    }
  unsigned long long written() const
    {
    return wrtot;
    }
  void *data() const
    {
    return (void*)((unsigned char*)d+readpos);
    }
  void *writeptr() const
    {
    return (void*)((unsigned char*)d+writepos);
    }
  int discard(unsigned int len)
    {
    if (len>inbytes())
      len=inbytes();
    readpos+=len;
    return len;
    }
  int discardback(unsigned int len)
    {
    if (len>inbytes())
      len=inbytes();
    writepos-=len;
    return len;
    }
  void resize(unsigned int newsize);
  void clear()
    {
    readpos=writepos=0;
    wrtot=0;
    }
  unsigned int getsize() const
    {
    return size;
    }
  unsigned int putdata(const void *data, unsigned int len, bool autoresize=false);
  unsigned int getdata(void *data, unsigned int len);
  int readdata(int fd);
  int writedata(int fd);

  int writedata(int fd, unsigned int minspace)
    {
    int wrn=0;
    if (minspace>size) {
      while (readpos<writepos) {
        int w=writedata(fd);
        if (w<0)
          return w;
        wrn+=w;
        }
      readpos=writepos=0;
      resize(minspace);
      } else {
      while (freebytes()<minspace) {
        int w=writedata(fd);
        if (w<0)
          return w;
        wrn+=w;
        }
      }
    return wrn;
    }

  int flush(int fd)
    {
    int wrn=0;
    while (inbytes()>0) {
      int w=writedata(fd);
      if (w<0)
        return w;
      wrn+=w;
      }
    return wrn;
    }
  };

class inbuffer {
protected:
  void *d;
  unsigned int size, mmapsize, readpos, writepos;

  struct infile {
    dvbcut_off_t off;
    dvbcut_off_t end;
    int fd;
  };
  std::vector<infile> files;
  bool eof;
  dvbcut_off_t pos;
  dvbcut_off_t filesize;
  bool mmapped;
  static long pagesize;
  bool sequential;

  void close();

public:
  inbuffer(unsigned int _size, unsigned int mmapsize = 0);
  ~inbuffer();
  bool open(std::string filename, std::string *errmsg = 0);
  void reset();

  const void *data() const { return (void*)((char*)d + readpos); }
  unsigned int getsize() const { return size; }
  unsigned int inbytes() const { return writepos - readpos; }
  bool iseof() const { return eof; }

  int providedata(unsigned int amount, long long position);
  int providedata(unsigned int amount) {
    if (amount <= inbytes())
      return inbytes();
    return providedata(amount, pos + readpos);
  }
  void discarddata(unsigned int amount) {
    readpos += amount;
    if (readpos >= writepos) {
      pos += readpos;
      readpos = 0;
      writepos = 0;
    }
  }
  dvbcut_off_t getfilesize() const { return filesize; }
  dvbcut_off_t getfilepos() const { return pos + readpos; }
  int getfilenum(dvbcut_off_t offset, dvbcut_off_t &fileoff);
  void setsequential(bool flag) { sequential = flag; }
};

class outbuffer : protected buffer
  {
protected:
  int fd;
  bool close;
public:
  outbuffer(unsigned int _size, int _fd=-1, bool tobeclosed=true) :
      buffer(_size), fd(_fd), close(tobeclosed)
    {}
  ~outbuffer();
  int open(const char *filename);
  int putdata(const void *data, unsigned int len, bool autoresize=false);

  using buffer::written;
  using buffer::getsize;
  using buffer::resize;
  };

#endif
