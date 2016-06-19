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
#include <QString>
#include "logoutput.h"

void
logoutput::setprogress(int permille) {
  if (currentprogress==permille)
    return;
  currentprogress=permille;
  fprintf(stderr,"[%3d.%d]\r",currentprogress/10,currentprogress%10);
}

void logoutput::printmsg(const QString &str, const QString head, const QString tail)
{
    if (!head.isEmpty())
        fprintf(stderr, "%s", head.toLatin1().data());
    fprintf(stderr, "%s", str.toLatin1().data());
    if (!tail.isEmpty())
        fprintf(stderr, "%s", tail.toLatin1().data());
    fprintf(stderr, "\n");
}

/*virtual*/
void logoutput::print(const QString &str)
{
    printmsg(str, "", "");
}

/*virtual*/
void logoutput::printheading(const QString &str)
{
    printmsg(str, "=== ", " ===");
}

/*virtual*/
void logoutput::printinfo(const QString &str)
{
    printmsg(str, "INFO: ", 0);
}

/*virtual*/
void logoutput::printerror(const QString &str)
{
    printmsg(str, "ERROR: ", 0);
}

/*virtual*/
void logoutput::printwarning(const QString &str)
{
    printmsg(str, "WARNING: ", 0);
}
