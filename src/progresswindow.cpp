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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <cstdio>
#include <cstdarg>
#include <cstdlib>

#include <qprogressbar.h>
#include <qtextbrowser.h>
#include <qpushbutton.h>
#include <qapplication.h>
#include <QCloseEvent>
#include "progresswindow.h"

progresswindow::progresswindow(QWidget *parent)
    :QDialog(parent), logoutput(),
    cancelwasclicked(false), waitingforclose(false)
  {
  ui = new Ui::progresswindowbase();
  ui->setupUi(this);
  setModal(true);

  fc_head.setFontWeight(QFont::Bold);
  fc_head.setFontUnderline(true);

  fc_warn.setForeground(Qt::red);

  fc_error.setFontWeight(QFont::Bold);
  fc_error.setForeground(Qt::red);
  fc_error.setFontUnderline(true);

  QPalette palette;
  palette.setColor(ui->cancelbutton->backgroundRole(), QColor( 255,0,0 ));
  ui->cancelbutton->setPalette(palette);

  show();
  qApp->processEvents();
  }

progresswindow::~progresswindow()
  {
  delete ui;
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
  ui->cancelbutton->setEnabled(false);
  waitingforclose=true;
  ui->cancelbutton->setText( tr( "Close" ) );

  QPalette palette;
  palette.setColor(ui->cancelbutton->backgroundRole(), QColor( 0,255,0 ));
  ui->cancelbutton->setPalette(palette);

  ui->cancelbutton->setEnabled(true);
  exec();
  }

void progresswindow::setprogress(int permille)
  {
  if (permille==currentprogress)
    return;
  currentprogress=permille;
  ui->progressbar->setValue(permille);
  qApp->processEvents();
  }

void progresswindow::printmsg(const QString &str, const QTextCharFormat &format)
{
    ui->logbrowser->setCurrentCharFormat(format);
    ui->logbrowser->append(str);
    qApp->processEvents();
}

void progresswindow::print(const QString &str)
{
    printmsg(str, fc_normal);
}

void progresswindow::printheading(const QString &str)
{
    printmsg(str, fc_head);
}

void progresswindow::printinfo(const QString &str)
{
    printmsg(str, fc_info);
}

void progresswindow::printerror(const QString &str)
{
    printmsg(str, fc_error);
}

void progresswindow::printwarning(const QString &str)
{
    printmsg(str, fc_warn);
}

void progresswindow::clickedcancel()
  {
  if ((cancelwasclicked==false) && (waitingforclose==false)) {
    // button function is cancel
    cancelwasclicked=true;
    ui->cancelbutton->setEnabled(false);
    qApp->processEvents();
    ui->cancelbutton->setText( tr( "Close" ) );

    QPalette palette;
    palette.setColor(ui->cancelbutton->backgroundRole(), QColor( 0,255,0 ));
    ui->cancelbutton->setPalette(palette);

    ui->cancelbutton->setEnabled(true);
  } else {
    // button function is close
    close();
  }
  }
