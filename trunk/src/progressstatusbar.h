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

#ifndef _DVBCUT_PROGRESSSTATUSBAR_H
#define _DVBCUT_PROGRESSSTATUSBAR_H

#include <qobject.h>
#include "logoutput.h"

class QStatusBar;
class QProgressBar;
class QPushButton;
class QLabel;

/**
@author Sven Over
*/
class progressstatusbar : public QObject, public logoutput
  {
  Q_OBJECT

protected:
  bool cancelwasclicked;
  QStatusBar *statusbar;
  QProgressBar *progressbar;
  QPushButton *cancelbutton;
  QLabel *label;

public:
  progressstatusbar(QStatusBar *bar);
  ~progressstatusbar();

  virtual bool cancelled()
    {
    return cancelwasclicked;
    }
  virtual void finish();
  virtual void print(const char *fmt, ...);

public slots:
  virtual void setprogress(int permille);
  virtual void clickedcancel();
  };

#endif
