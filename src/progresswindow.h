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

#ifndef _DVBCUT_PROGRESSWINDOW_H
#define _DVBCUT_PROGRESSWINDOW_H

#include <string>
#include <qstring.h>
#include "ui_progresswindowbase.h"
#include "logoutput.h"

class progresswindow: public QDialog, public logoutput
  {
  Q_OBJECT
protected:
  bool cancelwasclicked;
  bool waitingforclose;
  QTextCharFormat fc_normal;
  QTextCharFormat fc_head;
  QTextCharFormat fc_info;
  QTextCharFormat fc_warn;
  QTextCharFormat fc_error;
  void printmsg(const QString&str, const QTextCharFormat &format);

  void closeEvent(QCloseEvent *e);
  Ui::progresswindowbase* ui;
public:
  progresswindow(QWidget *parent = 0);
  ~progresswindow();
  virtual bool cancelled()
    {
    return cancelwasclicked;
    }
  virtual void finish();
  virtual void print(const QString &str);
  virtual void printheading(const QString &str);
  virtual void printinfo(const QString &str);
  virtual void printerror(const QString &str);
  virtual void printwarning(const QString &str);

public slots:
  virtual void setprogress(int permille);
  virtual void clickedcancel();
  };

#endif
