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

#include <unistd.h>

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
#include <QProxyStyle>
#include <QTextCodec>
#include <QTextStream>
#include <QWheelEvent>

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

#define DVBCUT_LOADFILTER \
        QT_TRANSLATE_NOOP("dvbcut", "Recognized files (*.dvbcut *.m2t *.mpg *.rec* *.ts *.tts* *.trp *.vdr);;" \
        "dvbcut project files (*.dvbcut);;" \
        "MPEG files (*.m2t *.mpg *.rec* *.ts *.tts* *.trp *.vdr);;" \
        "All files (*)")
#define DVBCUT_IDXFILTER \
        QT_TRANSLATE_NOOP("dvbcut", "dvbcut index files (*.idx);;All files (*)")
#define DVBCUT_PRJFILTER \
        QT_TRANSLATE_NOOP("dvbcut", "dvbcut project files (*.dvbcut);;All files (*)")

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

class SliderStyleAbsolute : public QProxyStyle {
public:
    virtual int styleHint ( StyleHint hint, const QStyleOption * option = 0, const QWidget * widget = 0, QStyleHintReturn * returnData = 0 ) const{
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons){
            return Qt::LeftButton;
        }else{
            return QProxyStyle::styleHint(hint, option, widget, returnData);
        }
    }
};

static QString get_resource(QString file)
{
    QFileInfo appDir(qApp->applicationDirPath());

    // first search in the directory containing dvbcut
    QString resFile = appDir.absoluteFilePath() + file;

  #ifndef __WIN32__
    // Unix/Linux: search in the associated share subdirectory
    if (!QFile::exists(resFile)) {
      resFile = appDir.absolutePath() + "/share/dvbcut" + file;
    }
  #endif

    return resFile;
}

// **************************************************************************
// ***  Moodbar support

struct RGB {
    RGB(uint8_t r=0, uint8_t g=0, uint8_t b=0) : r(r), g(g), b(b) {}

    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct Moodbar {
    Moodbar(const char *filename);

    int columns() const { return data.size() / 3; }

    RGB get(size_t pos) const {
        return RGB(data[pos * 3 + 0], data[pos * 3 + 1], data[pos * 3 + 2]);
    }

    std::vector<uint8_t> data;
    bool valid { false };
};

Moodbar::Moodbar(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return;
    }

    size_t len = ftell(fp);

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return;
    }

    data.resize(len);

    if (fread(data.data(), 1, len, fp) != len) {
        valid = false;
    }

    fclose(fp);
    valid = true;
}

static std::unique_ptr<Moodbar>
g_moodbar;

struct CutPoint {
    CutPoint(bool want=false, int picture=0) : want(want), picture(picture) {}

    bool want;
    int picture;
};

void
dvbcut::setSliderPercentage(float percentage)
{
    ui->linslider->setValue(pictures * percentage);
}

void
dvbcut::updateMoodbar()
{
    if (!g_moodbar) {
        ui->moodbar->setText("");
        ui->moodbar->setVisible(false);
        return;
    }

    std::vector<CutPoint> cutpoints;

    // TODO: Does it depend on the first event list item if it's true or false?
    cutpoints.emplace_back(false, 0);

    for (int i=0; i<ui->eventlist->count(); ++i) {
        EventListItem *eli = dynamic_cast<EventListItem *>(ui->eventlist->item(i));

        if (eli->geteventtype() == EventListItem::start) {
            cutpoints.emplace_back(true, eli->getpicture());
        } else if (eli->geteventtype() == EventListItem::stop) {
            cutpoints.emplace_back(false, eli->getpicture());
        }
    }

    int width = ui->moodbar->size().width();
    int height = 20;
    std::vector<uint8_t> pixels(width * height * 4);

    for (int x=0; x<width; ++x) {
        int column = x * g_moodbar->columns() / width;
        RGB moodbar = g_moodbar->get(column);

        int picture = x * pictures / width;
        int nextpicture = (x + 1) * pictures / width;

        // TODO: Don't go through the cutpoints for every x coordinate change,
        // but just keep the current index and advance
        bool want = false;
        for (size_t p=0; p<cutpoints.size()-1; ++p) {
            auto &a = cutpoints[p];
            auto &b = cutpoints[p+1];
            if (a.picture <= picture && b.picture > picture) {
                want = a.want;
                break;
            }
        }
        RGB cutpoint = want ? RGB(0, 255, 0) : RGB(255, 0, 0);

        // TODO: Nice indicator
        if (curpic >= picture && curpic < nextpicture) {
            moodbar = cutpoint = RGB(255, 255, 255);
        }

        for (int y=0; y<height*2/3; ++y) {
            pixels[(y*width+x) * 4 + 0] = moodbar.b;
            pixels[(y*width+x) * 4 + 1] = moodbar.g;
            pixels[(y*width+x) * 4 + 2] = moodbar.r;
            pixels[(y*width+x) * 4 + 3] = 0xFF;
        }

        for (int y=height*2/3; y<height; ++y) {
            pixels[(y*width+x) * 4 + 0] = cutpoint.b;
            pixels[(y*width+x) * 4 + 1] = cutpoint.g;
            pixels[(y*width+x) * 4 + 2] = cutpoint.r;
            pixels[(y*width+x) * 4 + 3] = 0xFF;
        }
    }

    QImage image(pixels.data(), width, 20, width * 4, QImage::Format_ARGB32);

    QPixmap pixmap;
    pixmap.convertFromImage(image);

    ui->moodbar->setPixmap(pixmap);
    ui->moodbar->setVisible(true);
}

class UpdateMoodbarEventFilter : public QObject {
public:
    UpdateMoodbarEventFilter(dvbcut *pdvbcut) : pdvbcut(pdvbcut) {}

protected:
    bool eventFilter(QObject *obj, QEvent *event) {
        if (event->type() == QEvent::Resize) {
            pdvbcut->updateMoodbar();
        } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove) {
            auto mouse = static_cast<QMouseEvent *>(event);

            if (mouse->buttons() & Qt::LeftButton) {
                auto widget = qobject_cast<QWidget *>(obj);
                pdvbcut->setSliderPercentage(mouse->localPos().x() / widget->width());
            }
        }

        return QObject::eventFilter(obj, event);
    }

private:
    dvbcut *pdvbcut;
};


// **************************************************************************
// ***  dvbcut::dvbcut (private constructor)

dvbcut::dvbcut()
    :QMainWindow(),
    audiotrackpopup(0), recentfilespopup(0), editconvertpopup(0), audiotrackmenu(0),
    buf(8 << 20, 128 << 20),
    mpg(0), pictures(0),
    curpic(~0), showimage(true), fine(false),
    jogsliding(false), jogmiddlepic(0),
    mplayer_process(0), imgp(0), busy(0),
    viewscalefactor(1.0),
    nogui(false)
  {
    ui = new Ui::dvbcutbase();
    ui->setupUi(this);
#ifndef HAVE_LIB_AO
  ui->playAudio1Action->setEnabled(false);
  ui->playAudio2Action->setEnabled(false);
  ui->playToolbar->removeAction(ui->playAudio1Action);
  ui->playToolbar->removeAction(ui->playAudio2Action);
  ui->playMenu->removeAction(ui->playAudio1Action);
  ui->playMenu->removeAction(ui->playAudio2Action);
#endif // ! HAVE_LIB_AO

  audiotrackpopup=new QMenu(tr("Audio track"), this);
  ui->playMenu->addSeparator();
  audiotrackmenu=ui->playMenu->addMenu(audiotrackpopup);
  connect( audiotrackpopup, SIGNAL( triggered(QAction*) ), this, SLOT( audiotrackchosen(QAction*) ) );

  recentfilespopup=new QMenu(tr("Open &recent..."), this);
  ui->fileMenu->insertMenu(ui->fileSaveAction, recentfilespopup);
  connect( recentfilespopup, SIGNAL( triggered(QAction*) ), this, SLOT( loadrecentfile(QAction*) ) );
  connect( recentfilespopup, SIGNAL( aboutToShow() ), this, SLOT( abouttoshowrecentfiles() ) );

  editconvertpopup=new QMenu(tr("Convert bookmarks"), this);
  ui->editMenu->insertMenu(ui->editBookmarkAction, editconvertpopup);
  connect( editconvertpopup, SIGNAL( triggered(QAction*) ), this, SLOT( editConvert(QAction*) ) );
  connect( editconvertpopup, SIGNAL( aboutToShow() ), this, SLOT( abouttoshoweditconvert() ) );
  
  ui->fileOpenAction->setIcon(QIcon::fromTheme("document-open", this->style()->standardIcon(QStyle::SP_DialogOpenButton)));
  ui->fileSaveAction->setIcon(QIcon::fromTheme("document-save", this->style()->standardIcon(QStyle::SP_DialogSaveButton)));
  ui->fileSaveAsAction->setIcon(QIcon::fromTheme("document-save-as", this->style()->standardIcon(QStyle::SP_DriveFDIcon)));
  ui->snapshotSaveAction->setIcon(QIcon::fromTheme("camera-photo", this->style()->standardIcon(QStyle::SP_DesktopIcon)));
  ui->playPlayAction->setIcon(QIcon::fromTheme("media-playback-start", this->style()->standardIcon(QStyle::SP_MediaPlay)));
  ui->playStopAction->setIcon(QIcon::fromTheme("media-playback-pause", this->style()->standardIcon(QStyle::SP_MediaPause)));
  ui->playAudio1Action->setIcon(QIcon::fromTheme("media-seek-backward", this->style()->standardIcon(QStyle::SP_MediaSeekBackward)));
  ui->playAudio2Action->setIcon(QIcon::fromTheme("media-seek-forward", this->style()->standardIcon(QStyle::SP_MediaSeekForward)));

  setviewscalefactor(settings().viewscalefactor);

  // install event handler
  ui->linslider->installEventFilter(this);
  ui->linslider->setStyle(new SliderStyleAbsolute);

  // set up moodbar resize handler
  ui->moodbar->installEventFilter(new UpdateMoodbarEventFilter(this));
  updateMoodbar();

  // set caption
  setWindowTitle(QString(VERSION_STRING));
  setWindowIcon(QIcon(get_resource("/icons/dvbcut.svg")));
}

