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

#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <memory>

#include <qlabel.h>
#include <qpixmap.h>
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
      setCursor(QCursor(Qt::WaitCursor));
    ++busy;
    } else if (busy>0) {
    --busy;
    if (busy==0)
      unsetCursor();
    }
  }

// **************************************************************************
// ***  dvbcut::dvbcut (private constructor)

dvbcut::dvbcut(QWidget *parent, const char *name, WFlags fl)
    :dvbcutbase(parent, name, fl),
    audiotrackpopup(0), recentfilespopup(0), audiotrackmenuid(-1),
    mpg(0), pictures(0),
    curpic(~0), showimage(true), fine(false),
    jogsliding(false), jogmiddlepic(0),
    mplayer_process(0), imgp(0), busy(0),
    viewscalefactor(1)
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

  setviewscalefactor(settings.viewscalefactor);

  // install event handler
  linslider->installEventFilter(this);

  show();
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

  if (imgp)
    delete imgp;
  if (mpg)
    delete mpg;
  }

// **************************************************************************
// ***  slots (actions)

void dvbcut::fileNew()
  {
  new dvbcut;
  }

void dvbcut::fileOpen()
  {
  open();
  }

void dvbcut::fileSaveAs()
  {
  QString s=QFileDialog::getSaveFileName(
              prjfilen,
              settings.prjfilter,
              this,
              "Save project as...",
              "Choose the name of the project file" );

  if (!s)
    return;

  if (QFileInfo(s).exists() && QMessageBox::question(this,
      "File exists - dvbcut",
      s+"\nalready exists. "
      "Overwrite?",
      QMessageBox::Yes,
      QMessageBox::No |
      QMessageBox::Default |
      QMessageBox::Escape) !=
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
    QMessageBox::critical(this,"Failed to write project file - dvbcut",QString(prjfilen)+
                          ":\nCould not open file",
                          QMessageBox::Abort,
                          QMessageBox::NoButton);
    return;
    }

  QDomDocument doc("dvbcut");
  QDomElement root=doc.createElement("dvbcut");
  root.setAttribute("mpgfile",mpgfilen);
  if (!idxfilen.empty())
    root.setAttribute("idxfile",idxfilen);
  doc.appendChild(root);

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
  stream <<  doc.toString();
  outfile.close();
  }

