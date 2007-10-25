/*  gettext.h - i18n hooks
    Copyright (c) 2007 Michael Riepe
 
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

#ifndef _DVBCUT_GETTEXT_H
#define _DVBCUT_GETTEXT_H

#include <qstring.h>

#if HAVE_LIBINTL_H

#include <libintl.h>

#define _(str)	gettext(str)

QString dvbcut_gettext(const char *str, const char *cmt = 0);
#define tr	::dvbcut_gettext

#else /* HAVE_LIBINTL_H */

#define _(str)	str
#define textdomain(x)		do{}while(0)
#define bindtextdomain(x,y)	do{}while(0)

#endif /* HAVE_LIBINTL_H */

#endif /* _DVBCUT_GETTEXT_H */
