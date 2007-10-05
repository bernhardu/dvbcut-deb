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
extern "C" {
#include <ffmpeg/avformat.h>
}
#include <qimage.h>
#include <qsettings.h>
#include <qtextcodec.h>
#include "dvbcut.h"
#include "mpgfile.h"
#include "index.h"

static char *argv0;

void
usage_exit(int rv=1) {
  fprintf(stderr,
    "Usage:\n"
    "  %s -generateidx [-idx <indexfilename>] [<mpgfilename> ...]\n"
    "  %s -batch <prjfilename> | <mpgfilename> ...\n\n",
    argv0, argv0);
  fprintf(stderr,
    "If no input files are specified, `dvbcut -generateidx' reads from\n"
    "standard input.  By default, it also writes the index to standard\n"
    "output, but you can specify another destination with `-idx'.\n\n");
  fprintf(stderr,
    "Options may be abbreviated as long as they remain unambiguous.\n\n");
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
    if (n == 2 && argv[i][1] == '-') {	// POSIX argument separator
      ++i;
      break;
    }
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

  /*
   * Generate mode
   */
  if (generateidx) {
    std::string mpgfilename = "<stdin>";
    std::string errormessage;
    inbuffer buf(8 << 20, 128 << 20);
    bool okay = true;
    if (i >= argc) {
      // no filenames given, read from stdin
      okay = buf.open(STDIN_FILENO, &errormessage);
    }
    else {
      mpgfilename = argv[i];	// use first one (for now)
      if (idxfilename.empty())
	idxfilename = mpgfilename + ".idx";
      while (okay && i < argc) {
	okay = buf.open(argv[i], &errormessage);
	++i;
      }
    }
    if (!okay) {
      fprintf(stderr, "%s: %s\n", argv0, errormessage.c_str());
      return 1;
    }
    buf.setsequential(true);

    mpgfile *mpg = mpgfile::open(buf, &errormessage);
    if (mpg == 0) {
      fprintf(stderr, "%s: %s\n", argv0, errormessage.c_str());
      return 1;
    }

    index::index idx(*mpg);
    int pics = idx.generate();
    if (pics <= 0) {
      fprintf(stderr, "%s: %s: %s\n", argv0, mpgfilename.c_str(),
	pics < 0 ? strerror(errno) : "no pictures found");
      return 1;
    }

    int rv;
    if (idxfilename.empty()) {
      rv = idx.save(STDOUT_FILENO);
      idxfilename = "<stdout>";
    }
    else
      rv = idx.save(idxfilename.c_str());
    if (rv < 0) {
      fprintf(stderr, "%s: %s: %s\n", argv0, idxfilename.c_str(), strerror(errno));
      return 1;
    }

    return 0;
  }

  /*
   * GUI and batch mode
   */
  QApplication a(argc, argv);
  QTextCodec::setCodecForCStrings(QTextCodec::codecForLocale());

#ifdef HAVE_LIB_AO
  ao_initialize();
#endif // HAVE_LIB_AO

  av_register_all();
  std::list<std::string> filenames;

  int rv=1;
  dvbcut *main=new dvbcut;
  main->batchmode(batchmode);

  while (i < argc) {
    filenames.push_back(std::string(argv[i]));
    ++i;
  }

  if (batchmode) {
    if (filenames.empty())	// must provide at least one filename
      usage_exit();
    main->open(filenames,idxfilename);
    main->fileExport();
    rv = 0;
  }
  else {
    main->show();

    if (!filenames.empty())
      main->open(filenames,idxfilename);

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
