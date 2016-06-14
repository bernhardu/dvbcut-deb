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

#include <qlabel.h>
#include <QHBoxLayout>
#include "eventlistitem.h"
#include "settings.h"

EventListItem::EventListItem( QListWidget *listbox, const QPixmap &pixmap,
                              eventtype type, int picture, int picturetype, pts_t _pts ) :
    QListWidgetItem("", listbox), evtype(type), pic(picture), pictype(picturetype), pts(_pts)
{
    QWidget *w = new QWidget();

    QLabel *label_pic = new QLabel(w);
    label_pic->setPixmap(pixmap.scaledToHeight(80, Qt::SmoothTransformation));

    label_text = new QLabel(getstring(), w);

    QHBoxLayout *hbox = new QHBoxLayout();
    hbox->addWidget(label_pic);
    hbox->addWidget(label_text);
    w->setLayout(hbox);

    listbox->setItemWidget(this, w);

    setSizeHint(label_pic->size());
}

EventListItem::~EventListItem()
  {}

QString EventListItem::getstring() const
  {
  QString label;
  if (evtype==start)
    label = settings().start_label;
  else if (evtype==stop)
    label = settings().stop_label;
  else if (evtype==chapter)
    label = settings().chapter_label;
  else if (evtype==bookmark)
    label = settings().bookmark_label;

  return label + QString().sprintf("<br>%02d:%02d:%02d.%03d<br>%d (%c)",
                           int(pts/(3600*90000)),
                           int(pts/(60*90000))%60,
                           int(pts/90000)%60,
                           int(pts/90)%1000,
                           pic,
                           ((const char *)".IPB....")[pictype&7]);
  }

/*virtual*/ bool EventListItem::operator<(const QListWidgetItem &other) const
{
    const EventListItem *item = dynamic_cast<const EventListItem *>(&other);
    if (!item)
        return false;

    if (pic != item->pic)
        return pic < item->pic;
    else
        return evtype < item->evtype;
}
