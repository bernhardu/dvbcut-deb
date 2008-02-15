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

#include <cstring>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <climits>
#include <memory>
#include <algorithm>

#include <qlabel.h>
#include <qpixmap.h>
#include <qimage.h>
#include <qcolor.h>
#include <qfiledialog.h>
#include <qmessagebox.h>
#include <qslider.h>
#include <qapplication.h>
#include <qstring.h>
#include <qlineedit.h>
#include <qprocess.h>
#include <qpopupmenu.h>
#include <qpushbutton.h>
#include <qaction.h>
#include <qtextbrowser.h>
#include <qfile.h>
#include <qdom.h>
#include <qcursor.h>
#include <qcombobox.h>
#include <qmenubar.h>
#include <qsettings.h>
#include <qregexp.h>
#include <qstatusbar.h>

#include "port.h"
#include "dvbcut.h"
#include "mpgfile.h"
#include "avframe.h"
#include "eventlistitem.h"
#include "mplayererrorbase.h"
#include "lavfmuxer.h"
#include "mpegmuxer.h"
#include "progresswindow.h"
#include "imageprovider.h"
#include "differenceimageprovider.h"
#include "busyindicator.h"
#include "progressstatusbar.h"
#include "exportdialog.h"
#include "settings.h"
#include "exception.h"

#include "version.h"

#define VERSION_STRING	"dvbcut " VERSION "/" REVISION

bool dvbcut::cache_friendly = true;

// **************************************************************************
// ***  busy cursor helpers

class dvbcutbusy : public busyindicator
{
  protected:
    dvbcut *d;
    int bsy;
  public:
    dvbcutbusy(dvbcut *_d) : busyindicator(), d(_d), bsy(0)
    {}
    ~dvbcutbusy()
    {
      while (bsy>0)
        setbusy(false);
      while (bsy<0)
        setbusy(true);
    }
    virtual void setbusy(bool busy=true)
    {
      if (busy)
        ++bsy;
      else {
        if (bsy<=0)
          return;
        --bsy;
      }
      d->setbusy(busy);
    }
};

void dvbcut::setbusy(bool b)
{
  if (b) {
    if (busy==0)
      QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    ++busy;
  } else if (busy>0) {
    --busy;
    if (busy==0)
      QApplication::restoreOverrideCursor();
  }
}

// **************************************************************************
// ***  dvbcut::dvbcut (private constructor)

dvbcut::dvbcut(QWidget *parent, const char *name, WFlags fl)
  :dvbcutbase(parent, name, fl),
    audiotrackpopup(0), recentfilespopup(0), editconvertpopup(0), audiotrackmenuid(-1),
    buf(8 << 20, 128 << 20),
    mpg(0), pictures(0),
    curpic(~0), showimage(true), fine(false),
    jogsliding(false), jogmiddlepic(0),
    mplayer_process(0), imgp(0), busy(0),
    viewscalefactor(1),
    nogui(false)
{
#ifndef HAVE_LIB_AO
  playAudio1Action->setEnabled(false);
  playAudio2Action->setEnabled(false);
  playAudio1Action->removeFrom(playToolbar);
  playAudio2Action->removeFrom(playToolbar);
  playAudio1Action->removeFrom(playMenu);
  playAudio2Action->removeFrom(playMenu);
#endif // ! HAVE_LIB_AO

  audiotrackpopup=new QPopupMenu(this);
  playMenu->insertSeparator();
  audiotrackmenuid=playMenu->insertItem(QString("Audio track"),audiotrackpopup);
  connect( audiotrackpopup, SIGNAL( activated(int) ), this, SLOT( audiotrackchosen(int) ) );

  recentfilespopup=new QPopupMenu(this);
  fileMenu->insertItem(QString("Open recent..."),recentfilespopup,-1,2);
  connect( recentfilespopup, SIGNAL( activated(int) ), this, SLOT( loadrecentfile(int) ) );
  connect( recentfilespopup, SIGNAL( aboutToShow() ), this, SLOT( abouttoshowrecentfiles() ) );

  editconvertpopup=new QPopupMenu(this);
  editMenu->insertItem(QString("Convert bookmarks"),editconvertpopup,-1,8);
  connect( editconvertpopup, SIGNAL( activated(int) ), this, SLOT( editConvert(int) ) );
  connect( editconvertpopup, SIGNAL( aboutToShow() ), this, SLOT( abouttoshoweditconvert() ) );

  setviewscalefactor(settings().viewscalefactor);

  // install event handler
  linslider->installEventFilter(this);

  // set caption
  setCaption(QString(VERSION_STRING));
}

// **************************************************************************
// ***  dvbcut::~dvbcut (destructor)

dvbcut::~dvbcut()
{
  if (mplayer_process) {
    mplayer_process->tryTerminate();
    delete mplayer_process;
  }

  if (audiotrackpopup)
    delete audiotrackpopup;
  if (recentfilespopup)
    delete recentfilespopup;
  if (editconvertpopup)
    delete editconvertpopup;

  if (imgp)
    delete imgp;
  if (mpg)
    delete mpg;
}

// **************************************************************************
// ***  slots (actions)

void dvbcut::fileNew()
{
  dvbcut *d = new dvbcut;
  d->show();
}

void dvbcut::fileOpen()
{
  open();
}

void dvbcut::fileSaveAs()
{
  if (prjfilen.empty() && !mpgfilen.empty() && !mpgfilen.front().empty()) {
    std::string prefix = mpgfilen.front();
    int lastdot = prefix.rfind(".");
    int lastslash = prefix.rfind("/");
    if (lastdot >= 0 && lastdot > lastslash)
      prefix = prefix.substr(0, lastdot);
    prjfilen = prefix + ".dvbcut";
    int nr = 0;
    while (QFileInfo(QString(prjfilen)).exists())
      prjfilen = prefix + "_" + ((const char*)QString::number(++nr)) + ".dvbcut";
  }

  QString s=QFileDialog::getSaveFileName(
    prjfilen,
    settings().prjfilter,
    this,
    "Save project as...",
    "Choose the name of the project file" );

  if (!s)
    return;

  if (QFileInfo(s).exists() && question(
      "File exists - dvbcut",
      s + "\nalready exists. Overwrite?") !=
      QMessageBox::Yes)
    return;

  prjfilen=(const char*)s;
  if (!prjfilen.empty())
    fileSave();
}

void dvbcut::fileSave()
{
  if (prjfilen.empty()) {
    fileSaveAs();
    return;
  }

  QFile outfile(prjfilen);
  if (!outfile.open(IO_WriteOnly)) {
    critical("Failed to write project file - dvbcut",
      QString(prjfilen) + ":\nCould not open file");
    return;
  }

  QDomDocument doc("dvbcut");
  QDomElement root = doc.createElement("dvbcut");
#if 0
  root.setAttribute("mpgfile",mpgfilen.front());
  if (!idxfilen.empty())
    root.setAttribute("idxfile",idxfilen);
#endif
  doc.appendChild(root);

  std::list<std::string>::const_iterator it = mpgfilen.begin();
  while (it != mpgfilen.end()) {
    QDomElement elem = doc.createElement("mpgfile");
    elem.setAttribute("path", *it);
    root.appendChild(elem);
    ++it;
  }

  if (!idxfilen.empty()) {
    QDomElement elem = doc.createElement("idxfile");
    elem.setAttribute("path", idxfilen);
    root.appendChild(elem);
  }

  for (QListBoxItem *item=eventlist->firstItem();item;item=item->next())
    if (item->rtti()==EventListItem::RTTI()) {
      QString elemname;
      EventListItem *eli=(EventListItem*)item;
      EventListItem::eventtype evt=eli->geteventtype();

      if (evt==EventListItem::start)
	elemname="start";
      else if (evt==EventListItem::stop)
	elemname="stop";
      else if (evt==EventListItem::chapter)
	elemname="chapter";
      else if (evt==EventListItem::bookmark)
	elemname="bookmark";
      else
	continue;

      QDomElement elem=doc.createElement(elemname);
      elem.setAttribute("picture",eli->getpicture());
      root.appendChild(elem);
    }

  QTextStream stream(&outfile);
  stream.setEncoding(QTextStream::Latin1);
  stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  stream << doc.toCString();
  outfile.close();
}

void dvbcut::snapshotSave()
{
  std::vector<int> piclist;
  piclist.push_back(curpic);

  snapshotSave(piclist);
}

void dvbcut::chapterSnapshotsSave()
{
  int found=0;
  std::vector<int> piclist;
  for (QListBoxItem *item=eventlist->firstItem();item;item=item->next())
    if (item->rtti()==EventListItem::RTTI()) {
      EventListItem *eli=(EventListItem*)item;
      if (eli->geteventtype()==EventListItem::chapter) {
         piclist.push_back(eli->getpicture());
         found++;
      }
    }

  if (found) {
    snapshotSave(piclist, settings().snapshot_range, settings().snapshot_samples);
  } else
    statusBar()->message(QString("*** No chapters to save! ***"));
}

