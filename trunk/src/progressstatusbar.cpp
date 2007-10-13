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
#include <qstatusbar.h>
#include <qprogressbar.h>
#include <qapplication.h>
#include <qpushbutton.h>
#include <qlabel.h>
#include <qsizepolicy.h>
#include "progressstatusbar.h"

progressstatusbar::progressstatusbar(QStatusBar *bar)
    : logoutput(), cancelwasclicked(false), statusbar(bar)
  {
  label=new QLabel(statusbar);
  label->setSizePolicy(QSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum));
  statusbar->addWidget(label,true);

  cancelbutton=new QPushButton(statusbar);
  cancelbutton->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum));
  cancelbutton->setText("cancel");
  cancelbutton->setMaximumWidth(80);
  statusbar->addWidget(cancelbutton,true);

  progressbar=new QProgressBar(statusbar);
  progressbar->setTotalSteps(1000);
  progressbar->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum));
  progressbar->setMinimumWidth(160);
  progressbar->setMaximumWidth(160);
  statusbar->addWidget(progressbar,true);

  connect(cancelbutton,SIGNAL(clicked()),SLOT(clickedcancel()));
  progressbar->show();
  cancelbutton->show();
  label->show();
  qApp->processEvents();
  }


progressstatusbar::~progressstatusbar()
  {
  delete progressbar;
  delete cancelbutton;
  delete label;
  statusbar->clear();
  }


void progressstatusbar::setprogress(int permille)
  {
  if (permille==currentprogress)
    return;
  currentprogress=permille;
  progressbar->setProgress(permille);
  qApp->processEvents();
  }

void progressstatusbar::finish()
  {
  cancelbutton->setEnabled(false);
  }

void progressstatusbar::clickedcancel()
  {
  cancelwasclicked=true;
  cancelbutton->setEnabled(false);
  qApp->processEvents();
  }

void progressstatusbar::print(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0 || (text==0))
    return;

  label->setText(text);
  free(text);
  qApp->processEvents();
  }

