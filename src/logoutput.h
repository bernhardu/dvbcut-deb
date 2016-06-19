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

#ifndef _DVBCUT_LOGOUTPUT_H
#define _DVBCUT_LOGOUTPUT_H

class QString;

/**
@author Sven Over
*/
class logoutput
  {
protected:
  int currentprogress;
  void printmsg(const QString &str, const QString head, const QString tail);

public:
  logoutput() : currentprogress(0)
    {}
  virtual ~logoutput()
    {}

  int getprogress()
    {
    return currentprogress;
    }
  virtual void setprogress(int permille);

  virtual void print(const QString &str);
  virtual void printheading(const QString &str);
  virtual void printinfo(const QString &str);
  virtual void printerror(const QString &str);
  virtual void printwarning(const QString &str);

  virtual bool cancelled()
    {
    return false;
    }
  virtual void finish()
    {
    return;
    }
  };

#endif