void dvbcut::snapshotSave(std::vector<int> piclist, int range, int samples)
{
  QString prefix;
  QString type=settings().snapshot_type;
  QString delim=settings().snapshot_delimiter;
  QString ext=settings().snapshot_extension;
  int first = settings().snapshot_first;
  int width = settings().snapshot_width;
  int quality = settings().snapshot_quality;

  // get unique filename
  if (picfilen.isEmpty()) {
    if(settings().snapshot_prefix.isEmpty()) {
      if (!prjfilen.empty())
        prefix = QString(prjfilen);
      else if (!mpgfilen.empty() && !mpgfilen.front().empty())
        prefix = QString(mpgfilen.front());
    } else
      prefix = settings().snapshot_prefix;

    if (!prefix.isEmpty()) {
      int lastdot = prefix.findRev('.');
      int lastslash = prefix.findRev('/');
      if (lastdot >= 0 && lastdot > lastslash)
        prefix = prefix.left(lastdot);
      int nr = first;
      picfilen = prefix + delim + QString::number(nr).rightJustify( width, '0' ) + "." + ext;
      while (QFileInfo(picfilen).exists())
        picfilen = prefix + delim + QString::number(++nr).rightJustify( width, '0' )+ "." + ext;
    }
  }  

  QString s = QFileDialog::getSaveFileName(
    picfilen,
    "Images (*."+ext+")",
    this,
    "Save picture as...",
    "Choose the name of the picture file" );

  if (s.isEmpty())
    return;

  if (QFileInfo(s).exists() && question(
    "File exists - dvbcut",
    s + "\nalready exists. Overwrite?") !=
    QMessageBox::Yes)
    return;

  QImage p;
  int pic, i, nr;
  bool ok=false;
  for (std::vector<int>::iterator it = piclist.begin(); it != piclist.end(); ++it) {

    if(samples>1 && range>0)
      pic = chooseBestPicture(*it, range, samples);
    else
      pic = *it+range;

    // save selected picture to file
    if (imgp)
      p = imgp->getimage(pic,fine);
    else
      p = imageprovider(*mpg, new dvbcutbusy(this), false, viewscalefactor).getimage(pic,fine);
    if(p.save(s,type,quality))
      statusBar()->message("Saved snapshot: " + s);
    else
      statusBar()->message("*** Unable to save snapshot: " + s + "! ***");
 
    // try to "increment" the choosen filename for next snapshot (or use old name as default)
    // No usage of "delim", so it's possible to choose any prefix in front of the number field!
    i = s.findRev(QRegExp("\\d{"+QString::number(width)+","+QString::number(width)+"}\\."+ext+"$"));
    if (i>0) {
      nr = s.mid(i,width).toInt(&ok,10);
      if (ok)
        picfilen = s.left(i) + QString::number(++nr).rightJustify(width, '0')+ "." + ext;
      else
        picfilen = s;
    }
    else
      picfilen = s;

    s = picfilen;
  }

}

int dvbcut::chooseBestPicture(int startpic, int range, int samples)
{
  QImage p;
  QRgb col;
  int idx, x, y, w, h, pic, norm, colors;
  int r, g, b, nr=11, ng=16, nb=5;  // "borrowed" the weights from calc. of qGray = (11*r+16*g+5*b)/32
  double entropy;
  std::vector<double> histogram;

  samples = samples>0 ? samples: 1;
  samples = samples>abs(range) ? abs(range)+1: samples;

  int bestpic = startpic+range, bestnr=0;
  double bestval = 0.;
  int dp = range/(samples-1);
  int ncol = nr*ng*nb;

  // choose the best picture among equidistant samples in the range (not for single snapshots!)
  for (int n=0; n<samples && samples>1 && range>0; n++) {
      pic = startpic+n*dp;

      if (imgp)
        p = imgp->getimage(pic,fine);
      else
        p = imageprovider(*mpg, new dvbcutbusy(this), false, viewscalefactor).getimage(pic,fine);

      // get a measure for complexity of picture (at least good enough to discard single colored frames!)

      // index color space and fill histogram
      w = p.width();
      h = p.height();
      histogram.assign(ncol,0.);
      for(x=0; x<w; x++)
        for(y=0; y<h; y++) {
          col=p.pixel(x,y);
          r=nr*qRed(col)/256;
          g=ng*qGreen(col)/256;
          b=nb*qBlue(col)/256;
          idx=r+nr*g+nr*ng*b;
          histogram[idx]++;
        }

      // calc. probability to fall in a given color class
      colors = 0;
      norm = w*h;
      for(unsigned int i=0; i<histogram.size(); i++)
        if(histogram[i]>0) {
          colors++;
          histogram[i] /= norm;
        }

      // calc. the information entropy (complexity) of the picture
      entropy = 0.;
      for(x=0; x<w; x++)
        for(y=0; y<h; y++) {
          col=p.pixel(x,y);
          r=nr*qRed(col)/256;
          g=ng*qGreen(col)/256;
          b=nb*qBlue(col)/256;
          idx=r+nr*g+nr*ng*b;
          entropy-=histogram[idx]*log(histogram[idx]);
        }
      entropy /= log(2.);

      //fprintf(stderr,"frame %7d, sample %4d (%7d): %10d %10.2f\n",startpic,n,pic,colors,entropy);

      // largest "information content"?
      if(entropy>bestval) {
        bestval=entropy;
        bestpic=pic;
        bestnr=n;
      }
  }
  //fprintf(stderr,"choosing sample / frame: %4d / %7d\n!", bestnr, bestpic);

  return bestpic;
}

