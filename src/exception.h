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

#ifndef _DVBCUT_EXCEPTION_H
#define _DVBCUT_EXCEPTION_H

#include <exception>
#include <string>

class dvbcut_exception : public std::exception
{
protected:
  std::string _M_msg;
  std::string _M_extype;
public:
  explicit dvbcut_exception(const std::string &__arg);
  explicit dvbcut_exception(const char* __arg);
  virtual ~dvbcut_exception() throw();
  
  virtual const char *what() const throw();
  
  const std::string &msg() const throw() { return _M_msg; }
  const std::string &type() const throw() { return _M_extype; }
  
  void show() const;
};

#endif // ifndef _DVBCUT_EXCEPTION_H
