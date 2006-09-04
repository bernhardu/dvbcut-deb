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
#include "logoutput.h"

void logoutput::setprogress(int permille)
  {
  if (currentprogress==permille)
    return;
  currentprogress=permille;
  fprintf(stderr,"[%3d.%d]\r",currentprogress/10,currentprogress%10);
  }

void logoutput::print(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0)
    text=0;

  fprintf(stderr,"[%3d.%d] %s\n",currentprogress/10,currentprogress%10,text);
  if (text)
    free(text);
  }

void logoutput::printheading(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0)
    text=0;

  fprintf(stderr,"[%3d.%d] >>> %s <<<\n",currentprogress/10,currentprogress%10,text);
  if (text)
    free(text);
  }

void logoutput::printinfo(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0)
    text=0;

  fprintf(stderr,"[%3d.%d] INFO: %s\n",currentprogress/10,currentprogress%10,text);
  if (text)
    free(text);
  }

void logoutput::printerror(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0)
    text=0;

  fprintf(stderr,"[%3d.%d] ERROR: %s\n",currentprogress/10,currentprogress%10,text);
  if (text)
    free(text);
  }

void logoutput::printwarning(const char *fmt, ...)
  {
  va_list ap;
  va_start(ap,fmt);
  char *text=0;
  if (vasprintf(&text,fmt,ap)<0)
    text=0;

  fprintf(stderr,"[%3d.%d] WARNING: %s\n",currentprogress/10,currentprogress%10,text);
  if (text)
    free(text);
  }


