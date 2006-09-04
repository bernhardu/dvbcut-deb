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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include <qprogressbar.h>
#include <qtextbrowser.h>
#include <qpushbutton.h>
#include <qapplication.h>
#include "progresswindow.h"

progresswindow::progresswindow(QWidget *parent, const char *name)
    :progresswindowbase(parent, name, true), logoutput(),
    cancelwasclicked(false), waitingforclose(false)
  {
  QStyleSheetItem *item;
  item = new QStyleSheetItem( logbrowser->styleSheet(), "h" );
  item->setFontWeight( QFont::Bold );
  item->setFontUnderline( TRUE );

  item = new QStyleSheetItem( logbrowser->styleSheet(), "info" );

  item = new QStyleSheetItem( logbrowser->styleSheet(), "warn" );
  item->setColor( "red" );

  item = new QStyleSheetItem( logbrowser->styleSheet(), "error" );
  item->setColor( "red" );
  item->setFontWeight( QFont::Bold );
  item->setFontUnderline( TRUE );

  show();
  qApp->processEvents();
  }

void progresswindow::closeEvent(QCloseEvent *e)
  {
  if (waitingforclose)
    e->accept();
  else
    e->ignore();
  }

void progresswindow::finish()
  {
  cancelbutton->setEnabled(false);
  waitingforclose=true;
  exec();
  }

void progresswindow::setprogress(int permille)
  {
  if (permille==currentprogress)
    return;
  currentprogress=permille;
  progressbar->setProgress(permille);
  qApp->processEvents();
  }

void progresswindow::print(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

  logbrowser->append(quotetext(text));
  free(text);
  qApp->processEvents();
  }

void progresswindow::printheading(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

  logbrowser->append(QString("<h>")+quotetext(text)+"</h>");
  free(text);
  qApp->processEvents();
  }

void progresswindow::printinfo(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

  logbrowser->append(QString("<info>")+quotetext(text)+"</info>");
  free(text);
  qApp->processEvents();
  }

void progresswindow::printerror(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

  logbrowser->append(QString("<error>")+quotetext(text)+"</error>");
  free(text);
  qApp->processEvents();
  }

void progresswindow::printwarning(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

  logbrowser->append(QString("<warn>")+quotetext(text)+"</warn>");
  free(text);
  qApp->processEvents();
  }

void progresswindow::clickedcancel()
  {
  cancelwasclicked=true;
  cancelbutton->setEnabled(false);
  qApp->processEvents();
  }

QString progresswindow::quotetext(const char *text)
  {
  return QString(text).replace('&',QString("&amp;")).replace('<',QString("&lt;")).replace('>',QString("&gt;"));
  }
