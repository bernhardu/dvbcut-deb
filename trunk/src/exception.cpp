/*  dvbcut
    Copyright (c) 2007 Sven Over <svenover@svenover.de>
 
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

#include "exception.h"
#include <qstring.h>
#include <qmessagebox.h>

dvbcut_exception::dvbcut_exception(const std::string &__arg) : _M_msg(__arg), _M_extype()
{
}

dvbcut_exception::dvbcut_exception(const char* __arg) : _M_msg(__arg), _M_extype()
{
}

dvbcut_exception::~dvbcut_exception() throw()
{
}

const char *dvbcut_exception::what() const throw()
{
  return _M_msg.c_str();
}

void dvbcut_exception::show() const
{
  std::string extype(type());

  if (extype.empty())  
    extype="DVBCUT error";

  QMessageBox::critical(NULL,extype,what(),QMessageBox::Abort,QMessageBox::NoButton);
}
