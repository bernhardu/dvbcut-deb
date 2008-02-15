/*  dvbcut settings
    Copyright (c) 2006 Michael Riepe
 
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

#include <string>
#include <vector>

#include <qstringlist.h>

#include <assert.h>

#include "defines.h"
#include "settings.h"

#define DVBCUT_QSETTINGS_DOMAIN "dvbcut.sf.net"
#define DVBCUT_QSETTINGS_PRODUCT "dvbcut"
#define DVBCUT_QSETTINGS_PATH "/" DVBCUT_QSETTINGS_DOMAIN "/" DVBCUT_QSETTINGS_PRODUCT "/"

#define DVBCUT_DEFAULT_LOADFILTER \
	"Recognized files (*.dvbcut *.mpg *.rec* *.ts *.tts* *.trp *.vdr);;" \
	"dvbcut project files (*.dvbcut);;" \
	"MPEG files (*.mpg *.rec* *.ts *.tts* *.trp *.vdr);;" \
	"All files (*)"
#define DVBCUT_DEFAULT_IDXFILTER \
	"dvbcut index files (*.idx);;All files (*)"
#define DVBCUT_DEFAULT_PRJFILTER \
	"dvbcut project files (*.dvbcut);;All files (*)"

#define DVBCUT_DEFAULT_START_LABEL \
	"<font size=\"+1\" color=\"darkgreen\"><b>START</b></font>"
#define DVBCUT_DEFAULT_STOP_LABEL \
	"<font size=\"+1\" color=\"darkred\"><b>STOP</b></font>"
#define DVBCUT_DEFAULT_CHAPTER_LABEL \
	"<font color=\"darkgoldenrod\">CHAPTER</font>"
#define DVBCUT_DEFAULT_BOOKMARK_LABEL \
	"<font color=\"darkblue\">BOOKMARK</font>"

#define DVBCUT_DEFAULT_PIPE_COMMAND \
        "|dvdauthor -t -c '%CHAPTERS%' -v mpeg2 -o %OUTPUT% -"
#define DVBCUT_DEFAULT_PIPE_POST \
        "dvdauthor -o %OUTPUT% -T"
#define DVBCUT_DEFAULT_PIPE_LABEL \
        "DVD-Video titleset (dvdauthor)"
#define DVBCUT_DEFAULT_PIPE_FORMAT (0)
/* 
// SOME OTHER EXAMPLES for the settings file ~/.qt/dvbcut.sf.netrc 
// (ok, for time consuming conversions one does not save any time, but it may be convenient...) 
// 1. Conversion to mpeg4 avi-file with ffmpeg:
//    (to recode to a smaller MPEG2 File use "--target dvd -acodec copy"?)!
pipe/1/command=|ffmpeg -f mpeg2video -i - -f avi -vcodec mpeg4 -b 1200k -g 250 -bf 2 -acodec libmp3lame -ab 128k -ar 44100 %OUTPUT%
pipe/1/format=1
pipe/1/label=MPEG-4/ASP (ffmpeg)
pipe/1/post=
// 2. Shrinking with vamps by 20%, before piping to dvdauthor:
pipe/2/command=| vamps -E 1.2 -S 10000000000 -a 1,2,3 | dvdauthor -t -c '%CHAPTERS%' -v mpeg2 -o %OUTPUT% -
pipe/2/format=0
pipe/2/label=20% shrinked DVD-Video titleset (vamps & dvdauthor)
pipe/2/post=dvdauthor -o %OUTPUT% -T
// 3. recoding to a (smaller?) MPEG2 file with DVD compliant resolution (ca. 3000kbps):
pipe/3/command=|ffmpeg -f mpeg2video -i - --target dvd -qscale 3.0 -bf 2 -acodec copy %OUTPUT%"
pipe/3/format=1
pipe/3/label=recoded DVD compliant video (ffmpeg)
pipe/3/post=
*/

dvbcut_settings::dvbcut_settings() {
  setPath(DVBCUT_QSETTINGS_DOMAIN, DVBCUT_QSETTINGS_PRODUCT);
  beginGroup("/" DVBCUT_QSETTINGS_DOMAIN "/" DVBCUT_QSETTINGS_PRODUCT);
  loaded = false;
}

dvbcut_settings::~dvbcut_settings() {
  if (loaded) {
    save_settings();
  }
  endGroup();
}

