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

#include "pts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string>

std::string ptsstring(pts_t pts)
  {
  char *str=0;
  const char *minus="";
  if (pts<0) {
    minus="-";
    pts*=-1;
    }

  if (asprintf(&str,"%s%02d:%02d:%02d.%03d/%02d",
               minus,
               int(pts/90000)/3600,
               (int(pts/90000)/60)%60,
               int(pts/90000)%60,
               int(pts/90)%1000,
               int(pts%90) )<0 || !str)
    return std::string();

  std::string s(str);
  free(str);
  return s;
  }