void dvbcut::fileExport()
{
  if (expfilen.empty()) {
    std::string newexpfilen;

    if (!prjfilen.empty())
      newexpfilen=prjfilen;
    else if (!mpgfilen.empty() && !mpgfilen.front().empty())
      newexpfilen=mpgfilen.front();

    if (!newexpfilen.empty()) {
      int lastdot(newexpfilen.rfind("."));
      int lastslash(newexpfilen.rfind("/"));
      if (lastdot>=0 && lastslash<lastdot)
        newexpfilen=newexpfilen.substr(0,lastdot);
      expfilen=newexpfilen+".mpg";
      int nr=0;
      while (QFileInfo(QString(expfilen)).exists())
        expfilen=newexpfilen+"_"+((const char*)QString::number(++nr))+".mpg";
    }
  }

  std::auto_ptr<exportdialog> expd(new exportdialog(expfilen,this));
  expd->muxercombo->insertItem("MPEG program stream/DVD (DVBCUT multiplexer)");
  expd->muxercombo->insertItem("MPEG program stream (DVBCUT multiplexer)");
  expd->muxercombo->insertItem("MPEG program stream/DVD (libavformat)");
  expd->muxercombo->insertItem("MPEG transport stream (libavformat)");
#ifndef __WIN32__
  // add possible user configured pipe commands 
  int pipe_items_start=expd->muxercombo->count();
  for (unsigned int i = 0; i < settings().pipe_command.size(); ++i)
    expd->muxercombo->insertItem(settings().pipe_label[i]);
#endif

  if (settings().export_format < 0
      || settings().export_format >= expd->muxercombo->count())
    settings().export_format = 0;
  expd->muxercombo->setCurrentItem(settings().export_format);

  for(int a=0;a<mpg->getaudiostreams();++a) {
    expd->audiolist->insertItem(mpg->getstreaminfo(audiostream(a)).c_str());
    expd->audiolist->setSelected(a,true);
  }

  int expfmt = 0;
  if (!nogui) {
    expd->show();
    if (!expd->exec())
      return;

    settings().export_format = expd->muxercombo->currentItem();
    expfmt = expd->muxercombo->currentItem();

    expfilen=(const char *)(expd->filenameline->text());
    if (expfilen.empty())
      return;
    expd->hide();
  } else if (exportformat > 0 && exportformat < expd->muxercombo->count()) 
    expfmt = exportformat;  

  // create usable chapter lists 
  std::string chapterstring, chaptercolumn;
  if (!chapterlist.empty()) {
    int nchar=0;
    char chapter[16];
    pts_t lastch=-1;
    for(std::list<pts_t>::const_iterator it=chapterlist.begin();
        it!=chapterlist.end();++it)
      if (*it != lastch) {
        lastch=*it;
        // formatting the chapter string
        if (nchar>0) {
          nchar++; 
          chapterstring+=",";
          chaptercolumn+="\n";
        }  
        nchar+=sprintf(chapter,"%02d:%02d:%02d.%03d",
                       int(lastch/(3600*90000)),
                       int(lastch/(60*90000))%60,
                       int(lastch/90000)%60,
                       int(lastch/90)%1000	);
        // append chapter marks to lists for plain text / dvdauthor xml-file         
        chapterstring+=chapter;
        chaptercolumn+=chapter;
      }
  }

  int child_pid = -1;
  int pipe_fds[2];

#ifndef __WIN32__
  // check for piped output
  std::string expcmd;
  size_t pos;
  int ip=expfmt-pipe_items_start; 
  if(ip>=0) {
    expfmt=settings().pipe_format[ip];
    if (settings().pipe_command[ip].find('|')==-1) 
      expcmd = "|"+std::string(settings().pipe_command[ip].ascii());
    else 
      expcmd = std::string(settings().pipe_command[ip].ascii());
       
    if ((pos=expcmd.find("%OUTPUT%"))!=std::string::npos)
      expcmd.replace(pos,8,expfilen);  
  } else 
    expcmd = expfilen;

  // chapter tag can also be used with input field pipes!
  if ((pos=expcmd.find("%CHAPTERS%"))!=std::string::npos)
    expcmd.replace(pos,10,chapterstring);  

  if ((pos=expcmd.find('|'))!=std::string::npos) {
    pos++;   
    size_t end=expcmd.find(' ',pos);  
    //if (!QFileInfo(expcmd.substr(pos,end-pos)).exists() ||
    //    !QFileInfo(expcmd.substr(pos,end-pos)).isExecutable()) {
    // better test if command is found in $PATH, so one don't needs to give the full path name
    std::string which="which "+expcmd.substr(pos,end-pos)+" >/dev/null";
    int irc = system(which.c_str());
    if(irc!=0) { 
      critical("Command not found - dvbcut","Problems with piped output to:\n"+expcmd.substr(pos,end-pos));
      return; 
    }       

    if (::pipe(pipe_fds) < 0)
      return;
    child_pid = fork();
    if (child_pid == 0) {
      ::close(pipe_fds[1]);
      if (pipe_fds[0] != STDIN_FILENO) {
	dup2(pipe_fds[0], STDIN_FILENO);
      }
      //fprintf(stderr, "Executing %s\n", expcmd.c_str()+pos);
      for (int fd=0; fd<256; ++fd)
	if (fd != STDIN_FILENO && fd != STDOUT_FILENO && fd != STDERR_FILENO)
	  ::close(fd);
      execl("/bin/sh", "sh", "-c", expcmd.c_str()+pos, (char *)0);
      _exit(127);
    }
    ::close(pipe_fds[0]);
    if (child_pid < 0) {
      ::close(pipe_fds[1]);
      return;
    }
  } else
#endif
  if (QFileInfo(expfilen).exists() && question(
      "File exists - dvbcut",
      expfilen+"\nalready exists. "
          "Overwrite?") !=
      QMessageBox::Yes)
    return;

  progresswindow *prgwin = 0;
  logoutput *log;
  if (nogui) {
    log = new logoutput;
  }
  else {
    prgwin = new progresswindow(this);
    prgwin->setCaption(QString("export - " + expfilen));
    log = prgwin;
  }

  //   lavfmuxer mux(fmt,*mpg,outfilename);

  std::auto_ptr<muxer> mux;
  uint32_t audiostreammask(0);

  for(int a=0;a<mpg->getaudiostreams();++a)
    if (expd->audiolist->isSelected(a))
      audiostreammask|=1u<<a;

  std::string out_file = (child_pid < 0) ? expfilen :
    std::string("pipe:") + (const char*)QString::number(pipe_fds[1]);

  switch(expfmt) {
    case 1:
      mux=std::auto_ptr<muxer>(new mpegmuxer(audiostreammask,*mpg,out_file.c_str(),false,0));
      break;
    case 2:
      mux=std::auto_ptr<muxer>(new lavfmuxer("dvd",audiostreammask,*mpg,out_file.c_str()));
      break;
    case 3:
      mux=std::auto_ptr<muxer>(new lavfmuxer("mpegts",audiostreammask,*mpg,out_file.c_str()));
      break;
    case 0:
    default:
      mux=std::auto_ptr<muxer>(new mpegmuxer(audiostreammask,*mpg,out_file.c_str()));
      break;
  }

  if (!mux->ready()) {
#ifndef __WIN32__
    if (child_pid > 0) {
      ::close(pipe_fds[1]);
      int wstatus;
      while (waitpid(child_pid, &wstatus, 0)==-1 && errno==EINTR);
    }
#endif
    log->printerror("Unable to set up muxer!");
    if (nogui)
      delete log;
    else {
      prgwin->finish();
      delete prgwin;
    }
    return;
  }

  // starting export, switch source to sequential mode
  buf.setsequential(cache_friendly);

  int startpic, stoppic, savedpic=0;
  pts_t startpts=(*mpg)[0].getpts(), stoppts, savedtime=0;

  for(unsigned int num=0; num<quick_picture_lookup.size(); num++) {
      startpic=quick_picture_lookup[num].startpicture;
      startpts=quick_picture_lookup[num].startpts;
      stoppic=quick_picture_lookup[num].stoppicture;
      stoppts=quick_picture_lookup[num].stoppts;
      
      log->printheading("%d. Exporting %d pictures: %s .. %s",
			num+1,stoppic-startpic,ptsstring(startpts).c_str(),ptsstring(stoppts).c_str());
      mpg->savempg(*mux,startpic,stoppic,savedpic,quick_picture_lookup.back().outpicture,log);

      savedpic=quick_picture_lookup[num].outpicture;
      savedtime=quick_picture_lookup[num].outpts;
  }

  mux.reset();

  log->printheading("Saved %d pictures (%02d:%02d:%02d.%03d)",savedpic,
		    int(savedtime/(3600*90000)),
		    int(savedtime/(60*90000))%60,
		    int(savedtime/90000)%60,
		    int(savedtime/90)%1000	);

#ifndef __WIN32__
  if (child_pid > 0) {
    ::close(pipe_fds[1]);
    int wstatus;
    while (waitpid(child_pid, &wstatus, 0)==-1 && errno==EINTR);
  }

  // do some post processing if requested
  if (ip>=0 && !settings().pipe_post[ip].isEmpty()) {
    expcmd = std::string(settings().pipe_post[ip].ascii());
       
    if ((pos=expcmd.find("%OUTPUT%"))!=std::string::npos)
      expcmd.replace(pos,8,expfilen);  
    if ((pos=expcmd.find("%CHAPTERS%"))!=std::string::npos)
      expcmd.replace(pos,10,chapterstring);  

    pos=expcmd.find(' ');  
    std::string which="which "+expcmd.substr(0,pos)+" >/dev/null";

    log->print("");
    log->printheading("Performing post processing");
    int irc = system(which.c_str());

    if(irc!=0) { 
      critical("Command not found - dvbcut","Problems with post processing command:\n"+expcmd.substr(0,pos));
      log->print("Command not found!");
    } else {      
      int irc = system(expcmd.c_str());
      if(irc!=0) { 
        critical("Post processing error - dvbcut","Post processing command:\n"+
                 expcmd+"\n returned non-zero exit code: " +QString::number(irc));
        log->print("Command reported some problems... please check!");
      } 
      //else      
      //  log->print("Everything seems to be OK...");
    }
  }   
#endif

  // print plain list of chapter marks
  log->print("");
  log->printheading("Chapter list");
  log->print(chaptercolumn.c_str());

  // simple dvdauthor xml file with chapter marks
  std::string filename,destname;
  if (expfilen.rfind("/")<expfilen.length()) 
    filename=expfilen.substr(expfilen.rfind("/")+1);
  else 
    filename=expfilen;
  destname=filename.substr(0,filename.rfind("."));
  log->print("");
  log->printheading("Simple XML-file for dvdauthor with chapter marks");
  log->print("<dvdauthor dest=\"%s\">",destname.c_str());
  log->print("  <vmgm />");
  log->print("  <titleset>");
  log->print("    <titles>");
  log->print("      <pgc>");
  log->print("        <vob file=\"%s\" chapters=\"%s\" />",filename.c_str(),chapterstring.c_str());
  log->print("      </pgc>");
  log->print("    </titles>");
  log->print("  </titleset>");
  log->print("</dvdauthor>");

  if (nogui)
    delete log;
  else {
    prgwin->finish();
    delete prgwin;
  }

  // done exporting, switch back to random mode
  buf.setsequential(false);
}

void dvbcut::fileClose()
{
  close();
}

void dvbcut::addEventListItem(int pic, EventListItem::eventtype type)
{
  //check if requested EventListItem is already in list to avoid doubles!
  for (QListBoxItem *item=eventlist->firstItem();item;item=item->next())
    if (item->rtti()==EventListItem::RTTI()) {
      EventListItem *eli=(EventListItem*)item;
      if (pic==eli->getpicture() && type==eli->geteventtype()) 
        return;
    }

  QPixmap p;
  if (imgp && imgp->rtti() == IMAGEPROVIDER_STANDARD)
    p = imgp->getimage(pic);
  else
    p = imageprovider(*mpg, new dvbcutbusy(this), false, 4).getimage(pic);

  new EventListItem(eventlist, p, type, pic, (*mpg)[pic].getpicturetype(),
                    (*mpg)[pic].getpts() - firstpts);
}

void dvbcut::editBookmark()
{
  addEventListItem(curpic, EventListItem::bookmark);
}


void dvbcut::editChapter()
{
  addEventListItem(curpic, EventListItem::chapter);
  update_quick_picture_lookup_table();
}


void dvbcut::editStop()
{
  addEventListItem(curpic, EventListItem::stop);
  update_quick_picture_lookup_table();
}


void dvbcut::editStart()
{
  addEventListItem(curpic, EventListItem::start);
  update_quick_picture_lookup_table();
}