// **************************************************************************
// ***  dvbcut::~dvbcut (destructor)

dvbcut::~dvbcut()
{
  if (mplayer_process) {
    mplayer_process->terminate();
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
  if(ui)
    delete ui;
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
    while (QFileInfo(QString::fromStdString(prjfilen)).exists())
      prjfilen = prefix + "_" + QString::number(++nr).toStdString() + ".dvbcut";
  }

  QString s=QFileDialog::getSaveFileName(
    this,
    tr("Choose the name of the project file"),
    QString::fromStdString(prjfilen),
    tr(DVBCUT_PRJFILTER) );

  if (s.isNull())
    return;

  if (QFileInfo(s).exists() && question(
      tr("File exists - dvbcut"),
      tr("%1\nalready exists. Overwrite?").arg(s)) !=
      QMessageBox::Yes)
    return;

  prjfilen = s.toStdString();
  if (!prjfilen.empty())
    fileSave();
}

void dvbcut::fileSave()
{
  if (prjfilen.empty()) {
    fileSaveAs();
    return;
  }

  std::list<std::string> dummy_list;
  dummy_list.push_back(prjfilen);
  addtorecentfiles(dummy_list);

  QFile outfile(QString::fromStdString(prjfilen));
  if (!outfile.open(QIODevice::WriteOnly)) {
    critical(tr("Failed to write project file - dvbcut"),
      //: Placeholder will be replaced with filename
      tr("%1:\nCould not open file").arg(QString::fromStdString(prjfilen)));
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
    elem.setAttribute("path", QString::fromStdString(*it));
    root.appendChild(elem);
    ++it;
  }

  if (!idxfilen.empty()) {
    QDomElement elem = doc.createElement("idxfile");
    elem.setAttribute("path", QString::fromStdString(idxfilen));
    root.appendChild(elem);
  }

  int index;
  for (index = 0; index < ui->eventlist->count(); index++)
  {
    EventListItem *eli = dynamic_cast<EventListItem *>(ui->eventlist->item(index));
    if (eli) {
      QString elemname;
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
  }

  QTextStream stream(&outfile);
  stream.setCodec(QTextCodec::codecForName("ISO-8859-1"));
  stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  stream << doc.toString().toStdString().c_str();
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
  int index;
  for (index = 0; index < ui->eventlist->count(); index++)
  {
    EventListItem *eli = dynamic_cast<EventListItem *>(ui->eventlist->item(index));
    if (eli) {
      if (eli->geteventtype()==EventListItem::chapter) {
         piclist.push_back(eli->getpicture());
         found++;
      }
    }
  }

  if (found) {
    snapshotSave(piclist, settings().snapshot_range, settings().snapshot_samples);
  } else
    statusBar()->showMessage(QString("*** No chapters to save! ***"));
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
        prefix = QString::fromStdString(prjfilen);
      else if (!mpgfilen.empty() && !mpgfilen.front().empty())
        prefix = QString::fromStdString(mpgfilen.front());
    } else
      prefix = settings().snapshot_prefix;

    if (!prefix.isEmpty()) {
      int lastdot = prefix.lastIndexOf('.');
      int lastslash = prefix.lastIndexOf('/');
      if (lastdot >= 0 && lastdot > lastslash)
        prefix = prefix.left(lastdot);
      int nr = first;
      picfilen = prefix + delim + QString::number(nr).rightJustified( width, '0' ) + "." + ext;
      while (QFileInfo(picfilen).exists())
        picfilen = prefix + delim + QString::number(++nr).rightJustified( width, '0' )+ "." + ext;
    }
  }  

  QString s = QFileDialog::getSaveFileName(
    this,
    tr("Choose the name of the picture file"),
    picfilen,
    //: Placeholder will be replaced with file extension
    tr("Images (*.%1)").arg(ext) );

  if (s.isEmpty())
    return;

  if (QFileInfo(s).exists() && question(
    tr("File exists - dvbcut"),
    //: Placeholder will be replaced with filename
    tr("%1\nalready exists. Overwrite?").arg(s)) !=
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
    if(p.save(s,type.toLatin1(),quality))
      statusBar()->showMessage("Saved snapshot: " + s);
    else
      statusBar()->showMessage("*** Unable to save snapshot: " + s + "! ***");
 
    // try to "increment" the choosen filename for next snapshot (or use old name as default)
    // No usage of "delim", so it's possible to choose any prefix in front of the number field!
    i = s.lastIndexOf(QRegExp("\\d{"+QString::number(width)+","+QString::number(width)+"}\\."+ext+"$"));
    if (i>0) {
      nr = s.mid(i,width).toInt(&ok,10);
      if (ok)
        picfilen = s.left(i) + QString::number(++nr).rightJustified(width, '0')+ "." + ext;
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
  if (0) fprintf(stderr,"choosing sample / frame: %4d / %7d\n!", bestnr, bestpic);

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
      while (QFileInfo(QString::fromStdString(expfilen)).exists())
        expfilen = newexpfilen + "_" + QString::number(++nr).toStdString() + ".mpg";
    }
  }

  std::unique_ptr<exportdialog> expd(new exportdialog(QString::fromStdString(expfilen),this));
  expd->ui->muxercombo->addItem(tr("MPEG program stream/DVD (DVBCUT multiplexer)"));
  expd->ui->muxercombo->addItem(tr("MPEG program stream (DVBCUT multiplexer)"));
  expd->ui->muxercombo->addItem(tr("MPEG program stream/DVD (libavformat)"));
  expd->ui->muxercombo->addItem(tr("MPEG transport stream (libavformat)"));
#ifndef __WIN32__
  // add possible user configured pipe commands 
  int pipe_items_start=expd->ui->muxercombo->count();
  for (unsigned int i = 0; i < settings().pipe_command.size(); ++i)
    expd->ui->muxercombo->addItem(settings().pipe_label[i]);
