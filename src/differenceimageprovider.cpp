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

#include <qcolor.h>

#include "differenceimageprovider.h"
#include "avframe.h"
#include "mpgfile.h"
#include "busyindicator.h"

differenceimageprovider::differenceimageprovider(mpgfile &mpg, int basepicture, busyindicator *bi, 
                                                 bool unscaled, double factor, int cachesize)
    : imageprovider(mpg,bi,unscaled,factor,cachesize), basepic(basepicture)
  {
  RTTI=unscaled?IMAGEPROVIDER_DIFFERENCE_UNSCALED:IMAGEPROVIDER_DIFFERENCE;
  baseimg=imageprovider(mpg,bi?new busyindicator(*bi):0,true).getimage(basepic);
  }


differenceimageprovider::~differenceimageprovider()
  {}


static inline int square(int x)
  {
  return x*x;
  }
static inline QRgb mixcolors(QRgb a, QRgb b, int num, int den)
  {
  return qRgb(
           (qRed(b)*num+qRed(a)*(den-num))/den,
           (qGreen(b)*num+qGreen(a)*(den-num))/den,
           (qBlue(b)*num+qBlue(a)*(den-num))/den
         );
  }

void differenceimageprovider::decodepicture(int picture, bool decodeallgop)
  {
  std::list<avframe*> framelist;
  int startpic=m.lastiframe(picture);
  m.decodegop(startpic,decodeallgop?-1:(picture+1),framelist);

  for (std::list<avframe*>::iterator it=framelist.begin();it!=framelist.end();++it) {
    QImage im=(*it)->getqimage(false);
    int displaywidth=(*it)->getdisplaywidth();
    delete *it;

    if (im.size()!=baseimg.size())
      im=im.scaled(baseimg.size());

    if (im.format() == QImage::Format_RGB888 &&
        baseimg.format() == QImage::Format_RGB888)
    {
      for (int y = 0; y < baseimg.height(); y++) {
        for (int x = 0; x < baseimg.width(); x++) {
          QRgb imd = im.pixel(x, y);
          QRgb bimd = baseimg.pixel(x, y);

          int dist = square(qRed(imd)-qRed(bimd)) +
                     square(qGreen(imd)-qGreen(bimd)) +
                     square(qBlue(imd)-qBlue(bimd));
          if (dist>1000)
            dist=1000;

          im.setPixel(x, y, mixcolors(imd, ((x/16+y/16)&1) ? qRgb(64,64,64) : qRgb(192,192,192), 1000-dist, 1000));
        }
      }
    } else {
      fprintf(stderr, "%s: wrong format.\n", __FUNCTION__);
    }


    if ((RTTI!=IMAGEPROVIDER_DIFFERENCE_UNSCALED && displaywidth!=im.width())||(viewscalefactor!=1.0))
      im=im.scaled(int(((RTTI!=IMAGEPROVIDER_DIFFERENCE_UNSCALED)?displaywidth:im.width())/viewscalefactor+0.5),
	int(im.height()/viewscalefactor+0.5));
    framecache.push_front(framecacheitem(startpic++,im));
    }
  }