void dvbcut::editAutoChapters()
{
  int inpic, chapters = 0;
  quick_picture_lookup_t::iterator it;
  QImage p1, p2;

  // the first chapter at 0sec is ALWAYS set by default from update_quick_picture_lookup_table()
  int chapter_start; 
  if(settings().chapter_interval>0) {
    chapter_start = settings().chapter_interval;  // fixed length of intervals
  } else {
    chapter_start = quick_picture_lookup.back().outpicture/(1-settings().chapter_interval);  // given number of chapters
    chapter_start = chapter_start > settings().chapter_minimum ? chapter_start : settings().chapter_minimum;
  }  
  // don't make a new chapter if it would be shorter than the specified minimal length
  int chapter_max = quick_picture_lookup.back().outpicture - settings().chapter_minimum;

  for(int outpic = chapter_start; outpic < chapter_max; outpic+=chapter_start) 
    if (!quick_picture_lookup.empty()) {
      // find the entry in the quick_picture_lookup table that corresponds to given output picture
      it = std::upper_bound(quick_picture_lookup.begin(),quick_picture_lookup.end(),outpic,quick_picture_lookup_s::cmp_outpicture());
      inpic = outpic - it->outpicture + it->stoppicture;   

      if(inpic+settings().chapter_tolerance>it->stoppicture) {
        if(it == quick_picture_lookup.end()) break;
        // take begin of next START/STOP range as chapter picture if to near at end of current range
        it++;
        inpic=it->startpicture;
      } else if(settings().chapter_tolerance>0) {  
        // look for the next scene change inside specified frame tolerance (VERY SLOW!!!)
        if (!imgp) 
          imgp = new imageprovider(*mpg, new dvbcutbusy(this), false, viewscalefactor, settings().chapter_tolerance);
        p2 = imgp->getimage(inpic,fine);  
        for(int pic=inpic+1; pic<inpic+settings().chapter_tolerance && pic<pictures; pic++) {
          // get next picture
          p1 = p2;
          p2 = imgp->getimage(pic,fine);
          if (p2.size()!=p1.size())
            p2=p2.scale(p1.size());

          // calculate color distance between two consecutive frames
          double dist=0.;
          if (p2.depth()==32 && p1.depth()==32)
            for (int y=0;y<p1.height();++y) {
              QRgb *col1=(QRgb*)p1.scanLine(y);
              QRgb *col2=(QRgb*)p2.scanLine(y);

              for (int x=p1.width();x>0;--x) {
                dist+=sqrt(pow(qRed(*col1)-qRed(*col2),2)+pow(qGreen(*col1)-qGreen(*col2),2)+pow(qBlue(*col1)-qBlue(*col2),2));
                // that's a bit faster...   
                //dist+=(abs(qRed(*col1)-qRed(*col2))+abs(qGreen(*col1)-qGreen(*col2))+abs(qBlue(*col1)-qBlue(*col2)));
                ++col1;
                ++col2;
              }
            }
          dist/=(p1.height()*p1.width());
 
          // 50. seems to be a good measure for the color distance at scene changes (about sqrt(3)*50. if sum of abs values)! 
          //fprintf(stderr,"%d, DIST=%f\n",pic,dist); 
          if(dist>settings().chapter_threshold) { 
            inpic=pic;
            statusBar()->message(QString().sprintf("%d. Scene change @ %d, DIST=%f\n",chapters+1,inpic,dist));   
            break;
          }  
        }
      }
      
      addEventListItem(inpic, EventListItem::chapter);
      chapters++;
    }  

  if (chapters)
    update_quick_picture_lookup_table();

}

void dvbcut::editSuggest()
{
  int pic = 0, found=0;
  while ((pic = mpg->nextaspectdiscontinuity(pic)) >= 0) {
    addEventListItem(pic, EventListItem::bookmark);
    found++;
  }
  if (!found)  
    statusBar()->message(QString("*** No aspect ratio changes detected! ***"));   
}

void dvbcut::editImport()
{
  int found=0;
  std::vector<int> bookmarks = mpg->getbookmarks();
  for (std::vector<int>::iterator b = bookmarks.begin(); b != bookmarks.end(); ++b) {   
    addEventListItem(*b, EventListItem::bookmark);
    found++;
  }
  if (!found)  
    statusBar()->message(QString("*** No valid bookmarks available/found! ***"));
}


void dvbcut::abouttoshoweditconvert()
{
  editconvertpopup->clear();
  editconvertpopup->insertItem(QString("START / STOP"), 0);    
  editconvertpopup->insertItem(QString("STOP / START"), 1);    
  editconvertpopup->insertItem(QString("4 : 3"), 2);    
  editconvertpopup->insertItem(QString("16 : 9"), 3);    
}

void dvbcut::editConvert(int option)
{
  // convert Bookmarks to START/STOP markers
  if(option<0 || option>3) return;
  
  int found=0;
  std::vector<int> cutlist;
  for (QListBoxItem *item=eventlist->firstItem();item;item=item->next())
    if (item->rtti()==EventListItem::RTTI()) {
      EventListItem *eli=(EventListItem*)item;
      if (eli->geteventtype()==EventListItem::bookmark) {
         cutlist.push_back(eli->getpicture());
         delete item;       
         found++;
      } 
    } 
  if (found) {
    addStartStopItems(cutlist, option);

    if (found%2) 
      statusBar()->message(QString("*** No matching stop marker!!! ***"));
  }  
  else
    statusBar()->message(QString("*** No bookmarks to convert! ***"));  
}

void dvbcut::addStartStopItems(std::vector<int> cutlist, int option)
{
  // take list of frame numbers and set alternating START/STOP markers
  bool alternate=true;
  EventListItem::eventtype type=EventListItem::start;
  if(option==1)
    type=EventListItem::stop;
  else if(option==2 || option==3)
    alternate=false;   
 
  // make sure list is sorted... 
  sort(cutlist.begin(),cutlist.end());

  // ...AND there are no old START/STOP pairs!!!
  for (QListBoxItem *item=eventlist->firstItem();item;item=item->next())
    if (item->rtti()==EventListItem::RTTI()) {
      EventListItem *eli=(EventListItem*)item;
      if (eli->geteventtype()==EventListItem::start || eli->geteventtype()==EventListItem::stop) 
         delete item;       
    } 
  
  for (std::vector<int>::iterator it = cutlist.begin(); it != cutlist.end(); ++it) {   
    if(!alternate) {
      // set START/STOP according aspect ratio (2=4:3, 3=16:9)
      if (option == (*mpg)[*it].getaspectratio()) 
        type=EventListItem::start;
      else 
        type=EventListItem::stop;  
    } 

    addEventListItem(*it, type);

    if(alternate) {
      // set START/STOP alternatingly
      if(type==EventListItem::start) 
        type=EventListItem::stop;
      else
        type=EventListItem::start;  
    }
  }
  
  update_quick_picture_lookup_table();
}

void dvbcut::viewDifference()
{
  viewNormalAction->setOn(false);
  viewUnscaledAction->setOn(false);
  viewDifferenceAction->setOn(true);

  if (imgp)
    delete imgp;
  imgp=new differenceimageprovider(*mpg,curpic, new dvbcutbusy(this), false, viewscalefactor);
  updateimagedisplay();
}


void dvbcut::viewUnscaled()
{
  viewNormalAction->setOn(false);
  viewUnscaledAction->setOn(true);
  viewDifferenceAction->setOn(false);

  if (!imgp || imgp->rtti()!=IMAGEPROVIDER_UNSCALED) {
    if (imgp)
      delete imgp;
    imgp=new imageprovider(*mpg,new dvbcutbusy(this),true,viewscalefactor);
    updateimagedisplay();
  }
}


void dvbcut::viewNormal()
{
  viewNormalAction->setOn(true);
  viewUnscaledAction->setOn(false);
  viewDifferenceAction->setOn(false);

  if (!imgp || imgp->rtti()!=IMAGEPROVIDER_STANDARD) {
    if (imgp)
      delete imgp;
    imgp=new imageprovider(*mpg,new dvbcutbusy(this),false,viewscalefactor);
    updateimagedisplay();
  }
}

void dvbcut::viewFullSize()
{
  setviewscalefactor(1);
}

void dvbcut::viewHalfSize()
{
  setviewscalefactor(2);
}

void dvbcut::viewQuarterSize()
{
  setviewscalefactor(4);
}

