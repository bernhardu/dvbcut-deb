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
#include <assert.h>

#include <string>
#include <vector>

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

inbuffer::inbuffer(unsigned int _size, unsigned int _mmapsize) :
    d(0), size(_size), mmapsize(_mmapsize), readpos(0), writepos(0),
    eof(false), pos(0), filesize(0), mmapped(false)
{
  if (!pagesize)
    pagesize=sysconf(_SC_PAGESIZE);
}

inbuffer::~inbuffer() {
  close();
}

bool
inbuffer::open(std::string filename, std::string *errmsg) {
  infile f;

  f.fd = ::open(filename.c_str(), O_RDONLY | O_BINARY);
  if (f.fd == -1) {
    if (errmsg)
      *errmsg = filename + ": open: " + strerror(errno);
    return false;
  }
  off_t size = ::lseek(f.fd, 0, SEEK_END);
  if (size == -1) {
    if (errmsg)
      *errmsg = filename + ": lseek: " + strerror(errno);
    ::close(f.fd);
    return false;
  }
  f.off = filesize;
  f.end = filesize += size;
  files.push_back(f);
  return true;
}

void
inbuffer::close() {
  // close all files
  std::vector<infile>::const_iterator i = files.begin();
  while (i != files.end()) {
    ::close(i->fd);
    ++i;
  }
  files.clear();
  // free buffer
  if (mmapped)
    ::munmap(d, writepos);
  else if (d)
    free(d);
  mmapped = false;
  d = 0;
}

void
inbuffer::reset() {
  close();
  // re-initialize members
  readpos = 0;
  writepos = 0;
  eof = false;
  pos = 0;
  filesize = 0;
}

int
inbuffer::providedata(unsigned int amount, long long position) {
  if (position < 0 || position >= filesize)
    return 0;

  if (position + amount > filesize)
    amount = filesize - position;
  if (position >= pos && position + amount <= pos + writepos) {
    readpos = position - pos;
    return inbytes();
  }

  std::vector<infile>::const_iterator i = files.begin();
  assert(i != files.end());	// otherwise we would have returned already
  while (position >= i->end) {
    ++i;
    assert(i != files.end());
  }
  assert(position >= i->off);

  // remove old mapping, if any
  if (mmapped) {
    ::munmap(d, writepos);
    mmapped = false;
    d = 0;
  }

  if (mmapsize > 0 && position + amount <= i->end) {
    // calculate mmap window
    dvbcut_off_t newpos = position + amount - mmapsize / 2;
    if (newpos > position)
      newpos = position;
    else if (newpos < i->off)
      newpos = i->off;
    // align to pagesize
    // note: relpos must be aligned, NOT newpos!
    off_t relpos = newpos - i->off;
    size_t modulus = relpos % pagesize;
    relpos -= modulus;
    newpos -= modulus;
    size_t len = mmapsize;
    if (newpos + len > i->end)
      len = i->end - newpos;
    void *ptr = ::mmap(0, len, PROT_READ, MAP_SHARED, i->fd, relpos);
    if (ptr != MAP_FAILED) {
      // mmap succeeded
      if (d)
	free(d);
      d = ptr;
      readpos = position - newpos;
      writepos = len;
      pos = newpos;
      mmapped = true;
      return inbytes();
    }
  }

  // allocate read buffer
  if (!d) {
    d = malloc(size);
    if (!d) {
      fprintf(stderr, "inbuffer::providedata: can't allocate %ld bytes: %s\n",
	(long)size, strerror(errno));
      abort();
    }
    readpos = 0;
    writepos = 0;
    pos = 0;
  }

  if (amount > size)
    amount = size;
  dvbcut_off_t seekpos = position;
  // reuse existing data if possible
  if (position >= pos && position < pos + writepos) {
    unsigned int pp = position - pos;
    if (pp > 0) {
      writepos -= pp;
      memmove(d, (char*)d + pp, writepos);
    }
    seekpos += writepos;
  }
  else {
    writepos = 0;
  }
  readpos = 0;
  pos = position;
  bool needseek = true;
  while (writepos < amount) {
    while (seekpos >= i->end) {
      ++i;
      assert(i != files.end());
      needseek = true;
    }
    assert(seekpos >= i->off);
    off_t relpos = seekpos - i->off;
    if (::lseek(i->fd, relpos, SEEK_SET) == -1)
      return -1;
    needseek = false;
    size_t len = amount - writepos;
    if (len > i->end - seekpos)
      len = i->end - seekpos;
    assert(len > 0);
    ssize_t n = ::read(i->fd, (char*)d + writepos, len);
    if (n == -1)
      return -1;
    if (n == 0) {	// this should NOT happen!
      eof = true;
      break;
    }
    writepos += n;
    seekpos += n;
  }
  return inbytes();
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
