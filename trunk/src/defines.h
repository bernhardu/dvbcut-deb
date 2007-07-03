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

#ifndef _DVBCUT_DEFINES_H
#define _DVBCUT_DEFINES_H

#define MUXER_FLAG_KEY		(1<<0)

#define MAXAUDIOSTREAMS (0x20)
#define MAXVIDEOSTREAMS (1)
#define MAXAVSTREAMS (MAXVIDEOSTREAMS+MAXAUDIOSTREAMS)
#define VIDEOSTREAM (MAXAUDIOSTREAMS)

static inline int audiostream(int s=0)
  {
  return s;
  }
static inline int videostream(int s=0)
  {
#if MAXVIDEOSTREAMS == 1
  return MAXAUDIOSTREAMS+s;
#else

  return MAXAUDIOSTREAMS+(s%MAXVIDEOSTREAMS);
#endif
  }

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define mbo32(x) \
      ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) | \
       (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#define htom32(x) (__bswap_32(x))
#define mbo16(x) ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#define htom16(x) (__bswap_16(x))
#else
#define mbo32(x) (x)
#define htom32(x) (x)
#define mbo16(x) (x)
#define htom16(x) (x)
#endif

#define DVBCUT_QSETTINGS_DOMAIN "dvbcut.sf.net"
#define DVBCUT_QSETTINGS_PRODUCT "dvbcut"
#define DVBCUT_QSETTINGS_PATH "/" DVBCUT_QSETTINGS_DOMAIN "/" DVBCUT_QSETTINGS_PRODUCT "/"

#define DVBCUT_DEFAULT_LOADFILTER \
	"Recognized files (*.dvbcut *.mpg *.rec* *.ts *.tts* *.vdr);;" \
	"dvbcut project files (*.dvbcut);;" \
	"MPEG files (*.mpg *.rec* *.ts *.tts* *.vdr);;" \
	"All files (*)"
#define DVBCUT_DEFAULT_IDXFILTER \
	"dvbcut index files (*.idx);;All files (*)"
#define DVBCUT_DEFAULT_PRJFILTER \
	"dvbcut project files (*.dvbcut);;All files (*)"

#endif