void
dvbcut_settings::load_settings() {
  int version = readNumEntry("/version", 0);
  if (version >= 1) {
	// config format version 1
	beginGroup("/wheel");
	  wheel_increments[WHEEL_INCR_NORMAL] = readNumEntry("/incr_normal", 25*60);
	  wheel_increments[WHEEL_INCR_SHIFT] = readNumEntry("/incr_shift", 25);
	  wheel_increments[WHEEL_INCR_CTRL] = readNumEntry("/incr_ctrl", 1);
	  wheel_increments[WHEEL_INCR_ALT] = readNumEntry("/incr_alt", 15*25*60);
	  wheel_threshold = readNumEntry("/threshold", 24);
	  // Note: delta is a multiple of 120 (see Qt documentation)
	  wheel_delta = readNumEntry("/delta", 120);
	  if (wheel_delta == 0)
		wheel_delta = 1;	// avoid devide by zero
	endGroup();	// wheel
	beginGroup("/slider");
	  jog_maximum = readNumEntry("/jog_maximum", 180000);
	  jog_threshold = readNumEntry("/jog_threshold", 50);
	  // to increase the "zero frames"-region of the jog-slider
	  jog_offset = readDoubleEntry("/jog_offset", 0.4);
	  // sub-intervals of jog_maximum
	  jog_interval = readNumEntry("/jog_interval", 1);
	  if (jog_interval < 0)
		jog_interval = 0;
	  lin_interval = readNumEntry("/lin_interval", 3600);
	  if (lin_interval < 0)
		lin_interval = 0;
	endGroup();	// slider
	beginGroup("/lastdir");
	  lastdir = readEntry("/name", ".");
	  lastdir_update = readBoolEntry("/update", true);
	endGroup(); // lastdir
	beginGroup("/filter");
	  idxfilter = readEntry("/idxfilter", DVBCUT_DEFAULT_IDXFILTER);
	  prjfilter = readEntry("/prjfilter", DVBCUT_DEFAULT_PRJFILTER);
	  loadfilter = readEntry("/loadfilter", DVBCUT_DEFAULT_LOADFILTER);
	endGroup();	// filter
  }
  else {
	// old (unnumbered) config format
	wheel_increments[WHEEL_INCR_NORMAL] = readNumEntry("/wheel_incr_normal", 25*60);
	wheel_increments[WHEEL_INCR_SHIFT] = readNumEntry("/wheel_incr_shift", 25);
	wheel_increments[WHEEL_INCR_CTRL] = readNumEntry("/wheel_incr_ctrl", 1);
	wheel_increments[WHEEL_INCR_ALT] = readNumEntry("/wheel_incr_alt", 15*25*60);
	wheel_threshold = readNumEntry("/wheel_threshold", 24);
	// Note: delta is a multiple of 120 (see Qt documentation)
	wheel_delta = readNumEntry("/wheel_delta", 120);
	if (wheel_delta == 0)
	  wheel_delta = 1;	// avoid devide by zero
	jog_maximum = readNumEntry("/jog_maximum", 180000);
	jog_threshold = readNumEntry("/jog_threshold", 50);
	// to increase the "zero frames"-region of the jog-slider
	jog_offset = readDoubleEntry("/jog_offset", 0.4);
	// sub-intervals of jog_maximum
	jog_interval = readNumEntry("/jog_interval", 1);
	if (jog_interval < 0)
	  jog_interval = 0;
	lin_interval = readNumEntry("/lin_interval", 3600);
	if (lin_interval < 0)
	  lin_interval = 0;
	lastdir = readEntry("/lastdir", ".");
	lastdir_update = true;
	idxfilter = readEntry("/idxfilter", DVBCUT_DEFAULT_IDXFILTER);
	prjfilter = readEntry("/prjfilter", DVBCUT_DEFAULT_PRJFILTER);
	loadfilter = readEntry("/loadfilter", DVBCUT_DEFAULT_LOADFILTER);
	// remove old-style entries
	removeEntry("/wheel_incr_normal");
	removeEntry("/wheel_incr_shift");
	removeEntry("/wheel_incr_ctrl");
	removeEntry("/wheel_incr_alt");
	removeEntry("/wheel_threshold");
	removeEntry("/wheel_delta");
	removeEntry("/jog_maximum");
	removeEntry("/jog_threshold");
	removeEntry("/jog_offset");
	removeEntry("/jog_interval");
	removeEntry("/lin_interval");
	removeEntry("/lastdir");
	removeEntry("/idxfilter");
	removeEntry("/prjfilter");
	removeEntry("/loadfilter");
  }
  viewscalefactor = readNumEntry("/viewscalefactor", 1);
  export_format = readNumEntry("/export_format", 0);
  beginGroup("/recentfiles");
    recentfiles_max = readNumEntry("/max", 5);
    recentfiles.clear();
    std::list<std::string> filenames;
    QStringList keys = entryList("/");
    for (unsigned int i = 0; i < recentfiles_max; ++i) {
      QString key = "/" + QString::number(i);
      if (version < 1 && keys.size()>1) {
		// OLD format (2 keys per input file, NO subkeys!)
        QString filename = readEntry(key);
        if (filename.isEmpty())
		  continue;
        filenames.clear();
        filenames.push_back(filename);
        QString idxfilename = readEntry(key + "-idx", "");
        recentfiles.push_back(
        std::pair<std::list<std::string>,std::string>(filenames, idxfilename));
      }
	  else {
	  	// NEW format with subkeys and multiple files!
		beginGroup(key);
		  QString filename = readEntry("/0");
		  if (!filename.isEmpty()) {
			// multiple input files?  
			int j=0;
			filenames.clear();
			while(!filename.isEmpty()) {
			  filenames.push_back(filename);
			  filename = readEntry("/" + QString::number(++j), "");
			}  
			QString idxfilename = readEntry("/idx", "");
			recentfiles.push_back(
			  std::pair<std::list<std::string>,std::string>(filenames, idxfilename));
		  }
		endGroup();	// key
	  }
	}
  endGroup();	// recentfiles
  beginGroup("/labels");
    start_label = readEntry("/start", DVBCUT_DEFAULT_START_LABEL);
    stop_label = readEntry("/stop", DVBCUT_DEFAULT_STOP_LABEL);
    chapter_label = readEntry("/chapter", DVBCUT_DEFAULT_CHAPTER_LABEL);
    bookmark_label = readEntry("/bookmark", DVBCUT_DEFAULT_BOOKMARK_LABEL);
  endGroup();	// labels
  start_bof = readBoolEntry("/start_bof", true);
  stop_eof = readBoolEntry("/stop_eof", true);
  beginGroup("/snapshots");
    snapshot_type = readEntry("/type", "PNG");
    snapshot_quality = readNumEntry("/quality", -1);
    snapshot_prefix = readEntry("/prefix", "");
    snapshot_delimiter = readEntry("/delimiter", "_");
    snapshot_first = readNumEntry("/first", 1);
    snapshot_width = readNumEntry("/width", 3);
    snapshot_extension = readEntry("/extension", "png");
    snapshot_range = readNumEntry("/range", 0);
    snapshot_samples = readNumEntry("/samples", 1);
  endGroup();	// snapshots
  beginGroup("/pipe");
    pipe_command.clear();
    pipe_post.clear();
    pipe_label.clear();
    pipe_format.clear();
    beginGroup("/0");
      QString command = readEntry("/command", DVBCUT_DEFAULT_PIPE_COMMAND);
      QString post = readEntry("/post", DVBCUT_DEFAULT_PIPE_POST);
      QString label = readEntry("/label", DVBCUT_DEFAULT_PIPE_LABEL);
      int format = readNumEntry("/format", DVBCUT_DEFAULT_PIPE_FORMAT);
    endGroup();	// 0
    unsigned int i = 0;
    while(!command.isEmpty() && !label.isEmpty()) {
      if(format<0 || format>3) format = 0;
      pipe_command.push_back(command);
      pipe_post.push_back(post);
      pipe_label.push_back(label);
      pipe_format.push_back(format);
      QString key = "/" + QString::number(++i);
      beginGroup(key);
	command = readEntry("/command","");
	post = readEntry("/post","");
	label = readEntry("/label","");
	format = readNumEntry("/format", 0);
      endGroup();	// key
    }
  endGroup();	// pipe
  beginGroup("/chapters");
    // length (>0) or number (<0) of chapter(s)
    chapter_interval = readNumEntry("/interval", 600*25);
    // detection of scene changes is rather time comsuming... 
    //chapter_tolerance = readNumEntry("/tolerance", 10*25);
    //... better switch it off per default!
    chapter_tolerance = readNumEntry("/tolerance", 0);
    // average color distance needed for a scene change
    chapter_threshold = readDoubleEntry("/threshold", 50.);
    // minimal length of a chapter
    chapter_minimum = readNumEntry("/minimum", 200*25);
  endGroup();	// auto chapters
}

