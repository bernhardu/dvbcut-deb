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

#include "imageprovider.h"
#include "mpgfile.h"
#include "avframe.h"
#include "busyindicator.h"

imageprovider::imageprovider(mpgfile &mpg, busyindicator *bi, bool unscaled, double factor, int cachesize) :
    RTTI(IMAGEPROVIDER_STANDARD), m(mpg), maxcachedframes(cachesize), viewscalefactor(factor),
    busyind(bi)
  {
  if (unscaled)
    RTTI=IMAGEPROVIDER_UNSCALED;
  }


imageprovider::~imageprovider()
  {
  if (busyind)
    delete busyind;
  }


void imageprovider::shrinkcache(int free)
  {
  int keep=maxcachedframes;
  if (free>0)
    keep-=free;

  std::list<framecacheitem>::iterator it=framecache.begin();
  for(int i=0;i<keep&&it!=framecache.end();++it,++i)
    ;

  while(it!=framecache.end())
    it=framecache.erase(it);
  }

QImage imageprovider::getimage(int picture, bool decodeallgop)
  {
  if (picture < 0 || picture >= m.getpictures())
    return QImage();

  for (std::list<framecacheitem>::iterator it = framecache.begin(); it != framecache.end(); ++it)
    if (it->first == picture) {
      framecache.push_front(*it);
      framecache.erase(it);
      return framecache.front().second;
      }

  if (busyind)
    busyind->setbusy(true);
  shrinkcache(1);

  decodepicture(picture,decodeallgop);
  if (busyind)
    busyind->setbusy(false);

  for (std::list<framecacheitem>::iterator it = framecache.begin(); it != framecache.end(); ++it)
    if (it->first == picture) {
      framecache.push_front(*it);
      framecache.erase(it);
      return framecache.front().second;
      }

  return QImage();
  }

void imageprovider::decodepicture(int picture, bool decodeallgop)
  {
  std::list<avframe*> framelist;
  int startpic=m.lastiframe(picture);
  m.decodegop(startpic,decodeallgop?-1:(picture+1),framelist);

  for (std::list<avframe*>::iterator it=framelist.begin();it!=framelist.end();++it) {
    framecache.push_front(framecacheitem(startpic++,(*it)->getqimage(RTTI!=IMAGEPROVIDER_UNSCALED,viewscalefactor)));
    delete *it;
    }
  }
