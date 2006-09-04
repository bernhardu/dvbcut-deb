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

#ifndef _DVBCUT_TYPES_H
#define _DVBCUT_TYPES_H

#include <sys/types.h>
#include <stdint.h>


class filepos_t
  {
private:
  uint64_t p;
public:
  filepos_t(off_t pos, uint32_t offset):p((pos<<24)|(offset&0xffffff))
    {}
  filepos_t(uint64_t pos) : p(pos)
    {}
  ~filepos_t()
    {}

  off_t packetposition() const
    {
    return (p>>24);
    }
  uint32_t packetoffset() const
    {
    return uint32_t(p)&0xffffff;
    }
  off_t fileposition() const
    {
    return packetposition()+packetoffset();
    }
  operator uint64_t() const
    {
    return fileposition();
    }

  filepos_t &operator+=(uint32_t a)
    {
    p+=a;
    return *this;
    }
  filepos_t operator+(uint32_t a) const
    {
    return filepos_t(p+a);
    }
  bool operator<(filepos_t a) const
    {
    return p<a.p;
    }
  bool operator<=(filepos_t a) const
    {
    return p<=a.p;
    }
  bool operator>(filepos_t a) const
    {
    return p>a.p;
    }
  bool operator>=(filepos_t a) const
    {
    return p>=a.p;
    }
  bool operator==(filepos_t a) const
    {
    return p==a.p;
    }
  bool operator!=(filepos_t a) const
    {
    return p!=a.p;
    }
  };

namespace streamtype
  {
enum type { unknown, mpeg2video, mpegaudio, ac3audio };
  };

#endif