#endif

  if (settings().export_format < 0
      || settings().export_format >= expd->ui->muxercombo->count())
    settings().export_format = 0;
  expd->ui->muxercombo->setCurrentIndex(settings().export_format);

  for(int a=0;a<mpg->getaudiostreams();++a) {
    expd->ui->audiolist->addItem(mpg->getstreaminfo(audiostream(a)).c_str());
  }
  expd->ui->audiolist->selectAll();

  int expfmt = 0;
  if (!nogui) {
    expd->show();
    if (!expd->exec())
      return;

    settings().export_format = expd->ui->muxercombo->currentIndex();
    expfmt = expd->ui->muxercombo->currentIndex();

    expfilen = expd->ui->filenameline->text().toStdString();
    if (expfilen.empty())
      return;
    expd->hide();
  } else if (exportformat > 0 && exportformat < expd->ui->muxercombo->count()) 
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
    if (settings().pipe_command[ip].indexOf('|') == -1)
      expcmd = "|" + settings().pipe_command[ip].toStdString();
    else 
      expcmd = settings().pipe_command[ip].toStdString();
       
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
      critical(tr("Command not found - dvbcut"),
        //: Placeholder will be replaced with pipe command
        tr("Problems with piped output to:\n%1").arg(QString::fromStdString(expcmd.substr(pos,end-pos))));
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
  if (QFileInfo(QString::fromStdString(expfilen)).exists() && question(
      tr("File exists - dvbcut"),
      tr("%1\nalready exists. Overwrite?").arg(QString::fromStdString(expfilen))) !=
      QMessageBox::Yes)
    return;

  progresswindow *prgwin = 0;
  logoutput *log;
  if (nogui) {
    log = new logoutput;
  }
  else {
    prgwin = new progresswindow(this);
    prgwin->setWindowTitle(QString("export - " + QString::fromStdString(expfilen)));
    log = prgwin;
  }

  //   lavfmuxer mux(fmt,*mpg,outfilename);

  std::unique_ptr<muxer> mux;
  uint32_t audiostreammask(0);

  for(int a=0;a<mpg->getaudiostreams();++a)
    if (expd->ui->audiolist->item(a)->isSelected())
      audiostreammask|=1u<<a;

  std::string out_file = (child_pid < 0) ? expfilen :
    std::string("pipe:") + QString::number(pipe_fds[1]).toStdString();

  switch(expfmt) {
    case 1:
      mux = std::unique_ptr<muxer>(new mpegmuxer(audiostreammask,*mpg,out_file.c_str(),false,0));
      break;
    case 2:
      mux = std::unique_ptr<muxer>(new lavfmuxer("dvd",audiostreammask,*mpg,out_file.c_str()));
      break;
    case 3:
      mux = std::unique_ptr<muxer>(new lavfmuxer("mpegts",audiostreammask,*mpg,out_file.c_str()));
      break;
    case 0:
    default:
      mux = std::unique_ptr<muxer>(new mpegmuxer(audiostreammask,*mpg,out_file.c_str()));
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
    log->printerror(tr("Unable to set up muxer!"));
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
      
      //: Example: 1. Exporting 600 pictures: 00:05:45.240/00 .. 00:06:09.240/00
      log->printheading(tr("%1. Exporting %n pictures: %2 .. %3", "", stoppic-startpic)
                        .arg(num+1).arg(ptsstring(startpts).c_str(),ptsstring(stoppts).c_str()));
      mpg->savempg(*mux,startpic,stoppic,savedpic,quick_picture_lookup.back().outpicture,log);

      savedpic=quick_picture_lookup[num].outpicture;
      savedtime=quick_picture_lookup[num].outpts;
  }

  mux.reset();

  //: Example: Saved 3627 pictures (00:02:25.80)
  log->printheading(tr("Saved %n pictures (%1:%2:%3.%4)", "", savedpic)
                    .arg(int(savedtime/(3600*90000)), 2, 10, QChar('0'))
                    .arg(int(savedtime/(60*90000))%60, 2, 10, QChar('0'))
                    .arg(int(savedtime/90000)%60, 2, 10, QChar('0'))
                    .arg(int(savedtime/90)%1000, 2, 10, QChar('0')));

#ifndef __WIN32__
  if (child_pid > 0) {
    ::close(pipe_fds[1]);
    int wstatus;
    while (waitpid(child_pid, &wstatus, 0)==-1 && errno==EINTR);
  }

  // do some post processing if requested
  if (ip>=0 && !settings().pipe_post[ip].isEmpty()) {
    expcmd = settings().pipe_post[ip].toStdString();
       
    if ((pos=expcmd.find("%OUTPUT%"))!=std::string::npos)
      expcmd.replace(pos,8,expfilen);  
    if ((pos=expcmd.find("%CHAPTERS%"))!=std::string::npos)
      expcmd.replace(pos,10,chapterstring);  

    pos=expcmd.find(' ');  
    std::string which="which "+expcmd.substr(0,pos)+" >/dev/null";

    log->print("");
    log->printheading(tr("Performing post processing"));
    int irc = system(which.c_str());

    if(irc!=0) { 
      critical(tr("Command not found - dvbcut"),
        //: Placeholder will be replaced with pipe command
        tr("Problems with post processing command:\n%1").arg(QString::fromStdString(expcmd.substr(0,pos))));
      log->print(tr("Command not found!"));
    } else {      
      int irc = system(expcmd.c_str());
      if(irc!=0) { 
        critical(tr("Post processing error - dvbcut"),
                //: %1 will be replaced with exit code, %2 with pipe command
                tr("Post processing command:\n%2\n returned non-zero exit code: %1").arg(irc).arg(QString::fromStdString(expcmd)));
        log->print(tr("Command reported some problems... please check!"));
      } 
      //else      
      //  log->print("Everything seems to be OK...");
    }
  }   
#endif

  // print plain list of chapter marks
  log->print("");
  log->printheading(tr("Chapter list"));
  log->print(chaptercolumn.c_str());

  // simple dvdauthor xml file with chapter marks
  std::string filename,destname;
  if (expfilen.rfind("/")<expfilen.length()) 
    filename=expfilen.substr(expfilen.rfind("/")+1);
  else 
    filename=expfilen;
  destname=filename.substr(0,filename.rfind("."));
  log->print("");
  log->printheading(tr("Simple XML-file for dvdauthor with chapter marks"));
  log->print("<dvdauthor dest=\"" + QString::fromStdString(destname) + "\">");
  log->print("  <vmgm />");
  log->print("  <titleset>");
  log->print("    <titles>");
  log->print("      <pgc>");
  log->print("        <vob file=\"" + QString::fromStdString(filename) + "\" chapters=\"" + QString::fromStdString(chapterstring) + "\" />");
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

void dvbcut::viewMoodbar()
{
  auto fileName = QFileDialog::getOpenFileName(this,
      tr("Open moodbar"), QString(), tr("Moodbar Files (*.mood)"));

  if (!fileName.isEmpty()) {
      g_moodbar = std::make_unique<Moodbar>(qPrintable(fileName));
      if (!g_moodbar->valid) {
          g_moodbar.reset();
      }
  }

  updateMoodbar();
}

