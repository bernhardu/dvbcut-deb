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

#ifndef _DVBCUT_DVBCUT_H
#define _DVBCUT_DVBCUT_H

#include <string>
#include <vector>
#include <list>
#include "mpgfile.h"
#include "dvbcutbase.h"
#include "pts.h"
#include "eventlistitem.h"

class QProcess;
class imageprovider;

class dvbcut: public dvbcutbase
  {
  Q_OBJECT

public:
  struct quick_picture_lookup_s
  {
    int picture;
    bool export_flag;
    pts_t pts;
    int outpicture;
    pts_t outpts;

    quick_picture_lookup_s(int _picture, bool _export_flag, pts_t _pts, int _outpicture, pts_t _outpts) :
      picture(_picture), export_flag(_export_flag), pts(_pts), outpicture(_outpicture), outpts(_outpts)
    {
    }

    struct cmp_picture
    {
      bool operator()(int lhs, const quick_picture_lookup_s &rhs) const
      {
        return lhs<rhs.picture;
      }
    };
  };
  typedef std::vector<quick_picture_lookup_s> quick_picture_lookup_t;
  
protected:
  quick_picture_lookup_t quick_picture_lookup;

  QPopupMenu *audiotrackpopup,*recentfilespopup;
  int audiotrackmenuid;
  inbuffer buf;
  mpgfile *mpg;
  int pictures;
  int curpic;
  double alpha;
  pts_t firstpts;
  bool showimage;
  bool fine;
  bool jogsliding;
  int jogmiddlepic;
  std::string prjfilen,idxfilen,expfilen;
  std::list<std::string> mpgfilen;
  QProcess *mplayer_process;
  bool mplayer_success;
  QString mplayer_out;
  pts_t mplayer_curpts;
  imageprovider *imgp;
  int busy;
  int viewscalefactor;
  int currentaudiotrack;
  bool nogui;

protected:
  //   QPixmap getpixmap(int picture, bool allgop=false);
  void exportvideo(const char *fmt);
  void addtorecentfiles(const std::string &filename, const std::string &idxfilename=std::string());
  void setviewscalefactor(int factor);

  // special event handling (mouse wheel)
  bool eventFilter(QObject *watched, QEvent *e);

  void update_time_display();
  void update_quick_picture_lookup_table();

  // QMessagebox interface
  int question(const QString & caption, const QString & text);
  int critical(const QString & caption, const QString & text);

  // filename handling
  void make_canonical(std::string &filename);
  void make_canonical(std::list<std::string> &filenames);

  // generic event item adder
  void addEventListItem(int pic, EventListItem::eventtype type);

public:
  ~dvbcut();
  dvbcut(QWidget *parent = 0, const char *name = 0, WFlags fl = WType_TopLevel|WDestructiveClose );
  void open(std::list<std::string> filenames=std::list<std::string>(), std::string idxfilename=std::string());
  void setbusy(bool b=true);
  void batchmode(bool b=true) { nogui = b; }
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
  virtual void editSuggest();
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
