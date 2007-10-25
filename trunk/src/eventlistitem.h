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

#ifndef _DVBCUT_EVENTLISTIEM_H_
#define _DVBCUT_EVENTLISTIEM_H_

#include <qpixmap.h>
#include <qlistbox.h>
#include "pts.h"

class EventListItem : public QListBoxItem
  {
public:
  enum eventtype { none, start, stop, chapter, bookmark };

public:
  EventListItem( QListBox *listbox, const QPixmap &pixmap, eventtype type, int picture, int picturetype, pts_t _pts );
  ~EventListItem();

  const QPixmap *pixmap() const
    {
    return &pm;
    }
  int getpicture() const
    {
    return pic;
    }
  pts_t getpts() const
    {
    return pts;
    }
  enum eventtype geteventtype() const
    {
    return evtype;
    }
  void seteventtype(enum eventtype type)
    {
    evtype=type;
    return;
    }

  int	 height( const QListBox *lb ) const;
  int	 width( const QListBox *lb )  const;

  int rtti() const;
  static int RTTI()
    {
    return 294268923;
    }

protected:
  void paint( QPainter * );

private:
  QPixmap pm;
  enum eventtype evtype;
  int pic;
  int pictype;
  pts_t pts;

  QString getstring() const;

  static QListBoxItem *afterwhich(QListBox *lb, int picture);
  };

#endif // ifndef _EVENTLISTIEM_H_
