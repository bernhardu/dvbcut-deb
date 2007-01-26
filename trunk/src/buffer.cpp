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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
// #include <stdio.h>
#include <stdint.h>

#include "port.h"
#include "buffer.h"

#ifndef O_BINARY
#define O_BINARY    0
#endif /* O_BINARY */

#ifndef MAP_FAILED
#define MAP_FAILED	((void*)-1)
#endif

buffer::buffer(unsigned int _size):size(_size), readpos(0), writepos(0), wrtot(0)
  {
  if (size > 0)
    d = malloc(size);
  }

buffer::~buffer()
  {
  if (d)
    free(d);
  }

void buffer::relax()
  {
  if (readpos == 0)
    return;
  if (writepos != readpos)
    memmove(d, (u_int8_t *) d + readpos, writepos - readpos);
  writepos -= readpos;
  readpos = 0;
  }

void buffer::resize(unsigned int newsize)
  {
  if (newsize == 0 || newsize == size)
    return;
  if (newsize < inbytes())
    writepos = readpos + newsize;

  void *olddata = d;

  d = malloc(newsize);
  memcpy(d, (u_int8_t *) olddata + readpos, inbytes());
  free(olddata);

  size = newsize;
  writepos -= readpos;
  readpos = 0;
  }

unsigned int buffer::putdata(const void *data, unsigned int len, bool autoresize)
  {
  if (len > freebytes()) {
    if (autoresize && len<(1u<<30))
      // Only resize if len has a reasonable value, to circumvent problems
      // when (signed int)len<0 (which can only be due to bug)
      resize(2*(inbytes()+len));
    else
      len = freebytes();
    }
  if (len == 0)
    return 0;
  if (readpos == writepos)
    readpos = writepos = 0;
  else if (len > (size - writepos))
    relax();
  memcpy((u_int8_t *) d + writepos, data, len);
  writepos += len;
  wrtot += len;
  return len;
  }

unsigned int buffer::getdata(void *data, unsigned int len)
  {
  if (len > inbytes())
    len = inbytes();
  if (len == 0)
    return 0;
  memcpy(data, (u_int8_t *) d + readpos, len);
  readpos += len;
  if (readpos == writepos)
    readpos = writepos = 0;
  return len;
  }

int buffer::readdata(int fd)
  {
  relax();
  if (writepos == size)
    return 0;
  int r = read(fd, (u_int8_t *) d + writepos, size - writepos);

  if (r > 0) {
    writepos += r;
    wrtot += r;
    }
  return r;
  }

int buffer::writedata(int fd)
  {
  if (readpos == writepos) {
    readpos = writepos = 0;
    return 0;
    }
  int w = write(fd, (u_int8_t *) d + readpos, writepos - readpos);

  if (w > 0)
    readpos += w;
  return w;
  }


// INBUFFER *****************************************************************

long inbuffer::pagesize=0;

inbuffer::inbuffer(unsigned int _size, int _fd, bool tobeclosed, unsigned int _mmapsize) :
    d(0), size(_size), mmapsize(_mmapsize), readpos(0), writepos(0), fd(_fd),
    close(tobeclosed), eof(false), pos(0), needseek(0),
    filesize(-1), filesizechecked(false), mmapped(false)
  {
  if (!pagesize)
    pagesize=sysconf(_SC_PAGESIZE);
  if (fd<0)
    return;
  setup();
  }

inbuffer::inbuffer(inbuffer &b, unsigned int _size, unsigned int _mmapsize) :
    d(0),fd(b.fd),close(b.close),eof(b.eof), needseek(b.needseek),
    filesize(b.filesize), filesizechecked(b.filesizechecked),
    mmapped(b.mmapped)
  {
  b.close=false;
  if (_size==0)
    size=b.size;
  else
    size=_size;

  if (_mmapsize==0)
    mmapsize=b.mmapsize;
  else
    mmapsize=_mmapsize;

  if (mmapped) {
    if (mmapsize>0)
      size=mmapsize;
    pos=b.pos-(b.pos%pagesize);
    readpos=b.readpos+(b.pos%pagesize);
    writepos=size;
    if (pos+writepos>filesize)
      writepos=filesize-pos;
    d=::mmap(0,writepos,PROT_READ,MAP_SHARED,fd,pos);
    if (d==MAP_FAILED) {
      size=_size;
      mmapped=false;
      } else
      return;
    }

  if (b.inbytes()>size)
    size=b.inbytes();
  if (size<1024)
    size=1024;
  d=malloc(size);

  uint32_t discard=0;
  if (b.writepos>size)
    discard=b.writepos-size;

  readpos=b.readpos-discard;
  writepos=b.writepos-discard;
  pos=b.pos+discard;
  memcpy(d,(char*)b.d+discard,writepos);
  }