void dvbcut::fileExport()
  {
  if (expfilen.empty()) {
    std::string newexpfilen;

    if (!prjfilen.empty())
      newexpfilen=prjfilen;
    else if (!mpgfilen.empty())
      newexpfilen=mpgfilen;

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
  expd->muxercombo->insertItem("DVD (DVBCUT multiplexer)");
  expd->muxercombo->insertItem("MPEG program stream (DVBCUT multiplexer)");
  expd->muxercombo->insertItem("MPEG program stream/DVD (libavformat)");
  expd->muxercombo->insertItem("MPEG transport stream (libavformat)");

  for(int a=0;a<mpg->getaudiostreams();++a) {
    expd->audiolist->insertItem(mpg->getstreaminfo(audiostream(a)).c_str());
    expd->audiolist->setSelected(a,true);
    }

  expd->show();
  if (!expd->exec())
    return;
  expfilen=(const char *)(expd->filenameline->text());
  if (expfilen.empty())
    return;
  expd->hide();

  if (QFileInfo(expfilen).exists() && QMessageBox::question(this,
      "File exists - dvbcut",
      expfilen+"\nalready exists. "
      "Overwrite?",
      QMessageBox::Yes,
      QMessageBox::No |
      QMessageBox::Default |
      QMessageBox::Escape) !=
      QMessageBox::Yes)
    return;

  progresswindow prgwin(this);
  //   lavfmuxer mux(fmt,*mpg,outfilename);

  std::auto_ptr<muxer> mux;
  uint32_t audiostreammask(0);

  for(int a=0;a<mpg->getaudiostreams();++a)
    if (expd->audiolist->isSelected(a))
      audiostreammask|=1u<<a;

  switch(expd->muxercombo->currentItem()) {
      case 1:
      mux=std::auto_ptr<muxer>(new mpegmuxer(audiostreammask,*mpg,expfilen.c_str(),false,0));
      break;
      case 2:
      mux=std::auto_ptr<muxer>(new lavfmuxer("dvd",audiostreammask,*mpg,expfilen.c_str()));
      break;
      case 3:
      mux=std::auto_ptr<muxer>(new lavfmuxer("mpegts",audiostreammask,*mpg,expfilen.c_str()));
      break;
      case 0:
      default:
      mux=std::auto_ptr<muxer>(new mpegmuxer(audiostreammask,*mpg,expfilen.c_str()));
      break;
    }

  if (!mux->ready()) {
    prgwin.printerror("Unable to set up muxer!");
    prgwin.finish();
    return;
    }

  int startpic=-1;
  int totalpics=0;

  for(QListBoxItem *lbi=eventlist->firstItem();lbi;lbi=lbi->next())
    if (lbi->rtti()==EventListItem::RTTI()) {
      EventListItem &eli=(EventListItem&)*lbi;
      switch (eli.geteventtype()) {
          case EventListItem::start:
          if (startpic<0) {
            startpic=eli.getpicture();
            }
          break;
          case EventListItem::stop:
          if (startpic>=0) {
            int stoppic=eli.getpicture();
            totalpics+=stoppic-startpic;
            startpic=-1;
            }
          break;
          default:
          break;
        }
      }

  int savedpic=0;
  long long savedtime=0;
  pts_t startpts=0;
  std::list<pts_t> chapterlist;
  chapterlist.push_back(0);
  startpic=-1;

  for(QListBoxItem *lbi=eventlist->firstItem();lbi && !prgwin.cancelled();lbi=lbi->next())
    if (lbi->rtti()==EventListItem::RTTI()) {
      EventListItem &eli=(EventListItem&)*lbi;

      switch (eli.geteventtype()) {
          case EventListItem::start:
          if (startpic<0) {
            startpic=eli.getpicture();
            startpts=(*mpg)[startpic].getpts();
            }
          break;
          case EventListItem::stop:
          if (startpic>=0) {
            int stoppic=eli.getpicture();
            pts_t stoppts=(*mpg)[stoppic].getpts();
            prgwin.printheading("Exporting %d pictures: %s .. %s",
                                stoppic-startpic,ptsstring(startpts-firstpts).c_str(),ptsstring(stoppts-firstpts).c_str());
            mpg->savempg(*mux,startpic,stoppic,savedpic,totalpics,&prgwin);
            savedpic+=stoppic-startpic;
            savedtime+=stoppts-startpts;
            startpic=-1;
            }
          break;
          case EventListItem::chapter:
          if (startpic==-1)
            chapterlist.push_back(savedtime);
          else
            chapterlist.push_back((*mpg)[eli.getpicture()].getpts()-startpts+savedtime);
          break;
          case EventListItem::none:
          case EventListItem::bookmark:
          break;
        }

      }

  mux.reset();

  prgwin.printheading("Saved %d pictures (%02d:%02d:%02d.%03d)\n",savedpic,
                      int(savedtime/(3600*90000)),
                      int(savedtime/(60*90000))%60,
                      int(savedtime/90000)%60,
                      int(savedtime/90)%1000	);

  if (!chapterlist.empty()) {
    prgwin.printheading("Chapterlist:");
    pts_t lastch=-1;
    for(std::list<pts_t>::const_iterator it=chapterlist.begin();
        it!=chapterlist.end();++it)
      if (*it != lastch) {
        lastch=*it;
        prgwin.print("%02d:%02d:%02d.%03d",
                     int(lastch/(3600*90000)),
                     int(lastch/(60*90000))%60,
                     int(lastch/90000)%60,
                     int(lastch/90)%1000	);
        }
    }

  prgwin.finish();
  }

void dvbcut::fileClose()
  {
  close();
  }

void dvbcut::editBookmark()
  {
  QPixmap p;
  if (imgp && imgp->rtti()==IMAGEPROVIDER_STANDARD)
    p=imgp->getimage(curpic);
  else
    p=imageprovider(*mpg,new dvbcutbusy(this),false,4).getimage(curpic);

  new EventListItem(eventlist,p,
                    EventListItem::bookmark,
                    curpic,(*mpg)[curpic].getpicturetype(),
                    (*mpg)[curpic].getpts()-firstpts);


  }


void dvbcut::editChapter()
  {
  QPixmap p;
  if (imgp && imgp->rtti()==IMAGEPROVIDER_STANDARD)
    p=imgp->getimage(curpic);
  else
    p=imageprovider(*mpg,new dvbcutbusy(this),false,4).getimage(curpic);

  new EventListItem(eventlist,p,
                    EventListItem::chapter,
                    curpic,(*mpg)[curpic].getpicturetype(),
                    (*mpg)[curpic].getpts()-firstpts);
  }


void dvbcut::editStop()
  {
  QPixmap p;
  if (imgp && imgp->rtti()==IMAGEPROVIDER_STANDARD)
    p=imgp->getimage(curpic);
  else
    p=imageprovider(*mpg,new dvbcutbusy(this),false,4).getimage(curpic);

  new EventListItem(eventlist,p,
                    EventListItem::stop,
                    curpic,(*mpg)[curpic].getpicturetype(),
                    (*mpg)[curpic].getpts()-firstpts);
  }


void dvbcut::editStart()
  {
  QPixmap p;
  if (imgp && imgp->rtti()==IMAGEPROVIDER_STANDARD)
    p=imgp->getimage(curpic);
  else
    p=imageprovider(*mpg,new dvbcutbusy(this),false,4).getimage(curpic);

  new EventListItem(eventlist,p,
                    EventListItem::start,
                    curpic,(*mpg)[curpic].getpicturetype(),
                    (*mpg)[curpic].getpts()-firstpts);
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
  fileExportAction->setEnabled(false);

  showimage=false;
  imagedisplay->setPixmap(QPixmap());
  imagedisplay->grabKeyboard();

  fine=true;
  linslider->setValue(mpg->lastiframe(curpic));
  off_t offset=(*mpg)[curpic].getpos().packetposition();
  mplayer_curpts=(*mpg)[curpic].getpts();

  mplayer_process=new QProcess(QString("mplayer"));
  mplayer_process->addArgument("-noconsolecontrols");
  mplayer_process->addArgument("-wid");
  mplayer_process->addArgument(QString().sprintf("0x%x",int(imagedisplay->winId())));
  mplayer_process->addArgument("-geometry");
  mplayer_process->addArgument(QString().sprintf("%dx%d",int(imagedisplay->width()),int(imagedisplay->height())));
  mplayer_process->addArgument("-sb");
  mplayer_process->addArgument(QString::number(offset));

  if (currentaudiotrack>=0 && currentaudiotrack<mpg->getaudiostreams()) {
    mplayer_process->addArgument("-aid");
    mplayer_process->addArgument(QString().sprintf("0x%x",int(mpg->mplayeraudioid(currentaudiotrack))));
    }

  mplayer_process->addArgument(QString(mpgfilen));

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
  mpg->playaudio(currentaudiotrack,curpic,-2000);
#endif // HAVE_LIB_AO

  }

void dvbcut::playAudio2()
  {
#ifdef HAVE_LIB_AO
  qApp->processEvents();
  mpg->playaudio(currentaudiotrack,curpic,2000);
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
  const index::picture &idx=(*mpg)[curpic];

  picnrlabel->setText(QString::number(curpic)+" "+IDX_PICTYPE[idx.getpicturetype()]);
  pts_t pts=idx.getpts()-firstpts;
  pictimelabel->setText(QString().sprintf("%02d:%02d:%02d.%03d",
                                          int(pts/(3600*90000)),
                                          int(pts/(60*90000))%60,
                                          int(pts/90000)%60,
                                          int(pts/90)%1000
                                         ));

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
    relpic=int(exp(alpha*v)-settings.jog_offset);
    if(relpic<0) relpic=0;
  }  
  else if (v<0) {
    relpic=-int(exp(-alpha*v)-settings.jog_offset);
    if(relpic>0) relpic=0;
  }  

  int newpic=jogmiddlepic+relpic;
  if (newpic<0)
    newpic=0;
  else if (newpic>=pictures)
    newpic=pictures-1;

  if (relpic>=settings.jog_threshold) {
    newpic=mpg->nextiframe(newpic);
    fine=false;
    } else if (relpic<=-settings.jog_threshold) {
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

  QPopupMenu popup(eventlist);
  popup.insertItem("Go to",1);
  popup.insertItem("Delete",2);
  popup.insertItem("Display difference from this picture",3);

  switch (popup.exec(point)) {
      case 1:
      fine=true;
      linslider->setValue(((EventListItem*)lbi)->getpicture());
      fine=false;
      break;

      case 2:
      delete lbi;
      break;

      case 3:
      if (imgp)
        delete imgp;
      imgp=new differenceimageprovider(*mpg,((EventListItem*)lbi)->getpicture(),new dvbcutbusy(this),false,viewscalefactor);
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
  int n=text.toInt(&okay,0);
  if (okay) {
    fine=true;
    linslider->setValue(n);
    fine=false;
    }
  goinput->clear();

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
  int id=0;
  for(std::vector<std::pair<std::string,std::string> >::iterator it=settings.recentfiles.begin();
      it!=settings.recentfiles.end();++it)
    recentfilespopup->insertItem(it->first,id++);
  }

void dvbcut::loadrecentfile(int id)
  {
  if (id<0 || id>=(signed)settings.recentfiles.size())
    return;
  open(settings.recentfiles[id].first, settings.recentfiles[id].second);
  }

// **************************************************************************
// ***  public functions

void dvbcut::open(std::string filename, std::string idxfilename)
  {
  if (filename.empty()) {
    QString fn=QFileDialog::getOpenFileName(
                 QString::null,
                 settings.loadfilter,
                 this,
                 "Open file...",
                 "Choose an MPEG file to open" );
    if (!fn)
      return;
    filename=(const char*)fn;
    }

  if (filename.empty())
    return;

  if (filename[0]!='/') {
    char resolved_path[PATH_MAX];
    char *rp=realpath(filename.c_str(),resolved_path);
    if (rp)
      filename=rp;
    }

  // a valid file name has been entered

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
  picnrlabel->clear();
  linslider->setValue(0);
  jogslider->setValue(0);

  viewNormalAction->setOn(true);
  viewUnscaledAction->setOn(false);
  viewDifferenceAction->setOn(false);

  fileOpenAction->setEnabled(false);
  fileSaveAction->setEnabled(false);
  fileSaveAsAction->setEnabled(false);
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
  picnrlabel->setEnabled(false);
  goinput->setEnabled(false);
  gobutton->setEnabled(false);
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
      if (line.startsWith(QString("<!DOCTYPE dvbcut"))) {
        infile.at(0);
        QString errormsg;
        if (domdoc.setContent(&infile,false,&errormsg)) {
          QDomElement docelem = domdoc.documentElement();
          if (docelem.tagName() != "dvbcut") {
            QMessageBox::critical(this,"Failed to read project file - dvbcut",QString(filename)+
                                  ":\nNot a valid dvbcut project file",
                                  QMessageBox::Abort,
                                  QMessageBox::NoButton);
            fileOpenAction->setEnabled(true);
            return;
            }

          QString mpgfilename=docelem.attribute("mpgfile");
          if (mpgfilename.isEmpty()) {
            QMessageBox::critical(this,"Failed to read project file - dvbcut",QString(filename)+
                                  ":\nNo mpeg filename "
                                  "given in project file",
                                  QMessageBox::Abort,
                                  QMessageBox::NoButton);
            fileOpenAction->setEnabled(true);
            return;
            }

          prjfilename=filename;
          filename=(const char *)mpgfilename;
          QString qidxfilename=docelem.attribute("idxfile");
          if (qidxfilename.isEmpty())
            idxfilename.clear();
          else
            idxfilename=(const char *)qidxfilename;
          } else {
          QMessageBox::critical(this,"Failed to read project file - dvbcut",QString(filename)+":\n"+errormsg, QMessageBox::Abort,
                                QMessageBox::NoButton);
          fileOpenAction->setEnabled(true);
          return;
          }
        }
      }
    }

  dvbcutbusy busy(this);
  busy.setbusy(true);

  std::string errormessage;
  mpg=mpgfile::open(filename,&errormessage);
  busy.setbusy(false);

  if (!mpg) {
    QMessageBox::critical(this,"Failed to open file - dvbcut",QString(filename)+":\n"+errormessage, QMessageBox::Abort,
                          QMessageBox::NoButton);
    fileOpenAction->setEnabled(true);
    return;
    }

  if (idxfilename.empty()) {
    QString s=QFileDialog::getSaveFileName(
                filename+".idx",
                settings.idxfilter,
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

    } else if (idxfilename[0]!='/') {
    char resolved_path[PATH_MAX];
    char *rp=realpath(idxfilename.c_str(),resolved_path);
    if (rp)
      idxfilename=rp;
    }

  pictures=-1;

  if (!idxfilename.empty()) {
    std::string errorstring;
    busy.setbusy(true);
    pictures=mpg->loadindex(idxfilename.c_str(),&errorstring);
    int serrno=errno;
    busy.setbusy(false);
    if (pictures==-1 && serrno!=ENOENT) {
      delete mpg;
      mpg=0;
      QMessageBox::critical(0,"Failed to open file - dvbcut",errorstring, QMessageBox::Abort,
                            QMessageBox::NoButton);
      fileOpenAction->setEnabled(true);
      return;
      }
    if (pictures==-2) {
      delete mpg;
      mpg=0;
      QMessageBox::critical(0,"Invalid index file - dvbcut",
                            errorstring,
                            QMessageBox::Abort,QMessageBox::NoButton);
      fileOpenAction->setEnabled(true);
      return;
      }
    if (pictures<=-3) {
      delete mpg;
      mpg=0;
      QMessageBox::critical(0,"Index file mismatch - dvbcut",
                            errorstring,
                            QMessageBox::Abort,QMessageBox::NoButton);
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

    if (psb.cancelled()) {
      delete mpg;
      mpg=0;
      fileOpenAction->setEnabled(true);
      return;
      }

    if (pictures<0) {
      delete mpg;
      mpg=0;
      QMessageBox::critical(0,"Error creating index - dvbcut",
                            QString("Cannot create index for\n")+filename+":\n"+errorstring,
                            QMessageBox::Abort,QMessageBox::NoButton);
      fileOpenAction->setEnabled(true);
      return;
      } else if (!errorstring.empty()) {
      QMessageBox::critical(0,"Error saving index file - dvbcut",
                            QString(errorstring),
                            QMessageBox::Abort,QMessageBox::NoButton);
      }
    }

  if (pictures<1) {
    delete mpg;
    mpg=0;
    QMessageBox::critical(0,"Invalid MPEG file - dvbcut",
                          QString("The chosen file\n")+filename+"\ndoes not contain any video",
                          QMessageBox::Abort,QMessageBox::NoButton);
    fileOpenAction->setEnabled(true);
    return;
    }

  mpgfilen=filename;
  idxfilen=idxfilename;
  prjfilen=prjfilename;
  expfilen.clear();
  if (prjfilen.empty())
    addtorecentfiles(mpgfilen,idxfilen);
  else
    addtorecentfiles(prjfilen);

  firstpts=(*mpg)[0].getpts();

  double fps=27.e6/double(mpgfile::frameratescr[(*mpg)[0].getframerate()]);
  linslider->setMaxValue(pictures-1);
  linslider->setLineStep(int(300*fps));
  linslider->setPageStep(int(900*fps));
  if (settings.lin_interval > 0)
    linslider->setTickInterval(int(settings.lin_interval*fps));

  //alpha=log(jog_maximum)/double(100000-jog_offset);
  // with alternative function
  alpha=log(settings.jog_maximum)/100000.;
  if (settings.jog_interval > 0 && settings.jog_interval <= 100000) 
    jogslider->setTickInterval(int(100000/settings.jog_interval));

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
        int picnum=e.attribute("picture","-1").toInt(&okay,0);
        if (okay && picnum>=0 && picnum<pictures) {
          new EventListItem(eventlist,imgp->getimage(picnum),evt,picnum,(*mpg)[picnum].getpicturetype(),(*mpg)[picnum].getpts()-firstpts);
          qApp->processEvents();
          }
        }
    }

  fileOpenAction->setEnabled(true);
  fileSaveAction->setEnabled(true);
  fileSaveAsAction->setEnabled(true);
  fileCloseAction->setEnabled(true);
  fileExportAction->setEnabled(true);
  playPlayAction->setEnabled(true);
  menubar->setItemEnabled(audiotrackmenuid,true);
  playStopAction->setEnabled(false);
  editStartAction->setEnabled(true);
  editStopAction->setEnabled(true);
  editChapterAction->setEnabled(true);
  editBookmarkAction->setEnabled(true);
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
  picnrlabel->setEnabled(true);
  goinput->setEnabled(true);
  gobutton->setEnabled(true);
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

void dvbcut::addtorecentfiles(const std::string &filename, const std::string &idxfilename)
  {

  for(std::vector<std::pair<std::string,std::string> >::iterator it=settings.recentfiles.begin();
      it!=settings.recentfiles.end();)
    if (it->first==filename)
      it=settings.recentfiles.erase(it);
    else
      ++it;

  settings.recentfiles.insert(settings.recentfiles.begin(),std::pair<std::string,std::string>(filename,idxfilename));

  while (settings.recentfiles.size()>settings.recentfiles_max)
    settings.recentfiles.pop_back();
  }

void dvbcut::setviewscalefactor(int factor)
  {
  if (factor!=1 && factor!=2 && factor!=4)
    factor=1;
  viewFullSizeAction->setOn(factor==1);
  viewHalfSizeAction->setOn(factor==2);
  viewQuarterSizeAction->setOn(factor==4);

  settings.viewscalefactor = factor;

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
	incr = settings.wheel_increments[WHEEL_INCR_ALT];
      else if (we->state() & ControlButton)
	incr = settings.wheel_increments[WHEEL_INCR_CTRL];
      else if (we->state() & ShiftButton)
	incr = settings.wheel_increments[WHEEL_INCR_SHIFT];
      else
	incr = settings.wheel_increments[WHEEL_INCR_NORMAL];
      if (incr != 0) {
	bool save = fine;
	// use fine positioning if incr is small
	fine = (incr < 0 ? -incr : incr) < settings.wheel_threshold;
	// Note: delta is a multiple of 120 (see Qt documentation)
	linslider->setValue(curpic - (delta * incr) / settings.wheel_delta);
	fine = save;
	}
      return true;
      }
    }
  // propagate to base class
  return dvbcutbase::eventFilter(watched, e);
  }
