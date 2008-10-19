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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
// #include <cstdio>
#include <stdint.h>
#include <cassert>

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
    eof(false), pos(0), filesize(0), mmapped(false),
    sequential(false), pipe_mode(false)
{
  if (!pagesize)
    pagesize=sysconf(_SC_PAGESIZE);
}

inbuffer::~inbuffer() {
  close();
}

bool
inbuffer::open(std::string filename, std::string *errmsg) {
  int fd;

  fd = ::open(filename.c_str(), O_RDONLY | O_BINARY);
  if (fd == -1) {
    if (errmsg)
      *errmsg = filename + ": open: " + strerror(errno);
    return false;
  }
  if (!open(fd, errmsg, true, filename)) {
    if (errmsg)
      *errmsg = filename + ": " + *errmsg;
    return false;
  }
  return true;
}

bool
inbuffer::open(int fd, std::string *errmsg, bool closeme, std::string filename) {
  infile f;

  f.fd = fd;
  f.name = filename;
  f.closeme = closeme;
  if (pipe_mode) {
    // no more files please!
    if (errmsg)
      *errmsg = std::string("open: can't add more input files");
    if (f.closeme)
      ::close(f.fd);
    return false;
  }
#ifdef __WIN32__
  struct _stati64 st;
  if (::_fstati64(f.fd, &st) == -1) {
#else /* __WIN32__ */
  struct stat st;
  if (::fstat(f.fd, &st) == -1) {
#endif /* __WIN32__ */
    if (errmsg)
      *errmsg = std::string("fstat: ") + strerror(errno);
    if (f.closeme)
      ::close(f.fd);
    return false;
  }
  if (S_ISREG(st.st_mode)) {
    f.off = filesize;
    f.end = filesize += st.st_size;
    files.push_back(f);
    return true;
  }
  /*
   * Input is a (named) pipe or device.
   * This is only allowed in single-file (aka "pipe") mode.
   */
  if (files.empty()) {
    pipe_mode = true;
    f.off = 0;
    f.end = filesize = INT64_MAX;
    files.push_back(f);
    return true;
  }
  if (errmsg)
    *errmsg = std::string("not a regular file");
  if (f.closeme)
    ::close(f.fd);
  return false;
}

void
inbuffer::close() {
  // close all files
  std::vector<infile>::const_iterator i = files.begin();
  while (i != files.end()) {
    if (i->closeme)
      ::close(i->fd);
    ++i;
  }
  files.clear();
  // free buffer
#ifndef _WIN32
  if (mmapped)
    ::munmap(d, writepos);
  else
#endif
  if (d)
    free(d);
  mmapped = false;
  d = 0;
  pipe_mode = false;
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
inbuffer::pipedata(unsigned int amount, long long position) {
  std::vector<infile>::const_iterator i = files.begin();
  assert(i != files.end());

  // allocate read buffer
  if (!d) {
    d = malloc(size);
    if (!d) {
      fprintf(stderr, "inbuffer::pipedata: can't allocate %ld bytes: %s\n",
	(long)size, strerror(errno));
      abort();
    }
    readpos = 0;
    writepos = 0;
  }

  if (amount > size)
    amount = size;

  // discard unused data
  readpos = 0;
  while (pos + writepos <= position) {
    pos += writepos;
    writepos = 0;
    ssize_t n = ::read(i->fd, (char*)d, size);
    if (n == -1)
      return -1;
    if (n == 0) {
      eof = true;
      return 0;
    }
    writepos = n;
  }
  if (pos < position) {
    size_t pp = position - pos;
    writepos -= pp;
    memmove(d, (char*)d + pp, writepos);
    pos = position;
  }

  // now read the data we want
  while (writepos < amount) {
    size_t len = size - writepos;
    assert(len > 0);
    ssize_t n = ::read(i->fd, (char*)d + writepos, len);
    if (n == -1)
      return -1;
    if (n == 0) {
      eof = true;
      break;
    }
    writepos += n;
  }
  return inbytes();
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

  if (pipe_mode) {
    if (position < pos)
      return -1;	// can't go backwards!
    return pipedata(amount, position);
  }

  std::vector<infile>::const_iterator i = files.begin();
  assert(i != files.end());	// otherwise we would have returned already
  while (position >= i->end) {
#ifdef POSIX_FADV_DONTNEED
    if (sequential) {
      off_t len = i->end - i->off;
      posix_fadvise(i->fd, 0, len, POSIX_FADV_DONTNEED);
    }
#endif
    ++i;
    assert(i != files.end());
  }
  assert(position >= i->off);

#ifndef _WIN32
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
#ifdef POSIX_FADV_DONTNEED
    if (sequential) {
      // we're done with earlier parts
      if (relpos)
	posix_fadvise(i->fd, 0, relpos, POSIX_FADV_DONTNEED);
    }
#endif
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
#endif

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
  // reuse existing data if possible
  if (position >= pos && position < pos + writepos) {
    unsigned int pp = position - pos;
    if (pp > 0) {
      writepos -= pp;
      memmove(d, (char*)d + pp, writepos);
    }
  }
  else {
    writepos = 0;
  }
  readpos = 0;
  pos = position;
  bool needseek = true;
  while (writepos < amount) {
    dvbcut_off_t seekpos = pos + writepos;
    while (seekpos >= i->end) {
      ++i;
      assert(i != files.end());
      needseek = true;
    }
    assert(seekpos >= i->off);
    if (needseek) {
#ifdef __WIN32__
      __int64 relpos = seekpos - i->off;
      if (::_lseeki64(i->fd, relpos, SEEK_SET) == -1)
#else /* __WIN32__ */
      off_t relpos = seekpos - i->off;
      if (::lseek(i->fd, relpos, SEEK_SET) == -1)
#endif /* __WIN32__ */
	return -1;
      needseek = false;
    }
    size_t len = size - writepos;
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
  }
  return inbytes();
}

int
inbuffer::getfilenum(dvbcut_off_t offset, dvbcut_off_t &fileoff) { 
  std::vector<infile>::const_iterator it = files.begin();
  unsigned int num = 0;
  while (it != files.end()) {
    if (offset < it->end) {
      fileoff = it->off;
      return num;
    }
    ++num;
    ++it;
  }
  return -1;
}
  
std::string 
inbuffer::getfilename(int filenum) {
  return files.at(filenum).name;
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
  return fd=::open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
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