void inbuffer::checkfilesize()
  {
  filesizechecked=true;
  filesize=lseek(fd,0,SEEK_END);

  if (filesize>0) // seek was successful and file has non-zero size
    needseek=-1;
  }

inbuffer::~inbuffer()
  {
  if (d) {
    if (mmapped)
      ::munmap(d,writepos);
    else
      free(d);
    }
  if (close && fd>=0)
    ::close(fd);
  }

int inbuffer::open(const char* filename)
  {
  if (filename[0]=='-' && filename[1]==0) // use stdint
    {
    close=false;
    needseek=0;
    return fd=STDIN_FILENO;
    }

  close=true;
  needseek=0;
  fd=::open(filename,O_RDONLY|O_BINARY);
  if (fd<0)
    return fd;

  setup();

  return fd;
  }

bool inbuffer::statfilesize(dvbcut_off_t& _size) const
  {
#ifdef __WIN32__
  struct _stati64 st;
  if ((::_fstati64(fd,&st)==0)&&(S_ISREG(st.st_mode))) {
#else /* __WIN32__ */
  struct stat st;
  if ((::fstat(fd,&st)==0)&&(S_ISREG(st.st_mode))) {
#endif /* __WIN32__ */
	_size=st.st_size;
	return true;
	}
  return false;
  }

void inbuffer::setup()
  {
  unsigned int _size=size;

  if (statfilesize(filesize)) {
    filesizechecked=true;
    if (mmapsize>0)
      size=mmapsize;
    if (size>filesize)
      size=filesize;
    d=::mmap(0,size,PROT_READ,MAP_SHARED,fd,0);
    if (d==MAP_FAILED) {
      size=_size;
      mmapped=false;
      } else {
      writepos=size;
      mmapped=true;
      return;
      }
    }

  if (size > 0)
    d = malloc(size);

  }

int inbuffer::providedata(unsigned int amount, long long position)
  {
  if (amount>size)
    amount=size;

  if (position>=pos && position+amount<=pos+writepos) {
    readpos=position-pos;
    return writepos-readpos;
    }

  if (mmapped) {
    if (position>=filesize)
      return 0;
    if (position+amount>filesize)
      amount=filesize-position;
    munmap(d,writepos);
    pos=position+amount-size/2;
    if (pos<0)
      pos=0;
    else if (pos>position)
      pos=position;
    pos-=pos%pagesize;
    readpos=position-pos;
    writepos=size;
    if (pos+writepos>filesize) {
      if (filesize>size) {
        pos=filesize-size;
        pos-=pos%pagesize;
        pos+=pagesize;
        if (pos<0)
          pos=0;
        readpos=position-pos;
        }
      writepos=filesize-pos;

      }
    d=::mmap(0,writepos,PROT_READ,MAP_SHARED,fd,pos);
    if (d==MAP_FAILED) {
      readpos=writepos=0;
      d=malloc(size);
      mmapped=false;
      } else {
      return writepos-readpos;
      }
    }

  if (position>=pos && position<pos+writepos) {
    unsigned int pp=position-pos;
    if (pp>0) {
      writepos-=pp;
      memmove(d,(char*)d+pp,writepos);
      }
    } else {
    needseek+=position-(pos+writepos);
    writepos=0;
    }

  pos=position;
  readpos=0;

  if (needseek && (lseek(fd,pos+writepos,SEEK_SET)<0)) {
    if (needseek>0) {
      while (needseek>0) {
        int seek=size-writepos;
        if (seek>needseek)
          seek=needseek;
        int rd=::read(fd,(char*)d+writepos,seek);
        if (rd<0)
          return rd;
        if (rd==0) {
          eof=true;
          return 0;
          }
        needseek-=rd;
        }
      } else
      return -1;
    }
  needseek=0;

  while (writepos<amount) {
    int rd=::read(fd,(char*)d+writepos,size-writepos);
    if (rd<0)
      return rd;
    if (rd==0) {
      eof=true;
      break;
      }
    writepos+=rd;
    }

  return writepos;
  }

// OUTBUFFER ****************************************************************

outbuffer::~outbuffer()
  {
  if (close && fd>=0)
    ::close(fd);
  }

int outbuffer::open(const char* filename)
  {
  close=true;
  return fd=::open(filename,O_WRONLY|O_CREAT|O_TRUNC|O_BINARY);
  }

int outbuffer::putdata(const void *data, unsigned int len, bool autoresize)
  {
  if (freebytes()>=len)
    return putdata(data,len,autoresize);

  int wrn=flush(fd);
  if (wrn<0)
    return wrn;

  while (len>0) {
    int wr=::write(fd,data,len);
    if (wr<0)
      return wr;
    data=(char*)data+wr;
    len-=wr;
    wrn+=wr;
    wrtot+=wr;
    }

  return wrn;
  }
