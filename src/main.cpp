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
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string>

#ifdef HAVE_LIB_AO
#include <ao/ao.h>
#endif // HAVE_LIB_AO

#include <qapplication.h>
#include <ffmpeg/avformat.h>
#include <qimage.h>
#include <qsettings.h>
#include "dvbcut.h"
#include "mpgfile.h"
#include "index.h"

static char *argv0;

void usage_exit(int rv=1)
  {
  fprintf(stderr,"Usage:\n"
    "  %s -generateidx [-idx <indexfilename>] <mpgfilename>\n"
    "  %s -batch <prjfilename>\n",
    argv0, argv0);
  exit(rv);
  }

int main(int argc, char *argv[])
  {
  argv0=argv[0];
  bool generateidx=false;
  bool batchmode=false;

  for(int i=1;i<argc;++i)
    if (!strcmp(argv[i],"-generateidx"))
      generateidx=true;

  if (generateidx) {
    std::string idxfilename,mpgfilename;

    for(int i=1;i<argc;++i) {
      if (argv[i][0]=='-' && argv[i][1]!=0) // option
        {
        if (!strcmp(argv[i]+1,"idx") && (i+1)<argc)
          idxfilename=argv[++i];
        else if (strcmp(argv[i]+1,"generateidx"))
          usage_exit();
        } else if (mpgfilename.empty())
        mpgfilename=argv[i];
      else
        usage_exit();
      }

    if (mpgfilename.empty())
      usage_exit();

    if (idxfilename.empty())
      if (mpgfilename=="-")
        idxfilename="-";
      else
        idxfilename=mpgfilename+".idx";

    std::string errormessage;
    mpgfile *mpg=mpgfile::open(mpgfilename,&errormessage);

    if (mpg==0) {
      fprintf(stderr,"%s: %s\n",argv0,errormessage.c_str());
      return 1;
      }

    index::index idx(*mpg);
    int pics=idx.generate();
    if (pics==0) {
      fprintf(stderr,"%s: file '%s' contains no pictures\n",argv0,mpgfilename.c_str());
      return 1;
      } else if (pics<0) {
      fprintf(stderr,"%s: '%s': %s\n",argv0,mpgfilename.c_str(),strerror(errno));
      return 1;
      }

    if (idx.save(idxfilename.c_str())<0) {
      fprintf(stderr,"%s: '%s': %s\n",argv0,idxfilename.c_str(),strerror(errno));
      return 1;
      }

    return 0;
    }

  QApplication a(argc, argv);

#ifdef HAVE_LIB_AO
  ao_initialize();
#endif // HAVE_LIB_AO

  av_register_all();
  std::string filename,idxfilename;

  for(int i=1;i<argc;++i) {
    if (argv[i][0]=='-' && argv[i][1]!=0) // option
      {
      if (!strcmp(argv[i]+1,"idx") && (i+1)<argc)
        idxfilename=argv[++i];
      else if (!strcmp(argv[i]+1,"batch"))
	batchmode=true;
      else
        usage_exit();
      } else if (filename.empty())
      filename=argv[i];
    else
      usage_exit();
    }

  int rv=1;
  dvbcut *main=new dvbcut;
  main->batchmode(batchmode);

  if (batchmode) {
      if (filename.empty())
	usage_exit();
      main->open(filename,idxfilename);
      main->fileExport();
      rv = 0;
    }
  else {
    main->show();

    if (!filename.empty())
      main->open(filename,idxfilename);


    if (main) {
      a.connect( &a, SIGNAL( lastWindowClosed() ), &a, SLOT( quit() ) );
      rv = a.exec();
      }
    }

#ifdef HAVE_LIB_AO
  ao_shutdown();
#endif // HAVE_LIB_AO

  return rv;
  }
