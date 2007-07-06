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
#include <list>

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

void
usage_exit(int rv=1) {
  fprintf(stderr,"Usage:\n"
    "  %s -generateidx [-idx <indexfilename>] <mpgfilename> ...\n"
    "  %s -batch <prjfilename>\n",
    argv0, argv0);
  exit(rv);
}

int
main(int argc, char *argv[]) {
  argv0=argv[0];
  bool generateidx=false;
  bool batchmode=false;
  std::string idxfilename;
  int i;

  /*
   * process arguments
   */
  for (i = 1; i < argc && argv[i][0] == '-'; ++i) {
    size_t n = strlen(argv[i]);
    if (n == 1)	// "-"
      break;
    else if (strncmp(argv[i], "-batch", n) == 0)
      batchmode = true;
    else if (strncmp(argv[i], "-generateidx", n) == 0)
      generateidx = true;
    else if (strncmp(argv[i], "-idx", n) == 0 && ++i < argc)
      idxfilename = argv[i];
    else
      usage_exit();
  }

  /*
   * sanity check
   */
  if (batchmode && generateidx)
    usage_exit();

  if (generateidx) {
    if (i >= argc) // no input files given
      usage_exit();

    std::string mpgfilename = argv[i];	// use first one (for now)

    if (idxfilename.empty())
      idxfilename = mpgfilename + ".idx";

    mpgfile *mpg = 0;
    std::string errormessage;
    inbuffer buf(8 << 20, 128 << 20);
    while (i < argc && buf.open(argv[i], &errormessage)) {
      ++i;
    }
    buf.setsequential(true);
    if (i == argc) {
      mpg = mpgfile::open(buf, &errormessage);
    }

    if (mpg==0) {
      fprintf(stderr,"%s: %s\n",argv0,errormessage.c_str());
      return 1;
    }

    index::index idx(*mpg);
    int pics=idx.generate();
    if (pics==0) {
      fprintf(stderr,"%s: file '%s' contains no pictures\n",argv0,mpgfilename.c_str());
      return 1;
    }
    else if (pics<0) {
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
  std::list<std::string> filenames;

  int rv=1;
  dvbcut *main=new dvbcut;
  main->batchmode(batchmode);

  if (batchmode) {
    if (i + 1 != argc)	// must provide exactly one filename
      usage_exit();
    filenames.push_back(std::string(argv[i]));
    main->open(filenames,idxfilename);
    main->fileExport();
    rv = 0;
  }
  else {
    main->show();

    if (i < argc) {
      do {
	filenames.push_back(std::string(argv[i]));
      }
      while (++i < argc);
      main->open(filenames,idxfilename);
    }

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