void
dvbcut_settings::save_settings() {
  writeEntry("/version", 1);	// latest config version
  beginGroup("/wheel");
	  writeEntry("/incr_normal", wheel_increments[WHEEL_INCR_NORMAL]);
	  writeEntry("/incr_shift", wheel_increments[WHEEL_INCR_SHIFT]);
	  writeEntry("/incr_ctrl", wheel_increments[WHEEL_INCR_CTRL]);
	  writeEntry("/incr_alt", wheel_increments[WHEEL_INCR_ALT]);
	  writeEntry("/threshold", wheel_threshold);
	  writeEntry("/delta", wheel_delta);
  endGroup();	// wheel
  beginGroup("/slider");
	  writeEntry("/jog_maximum", jog_maximum);
	  writeEntry("/jog_threshold", jog_threshold);
	  writeEntry("/jog_offset", jog_offset);
	  writeEntry("/jog_interval", jog_interval);
	  writeEntry("/lin_interval", lin_interval);
  endGroup();	// slider
  beginGroup("/lastdir");
    writeEntry("/name", lastdir);
    writeEntry("/update", lastdir_update);
  endGroup();	// lastdir
  beginGroup("/filter");
	  writeEntry("/idxfilter", idxfilter);
	  writeEntry("/prjfilter", prjfilter);
	  writeEntry("/loadfilter", loadfilter);
  endGroup();	// filter
  writeEntry("/viewscalefactor", viewscalefactor);
  writeEntry("/export_format", export_format);
  beginGroup("/recentfiles");
    // first remove any OLD recentfiles entries to clean the settings file (<revision 108)!!!
    QStringList keys = entryList("/");
    for ( QStringList::Iterator it = keys.begin(); it != keys.end(); ++it ) 
      removeEntry("/" + *it);
    // then remove ALL new recentfiles entries!!!
    // (otherwise it would be a mess with erased&inserted muliple file entries of different size)
    QStringList subkeys = subkeyList("/");
    for ( QStringList::Iterator its = subkeys.begin(); its != subkeys.end(); ++its ) {
      QStringList keys = entryList("/" + *its);
      for ( QStringList::Iterator itk = keys.begin(); itk != keys.end(); ++itk ) 
        removeEntry("/"  + *its + "/" + *itk);
    }    
    writeEntry("/max", int(recentfiles_max));
    // and NOW write the updated list from scratch!!!
    for (unsigned int i = 0; i < recentfiles.size(); ++i) {
      QString key = "/" + QString::number(i);
      beginGroup(key);
        int j=0;
        for(std::list<std::string>::iterator it=settings().recentfiles[i].first.begin();
                                             it!=settings().recentfiles[i].first.end(); it++, j++) 
          writeEntry("/" + QString::number(j), *it);
        writeEntry("/idx", recentfiles[i].second);
      endGroup();	// key
    }
  endGroup();	// recentfiles
  beginGroup("/labels");
    writeEntry("/start", start_label);
    writeEntry("/stop", stop_label);
    writeEntry("/chapter", chapter_label);
    writeEntry("/bookmark", bookmark_label);
  endGroup();	// labels
  writeEntry("/start_bof", start_bof);
  writeEntry("/stop_eof", stop_eof);
  beginGroup("/snapshots");
    writeEntry("/type", snapshot_type);
    writeEntry("/quality", snapshot_quality);
    writeEntry("/prefix", snapshot_prefix);
    writeEntry("/delimiter", snapshot_delimiter);
    writeEntry("/first", snapshot_first);
    writeEntry("/width", snapshot_width);
    writeEntry("/extension", snapshot_extension);
    writeEntry("/range", snapshot_range);
    writeEntry("/samples", snapshot_samples);
  endGroup();	// snapshots
  beginGroup("/pipe");
    for (unsigned int i = 0; i < pipe_command.size(); ++i) {
      QString key = "/" + QString::number(i);
      beginGroup(key);
	writeEntry("/command", pipe_command[i]);
	writeEntry("/post", pipe_post[i]);
	writeEntry("/label", pipe_label[i]);
	writeEntry("/format", pipe_format[i]);
      endGroup();	// key
    }
  endGroup();	// pipe
  beginGroup("/chapters");
    writeEntry("/interval", chapter_interval);
    writeEntry("/tolerance", chapter_tolerance);
    writeEntry("/threshold", chapter_threshold);
    writeEntry("/minimum", chapter_minimum);
  endGroup();	// auto chapters
}

// private settings variable
static dvbcut_settings mysettings;

// access function (includes delayed loading)
dvbcut_settings& settings() {
  if (!mysettings.loaded) {
    mysettings.load_settings();
    mysettings.loaded = true;
  }
  return mysettings;
}
