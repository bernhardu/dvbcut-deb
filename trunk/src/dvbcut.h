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

#ifndef _DVBCUT_DVBCUT_H
#define _DVBCUT_DVBCUT_H

#include <string>
#include "mpgfile.h"
#include "dvbcutbase.h"
#include "pts.h"

class QProcess;
class imageprovider;

class dvbcut: public dvbcutbase
  {
  Q_OBJECT

protected:
  QPopupMenu *audiotrackpopup,*recentfilespopup;
  int audiotrackmenuid;
  mpgfile *mpg;
  int pictures;
  int curpic;
  double alpha;
  pts_t firstpts;
  bool showimage;
  bool fine;
  bool jogsliding;
  int jogmiddlepic;
  std::string prjfilen,mpgfilen,idxfilen,expfilen;
  QProcess *mplayer_process;
  bool mplayer_success;
  QString mplayer_out;
  pts_t mplayer_curpts;
  imageprovider *imgp;
  int busy;
  int viewscalefactor;
  int currentaudiotrack;
  std::vector<std::pair<std::string,std::string> > recentfiles;

protected:
  //   QPixmap getpixmap(int picture, bool allgop=false);
  void exportvideo(const char *fmt);
  void addtorecentfiles(const std::string &filename, const std::string &idxfilename=std::string());
  void loadrecentfilesfromsettings();
  void setviewscalefactor(int factor);

public:
  ~dvbcut();
  dvbcut(QWidget *parent = 0, const char *name = 0, WFlags fl = WType_TopLevel|WDestructiveClose );
  void open(std::string filename=std::string(), std::string idxfilename=std::string());
  void setbusy(bool b=true);
  // static dvbcut *New(std::string filename=std::string(), std::string idxfilename=std::string());

public slots:
  virtual void fileNew();
  virtual void fileOpen();
  virtual void fileSaveAs();
  virtual void fileSave();
  virtual void fileExport();
  virtual void fileClose();
  virtual void editBookmark();
  virtual void editChapter();
  virtual void editStop();
  virtual void editStart();
  virtual void viewDifference();
  virtual void viewUnscaled();
  virtual void viewNormal();
  virtual void viewFullSize();
  virtual void viewHalfSize();
  virtual void viewQuarterSize();
  virtual void playAudio2();
  virtual void playAudio1();
  virtual void playStop();
  virtual void playPlay();
  virtual void jogsliderreleased();
  virtual void jogslidervalue(int);
  virtual void linslidervalue(int);
  virtual void doubleclickedeventlist(QListBoxItem *lbi);
  virtual void eventlistcontextmenu(QListBoxItem *, const QPoint &);
  virtual void mplayer_exited();
  virtual void mplayer_readstdout();
  virtual void clickedgo();
  virtual void updateimagedisplay();
  virtual void audiotrackchosen(int id);
  virtual void loadrecentfile(int id);
  virtual void abouttoshowrecentfiles();
  };

#endif