void dvbcut::playPlay()
{
  if (mplayer_process)
    return;

  eventlist->setEnabled(false);
  linslider->setEnabled(false);
  jogslider->setEnabled(false);
  gobutton->setEnabled(false);
  goinput->setEnabled(false);
  gobutton2->setEnabled(false);
  goinput2->setEnabled(false);

#ifdef HAVE_LIB_AO

  playAudio1Action->setEnabled(false);
  playAudio2Action->setEnabled(false);
#endif // HAVE_LIB_AO

  playPlayAction->setEnabled(false);
  playStopAction->setEnabled(true);
  menubar->setItemEnabled(audiotrackmenuid,false);

  fileOpenAction->setEnabled(false);
  fileSaveAction->setEnabled(false);
  fileSaveAsAction->setEnabled(false);
  snapshotSaveAction->setEnabled(false);
  chapterSnapshotsSaveAction->setEnabled(false);
  fileExportAction->setEnabled(false);

  showimage=false;
  imagedisplay->setPixmap(QPixmap());
  imagedisplay->grabKeyboard();

  fine=true;
  linslider->setValue(mpg->lastiframe(curpic));
  dvbcut_off_t offset=(*mpg)[curpic].getpos().packetposition();
  mplayer_curpts=(*mpg)[curpic].getpts();

  dvbcut_off_t partoffset;
  int partindex = buf.getfilenum(offset, partoffset);
  if (partindex == -1)
    return;	// what else can we do?

  mplayer_process=new QProcess(QString("mplayer"));
  mplayer_process->addArgument("-noconsolecontrols");
#ifdef __WIN32__
  mplayer_process->addArgument("-vo");
  mplayer_process->addArgument("directx:noaccel");
#endif
  mplayer_process->addArgument("-wid");
  mplayer_process->addArgument(QString().sprintf("0x%x",int(imagedisplay->winId())));
  mplayer_process->addArgument("-sb");
  mplayer_process->addArgument(QString::number(offset - partoffset));
  mplayer_process->addArgument("-geometry");
  mplayer_process->addArgument(QString().sprintf("%dx%d+0+0",int(imagedisplay->width()),int(imagedisplay->height())));

  if (currentaudiotrack>=0 && currentaudiotrack<mpg->getaudiostreams()) {
    mplayer_process->addArgument("-aid");
    mplayer_process->addArgument(QString().sprintf("0x%x",int(mpg->mplayeraudioid(currentaudiotrack))));
  }

  // for now, pass all filenames from the current one up to the last one
  std::list<std::string>::const_iterator it = mpgfilen.begin();
  for (int i = 0; it != mpgfilen.end(); ++i, ++it)
    if (i >= partindex)
      mplayer_process->addArgument(QString(*it));
 
  mplayer_process->setCommunication(QProcess::Stdout|QProcess::Stderr|QProcess::DupStderr);

  connect(mplayer_process, SIGNAL(processExited()), this, SLOT(mplayer_exited()));
  connect(mplayer_process, SIGNAL(readyReadStdout()), this, SLOT(mplayer_readstdout()));

  mplayer_success=false;

  if (!mplayer_process->start()) {
    delete mplayer_process;
    mplayer_process=0;
    mplayer_exited();
    return;
  }
}

void dvbcut::playStop()
{
  if (mplayer_process)
    mplayer_process->tryTerminate();
}

void dvbcut::playAudio1()
{
#ifdef HAVE_LIB_AO
  qApp->processEvents();
  try
  {
    mpg->playaudio(currentaudiotrack,curpic,-2000);
  }
  catch (const dvbcut_exception &ex)
  {
    ex.show();
  }
#endif // HAVE_LIB_AO
}

void dvbcut::playAudio2()
{
#ifdef HAVE_LIB_AO
  qApp->processEvents();
  try
  {
    mpg->playaudio(currentaudiotrack,curpic,2000);
  }
  catch (const dvbcut_exception &ex)
  {
    ex.show();
  }
#endif // HAVE_LIB_AO
}

// **************************************************************************
// ***  slots

void dvbcut::linslidervalue(int newpic)
{
  if (!mpg || newpic==curpic)
    return;
  if (!fine)
    newpic=mpg->lastiframe(newpic);
  if (!jogsliding)
    jogmiddlepic=newpic;
  if (newpic==curpic)
    return;

  curpic=newpic;
  if (!jogsliding)
    jogmiddlepic=newpic;
  
  update_time_display();
  updateimagedisplay();
}
  
void dvbcut::jogsliderreleased()
{
  jogsliding=false;
  jogmiddlepic=curpic;
  jogslider->setValue(0);
}

void dvbcut::jogslidervalue(int v)
{
  if (!mpg || (v==0 && curpic==jogmiddlepic))
    return;
  jogsliding=true;

  int relpic=0;

  /*
  if (v>jog_offset)
  relpic=int(exp(alpha*(v-jog_offset))+.5);
  else if (v<-jog_offset)
  relpic=-int(exp(alpha*(-v-jog_offset))+.5);
  */
  /*
  alternative function 
  (fits better to external tick interval setting, because jog_offset 
  only affects scale at small numbers AND range of 1 frame is NOT smaller 
  than range of 0 and 2 frames as in old function!)
  */ 
  if (v>0) {
    relpic=int(exp(alpha*v)-settings().jog_offset);
    if (relpic<0) relpic=0;
  }  
  else if (v<0) {
    relpic=-int(exp(-alpha*v)-settings().jog_offset);
    if (relpic>0) relpic=0;
  }  

  int newpic=jogmiddlepic+relpic;
  if (newpic<0)
    newpic=0;
  else if (newpic>=pictures)
    newpic=pictures-1;

  if (relpic>=settings().jog_threshold) {
    newpic=mpg->nextiframe(newpic);
    fine=false;
  } else if (relpic<=-settings().jog_threshold) {
    fine=false;
  } else
    fine=true;

    if (curpic!=newpic)
      linslider->setValue(newpic);

    fine=false;
}


void dvbcut::doubleclickedeventlist(QListBoxItem *lbi)
{
  if (lbi->rtti()!=EventListItem::RTTI())
    return;

  fine=true;
  linslider->setValue(((EventListItem*)lbi)->getpicture());
  fine=false;
}

void dvbcut::eventlistcontextmenu(QListBoxItem *lbi, const QPoint &point)
{
  if (!lbi)
    return;
  if (lbi->rtti()!=EventListItem::RTTI())
    return;
  // is it a problem to have no "const EventListItem &eli=..."? Needed for seteventtype()...! 
  EventListItem &eli=*static_cast<const EventListItem*>(lbi);

  QPopupMenu popup(eventlist);
  popup.insertItem("Go to",1);
  popup.insertItem("Delete",2);
  popup.insertItem("Delete others",3);
  popup.insertItem("Delete all",4);
  popup.insertItem("Delete all start/stops",5);
  popup.insertItem("Delete all chapters",6);
  popup.insertItem("Delete all bookmarks",7);
  popup.insertItem("Convert to start marker",8);
  popup.insertItem("Convert to stop marker",9);
  popup.insertItem("Convert to chapter marker",10);
  popup.insertItem("Convert to bookmark",11);
  popup.insertItem("Display difference from this picture",12);

  QListBox *lb=lbi->listBox();
  QListBoxItem *first=lb->firstItem(),*current,*next;
  EventListItem::eventtype cmptype=EventListItem::none, cmptype2=EventListItem::none;

  switch (popup.exec(point)) {
    case 1:
      fine=true;
      linslider->setValue(eli.getpicture());
      fine=false;
      break;

    case 2:
      {
      EventListItem::eventtype type=eli.geteventtype();
      delete lbi;
      if (type!=EventListItem::bookmark) update_quick_picture_lookup_table();
      }
      break;

    case 3:
      current=first;
      while(current) {
         next=current->next();
         if (current!=lbi) delete current;
         current=next;
      }   
      update_quick_picture_lookup_table();
      break;

    case 4:
      lb->clear();
      update_quick_picture_lookup_table();
      break;

    case 5:
      cmptype=EventListItem::start;
      cmptype2=EventListItem::stop;
    case 6:
      if (cmptype==EventListItem::none) cmptype=EventListItem::chapter;
    case 7:
      if (cmptype==EventListItem::none) cmptype=EventListItem::bookmark;
      current=first;
      while(current) {
        next=current->next();
        const EventListItem &eli_current=*static_cast<const EventListItem*>(current);
        if (eli_current.geteventtype()==cmptype || eli_current.geteventtype()==cmptype2) 
          delete current;
        current=next;
      }       
      if (cmptype!=EventListItem::bookmark) update_quick_picture_lookup_table();
      break;

    case 8:
      eli.seteventtype(EventListItem::start);
      update_quick_picture_lookup_table();
      break;

    case 9: 
      eli.seteventtype(EventListItem::stop);
      update_quick_picture_lookup_table();
      break; 

    case 10:
      eli.seteventtype(EventListItem::chapter);
      update_quick_picture_lookup_table();
      break;

    case 11: 
      eli.seteventtype(EventListItem::bookmark);
      update_quick_picture_lookup_table();
      break; 

    case 12:
      if (imgp)
        delete imgp;
      imgp=new differenceimageprovider(*mpg,eli.getpicture(),new dvbcutbusy(this),false,viewscalefactor);
      updateimagedisplay();
      viewNormalAction->setOn(false);
      viewUnscaledAction->setOn(false);
      viewDifferenceAction->setOn(true);
      break;
  }

}

void dvbcut::clickedgo()
{
  QString text=goinput->text();
  text.stripWhiteSpace();
  bool okay=false;
  int inpic;
  if (text.contains(':') || text.contains('.')) {
    okay=true;
    inpic=string2pts(text)/getTimePerFrame();
  }
  else
    inpic=text.toInt(&okay,0);
  if (okay) {
    fine=true;
    linslider->setValue(inpic);
    fine=false;
  }
  //goinput->clear();
}

void dvbcut::clickedgo2()
{
  QString text=goinput2->text();
  text.stripWhiteSpace();
  bool okay=false;
  int inpic, outpic;
  if (text.contains(':') || text.contains('.')) {
    okay=true;
    outpic=string2pts(text)/getTimePerFrame();
  }
  else
    outpic=text.toInt(&okay,0);
  if (okay && !quick_picture_lookup.empty()) {
    // find the entry in the quick_picture_lookup table that corresponds to given output picture
    quick_picture_lookup_t::iterator it=
      std::upper_bound(quick_picture_lookup.begin(),quick_picture_lookup.end(),outpic,quick_picture_lookup_s::cmp_outpicture());
    inpic=outpic-it->outpicture+it->stoppicture;   
    fine=true;
    linslider->setValue(inpic);
    fine=false;
  }
  //goinput2->clear();
}

