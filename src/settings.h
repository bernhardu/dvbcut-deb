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

#ifndef _DVBCUT_SETTINGS_H
#define _DVBCUT_SETTINGS_H

#include <string>
#include <vector>

#include <qsettings.h>

enum {
  WHEEL_INCR_NORMAL,
  WHEEL_INCR_SHIFT,
  WHEEL_INCR_CTRL,
  WHEEL_INCR_ALT,
  WHEEL_INCR_num
};

class dvbcut_settings : QSettings {
public:
  dvbcut_settings();
  ~dvbcut_settings();

  void load_settings();
  void save_settings();

  bool loaded;

  QString lastdir;
  bool lastdir_update;
  QString idxfilter;
  QString prjfilter;
  QString loadfilter;
  std::vector<std::pair<std::list<std::string>,std::string> > recentfiles;
  unsigned int recentfiles_max;
  double viewscalefactor;
  double viewscalefactor_custom;
  int wheel_increments[WHEEL_INCR_num];
  int wheel_threshold;
  int wheel_delta;
  int jog_maximum;
  int jog_threshold;
  double jog_offset;
  int jog_interval;
  int lin_interval;
  int export_format;
  QString start_label;
  QString stop_label;
  QString chapter_label;
  QString bookmark_label;
  bool start_bof;
  bool stop_eof;
  QString snapshot_type;
  QString snapshot_prefix;
  QString snapshot_delimiter;
  QString snapshot_extension;
  int snapshot_quality;
  int snapshot_first;
  int snapshot_width;
  int snapshot_range;
  int snapshot_samples;
  std::vector<QString> pipe_command;
  std::vector<QString> pipe_post;
  std::vector<QString> pipe_label;
  std::vector<int> pipe_format;
  int chapter_interval;
  int chapter_tolerance;
  double chapter_threshold;
  int chapter_minimum;

};

// access function
extern dvbcut_settings& settings();

#endif // _DVBCUT_SETTINGS_H
