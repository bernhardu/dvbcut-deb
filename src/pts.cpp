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

#include "pts.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <list>

// convert a pts (1/90000 sec) to a readable timestamp string
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

// parse timestamp string (hh:mm:ss.frac/nn) and convert to pts (1/90000 of a second)
pts_t string2pts(std::string str)
  {
  int hour=0,min=0,sec=0,ms=0,sub=0,sign=1;
  double dsec;
  std::list<std::string> tokens;

  size_t from=0, pos;
  while((pos=str.find(':',from))!=std::string::npos)  {
    tokens.push_back(str.substr(from,pos-from));
    from=pos+1;
  }  
  tokens.push_back(str.substr(from));
 
  if(!tokens.empty()) {
    std::string t=tokens.back(); 
    pos=t.find('/');
    dsec=atof(t.substr(0,pos).c_str());
    if(dsec<0) {
      dsec*=-1;
      sign=-1;
    }
    sec=int(dsec);
    ms=int(1000*(dsec-sec)+0.5);

    if(pos!=std::string::npos) 
      sub=atoi(t.substr(pos+1).c_str())%90; 
    tokens.pop_back();

    if(!tokens.empty()) {
      min=atoi(tokens.back().c_str());
      if(min<0) {
        min*=-1;  
        sign=-1;
      }
      tokens.pop_back();

      if(!tokens.empty()) {
        hour=atoi(tokens.back().c_str());
        if(hour<0) {
          hour*=-1;  
          sign=-1;
        }
      }    
    }  
  }
  
  return sign*((((hour*60 + min)*60 + sec)*1000 + ms)*90 + sub);
  }