void dvbcut::mplayer_exited()
{
  if (mplayer_process) {
    if (!mplayer_success)// && !mplayer_process->normalExit())
    {
      mplayererrorbase *meb=new mplayererrorbase(this,0,false,WDestructiveClose);
      meb->textbrowser->setText(mplayer_out);
      meb->show();
    }


    delete mplayer_process;
    mplayer_process=0;
  }

  eventlist->setEnabled(true);
  linslider->setEnabled(true);
  jogslider->setEnabled(true);
  gobutton->setEnabled(true);
  goinput->setEnabled(true);
  gobutton2->setEnabled(true);
  goinput2->setEnabled(true);

#ifdef HAVE_LIB_AO

  playAudio1Action->setEnabled(true);
  playAudio2Action->setEnabled(true);
#endif // HAVE_LIB_AO

  playPlayAction->setEnabled(true);
  playStopAction->setEnabled(false);
  menubar->setItemEnabled(audiotrackmenuid,true);

  fileOpenAction->setEnabled(true);
  fileSaveAction->setEnabled(true);
  fileSaveAsAction->setEnabled(true);
  snapshotSaveAction->setEnabled(true);
  chapterSnapshotsSaveAction->setEnabled(true);
  fileExportAction->setEnabled(true);

  imagedisplay->releaseKeyboard();

  int cp=curpic;
  jogmiddlepic=curpic;
  curpic=-1;
  showimage=true;
  fine=true;
  linslidervalue(cp);
  linslider->setValue(cp);
  fine=false;
}

void dvbcut::mplayer_readstdout()
{
  if (!mplayer_process)
    return;

  QString line(mplayer_process->readStdout());
  if (!line.length())
    return;

  if (!mplayer_success)
    mplayer_out += line;

  int pos=line.find("V:");
  if (pos<0)
    return;

  line.remove(0,pos+2);
  line=line.stripWhiteSpace();
  for(pos=0;pos<(signed)line.length();++pos)
    if ((line[pos]<'0' || line[pos]>'9')&&line[pos]!='.')
      break;
  line.truncate(pos);
  if (line.length()==0)
    return;

  bool okay;
  double d=line.toDouble(&okay);
  if (d==0 || !okay)
    return;

  if (!mplayer_success) {
    mplayer_success=true;
    mplayer_out.truncate(0);
  }
  pts_t pts=mplayer_ptsreference(pts_t(d*90000.),mplayer_curpts);


  if (pts==mplayer_curpts)
    return;

  int cp=curpic;

  if (pts>mplayer_curpts) {
    while (cp+1<pictures && pts>mplayer_curpts)
      mplayer_curpts=(*mpg)[++cp].getpts();
  } else if (pts<mplayer_curpts-18000) {
    while (cp>1 && mplayer_curpts>pts)
      mplayer_curpts=(*mpg)[--cp].getpts();
  }

  linslider->setValue(cp);
}

void dvbcut::updateimagedisplay()
{
  if (showimage) {
    if (!imgp)
      imgp=new imageprovider(*mpg,new dvbcutbusy(this),false,viewscalefactor);
    QPixmap px=imgp->getimage(curpic,fine);
    imagedisplay->setMinimumSize(px.size());
    imagedisplay->setPixmap(px);
    qApp->processEvents();
  }
}

void dvbcut::audiotrackchosen(int id)
{
  if (id<0 || id>mpg->getaudiostreams())
    return;
  currentaudiotrack=id;
  for(int a=0;a<mpg->getaudiostreams();++a)
    audiotrackpopup->setItemChecked(a,a==id);
}

void dvbcut::abouttoshowrecentfiles()
{
  recentfilespopup->clear();
  QString menuitem;
  for(unsigned int id=0; id<settings().recentfiles.size(); id++) {
    menuitem=QString(settings().recentfiles[id].first.front());
    if(settings().recentfiles[id].first.size()>1)
      menuitem += " ... (+" + QString::number(settings().recentfiles[id].first.size()-1) + ")";
    recentfilespopup->insertItem(menuitem,(signed)id);    
  }   
}

void dvbcut::loadrecentfile(int id)
{
  if (id<0 || id>=(signed)settings().recentfiles.size())
    return;
  open(settings().recentfiles[(unsigned)id].first, settings().recentfiles[(unsigned)id].second);
}

// **************************************************************************
// ***  public functions

