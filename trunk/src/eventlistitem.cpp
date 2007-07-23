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

#include <qsimplerichtext.h>
#include <qapplication.h>
#include <qpainter.h>
#include <qimage.h>
#include "eventlistitem.h"

EventListItem::EventListItem( QListBox *listbox, const QPixmap &pixmap,
                              eventtype type, int picture, int picturetype, pts_t _pts ) :
    QListBoxItem(listbox, afterwhich(listbox,picture)), pm(pixmap), evtype(type), pic(picture), pictype(picturetype), pts(_pts)
  {
  if (pm.width()>160 || pm.height()>90)
    pm=pm.convertToImage().smoothScale(130,90,QImage::ScaleMin);
  }

EventListItem::~EventListItem()
  {}


int EventListItem::rtti() const
  {
  return RTTI();
  }



void EventListItem::paint( QPainter *painter )
  {
  int itemHeight = height( listBox() );
  int x=3;

  if ( !pm.isNull() ) {
    painter->drawPixmap( x, (itemHeight-pm.height())/2, pm);
    x+=pm.width()+3;
    }

  if (listBox()) {
    QSimpleRichText rt(getstring(),listBox()->font());
    rt.setWidth(1000);

    QColorGroup cg(listBox()->colorGroup());

    if (isSelected()) {
      QColor c=cg.color(QColorGroup::Text);
      cg.setColor(QColorGroup::Text,cg.color(QColorGroup::HighlightedText));
      cg.setColor(QColorGroup::HighlightedText,c);
      }

    rt.draw(painter,x,(itemHeight-rt.height())/2,QRect(),cg);
    }

  }

int EventListItem::height( const QListBox*  ) const
  {
  int h=0;

  if (!pm.isNull())
    h=pm.height();

  return QMAX( h+6, QApplication::globalStrut().height() );
  }

int EventListItem::width( const QListBox* lb ) const
  {
  int width=3;

  if (!pm.isNull())
    width += pm.width()+3;

  if (lb) {
    QSimpleRichText rt(getstring(),lb->font());
    rt.setWidth(1000); //drawinglistbox->width());
    width+=rt.widthUsed()+3;
    }

  return QMAX( width,
               QApplication::globalStrut().width() );
  }

QString EventListItem::getstring() const
  {
  const char *type="";
  if (evtype==start)
    type="<font size=\"+1\"><b>START</b></font><br>";
  else if (evtype==stop)
    type="<font size=\"+1\"><b>STOP</b></font><br>";
  else if (evtype==chapter)
    type="CHAPTER<br>";
  else if (evtype==bookmark)
    type="BOOKMARK<br>";

  return QString().sprintf("%s%02d:%02d:%02d.%03d<br>%d (%c)",type,
                           int(pts/(3600*90000)),
                           int(pts/(60*90000))%60,
                           int(pts/90000)%60,
                           int(pts/90)%1000,
                           pic,
                           ((const char *)".IPB....")[pictype&7]);
  }

QListBoxItem *EventListItem::afterwhich(QListBox *lb, int picture)
  {
  if (!lb)
    return 0;
  QListBoxItem *after=0;

  for (QListBoxItem *next=lb->firstItem();next;after=next,next=next->next())
    if (next->rtti()==RTTI())
      if ( ((EventListItem*)(next))->pic > picture)
        break;

  return after;
  }