void dvbcut::addEventListItem(int pic, EventListItem::eventtype type)
{
  //check if requested EventListItem is already in list to avoid doubles!
  int index;
  for (index = 0; index < ui->eventlist->count(); index++)
  {
    EventListItem *eli = dynamic_cast<EventListItem *>(ui->eventlist->item(index));
    if (eli) {
      if (pic==eli->getpicture() && type==eli->geteventtype()) 
        return;
    }
  }

  QPixmap p;
  if (imgp && imgp->rtti() == IMAGEPROVIDER_STANDARD)
    p = QPixmap::fromImage(imgp->getimage(pic));
  else
    p = QPixmap::fromImage(imageprovider(*mpg, new dvbcutbusy(this), false, 4).getimage(pic));
  
  new EventListItem(ui->eventlist, p, type, pic, (*mpg)[pic].getpicturetype(),
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
            p2=p2.scaled(p1.size());

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
            statusBar()->showMessage(QString().sprintf("%d. Scene change @ %d, DIST=%f\n",chapters+1,inpic,dist));   
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
    statusBar()->showMessage(QString("*** No aspect ratio changes detected! ***"));   
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
    statusBar()->showMessage(QString("*** No valid bookmarks available/found! ***"));
}

void dvbcut::abouttoshoweditconvert()
{
  editconvertpopup->clear();
  editconvertpopup->addAction(tr("START / STOP"))->setData(act_start_stop);
  editconvertpopup->addAction(tr("STOP / START"))->setData(act_stop_start);
  editconvertpopup->addAction(tr("4 : 3"))->setData(act_4_3);
  editconvertpopup->addAction(tr("16 : 9"))->setData(act_16_9);
}

void dvbcut::editConvert(QAction *a)
{
  int option = a->data().toInt();
  if (option < act_start_stop || option > act_16_9)
      return;
  editConvert((editconvertpop_actions)option);
}

void dvbcut::editConvert(editconvertpop_actions option)
{
  // convert Bookmarks to START/STOP markers
  int found=0;
  std::vector<int> cutlist;
  int index;
  for (index = 0; index < ui->eventlist->count(); index++)
  {
    EventListItem *eli = dynamic_cast<EventListItem *>(ui->eventlist->item(index));
    if (eli) {
      if (eli->geteventtype()==EventListItem::bookmark) {
         cutlist.push_back(eli->getpicture());
         delete eli;
         found++;
      } 
    } 
  }
  if (found) {
    addStartStopItems(cutlist, option);

    if (found%2) 
      statusBar()->showMessage(QString("*** No matching stop marker!!! ***"));
  }  
  else
    statusBar()->showMessage(QString("*** No bookmarks to convert! ***"));  
}

void dvbcut::addStartStopItems(std::vector<int> cutlist, int option)
{
  // take list of frame numbers and set alternating START/STOP markers
  bool alternate=true;
  EventListItem::eventtype type=EventListItem::start;
  if (option == act_stop_start)
    type=EventListItem::stop;
  else if (option == act_4_3 || option == act_16_9)
    alternate=false;   
 
  // make sure list is sorted... 
  std::sort(cutlist.begin(),cutlist.end());

  // ...AND there are no old START/STOP pairs!!!
  int index;
  for (index = 0; index < ui->eventlist->count(); index++)
  {
    EventListItem *eli = dynamic_cast<EventListItem *>(ui->eventlist->item(index));
    if (eli) {
      if (eli->geteventtype()==EventListItem::start || eli->geteventtype()==EventListItem::stop) 
         delete eli;
    } 
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
  ui->viewNormalAction->setChecked(false);
  ui->viewUnscaledAction->setChecked(false);
  ui->viewDifferenceAction->setChecked(true);

  if (imgp)
    delete imgp;
  imgp=new differenceimageprovider(*mpg,curpic, new dvbcutbusy(this), false, viewscalefactor);
  updateimagedisplay();
}

inline int square(int x)
{
    return x * x;
}

unsigned long calc_distance(QImage baseimg, QImage im)
{
    if (im.size() != baseimg.size())
        im = im.scaled(baseimg.size());

    unsigned long distance = 0;
    if (im.depth() == 32 && baseimg.depth() == 32) {
        for (int y = 0; y < baseimg.height(); ++y) {
            QRgb *imd = (QRgb*) im.scanLine(y);
            QRgb *bimd = (QRgb*) baseimg.scanLine(y);

            for (int x = im.width(); x > 0; --x) {
                int dist = square(qRed(*imd) - qRed(*bimd)) + square(qGreen(*imd) - qGreen(*bimd)) + square(qBlue(*imd) - qBlue(*bimd));
                if (dist > 1000)
                    dist = 1000;

                distance += dist;
                ++imd;
                ++bimd;
            }
        }
    }

    return distance;
}

int dvbcut::getPreviousStopEvent(int picture)
{
    int previous = 0;

    for (int i = 0; i < ui->eventlist->count(); i++) {
        EventListItem *eli = dynamic_cast<EventListItem *>(ui->eventlist->item(i));
        if (eli->geteventtype() == EventListItem::stop && eli->getpicture() < picture)
            previous = eli->getpicture();
    }

    if (previous)
        return previous;
    else
        critical(tr("Searching ..."), tr("Please set a stop marker for the picture to be searched.\nThen go where the picture is expected to appear the next time e.g. the end of the commercial break."));

    return picture;
}

void dvbcut::searchDuplicate()
{
    ui->searchDuplicateAction->setEnabled(false);

    progressstatusbar psb(statusBar());
    psb.print(tr("Searching ..."));
    psb.setprogress(0);
    imageprovider imgp(*mpg);

    int search_start_pic = curpic;
    QImage last_stop_img = imgp.getimage(getPreviousStopEvent(search_start_pic));

    unsigned long min = calc_distance(last_stop_img, imgp.getimage(search_start_pic, true));
    static const long n = settings().search_dups_range;

    for (long i = 0; i < n; ++i) {
        unsigned long d = calc_distance(last_stop_img, imgp.getimage(search_start_pic + i, true));
        if (d < min) {
            min = d;
            gotoFrame(search_start_pic + i);
        }

        psb.setprogress(i * 1000 / n);
        if (psb.cancelled()) {
            // FIXME
            break;
        }
    }

    ui->searchDuplicateAction->setEnabled(true);
}


void dvbcut::viewUnscaled()
{
  ui->viewNormalAction->setChecked(false);
  ui->viewUnscaledAction->setChecked(true);
  ui->viewDifferenceAction->setChecked(false);

  if (!imgp || imgp->rtti()!=IMAGEPROVIDER_UNSCALED) {
    if (imgp)
      delete imgp;
    imgp=new imageprovider(*mpg,new dvbcutbusy(this),true,viewscalefactor);
    updateimagedisplay();
  }
}


void dvbcut::viewNormal()
{
  ui->viewNormalAction->setChecked(true);
  ui->viewUnscaledAction->setChecked(false);
  ui->viewDifferenceAction->setChecked(false);

  if (!imgp || imgp->rtti()!=IMAGEPROVIDER_STANDARD) {
    if (imgp)
      delete imgp;
    imgp=new imageprovider(*mpg,new dvbcutbusy(this),false,viewscalefactor);
    updateimagedisplay();
  }
}

void dvbcut::zoomIn()
{
  setviewscalefactor(viewscalefactor / 1.2);
}

void dvbcut::zoomOut()
{
  setviewscalefactor(viewscalefactor * 1.2);
}

void dvbcut::viewFullSize()
{
  setviewscalefactor(1.0);
}

void dvbcut::viewHalfSize()
{
  setviewscalefactor(2.0);
}

void dvbcut::viewQuarterSize()
{
  setviewscalefactor(4.0);
}

void dvbcut::viewCustomSize()
{
  setviewscalefactor(settings().viewscalefactor_custom);
}

void dvbcut::playPlay()
{
  if (mplayer_process)
    return;


  ui->eventlist->setEnabled(false);
  ui->linslider->setEnabled(false);
  ui->jogslider->setEnabled(false);
  ui->gobutton->setEnabled(false);
  ui->goinput->setEnabled(false);
  ui->gobutton2->setEnabled(false);
  ui->goinput2->setEnabled(false);

#ifdef HAVE_LIB_AO

  ui->playAudio1Action->setEnabled(false);
  ui->playAudio2Action->setEnabled(false);
#endif // HAVE_LIB_AO

  ui->playPlayAction->setEnabled(false);
  ui->playStopAction->setEnabled(true);
  audiotrackmenu->setEnabled(false);

  ui->fileOpenAction->setEnabled(false);
  ui->fileSaveAction->setEnabled(false);
  ui->fileSaveAsAction->setEnabled(false);
  ui->snapshotSaveAction->setEnabled(false);
  ui->chapterSnapshotsSaveAction->setEnabled(false);
  ui->fileExportAction->setEnabled(false);

  showimage=false;
  ui->imagedisplay->setPixmap(QPixmap());
  ui->imagedisplay->grabKeyboard();

  fine=true;
  ui->linslider->setValue(mpg->lastiframe(curpic));
  dvbcut_off_t offset=(*mpg)[curpic].getpos().packetposition();
  mplayer_curpts=(*mpg)[curpic].getpts();

  QString process("mplayer");
  QStringList arguments;
  
  arguments << "-noconsolecontrols";
  dvbcut_off_t partoffset;
  int partindex = buf.getfilenum(offset, partoffset);
  if (partindex == -1)
    return;	// what else can we do?

#ifdef __WIN32__
  arguments << "-vo" << "directx:noaccel";
#endif
  arguments << "-wid" << QString().sprintf("0x%x",int(ui->imagedisplay->winId()));
  arguments << "-sb" << QString::number(offset - partoffset);
  arguments << "-geometry" << QString().sprintf("%dx%d+0+0",int(ui->imagedisplay->width()),int(ui->imagedisplay->height()));

  if (currentaudiotrack>=0 && currentaudiotrack<mpg->getaudiostreams()) {
    arguments << "-aid" << QString().sprintf("0x%x", int(mpg->mplayeraudioid(currentaudiotrack)));
  }
    
  // for now, pass all filenames from the current one up to the last one
  std::list<std::string>::const_iterator it = mpgfilen.begin();
  for (int i = 0; it != mpgfilen.end(); ++i, ++it)
    if (i >= partindex)
      arguments << QString::fromStdString(*it); 
  
  mplayer_process=new QProcess(this);
  mplayer_process->setReadChannel(QProcess::StandardOutput);
  mplayer_process->setProcessChannelMode(QProcess::MergedChannels);
  connect(mplayer_process, SIGNAL(finished(int)), this, SLOT(mplayer_exited()));
  connect(mplayer_process, SIGNAL(readyReadStandardOutput()), this, SLOT(mplayer_readstdout()));

  mplayer_success=false;
  mplayer_process->start(process, arguments);
}

void dvbcut::playStop()
{
  if (mplayer_process)
    mplayer_process->terminate();
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
  ui->linslider->setTracking(false);

  if (!mpg || newpic==curpic)
  {
    ui->linslider->setTracking(true);
    return;
  }
  if (!fine)
    newpic=mpg->lastiframe(newpic);
  if (!jogsliding)
    jogmiddlepic=newpic;
  if (newpic==curpic)
  {
    ui->linslider->setTracking(true);
    return;
  }

  curpic=newpic;
  updateMoodbar();

  if (!jogsliding)
    jogmiddlepic=newpic;
  
  update_time_display();
  updateimagedisplay();

  ui->linslider->setTracking(true);
}
  
void dvbcut::jogsliderreleased()
{
  jogsliding=false;
  jogmiddlepic=curpic;
  ui->jogslider->setValue(0);
}

void dvbcut::jogslidervalue(int v)
{
  ui->jogslider->setTracking(false);

  if (!mpg || (v==0 && curpic==jogmiddlepic))
  {
    ui->jogslider->setTracking(true);
    return;
  }
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
    ui->linslider->setValue(newpic);

  fine=false;

  ui->jogslider->setTracking(true);
}

void dvbcut::doubleclickedeventlist(QListWidgetItem *lbi)
{
  EventListItem *item = dynamic_cast<EventListItem *>(lbi);
  if (!item)
    return;

  fine=true;
  ui->linslider->setValue(item->getpicture());
  fine=false;
}

void dvbcut::eventlistcontextmenu(const QPoint &point)
{
  EventListItem *eli = dynamic_cast<EventListItem *>(ui->eventlist->itemAt(point));
  if (!eli)
    return;

  enum popup_actions {
      act_go_to = 1,
      act_delete,
      act_delete_others,
      act_delete_all,
      act_delete_all_startstops,
      act_delete_all_chapters,
      act_delete_all_bookmarks,
      act_convert_to_start,
      act_convert_to_stop,
      act_convert_to_chapter,
      act_convert_to_bookmark,
      act_display_difference
  };

  QMenu popup(ui->eventlist);
  popup.addAction(tr("Go to"))->setData(act_go_to);
  popup.addSeparator();
  popup.addAction(tr("Delete"))->setData(act_delete);
  popup.addAction(tr("Delete others"))->setData(act_delete_others);
  popup.addAction(tr("Delete all"))->setData(act_delete_all);
  popup.addAction(tr("Delete all start/stops"))->setData(act_delete_all_startstops);
  popup.addAction(tr("Delete all chapters"))->setData(act_delete_all_chapters);
  popup.addAction(tr("Delete all bookmarks"))->setData(act_delete_all_bookmarks);
  popup.addSeparator();
  popup.addAction(tr("Convert to start marker"))->setData(act_convert_to_start);
  popup.addAction(tr("Convert to stop marker"))->setData(act_convert_to_stop);
  popup.addAction(tr("Convert to chapter marker"))->setData(act_convert_to_chapter);
  popup.addAction(tr("Convert to bookmark"))->setData(act_convert_to_bookmark);
  popup.addSeparator();
  popup.addAction(tr("Display difference from this picture"))->setData(act_display_difference);

  QListWidget *lb = ui->eventlist;
  EventListItem::eventtype cmptype=EventListItem::none, cmptype2=EventListItem::none;
  int index;

  QAction *a = popup.exec(ui->eventlist->mapToGlobal(point));
  if (!a)
      return;
  switch (a->data().toInt()) {
    case act_go_to:
      fine=true;
      ui->linslider->setValue(eli->getpicture());
      fine=false;
      break;

    case act_delete:
      {
      EventListItem::eventtype type=eli->geteventtype();
      delete eli;
      if (type!=EventListItem::bookmark) update_quick_picture_lookup_table();
      }
      break;

    case act_delete_others:
      for (index = lb->count(); index > 0; index--) {
          EventListItem *t = dynamic_cast<EventListItem *>(lb->item(index-1));
          if (t != eli)
              delete t;
      }
      update_quick_picture_lookup_table();
      break;

    case act_delete_all:
      lb->clear();
      update_quick_picture_lookup_table();
      break;

    case act_delete_all_startstops:
      cmptype=EventListItem::start;
      cmptype2=EventListItem::stop;
    case act_delete_all_chapters:
      if (cmptype==EventListItem::none) cmptype=EventListItem::chapter;
    case act_delete_all_bookmarks:
      if (cmptype==EventListItem::none) cmptype=EventListItem::bookmark;
      for (index = lb->count(); index > 0; index--) {
          EventListItem *t = dynamic_cast<EventListItem *>(lb->item(index-1));
          if (t->geteventtype() == cmptype || t->geteventtype() == cmptype2)
              delete t;
      }
      if (cmptype!=EventListItem::bookmark) update_quick_picture_lookup_table();
      break;

    case act_convert_to_start:
      eli->seteventtype(EventListItem::start);
      update_quick_picture_lookup_table();
      break;

    case act_convert_to_stop:
      eli->seteventtype(EventListItem::stop);
      update_quick_picture_lookup_table();
      break; 

    case act_convert_to_chapter:
      eli->seteventtype(EventListItem::chapter);
      update_quick_picture_lookup_table();
      break;

    case act_convert_to_bookmark:
      eli->seteventtype(EventListItem::bookmark);
      update_quick_picture_lookup_table();
      break; 

    case act_display_difference:
      if (imgp)
        delete imgp;
      imgp=new differenceimageprovider(*mpg,eli->getpicture(),new dvbcutbusy(this),false,viewscalefactor);
      updateimagedisplay();
      ui->viewNormalAction->setChecked(false);
      ui->viewUnscaledAction->setChecked(false);
      ui->viewDifferenceAction->setChecked(true);
      break;
  }

}

void dvbcut::clickedgo()
{
  QString text=ui->goinput->text();
  text = text.trimmed();
  bool okay=false;
  int inpic;
  if (text.contains(':') || text.contains('.')) {
    okay=true;
    inpic=string2pts(text.toStdString())/getTimePerFrame();
  }
  else
    inpic=text.toInt(&okay,0);
  if (okay) {
    fine=true;
    ui->linslider->setValue(inpic);
    fine=false;
  }
  //goinput->clear();
}

void dvbcut::clickedgo2()
{
  QString text=ui->goinput2->text();
  text = text.trimmed();
  bool okay=false;
  int inpic, outpic;
  if (text.contains(':') || text.contains('.')) {
    okay=true;
    outpic=string2pts(text.toStdString())/getTimePerFrame();
  }
  else
    outpic=text.toInt(&okay,0);
  if (okay && !quick_picture_lookup.empty()) {
    // find the entry in the quick_picture_lookup table that corresponds to given output picture
    quick_picture_lookup_t::iterator it=
      std::upper_bound(quick_picture_lookup.begin(),quick_picture_lookup.end(),outpic,quick_picture_lookup_s::cmp_outpicture());
    inpic=outpic-it->outpicture+it->stoppicture;   
    fine=true;
    ui->linslider->setValue(inpic);
    fine=false;
  }
  //goinput2->clear();
}

void dvbcut::mplayer_exited()
{
  if (mplayer_process) {
    if (!mplayer_success)// && !mplayer_process->normalExit())
    {
      mplayererrorbase *meb=new mplayererrorbase(this);
      meb->setText(mplayer_out);
    }
    mplayer_process->deleteLater();
    mplayer_process=0;
  }
  
  ui->eventlist->setEnabled(true);
  ui->linslider->setEnabled(true);
  ui->jogslider->setEnabled(true);
  ui->gobutton->setEnabled(true);
  ui->goinput->setEnabled(true);
  ui->eventlist->setEnabled(true);
  ui->linslider->setEnabled(true);
  ui->jogslider->setEnabled(true);
  ui->gobutton->setEnabled(true);
  ui->goinput->setEnabled(true);
  ui->gobutton2->setEnabled(true);
  ui->goinput2->setEnabled(true);

#ifdef HAVE_LIB_AO

  ui->playAudio1Action->setEnabled(true);
  ui->playAudio2Action->setEnabled(true);
#endif // HAVE_LIB_AO

  ui->playPlayAction->setEnabled(true);
  ui->playStopAction->setEnabled(false);
  audiotrackmenu->setEnabled(true);

  ui->fileOpenAction->setEnabled(true);
  ui->fileSaveAction->setEnabled(true);
  ui->fileSaveAsAction->setEnabled(true);
  ui->snapshotSaveAction->setEnabled(true);
  ui->chapterSnapshotsSaveAction->setEnabled(true);
  ui->fileExportAction->setEnabled(true);

  ui->imagedisplay->releaseKeyboard();

  int cp=curpic;
  jogmiddlepic=curpic;
  curpic=-1;
  showimage=true;
  fine=true;
  linslidervalue(cp);
  ui->linslider->setValue(cp);
  fine=false;
}

void dvbcut::mplayer_readstdout()
{
  if (!mplayer_process)
    return;

  QString line(mplayer_process->readAllStandardOutput());
  if (!line.length())
    return;

  if (!mplayer_success)
    mplayer_out += line;

  int pos=line.indexOf("V:");
  if (pos<0)
    return;

  line.remove(0,pos+2);
  line=line.trimmed();
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

  ui->linslider->setValue(cp);
}

void dvbcut::updateimagedisplay()
{
  if (showimage) {
    if (!imgp)
      imgp=new imageprovider(*mpg,new dvbcutbusy(this),false,viewscalefactor);
    QImage px=imgp->getimage(curpic,fine);
    ui->imagedisplay->setMinimumSize(px.size());
    ui->imagedisplay->setPixmap(QPixmap::fromImage(px));
    ui->imagedisplay->update();
    qApp->processEvents();
  }
}

void dvbcut::audiotrackchosen(QAction* a)
{
  int id = a->data().toInt();
  if (id<0 || id>mpg->getaudiostreams())
    return;
  currentaudiotrack=id;
}

void dvbcut::abouttoshowrecentfiles()
{
  recentfilespopup->clear();
  QString menuitem;
  for(unsigned int id=0; id<settings().recentfiles.size(); id++) {
    menuitem=QString(QString::fromStdString(settings().recentfiles[id].first.front()));
    if(settings().recentfiles[id].first.size()>1)
      menuitem += " ... (+" + QString::number(settings().recentfiles[id].first.size()-1) + ")";
    recentfilespopup->addAction(menuitem)->setData(id);
  }   
}

void dvbcut::loadrecentfile(QAction* a)
{
  int id = a->data().toInt();
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
      this,
      tr("Choose one or more MPEG files to open"),
      settings().lastdir,
      tr(DVBCUT_LOADFILTER));
    if (fn.empty()) {
//      fprintf(stderr,"open(): QFileDialog::getOpenFileNames() returned EMPTY filelist!!!\n");    
//      fprintf(stderr,"        If you didn't saw a FileDialog, please check your 'lastdir' settings variable...");    
      return;
    }  
    for (QStringList::Iterator it = fn.begin(); it != fn.end(); ++it)
      filenames.push_back(it->toStdString());

    // remember last directory if requested
    if (settings().lastdir_update) {
      QString dir = fn.front();
      int n = dir.lastIndexOf('/');
      if (n > 0)
        dir = dir.left(n);
      // adding a slash is NEEDED for top level directories (win or linux), and doesn't matter otherwise!
      dir = dir + "/"; 
      settings().lastdir = dir;
    }
  } 

  // hmmmm,... do we really need this? With fn.empty() we never reach this line...
  if (filenames.empty()) 
    return;

  make_canonical(filenames);

  std::string filename = filenames.front();

  // a valid file name has been entered
  setWindowTitle(QString(VERSION_STRING " - " + QString::fromStdString(filename)));

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
  ui->eventlist->clear();
  ui->imagedisplay->setMinimumSize(QSize(0,0));
  ui->imagedisplay->setPixmap(QPixmap());
  ui->pictimelabel->clear();
  ui->pictimelabel2->clear();
  ui->picinfolabel->clear();
  ui->picinfolabel2->clear();
  ui->linslider->setValue(0);
  ui->jogslider->setValue(0);

  ui->viewNormalAction->setChecked(true);
  ui->viewUnscaledAction->setChecked(false);
  ui->viewDifferenceAction->setChecked(false);

  ui->fileOpenAction->setEnabled(false);
  ui->fileSaveAction->setEnabled(false);
  ui->fileSaveAsAction->setEnabled(false);
  ui->snapshotSaveAction->setEnabled(false);
  ui->chapterSnapshotsSaveAction->setEnabled(false);
  // enable closing even if no file was loaded (mr)
  //fileCloseAction->setEnabled(false);
  ui->fileExportAction->setEnabled(false);
  ui->playPlayAction->setEnabled(false);
  ui->playStopAction->setEnabled(false);
  audiotrackmenu->setEnabled(false);
  audiotrackpopup->clear();
  ui->editStartAction->setEnabled(false);
  ui->editStopAction->setEnabled(false);
  ui->editChapterAction->setEnabled(false);
  ui->editBookmarkAction->setEnabled(false);
  ui->editAutoChaptersAction->setEnabled(false);
  ui->editSuggestAction->setEnabled(false);
  ui->editImportAction->setEnabled(false);
  //editConvertAction->setEnabled(false);

#ifdef HAVE_LIB_AO

  ui->playAudio1Action->setEnabled(false);
  ui->playAudio2Action->setEnabled(false);
#endif // HAVE_LIB_AO

  ui->viewNormalAction->setEnabled(false);
  ui->viewUnscaledAction->setEnabled(false);
  ui->viewDifferenceAction->setEnabled(false);

  ui->eventlist->setEnabled(false);
  ui->imagedisplay->setEnabled(false);
  ui->pictimelabel->setEnabled(false);
  ui->pictimelabel2->setEnabled(false);
  ui->picinfolabel->setEnabled(false);
  ui->picinfolabel2->setEnabled(false);
  ui->goinput->setEnabled(false);
  ui->gobutton->setEnabled(false);
  ui->goinput2->setEnabled(false);
  ui->gobutton2->setEnabled(false);
  ui->linslider->setEnabled(false);
  ui->jogslider->setEnabled(false);

  std::string prjfilename;
  QDomDocument domdoc;
  {
    QFile infile(QString::fromStdString(filename));
    if (infile.open(QIODevice::ReadOnly)) {
      char buff[512];
      QString line;
      qint64 readBytes = infile.readLine(buff,512);
      if(readBytes>0)
        line = QString::fromLatin1(buff, readBytes);
      if (line.startsWith(QString("<!DOCTYPE"))
       || line.startsWith(QString("<?xml"))) {
        infile.seek(0);
        QString errormsg;
        if (domdoc.setContent(&infile,false,&errormsg)) {
          QDomElement docelem = domdoc.documentElement();
          if (docelem.tagName() != "dvbcut") {
            critical(tr("Failed to read project file - dvbcut"),
                     //: Placeholder will be replaced with filename
                     tr("%1:\nNot a valid dvbcut project file").arg(QString::fromStdString(filename)));
            ui->fileOpenAction->setEnabled(true);
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
		filenames.push_back(qs.toStdString());
	    }
	    else if (e.tagName() == "idxfile" && idxfilename.empty()) {
	      QString qs = e.attribute("path");
	      if (!qs.isEmpty())
		idxfilename = qs.toStdString();
	    }
	    else if (e.tagName() == "expfile" && expfilename.empty()) {           
	      QString qs = e.attribute("path");
	      if (!qs.isEmpty())
		expfilename = qs.toStdString();
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
	      filenames.push_back(qs.toStdString());
	  }
	  if (idxfilename.empty()) {
	    QString qs = docelem.attribute("idxfile");
	    if (!qs.isEmpty())
	      idxfilename = qs.toStdString();
	  }
	  if (expfilename.empty()) {
	    QString qs = docelem.attribute("expfile");
	    if (!qs.isEmpty())
	      expfilename = qs.toStdString();
	  }
	  // sanity check
	  if (filenames.empty()) {
	    critical(tr("Failed to read project file - dvbcut"),
		     //: Placeholder will be replaced with project filename
		     tr("%1:\nNo MPEG filename given in project file").arg(QString::fromStdString(filename)));
	    ui->fileOpenAction->setEnabled(true);
	    return;
	  }
          prjfilename = filename;
        }
        else {
          critical(tr("Failed to read project file - dvbcut"),
	    QString::fromStdString(filename) + ":\n" + errormsg);
          ui->fileOpenAction->setEnabled(true);
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
    critical(tr("Failed to open file - dvbcut"), errormessage);
    ui->fileOpenAction->setEnabled(true);
    return;
  }

  if (idxfilename.empty()) {
    idxfilename = filename + ".idx";
    if (!nogui) {
	  /*
	   * BEWARE! UGLY HACK BELOW!
	   *
	   * In order to circumvent the QFileDialog's stupid URL parsing,
	   * we need to first change to the directory and then pass the
	   * filename as a relative file: URL. Otherwise, filenames that
	   * contain ":" will not be handled correctly. --mr
	   */
	  QUrl u;
	  u.setScheme("file");
	  u.setPath(QString::fromStdString(idxfilename));
	  if (chdir(QFileInfo(u.path()).absolutePath().toLatin1()) == -1)
	      if (chdir("/") == -1)
		  fprintf(stderr, "chdir(\"/\") failed\n");
	  QString relname = QFileInfo(u.path()).fileName();
	  QString s=QFileDialog::getSaveFileName(
		  this,
		  tr("Choose the name of the index file"),
		  relname,
		  tr(DVBCUT_IDXFILTER) );
	  if (s.isEmpty()) {
		delete mpg;
		mpg=0;
		ui->fileOpenAction->setEnabled(true);
		return;
	  }
	  idxfilename = s.toStdString();
	  // it's now a relative name that will be canonicalized again soon
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
      critical(tr("Failed to open file - dvbcut"),errorstring);
      ui->fileOpenAction->setEnabled(true);
      return;
    }
    if (pictures==-2) {
      delete mpg;
      mpg=0;
      critical(tr("Invalid index file - dvbcut"), errorstring);
      ui->fileOpenAction->setEnabled(true);
      return;
    }
    if (pictures<=-3) {
      delete mpg;
      mpg=0;
      //: The index file belongs to another MPEG file
      critical(tr("Index file mismatch - dvbcut"), errorstring);
      ui->fileOpenAction->setEnabled(true);
      return;
    }
  }

  if (pictures<0) {
    progressstatusbar psb(statusBar());
    psb.setprogress(500);
    //: Text shown in the main window status bar when generating index. For example: "Indexing 'path/to/file.mpg'..."
    psb.print(tr("Indexing '%1'...").arg(QString::fromStdString(filename)));
    std::string errorstring;
    busy.setbusy(true);
    pictures=mpg->generateindex(idxfilename.empty()?0:idxfilename.c_str(),&errorstring,&psb);
    busy.setbusy(false);
    if (nogui && pictures > 0)
      fprintf(stderr,"Generated index with %d pictures!\n",pictures);

    if (psb.cancelled()) {
      delete mpg;
      mpg=0;
      ui->fileOpenAction->setEnabled(true);
      return;
    }

    if (pictures<0) {
      delete mpg;
      mpg=0;
      critical(tr("Error creating index - dvbcut"),
               //: %1 will be replaced with filename, %2 with error description
               tr("Cannot create index for\n%1:\n%2").arg(QString::fromStdString(filename), QString::fromStdString(errorstring)));
      ui->fileOpenAction->setEnabled(true);
      return;
    } else if (!errorstring.empty()) {
      critical(tr("Error saving index file - dvbcut"), QString::fromStdString(errorstring));
    }
  }

  if (pictures<1) {
    delete mpg;
    mpg=0;
    critical(tr("Invalid MPEG file - dvbcut"),
             //: Placeholder will be replaced with filename
             tr("The chosen file\n%1\ndoes not contain any video").arg(QString::fromStdString(filename)));
    ui->fileOpenAction->setEnabled(true);
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
  ui->linslider->setMaximum(pictures-1);
  ui->linslider->setSingleStep(int(300*fps));
  ui->linslider->setPageStep(int(900*fps));
  if (settings().lin_interval > 0)
    ui->linslider->setTickInterval(int(settings().lin_interval*fps));

  //alpha=log(jog_maximum)/double(100000-jog_offset);
  // with alternative function
  alpha=log(settings().jog_maximum)/100000.;
  if (settings().jog_interval > 0 && settings().jog_interval <= 100000) 
    ui->jogslider->setTickInterval(int(100000/settings().jog_interval));

  curpic=~0;
  showimage=true;
  fine=false;
  jogsliding=false;
  jogmiddlepic=0;
  imgp=new imageprovider(*mpg,new dvbcutbusy(this),false,viewscalefactor);

  linslidervalue(0);
  ui->linslider->setValue(0);
  ui->jogslider->setValue(0);

  ui->eventlist->setMinimumWidth(200);

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
        picnum=string2pts(str.toStdString())/getTimePerFrame();
      }
      else
        picnum=str.toInt(&okay,0);
      if (okay && picnum>=0 && picnum<pictures) {
        new EventListItem(ui->eventlist,QPixmap::fromImage(imgp->getimage(picnum)),evt,picnum,(*mpg)[picnum].getpicturetype(),(*mpg)[picnum].getpts()-firstpts);
      }
    }
  }

  update_quick_picture_lookup_table();

  ui->fileOpenAction->setEnabled(true);
  ui->fileSaveAction->setEnabled(true);
  ui->fileSaveAsAction->setEnabled(true);
  ui->snapshotSaveAction->setEnabled(true);
  ui->chapterSnapshotsSaveAction->setEnabled(true);
  ui->fileCloseAction->setEnabled(true);
  ui->fileExportAction->setEnabled(true);
  ui->playPlayAction->setEnabled(true);
  audiotrackmenu->setEnabled(true);
  ui->playStopAction->setEnabled(false);
  ui->editStartAction->setEnabled(true);
  ui->editStopAction->setEnabled(true);
  ui->editChapterAction->setEnabled(true);
  ui->editBookmarkAction->setEnabled(true);
  ui->editAutoChaptersAction->setEnabled(true);
  ui->editSuggestAction->setEnabled(true);
  ui->editImportAction->setEnabled(true);
  //editConvertAction->setEnabled(true);
  ui->viewNormalAction->setEnabled(true);
  ui->viewUnscaledAction->setEnabled(true);
  ui->viewDifferenceAction->setEnabled(true);

#ifdef HAVE_LIB_AO

  if (mpg->getaudiostreams()) {
    ui->playAudio1Action->setEnabled(true);
    ui->playAudio2Action->setEnabled(true);
  }
#endif // HAVE_LIB_AO

  ui->eventlist->setEnabled(true);
  ui->imagedisplay->setEnabled(true);
  ui->pictimelabel->setEnabled(true);
  ui->pictimelabel2->setEnabled(true);
  ui->picinfolabel->setEnabled(true);
  ui->picinfolabel2->setEnabled(true);
  ui->goinput->setEnabled(true);
  ui->gobutton->setEnabled(true);
  ui->goinput2->setEnabled(true);
  ui->gobutton2->setEnabled(true);
  ui->linslider->setEnabled(true);
  ui->jogslider->setEnabled(true);

  QActionGroup *audiotrackmenugroup = new QActionGroup(this);
  for(int a=0;a<mpg->getaudiostreams();++a) {
    QAction* action = audiotrackpopup->addAction(QString::fromStdString(mpg->getstreaminfo(audiostream(a))));
    action->setData(a);
    action->setCheckable(true);
    audiotrackmenugroup->addAction(action);
  }
  if (mpg->getaudiostreams()>0) {
    audiotrackpopup->actions().at(0)->setChecked(true);
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

void dvbcut::setviewscalefactor(double factor)
{
  if (factor<=0.0)
    factor=1.0;
  ui->viewFullSizeAction->setChecked(factor==1.0);
  ui->viewHalfSizeAction->setChecked(factor==2.0);
  ui->viewQuarterSizeAction->setChecked(factor==4.0);
  ui->viewCustomSizeAction->setChecked(factor==settings().viewscalefactor_custom);

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
  bool myEvent = true;
  int delta = 0;
  int incr = WHEEL_INCR_NORMAL;
  Qt::KeyboardModifiers mods;

  if (e->type() == QEvent::Wheel && watched != ui->jogslider) {
    QWheelEvent *we = (QWheelEvent*)e;
    // Note: delta is a multiple of 120 (see Qt documentation)
    delta = we->delta();
    mods = we->modifiers();
    if(mods & Qt::AltModifier) {
      incr = WHEEL_INCR_ALT;
    } else if(mods & Qt::ControlModifier) {
      incr = WHEEL_INCR_CTRL;
    } else if(mods & Qt::ShiftModifier) {
      incr = WHEEL_INCR_SHIFT;
    } else if(mods & Qt::MetaModifier) {
      // TODO: do we want/need another step-size?
      // incr = WHEEL_INCR_META;
    }

  }
  else if (e->type() == QEvent::KeyPress) {
    QKeyEvent *ke = (QKeyEvent*)e;
    delta = ke->count() * settings().wheel_delta;
    if (ke->key() == Qt::Key_Right)
      delta = -delta;
    else if (ke->key() != Qt::Key_Left)
      myEvent = false;
    mods = ke->modifiers();
    if(mods & Qt::AltModifier) {
      incr = WHEEL_INCR_ALT;
    } else if(mods & Qt::ControlModifier) {
      incr = WHEEL_INCR_CTRL;
    } else if(mods & Qt::ShiftModifier) {
      incr = WHEEL_INCR_SHIFT;
    } else if(mods & Qt::MetaModifier) {
      // TODO: do we want/need another step-size?
      //  incr = WHEEL_INCR_META;
    }
  }
  else
    myEvent = false;

  if (myEvent) {
    // process scroll event myself
    incr = settings().wheel_increments[incr];
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
        ui->linslider->setValue(newpos);
        fine = save;
      }
      return true;
    }

  // propagate to base class
  return QMainWindow::eventFilter(watched, e);
}

/*virtual*/
void dvbcut::wheelEvent(QWheelEvent* event)
{
    eventFilter(NULL, event);
}

int
dvbcut::question(const QString & caption, const QString & text)
{
  if (nogui) {
    //: Question during batch processing - print the prompt and tell the user that you assume the answer is No.
    fprintf(stderr, "%s", tr("%1\n%2\n(assuming No)\n").arg(caption, text).toLatin1().data());
    return QMessageBox::No;
  }
  return QMessageBox::question(this, caption, text, QMessageBox::Yes,
    QMessageBox::No | QMessageBox::Default | QMessageBox::Escape);
}

int
dvbcut::critical(const QString & caption, const std::string & text) {
  return critical(caption,  QString::fromStdString(text));
}

int
dvbcut::critical(const QString & caption, const QString & text)
{
  if (nogui) {
    //: Error during batch processing - print text and tell the user you're aborting the batch
    fprintf(stderr, "%s", tr("%1\n%2\n(aborting)\n").arg(caption, text).toLatin1().data());
    return QMessageBox::Abort;
  }
  return QMessageBox::critical(this, caption, text,
    QMessageBox::Abort, QMessageBox::NoButton);
}

void
dvbcut::make_canonical(std::string &filename) {
  if (filename[0] != '/') {
    char *rp = realpath(filename.c_str(), NULL);
    if (rp)
      filename = rp;
    free(rp);
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
  const char *AR[]={"forbidden","1:1","4:3","16:9","2.21:1","reserved"};
  const char *FR[]={"forbidden","23.976","24","25","29.97","30","50","59.94","60","reserved"};  
 
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
  ui->pictimelabel->setText(curtime);
  ui->pictimelabel2->setText(outtime);
  ui->goinput->setText(QString::number(curpic));
  ui->goinput2->setText(QString::number(outpic));
  
  int res=idx.getresolution();	// are found video resolutions stored in index?
  if (res) {	
    // new index with resolution bits set and lookup table at the end			  
    ui->picinfolabel->setText(QString::number(mpg->getwidth(res)) + "x" 
                        + QString::number(mpg->getheight(res)));
  } else {
    // in case of an old index file type (or if we don't want to change the index format/encoding?)
    // ==> get info directly from each image (which could be somewhat slower?!?)
    QImage p = imageprovider(*mpg, new dvbcutbusy(this), true).getimage(curpic,false);   
    ui->picinfolabel->setText(QString::number(p.width()) + "x" 
                        + QString::number(p.height()));
  }
  ui->picinfolabel2->setText(QString(FR[idx.getframerate()]) + "fps, "
                      + QString(AR[idx.getaspectratio()]));

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
  
  int index;
  for (index = 0; index < ui->eventlist->count(); index++)
  {
    EventListItem *item = dynamic_cast<EventListItem *>(ui->eventlist->item(index));
    if (item) {
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

void dvbcut::helpAboutAction_activated()
{
  QMessageBox::about(this, tr("dvbcut"), 
    tr("<head></head><body><span style=\"font-family: Helvetica,Arial,sans-serif;\">"
      "<p>Version %1</p>"
      "<a href=\"https://github.com/bernhardu/dvbcut-deb\">Git source repository.</a></p>"
      "<p>This program is free software; you can redistribute it and/or "
      "modify it under the terms of the GNU General Public License as "
      "published by the Free Software Foundation; either version 2 of "
      "the License, or (at your option) any later version. This "
      "program is distributed in the hope that it will be useful, "
      "but WITHOUT ANY WARRANTY; without even the implied warranty of "
      "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU "
      "General Public License for more details.</p>"
      "<p>You should have received a copy of the GNU General Public License along "
      "with this program; if not, see "
      "<a href=\"http://www.gnu.org/licenses/\">http://www.gnu.org/licenses/</a>.</p>"
      "</span></body></html>").arg(VERSION_STRING));
}

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <qdialog.h>
#include <qtextbrowser.h>
#include <qpushbutton.h>

class helpDialog : public QDialog {
public:
  helpDialog(QWidget *parent, QString file)
  : QDialog(parent)
  {
    prev = new QPushButton(tr("Prev"));
    next = new QPushButton(tr("Next"));
    home = new QPushButton(tr("Home"));
    close = new QPushButton(tr("Close"));
    viewer = new QTextBrowser();

    hbox = new QHBoxLayout();
    hbox->addWidget(prev);
    hbox->addWidget(next);
    hbox->addWidget(home);
    hbox->addWidget(close);

    QWidget *hboxw = new QWidget();
    hboxw->setLayout(hbox);

    vbox = new QVBoxLayout();
    vbox->addWidget(viewer);
    vbox->addWidget(hboxw);
    setLayout(vbox);
    resize(640, 480);

    close->setDefault(true);
    connect(prev, SIGNAL(clicked()), viewer, SLOT(backward()));
    connect(viewer, SIGNAL(backwardAvailable(bool)), prev, SLOT(setEnabled(bool)));
    connect(next, SIGNAL(clicked()), viewer, SLOT(forward()));
    connect(viewer, SIGNAL(forwardAvailable(bool)), next, SLOT(setEnabled(bool)));
    connect(home, SIGNAL(clicked()), viewer, SLOT(home()));
    connect(close, SIGNAL(clicked()), this, SLOT(accept()));
    viewer->setSource(file);
    setWindowTitle(tr("dvbcut help"));
    show();
  }
  virtual ~helpDialog() {
    delete prev;
    delete next;
    delete home;
    delete close;
    delete hbox;
    delete viewer;
    delete vbox;
  }
private:
  QVBoxLayout *vbox;
  QHBoxLayout *hbox;
  QTextBrowser *viewer;
  QPushButton *prev, *next, *home, *close;
};

static QString get_help_file(QString locale)
{
    QString file = QString("/dvbcut_%1.html").arg(locale);

    return get_resource(file);
}

void dvbcut::helpContentAction_activated()
{
  QString locale = QLocale::system().name();

  QString helpFile = get_help_file(locale);

  if (!QFile::exists(helpFile))
      helpFile = get_help_file(locale.left(2));

  if (!QFile::exists(helpFile))
      helpFile = get_help_file("en");

  if (QFile::exists(helpFile)) {
    helpDialog dlg(this, helpFile);
    dlg.exec();
  }
  else {
    QMessageBox::information(this, tr("dvbcut"),
      tr("Help file %1 not available").arg(helpFile));
  }
}

void dvbcut::gotoFrame(int frameno)
{
  bool save = fine;
  fine = true;
  ui->linslider->setValue(frameno);
  fine = save;
}