void dvbcut::open(std::list<std::string> filenames, std::string idxfilename, std::string expfilename)
{
  if (filenames.empty()) {
    QStringList fn = QFileDialog::getOpenFileNames(
      settings().loadfilter,
      settings().lastdir,
      this,
      "Open file...",
      "Choose one or more MPEG files to open");
    if (fn.empty()) {
      fprintf(stderr,"open(): QFileDialog::getOpenFileNames() returned EMPTY filelist!!!\n");    
      fprintf(stderr,"        If you didn't saw a FileDialog, please check your 'lastdir' settings variable...");    
      return;
    }  
    for (QStringList::Iterator it = fn.begin(); it != fn.end(); ++it)
      filenames.push_back((const char*)*it);

    // remember last directory if requested
    if (settings().lastdir_update) {
      QString dir = fn.front();
      int n = dir.findRev('/');
      if (n > 0)
        dir = dir.left(n);
      else if (n == 0)
        dir = "/";
#ifdef __WIN32__
      // there has to be a / after the device name in case of a top level directory (i.e. 'C:/' and NOT 'C:')
      if (dir.findRev(':') == int(dir.length())-1)
        dir = dir+"/"; 
#endif /* __WIN32__ */      
      settings().lastdir = dir;
    }
  } 

  // hmmmm,... do we really need this? With fn.empty() we never reach this line...
  if (filenames.empty()) 
    return;

  make_canonical(filenames);

  std::string filename = filenames.front();

  // a valid file name has been entered
  setCaption(QString(VERSION_STRING " - " + filename));

  // reset inbuffer
  buf.reset();

  // if an MPEG file has already been opened, close it and reset the UI
  if (mpg) {
    delete mpg;
    mpg=0;
  }

  curpic=~0;
  if (imgp) {
    delete imgp;
    imgp=0;
  }
  eventlist->clear();
  imagedisplay->setBackgroundMode(Qt::PaletteBackground);
  imagedisplay->setMinimumSize(QSize(0,0));
  imagedisplay->setPixmap(QPixmap());
  pictimelabel->clear();
  pictimelabel2->clear();
  linslider->setValue(0);
  jogslider->setValue(0);

  viewNormalAction->setOn(true);
  viewUnscaledAction->setOn(false);
  viewDifferenceAction->setOn(false);

  fileOpenAction->setEnabled(false);
  fileSaveAction->setEnabled(false);
  fileSaveAsAction->setEnabled(false);
  snapshotSaveAction->setEnabled(false);
  chapterSnapshotsSaveAction->setEnabled(false);
  // enable closing even if no file was loaded (mr)
  //fileCloseAction->setEnabled(false);
  fileExportAction->setEnabled(false);
  playPlayAction->setEnabled(false);
  playStopAction->setEnabled(false);
  menubar->setItemEnabled(audiotrackmenuid,false);
  audiotrackpopup->clear();
  editStartAction->setEnabled(false);
  editStopAction->setEnabled(false);
  editChapterAction->setEnabled(false);
  editBookmarkAction->setEnabled(false);
  editAutoChaptersAction->setEnabled(false);
  editSuggestAction->setEnabled(false);
  editImportAction->setEnabled(false);
  //editConvertAction->setEnabled(false);

#ifdef HAVE_LIB_AO

  playAudio1Action->setEnabled(false);
  playAudio2Action->setEnabled(false);
#endif // HAVE_LIB_AO

  viewNormalAction->setEnabled(false);
  viewUnscaledAction->setEnabled(false);
  viewDifferenceAction->setEnabled(false);

  eventlist->setEnabled(false);
  imagedisplay->setEnabled(false);
  pictimelabel->setEnabled(false);
  pictimelabel2->setEnabled(false);
  goinput->setEnabled(false);
  gobutton->setEnabled(false);
  goinput2->setEnabled(false);
  gobutton2->setEnabled(false);
  linslider->setEnabled(false);
  jogslider->setEnabled(false);

  std::string prjfilename;
  QDomDocument domdoc;
  {
    QFile infile(filename);
    if (infile.open(IO_ReadOnly)) {
      QString line;
      while (line.length()==0) {
        if (infile.readLine(line,512)<=0)
          break;
        line=line.stripWhiteSpace();
      }
      if (line.startsWith(QString("<!DOCTYPE"))
       || line.startsWith(QString("<?xml"))) {
        infile.at(0);
        QString errormsg;
        if (domdoc.setContent(&infile,false,&errormsg)) {
          QDomElement docelem = domdoc.documentElement();
          if (docelem.tagName() != "dvbcut") {
            critical("Failed to read project file - dvbcut",
	      QString(filename) + ":\nNot a valid dvbcut project file");
            fileOpenAction->setEnabled(true);
            return;
          }
	  // parse elements, new-style first
	  QDomNode n;
          filenames.clear();
	  if (!nogui) {    
	    // in batch mode CLI-switches have priority!              
	    idxfilename.clear();
	    expfilename.clear();
	  }    
	  for (n = domdoc.documentElement().firstChild(); !n.isNull(); n = n.nextSibling()) {
	    QDomElement e = n.toElement();
	    if (e.isNull())
	      continue;
	    if (e.tagName() == "mpgfile") {
	      QString qs = e.attribute("path");
	      if (!qs.isEmpty())
		filenames.push_back((const char*)qs);
	    }
	    else if (e.tagName() == "idxfile" && idxfilename.empty()) {
	      QString qs = e.attribute("path");
	      if (!qs.isEmpty())
		idxfilename = (const char*)qs;
	    }
	    else if (e.tagName() == "expfile" && expfilename.empty()) {           
	      QString qs = e.attribute("path");
	      if (!qs.isEmpty())
		expfilename = (const char*)qs;
	      qs = e.attribute("format");
              bool okay=false;
	      if (!qs.isEmpty()) {
                int val = qs.toInt(&okay,0);
		if(okay) exportformat = val;
              }  
	    }
	  }
	  // try old-style project file format
	  if (filenames.empty()) {
	    QString qs = docelem.attribute("mpgfile");
	    if (!qs.isEmpty())
	      filenames.push_back((const char*)qs);
	  }
	  if (idxfilename.empty()) {
	    QString qs = docelem.attribute("idxfile");
	    if (!qs.isEmpty())
	      idxfilename = (const char*)qs;
	  }
	  if (expfilename.empty()) {
	    QString qs = docelem.attribute("expfile");
	    if (!qs.isEmpty())
	      expfilename = (const char*)qs;
	  }
	  // sanity check
	  if (filenames.empty()) {
	    critical("Failed to read project file - dvbcut",
	      QString(filename) + ":\nNo MPEG filename given in project file");
	    fileOpenAction->setEnabled(true);
	    return;
	  }
          prjfilename = filename;
        }
        else {
          critical("Failed to read project file - dvbcut",
	    QString(filename) + ":\n" + errormsg);
          fileOpenAction->setEnabled(true);
          return;
        }
      }
    }
  }

  // make filename the first MPEG file
  filename = filenames.front();

  dvbcutbusy busy(this);
  busy.setbusy(true);

  mpg = 0;
  std::string errormessage;
  std::list<std::string>::const_iterator it = filenames.begin();
  while (it != filenames.end() && buf.open(*it, &errormessage))
    ++it;
  buf.setsequential(cache_friendly);
  if (it == filenames.end()) {
    mpg = mpgfile::open(buf, &errormessage);
  }
  busy.setbusy(false);

  if (!mpg) {
    critical("Failed to open file - dvbcut", errormessage);
    fileOpenAction->setEnabled(true);
    return;
  }

  if (nogui && idxfilename.empty())
    idxfilename = filename + ".idx";

  if (idxfilename.empty()) {
    QString s=QFileDialog::getSaveFileName(
        filename+".idx",
    settings().idxfilter,
    this,
    "Choose index file...",
    "Choose the name of the index file" );
    if (s)
      idxfilename=(const char*)s;
    else {
      delete mpg;
      mpg=0;
      fileOpenAction->setEnabled(true);
      return;
    }
  }

  make_canonical(idxfilename);

  pictures=-1;

  if (!idxfilename.empty()) {
    std::string errorstring;
    busy.setbusy(true);
    pictures=mpg->loadindex(idxfilename.c_str(),&errorstring);
    int serrno=errno;
    busy.setbusy(false);
    if (nogui && pictures > 0)
      fprintf(stderr,"Loaded index with %d pictures!\n",pictures);
    if (pictures==-1 && serrno!=ENOENT) {
      delete mpg;
      mpg=0;
      critical("Failed to open file - dvbcut",errorstring);
      fileOpenAction->setEnabled(true);
      return;
    }
    if (pictures==-2) {
      delete mpg;
      mpg=0;
      critical("Invalid index file - dvbcut", errorstring);
      fileOpenAction->setEnabled(true);
      return;
    }
    if (pictures<=-3) {
      delete mpg;
      mpg=0;
      critical("Index file mismatch - dvbcut", errorstring);
      fileOpenAction->setEnabled(true);
      return;
    }
  }

  if (pictures<0) {
    progressstatusbar psb(statusBar());
    psb.setprogress(500);
    psb.print("Indexing '%s'...",filename.c_str());
    std::string errorstring;
    busy.setbusy(true);
    pictures=mpg->generateindex(idxfilename.empty()?0:idxfilename.c_str(),&errorstring,&psb);
    busy.setbusy(false);
    if (nogui && pictures > 0)
      fprintf(stderr,"Generated index with %d pictures!\n",pictures);

    if (psb.cancelled()) {
      delete mpg;
      mpg=0;
      fileOpenAction->setEnabled(true);
      return;
    }

    if (pictures<0) {
      delete mpg;
      mpg=0;
      critical("Error creating index - dvbcut",
	       QString("Cannot create index for\n")+filename+":\n"+errorstring);
      fileOpenAction->setEnabled(true);
      return;
    } else if (!errorstring.empty()) {
      critical("Error saving index file - dvbcut", QString(errorstring));
    }
  }

  if (pictures<1) {
    delete mpg;
    mpg=0;
    critical("Invalid MPEG file - dvbcut",
	     QString("The chosen file\n")+filename+"\ndoes not contain any video");
    fileOpenAction->setEnabled(true);
    return;
  }

  // file loaded, switch to random mode
  buf.setsequential(false);

  mpgfilen=filenames;
  idxfilen=idxfilename;
  prjfilen=prjfilename;
  expfilen=expfilename;
  picfilen=QString::null;
  if (prjfilen.empty())
    addtorecentfiles(mpgfilen,idxfilen);
  else {
    std::list<std::string> dummy_list;
    dummy_list.push_back(prjfilen);
    addtorecentfiles(dummy_list);
  }

  firstpts=(*mpg)[0].getpts();
  timeperframe=(*mpg)[1].getpts()-(*mpg)[0].getpts();

  double fps=27.e6/double(mpgfile::frameratescr[(*mpg)[0].getframerate()]);
  linslider->setMaxValue(pictures-1);
  linslider->setLineStep(int(300*fps));
  linslider->setPageStep(int(900*fps));
  if (settings().lin_interval > 0)
    linslider->setTickInterval(int(settings().lin_interval*fps));

  //alpha=log(jog_maximum)/double(100000-jog_offset);
  // with alternative function
  alpha=log(settings().jog_maximum)/100000.;
  if (settings().jog_interval > 0 && settings().jog_interval <= 100000) 
    jogslider->setTickInterval(int(100000/settings().jog_interval));

  imagedisplay->setBackgroundMode(Qt::NoBackground);
  curpic=~0;
  showimage=true;
  fine=false;
  jogsliding=false;
  jogmiddlepic=0;
  imgp=new imageprovider(*mpg,new dvbcutbusy(this),false,viewscalefactor);

  linslidervalue(0);
  linslider->setValue(0);
  jogslider->setValue(0);

  {
    EventListItem *eli=new EventListItem(0,imgp->getimage(0),EventListItem::start,9999999,2,0);
    eventlist->setMinimumWidth(eli->width(eventlist)+24);
    delete eli;
  }

  if (!domdoc.isNull()) {
    QDomElement e;
    for (QDomNode n=domdoc.documentElement().firstChild();!n.isNull();n=n.nextSibling())
      if (!(e=n.toElement()).isNull()) {
      EventListItem::eventtype evt;
      if (e.tagName()=="start")
        evt=EventListItem::start;
      else if (e.tagName()=="stop")
        evt=EventListItem::stop;
      else if (e.tagName()=="chapter")
        evt=EventListItem::chapter;
      else if (e.tagName()=="bookmark")
        evt=EventListItem::bookmark;
      else
        continue;
      bool okay=false;
      int picnum;
      QString str=e.attribute("picture","-1");
      if (str.contains(':') || str.contains('.')) {
        okay=true;
        picnum=string2pts(str)/getTimePerFrame();
      }
      else
        picnum=str.toInt(&okay,0);
      if (okay && picnum>=0 && picnum<pictures) {
        new EventListItem(eventlist,imgp->getimage(picnum),evt,picnum,(*mpg)[picnum].getpicturetype(),(*mpg)[picnum].getpts()-firstpts);
        qApp->processEvents();
      }
    }
  }

  update_quick_picture_lookup_table();

  fileOpenAction->setEnabled(true);
  fileSaveAction->setEnabled(true);
  fileSaveAsAction->setEnabled(true);
  snapshotSaveAction->setEnabled(true);
  chapterSnapshotsSaveAction->setEnabled(true);
  fileCloseAction->setEnabled(true);
  fileExportAction->setEnabled(true);
  playPlayAction->setEnabled(true);
  menubar->setItemEnabled(audiotrackmenuid,true);
  playStopAction->setEnabled(false);
  editStartAction->setEnabled(true);
  editStopAction->setEnabled(true);
  editChapterAction->setEnabled(true);
  editBookmarkAction->setEnabled(true);
  editAutoChaptersAction->setEnabled(true);
  editSuggestAction->setEnabled(true);
  editImportAction->setEnabled(true);
  //editConvertAction->setEnabled(true);
  viewNormalAction->setEnabled(true);
  viewUnscaledAction->setEnabled(true);
  viewDifferenceAction->setEnabled(true);

#ifdef HAVE_LIB_AO

  if (mpg->getaudiostreams()) {
    playAudio1Action->setEnabled(true);
    playAudio2Action->setEnabled(true);
  }
#endif // HAVE_LIB_AO

  eventlist->setEnabled(true);
  imagedisplay->setEnabled(true);
  pictimelabel->setEnabled(true);
  pictimelabel2->setEnabled(true);
  goinput->setEnabled(true);
  gobutton->setEnabled(true);
  goinput2->setEnabled(true);
  gobutton2->setEnabled(true);
  linslider->setEnabled(true);
  jogslider->setEnabled(true);

  //   audiotrackmenuids.clear();
  for(int a=0;a<mpg->getaudiostreams();++a) {
    //     audiotrackmenuids.push_back(audiotrackpopup->insertItem(QString(mpg->getstreaminfo(audiostream(a)))));
    audiotrackpopup->insertItem(QString(mpg->getstreaminfo(audiostream(a))),a);
  }
  if (mpg->getaudiostreams()>0) {
    audiotrackpopup->setItemChecked(0,true);
    currentaudiotrack=0;
  } else
    currentaudiotrack=-1;
}

