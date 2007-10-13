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
#include "logoutput.h"

void
logoutput::setprogress(int permille) {
  if (currentprogress==permille)
    return;
  currentprogress=permille;
  fprintf(stderr,"[%3d.%d]\r",currentprogress/10,currentprogress%10);
}

static void
vprintmsg(const char *fmt, va_list ap, const char *head, const char *tail) {
//  fprintf(stderr, "[%3d.%d] ", currentprogress / 10, currentprogress % 10);
  if (head)
    fputs(head, stderr);
  vfprintf(stderr, fmt, ap);
  if (tail)
    fputs(tail, stderr);
  fprintf(stderr, "\n");
}

void
logoutput::print(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintmsg(fmt, ap, 0, 0);
  va_end(ap);
}

void
logoutput::printheading(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintmsg(fmt, ap, "=== ", " ===");
  va_end(ap);
}

void
logoutput::printinfo(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintmsg(fmt, ap, "INFO: ", 0);
  va_end(ap);
}

void
logoutput::printerror(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintmsg(fmt, ap, "ERROR: ", 0);
  va_end(ap);
}

void
logoutput::printwarning(const char *fmt, ...) {
  va_list ap;
  va_start(ap,fmt);
  vprintmsg(fmt, ap, "WARNING: ", 0);
  va_end(ap);
}
