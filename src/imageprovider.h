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

#ifndef _DVBCUT_IMAGEPROVIDER_H
#define _DVBCUT_IMAGEPROVIDER_H

#include <utility>
#include <qimage.h>
class mpgfile;
class busyindicator;

#define IMAGEPROVIDER_STANDARD 1
#define IMAGEPROVIDER_UNSCALED 2
#define IMAGEPROVIDER_DIFFERENCE 3
#define IMAGEPROVIDER_DIFFERENCE_UNSCALED 4

/**
@author Sven Over
*/
class imageprovider
  {
protected:
  int RTTI;
  mpgfile &m;
  int maxcachedframes;
  double viewscalefactor;
  typedef std::pair<int,QImage> framecacheitem;
  std::list<framecacheitem> framecache;
  busyindicator *busyind;

  void shrinkcache(int free=0);
  virtual void decodepicture(int picture, bool decodeallgop=false);
  
public:
  imageprovider(mpgfile &mpg, busyindicator *bi=0, bool unscaled=false, double factor=1.0, int cachesize=50);
  virtual ~imageprovider();
  int rtti() const
    {
    return RTTI;
    }
  QImage getimage(int picture, bool decodeallgop=false);
  void clearcache()
    {
    framecache.clear();
    }
  void setviewscalefactor(double factor)
  {
  if (factor<=0.0) factor=1.0;
  if (factor==viewscalefactor) return;
  clearcache();
  viewscalefactor=factor;
  }

  };

#endif