// **************************************************************************
// ***  protected functions

void dvbcut::addtorecentfiles(const std::list<std::string> &filenames, const std::string &idxfilename)
{

  for(std::vector<std::pair<std::list<std::string>,std::string> >::iterator it=settings().recentfiles.begin();
      it!=settings().recentfiles.end();)
    // checking the size and the first/last filename should be enough... but maybe I'm to lazy! ;-)
    if (it->first.size()==filenames.size() && it->first.front()==filenames.front() && it->first.back()==filenames.back())
      it=settings().recentfiles.erase(it);
    else
      ++it;

  settings().recentfiles.insert(settings().recentfiles.begin(),std::pair<std::list<std::string>,std::string>(filenames,idxfilename));

  while (settings().recentfiles.size()>settings().recentfiles_max)
    settings().recentfiles.pop_back();
}

void dvbcut::setviewscalefactor(int factor)
{
  if (factor!=1 && factor!=2 && factor!=4)
    factor=1;
  viewFullSizeAction->setOn(factor==1);
  viewHalfSizeAction->setOn(factor==2);
  viewQuarterSizeAction->setOn(factor==4);

  settings().viewscalefactor = factor;

  if (factor!=viewscalefactor) {
    viewscalefactor=factor;
    if (imgp) {
      imgp->setviewscalefactor(factor);
      updateimagedisplay();
    }
  }
}

bool dvbcut::eventFilter(QObject *watched, QEvent *e) {
  if (e->type() == QEvent::Wheel) {
    QWheelEvent *we = (QWheelEvent*)e;
    if (watched == linslider) {
      // process event myself
      int delta = we->delta();
      int incr = 0;
      if (we->state() & AltButton)
        incr = settings().wheel_increments[WHEEL_INCR_ALT];
      else if (we->state() & ControlButton)
        incr = settings().wheel_increments[WHEEL_INCR_CTRL];
      else if (we->state() & ShiftButton)
        incr = settings().wheel_increments[WHEEL_INCR_SHIFT];
      else
        incr = settings().wheel_increments[WHEEL_INCR_NORMAL];
      if (incr != 0) {
        bool save = fine;
	// use fine positioning if incr is small
        fine = (incr < 0 ? -incr : incr) < settings().wheel_threshold;
	// Note: delta is a multiple of 120 (see Qt documentation)
        int newpos = curpic - (delta * incr) / settings().wheel_delta;
        if (newpos < 0)
	  newpos = 0;
	else if (newpos >= pictures)
	  newpos = pictures - 1;
        linslider->setValue(newpos);
        fine = save;
      }
      return true;
    }
  }
  // propagate to base class
  return dvbcutbase::eventFilter(watched, e);
}

int
dvbcut::question(const QString & caption, const QString & text)
{
  if (nogui) {
    fprintf(stderr, "%s\n%s\n(assuming No)\n", caption.ascii(), text.ascii());
    return QMessageBox::No;
  }
  return QMessageBox::question(this, caption, text, QMessageBox::Yes,
    QMessageBox::No | QMessageBox::Default | QMessageBox::Escape);
}

int
dvbcut::critical(const QString & caption, const QString & text)
{
  if (nogui) {
    fprintf(stderr, "%s\n%s\n(aborting)\n", caption.ascii(), text.ascii());
    return QMessageBox::Abort;
  }
  return QMessageBox::critical(this, caption, text,
    QMessageBox::Abort, QMessageBox::NoButton);
}

void
dvbcut::make_canonical(std::string &filename) {
  if (filename[0] != '/') {
    char resolved_path[PATH_MAX];
    char *rp = realpath(filename.c_str(), resolved_path);
    if (rp)
      filename = rp;
  }
}

void
dvbcut::make_canonical(std::list<std::string> &filenames) {
  std::list<std::string>::const_iterator it = filenames.begin();
  std::list<std::string> newlist;

  while (it != filenames.end()) {
    std::string tmp = *it;
    make_canonical(tmp);
    newlist.push_back(tmp);
    ++it;
  }
  filenames = newlist;
}
 
inline static QString
timestr(pts_t pts) {
  return QString().sprintf("%02d:%02d:%02d.%03d",
    int(pts/(3600*90000)),
    int(pts/(60*90000))%60,
    int(pts/90000)%60,
    int(pts/90)%1000);
}

void dvbcut::update_time_display()
  {
  const index::picture &idx=(*mpg)[curpic];
  const pts_t pts=idx.getpts()-firstpts;
  
  int outpic=0;
  pts_t outpts=0;
  QChar mark = ' ';
  
  // find the entry in the quick_picture_lookup table that corresponds to curpic
  quick_picture_lookup_t::iterator it=
    std::upper_bound(quick_picture_lookup.begin(),quick_picture_lookup.end(),curpic,quick_picture_lookup_s::cmp_picture());
   
  if (it!=quick_picture_lookup.begin())
   {
     // curpic is not before the first entry of the table
     --it;
     if (curpic < it->stoppicture)
     {
       // curpic is between (START and STOP[ pics of the current entry
       outpic=curpic-it->stoppicture+it->outpicture;
       outpts=pts-it->stoppts+it->outpts;
       mark = '*';
     }
     else
     {
       // curpic is after the STOP-1 pic of the current entry
       outpic=it->outpicture;
       outpts=it->outpts;
     }
   }
       
  QString curtime =
    QString(QChar(IDX_PICTYPE[idx.getpicturetype()]))
    + " " + timestr(pts);
  QString outtime =
    QString(mark) + " " + timestr(outpts);
  pictimelabel->setText(curtime);
  pictimelabel2->setText(outtime);
  goinput->setText(QString::number(curpic));
  goinput2->setText(QString::number(outpic));

  }

void dvbcut::update_quick_picture_lookup_table() {
  // that's the (only) place where the event list should be scanned for  
  // the exported pictures ranges, i.e. for START/STOP/CHAPTER markers!
  quick_picture_lookup.clear(); 
  chapterlist.clear();
  
  chapterlist.push_back(0);
    
  int startpic, stoppic, outpics=0, lastchapter=-2;
  pts_t startpts, stoppts, outpts=0;
  bool realzero=false;
  
  if(!nogui) {
    // overwrite CLI options
    start_bof = settings().start_bof;
    stop_eof = settings().stop_eof;
  }
  
  if (start_bof) {
    startpic=0;
    startpts=(*mpg)[0].getpts()-firstpts; 
  }
  else {
    startpic=-1;
    startpts=0; 
  }
  
  for (QListBoxItem *item=eventlist->firstItem();item;item=item->next())
    if (item->rtti()==EventListItem::RTTI()) {
    const EventListItem &eli=*static_cast<const EventListItem*>(item);
    switch (eli.geteventtype()) {
      case EventListItem::start:
	if (startpic<0 || (start_bof && startpic==0 && !realzero)) {
          startpic=eli.getpicture();
          startpts=eli.getpts();
          if (startpic==0)
	    realzero=true;        
          // did we have a chapter in the eventlist directly before?
          if(lastchapter==startpic)
            chapterlist.push_back(outpts);
        }
        break;
      case EventListItem::stop:
        if (startpic>=0) {
          stoppic=eli.getpicture();
          stoppts=eli.getpts();        
          outpics+=stoppic-startpic;
          outpts+=stoppts-startpts;
          
          quick_picture_lookup.push_back(quick_picture_lookup_s(startpic,startpts,stoppic,stoppts,outpics,outpts));

          startpic=-1;
        }
        break;
      case EventListItem::chapter:
        lastchapter=eli.getpicture();
	if (startpic>=0)
	  chapterlist.push_back(eli.getpts()-startpts+outpts);
	break;
      default:
        break;
      }
    }

  // last item in list was a (real or virtual) START
  if (stop_eof && startpic>=0) {
    // create a new export range by adding a virtual STOP marker at EOF 
    stoppic=pictures-1;
    stoppts=(*mpg)[stoppic].getpts()-firstpts;
    outpics+=stoppic-startpic;
    outpts+=stoppts-startpts;
    
    quick_picture_lookup.push_back(quick_picture_lookup_s(startpic,startpts,stoppic,stoppts,outpics,outpts)); 
  }
  
  update_time_display();
}

