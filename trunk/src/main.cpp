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
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <clocale>
#include <string>
#include <list>

#ifdef HAVE_LIB_AO
#include <ao/ao.h>
#endif // HAVE_LIB_AO

#include <qapplication.h>
extern "C" {
#include <avformat.h>
}
#include <qimage.h>
#include <qsettings.h>
#include <qtextcodec.h>
#include "dvbcut.h"
#include "mpgfile.h"
#include "index.h"

#include "gettext.h"

#include "version.h"

#define VERSION_STRING	"dvbcut " VERSION "/" REVISION

static char *argv0;

void
usage_exit(int rv=1) {
  fprintf(stderr,
    "Usage ("VERSION_STRING"):\n"
    "  %s -generateidx [-idx <idxfilename>] [<mpgfilename> ...]\n"
    "  %s -batch [ OPTIONS ] <prjfilename> | <mpgfilename> ...\n\n"
    "OPTIONS: -cut 4:3|16:9|TS|TS2|<list>, -exp <expfilename>,\n"
    "         -format <num>, -automarker <num>\n\n",
    argv0, argv0);
  fprintf(stderr,
    "If no input files are specified, `dvbcut -generateidx' reads from\n"
    "standard input.  By default, it also writes the index to standard\n"
    "output, but you can specify another destination with `-idx'.\n\n");
  fprintf(stderr,
    "In batch mode you can use `-cut' to keep only 4:3 resp. 16:9 frames or\n"
    "create automatically alternating START/STOP cut markers for the bookmarks\n"
    "imported from the input transport stream (TS, TS2) or for a given list of\n"
    "frame numbers / time stamps (you can use any of ',-|;' as separators).\n" 
    "Without any (valid) cut markers the whole file will be converted!\n\n");
  fprintf(stderr,
    "The -exp switch specifies the name of the exported file, with -format\n"
    "the default export format (0=MPEG program stream/DVD) can be changed and\n"
    "-automarker sets START/STOP markers at BOF/EOF (0=none,1=BOF,2=EOF,3=both).\n\n");
  fprintf(stderr,
    "Options may be abbreviated as long as they remain unambiguous.\n\n");
  exit(rv);
}

int
main(int argc, char *argv[]) {
  argv0=argv[0];
  bool generateidx=false;
  bool batchmode=false, start_bof=true, stop_eof=true;
  int exportformat=0;
  std::string idxfilename, expfilename;
  std::vector<std::string> cutlist;
  std::list<std::string> filenames;
  int i;

  setlocale(LC_ALL, "");
  textdomain("dvbcut");
  /*
   * process arguments
   */
  for (i = 1; i < argc; ++i) {
    if (argv[i][0] == '-' || argv[i][0] == '+') {
      // process switches / options
      size_t n = strlen(argv[i]);
      if (n == 2 && argv[i][1] == '-') {	// POSIX argument separator
        ++i;
        break;
      }
      else if (strncmp(argv[i], "-batch", n) == 0)
        batchmode = true;
      else if (strncmp(argv[i], "-generateidx", n) == 0)
        generateidx = true;
      else if (strncmp(argv[i], "-voracious", n) == 0)
	dvbcut::cache_friendly = false;
      else if (strncmp(argv[i], "-idx", n) == 0 && ++i < argc)
        idxfilename = argv[i];
      else if (strncmp(argv[i], "-exp", n) == 0 && ++i < argc)
        expfilename = argv[i];
      else if (strncmp(argv[i], "-format", n) == 0 && ++i < argc)
        exportformat = atoi(argv[i]);
      else if (strncmp(argv[i], "-automarker", n) == 0 && ++i < argc) {
        int bofeof = atoi(argv[i]);
        start_bof = (bofeof&1)==1;
        stop_eof  = (bofeof&2)==2;
      } else if (strncmp(argv[i], "-cut", n) == 0 && ++i < argc) {
        char *pch = strtok(argv[i],",-|;");
        while(pch) {
          if(strlen(pch))
            cutlist.push_back((std::string)pch);    
          pch = strtok(NULL,",-|;");                 
        } 
      } else 
        usage_exit(); 
    } else
      // process input files 
      // (that way files also can come first / options last and 
      // argument processing only happens once and only in this loop!)
      filenames.push_back(std::string(argv[i])); 
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
    if (filenames.empty()) {
      // no filenames given, read from stdin
      okay = buf.open(STDIN_FILENO, &errormessage);
    }
    else {
      mpgfilename = filenames.front();  // use first one (for now) 
      if (idxfilename.empty())
        idxfilename = mpgfilename + ".idx"; 
      for(std::list<std::string>::iterator it = filenames.begin(); 
                                    okay && it != filenames.end(); it++)	
      okay = buf.open(*it, &errormessage);
    }
    if (!okay) {
      fprintf(stderr, "%s: %s\n", argv0, errormessage.c_str());
      return 1;
    }
    buf.setsequential(dvbcut::cache_friendly);

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

  int rv=1;
  dvbcut *main=new dvbcut;
  main->batchmode(batchmode);
  main->exportoptions(exportformat,start_bof,stop_eof);

  if (batchmode) {
    if (filenames.empty())	// must provide at least one filename
      usage_exit();
    main->open(filenames,idxfilename,expfilename);
    if(!cutlist.empty()) {
      if(cutlist.front()=="AR") {  // obsolete (use 4:3 resp. 16:9 instead)! Or just in case of another AR... 
          main->editSuggest();
          main->editConvert(0);             
      } else if(cutlist.front()=="4:3") {
          main->editSuggest();     
          main->editConvert(2);             
      } else if(cutlist.front()=="16:9") {
          main->editSuggest();     
          main->editConvert(3);             
      } else if(cutlist.front()=="TS" || cutlist.front()=="TS1") {  // first bookmark is a START
          main->editImport();     
          main->editConvert(0);             
      } else if(cutlist.front()=="TS2") { // 2nd bookmark is a START
          main->editImport();     
          main->editConvert(1);             
      } else { 
          std::vector<int> piclist, prob_item, prob_pos;
          unsigned int j;
          size_t pos;
          for (j=0; j<cutlist.size(); j++)
            if((pos=cutlist[j].find_first_not_of("0123456789:./"))==std::string::npos) {                 
              if(cutlist[j].find_first_of(":./")!=std::string::npos) 
                piclist.push_back(string2pts(cutlist[j])/main->getTimePerFrame()); // pts divided by 3600(PAL) or 3003(NTSC)
              else
                piclist.push_back(atoi(cutlist[j].c_str()));                       // integers are treated as frame numbers!
            } else {
              prob_item.push_back(j);
              prob_pos.push_back(pos);
            }  
          if(piclist.size()%2) 
            fprintf(stderr,"*** Cut list contains an odd number of entries! ***\n");    
          if(!prob_item.empty()) {
            fprintf(stderr,"*** Problems parsing parameter provided with option `-cut'! ***\n");    
            for (j=0; j<prob_item.size(); j++) {
              fprintf(stderr,"    '%s' ==> discarded!\n",cutlist[prob_item[j]].c_str());
              for (i=0; i<5+prob_pos[j]; i++) fprintf(stderr," ");
              fprintf(stderr,"^\n");
            }  
          }  
          main->addStartStopItems(piclist);
      }  
    }  
    main->fileExport();
    rv = 0;
  }
  else {
    main->show();

    if (!filenames.empty())
      main->open(filenames,idxfilename,expfilename);

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
