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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <list>
#include <utility>
#include <set>
#include <vector>
#include <string>

#include "port.h"
#include "index.h"
#include "mpgfile.h"
#include "streamhandle.h"
#include "types.h"
#include "logoutput.h"

#ifndef O_BINARY
#define O_BINARY    0
#endif /* O_BINARY */

static inline ssize_t writer(int fd, const void *buf, size_t count)
  {
  int written=0;

  while (count>0) {
    int wr=write(fd,buf,count);
    if (wr<0)
      return wr;

    written+=wr;
    count-=wr;
    buf=(const void*)((const char*)buf+wr);
    }

  return written;
  }

index::~index()
  {
    if (p) {
      free(p);
      // delete read resolutions
      WIDTH.clear();		
      HEIGHT.clear();		
    }
  }

int index::generate(const char *savefilename, std::string *errorstring, logoutput *log)
  {
  int fd=-1;
  bool usestdout=false;
  int pictureswritten=0;
  dvbcut_off_t filesize=0;
  
  if (savefilename && savefilename[0]) {
    if (savefilename[0]=='-' && savefilename[1]==0) // use stdout
      {
      fd=STDOUT_FILENO;
      usestdout=true;
      } else
      if ((fd=::open(savefilename,O_WRONLY|O_CREAT|O_TRUNC|O_BINARY,0666))<0) {
        if (errorstring)
          *errorstring+=std::string("Open (")+savefilename+"): "+strerror(errno)+"\n";
        return fd;
        }
    }

  int size=90000;

  if (p) {
    if (pictures>0)
      size=pictures;
    else {
      free(p);
      p=0;
      }
    }

  if (!p)
    p=(picture*)malloc(size*sizeof(picture));

  pictures=0;

  if (log)
    filesize=mpg.getfilesize();
  streamhandle s(mpg.getinitialoffset());
  streamdata *sd=s.newstream(VIDEOSTREAM,streamtype::mpeg2video,mpg.istransportstream());

  bool foundseqheader=false;
  bool waitforfirstsequenceheader=true;
  int aspectratio=0,framerate=0;
  double maxbitrate=0., bitrate;
  long int bits=0;
  pts_t maxbitratepts=0, dt;
  std::pair<int,int> res;
  int nres = 0;
  filepos_t seqheaderpos=0, lastseqheaderpos=0;
  int seqheaderpic=0;
  pts_t referencepts=0; // pts of picture #0 in current sequence
  int maxseqnr=-1; // maximum sequence number in current sequence
  pts_t lastpts=1ll<<31, lastseqheaderpts=0, firstseqheaderpts=0;
  int last_non_b_pic = -1;
  int last_non_b_seqnr = -1;
  int last_seqnr = -1;
  int ptsmod = -1;
  int errcnt = 0;
  int err1cnt = 0;
  int lasterr = 0;
  int lastiframe = -1;

  while (mpg.streamreader(s)>0) {
    while (sd->getbuffer().inbytes()< (sd->getbuffer().getsize()/2))
      if (mpg.streamreader(s)<=0)
        break;

    if (log) {
      log->setprogress( (filesize>0)?(mpg.getfilepos()*1000/filesize):((mpg.getfilepos()/104753)%1001) );
      if (log->cancelled()) {
        if (p)
          free(p);
        p=0;
        pictures=realpictures=0;
        return 0;
        }
      }

    // flush data to file
    if (fd>=0 && pictureswritten<seqheaderpic) {
      int len=(seqheaderpic-pictureswritten)*sizeof(picture);
      if (::writer(fd,(void*)&p[pictureswritten],len)<0) {
        if (errorstring)
          *errorstring+=std::string("Write (")+savefilename+"): "+strerror(errno)+"\n";
        if (!usestdout)
          ::close(fd);
        fd=-1;
        } else
        pictureswritten=seqheaderpic;
      }

    const uint8_t *data=(const uint8_t*) sd->getdata();
    unsigned int inbytes=sd->inbytes();
    unsigned int skip=0;
    while(skip+11<inbytes) {
      if (data[skip+2]&0xfe) {
        skip+=3;
        continue;
        }
      if (data[skip+1]!=0) {
        skip+=2;
        continue;
        }

      if (*(uint32_t*)(data+skip)==mbo32(0x000001b3)) // sequence header
        {
        if (last_non_b_pic >= 0) {
          p[last_non_b_pic].setsequencenumber(++maxseqnr);
          last_non_b_pic = -1;
          }
	last_seqnr = -1;

        waitforfirstsequenceheader=false;
        foundseqheader=true;
        sd->discard(skip);
        data=(const uint8_t*) sd->getdata();

        // store found resolutions in lookup table 
        res.first=(data[4]<<4)|((data[5]>>4)&0xf);      // width
        res.second=((data[5]&0xf)<<8)|data[6];          // height
        if (resolutions.find(res)==resolutions.end() && nres<7) {
          nres++; 
	  resolutions.insert(res);   // a set helps checking already read resolutions
	  WIDTH.push_back(res.first);		
	  HEIGHT.push_back(res.second);		
	  //fprintf(stderr, "RESOLUTION[%d]: %d x %d\n", nres, res.first, res.second);
	}
        
        // that's always the same preset value and thus not very usefull (in 400 bps)!!!
	//bitrate = ((data[8]<<10)|(data[9]<<2)|((data[10]>>6)&0x3))*400;      
        //if (bitrate>maxbitrate) maxbitrate=bitrate;

        aspectratio=(data[7]>>4)&0xf;
        framerate=data[7]&0xf;
        referencepts+=((maxseqnr+1)*mpgfile::frameratescr[framerate])/300;
        seqheaderpos=sd->itemlist().front().fileposition;
	seqheaderpic=pictures;
        maxseqnr=-1;

        sd->discard(12);
        data=(const uint8_t*) sd->getdata();
        inbytes=sd->inbytes();
        skip=0;

        } else if ((*(uint32_t*)(data+skip)==mbo32(0x00000100))&&!waitforfirstsequenceheader) // picture header
        {
        sd->discard(skip);
        data=(const uint8_t*) sd->getdata();

        filepos_t picpos=sd->itemlist().front().fileposition;
        int seqnr=(data[4]<<2)|((data[5]>>6)&3);
        int frametype=(data[5]>>3)&7;
        if (frametype>3)
          frametype=0;

        pts_t pts=sd->itemlist().front().headerpts();
        if (pts>=0)
          {
          pts=ptsreference(pts,lastpts);
          lastpts = pts;
          int ptsdelta = mpgfile::frameratescr[framerate] / 300;
          int epsilon = ptsdelta / 100;	/* allow at most 1% deviation */
          int mod = pts % ptsdelta;
          if (ptsmod == -1)
	    ptsmod = mod;
          else if (mod != ptsmod) {
	    int error = (mod - ptsmod + ptsdelta) % ptsdelta;
	    if (error > ptsdelta / 2)
	      error -= ptsdelta;
	    bool complain = false;
	    if (lasterr != error) {
	      if (err1cnt > 0) {
		fprintf(stderr, "last video PTS error repeated %d times\n", err1cnt);
		err1cnt = 0;
		}
	      complain = true;
	      lasterr = error;
	      }
	    else
	      ++err1cnt;
	    ++errcnt;
	    if (-epsilon <= error && error <= epsilon) {
	      if (complain)
		fprintf(stderr, "inconsistent video PTS (%+d), correcting\n", error);
	      pts -= error;
	      } else {
	      if (complain)
		fprintf(stderr, "inconsistent video PTS (%+d) in %c frame %d, NOT correcting\n",
		  error, frametype["?IPB"], pictures);
	      }
	    }
          referencepts=pts-(seqnr*mpgfile::frameratescr[framerate])/300;
          sd->discardheader();
          } else
          pts=referencepts+(seqnr*mpgfile::frameratescr[framerate])/300;

        if (pictures>=size)
          {
          size+=90000;
          p=(picture*)realloc((void*)p,size*sizeof(picture));
          }

        p[pictures]=picture(foundseqheader?seqheaderpos:picpos,pts,framerate,aspectratio,seqnr,frametype,foundseqheader,0/*nres*/);

        // try to determine bitrate per GOP manually since the read one is not very usefull 
        if (foundseqheader) {
          if (firstseqheaderpts && lastseqheaderpos<seqheaderpos && lastseqheaderpts<pts) {
            dt = (pts-lastseqheaderpts)/90;
            bits = (seqheaderpos-lastseqheaderpos)*8;
            // avergage (input) bitrate per GOP
            bitrate=1000*double(bits)/double(dt);
            if (maxbitrate<bitrate) {
              maxbitrate=bitrate;
              maxbitratepts=lastseqheaderpts; 
            }  
	    //fprintf(stderr, "%s: BITRATE = %d kbps over next %d ms\n",
           //                 ptsstring(lastseqheaderpts-firstseqheaderpts).c_str(), int(bitrate/1024.), int(dt));          
          } else {
            firstseqheaderpts=pts;
          }
          lastseqheaderpos=seqheaderpos;
          lastseqheaderpts=pts;
        } 

        if (frametype == IDX_PICTYPE_I) {
	  if (lastiframe >= 0) {
	    int framepts = mpgfile::frameratescr[framerate] / 300;
	    pts_t ptsdelta = pts - p[lastiframe].getpts();
	    int pdelta = pictures - lastiframe + seqnr - p[lastiframe].getsequencenumber();
	    if (pdelta * framepts < ptsdelta)
	      fprintf(stderr, "missing frames in GOP (%d, %d): %lld\n",
		lastiframe, pictures, ptsdelta / framepts - pdelta);
	    }
	  lastiframe = pictures;
	  }

        if (frametype == IDX_PICTYPE_B) {
	  /* check sequence number */
	  if (seqnr != last_seqnr + 1) {
	    fprintf(stderr,
	      "missing frame(s) before B frame %d (%d != %d)\n",
	      pictures, seqnr, last_seqnr + 1);
	    if (seqnr <= last_seqnr) {
	      fprintf(stderr, "=> sequence number reset (%d => %d)\n", last_seqnr + 1, seqnr);
	      if (last_non_b_pic >= 0 && last_non_b_seqnr > last_seqnr) {
		fprintf(stderr, "=> inserting delayed picture (%d)\n", last_non_b_seqnr);
		p[last_non_b_pic].setsequencenumber(++maxseqnr);
		last_non_b_pic = -1;
		}
	      }
	    else if (last_non_b_pic >= 0 && last_non_b_seqnr < seqnr) {
	      fprintf(stderr, "=> inserting delayed picture (%d)\n", last_non_b_seqnr);
	      p[last_non_b_pic].setsequencenumber(++maxseqnr);
	      last_non_b_pic = -1;
	      }
	    }
	  p[pictures].setsequencenumber(++maxseqnr);
	  last_seqnr = seqnr;
	  } else {
	    /* I and P frames are delayed */
	    if (last_non_b_pic >= 0) {
	      /* check sequence number */
	      if (last_non_b_seqnr != last_seqnr + 1) {
		fprintf(stderr,
		  "missing frame(s) before %c frame %d (%d != %d)\n",
		  p[last_non_b_pic].isiframe() ? 'I' : 'P',
		  pictures, last_non_b_seqnr, last_seqnr + 1);
		}
	      p[last_non_b_pic].setsequencenumber(++maxseqnr);
	      last_seqnr = last_non_b_seqnr;
	      }
	    last_non_b_pic = pictures;
	    last_non_b_seqnr = seqnr;
	    if (frametype == IDX_PICTYPE_I)
	      last_seqnr = -1;
	  }

	++pictures;

        foundseqheader=false;
        sd->discard(8);
        data=(const uint8_t*) sd->getdata();
        inbytes=sd->inbytes();
        skip=0;
        } else
        ++skip;
      }
    sd->discard(skip);
    }

  if (err1cnt > 0)
    fprintf(stderr, "last video PTS error repeated %d times\n", err1cnt);
  if (errcnt > 0)
    fprintf(stderr, "found %d video PTS errors\n", errcnt);

  if (last_non_b_pic >= 0) {
    p[last_non_b_pic].setsequencenumber(++maxseqnr);
    last_non_b_pic = -1;
    }

  if (pictures==0) {
    free(p);
    p=0;
  } else {
#if 0
    if (size < pictures + 7) {
      size = pictures + 7;
      p=(picture*)realloc((void*)p,size*sizeof(picture));
    }
    // create 7 fake pictures containing read resolutions (pos: width, pts: height)
    for (int i=0; i<nres; i++) {
      p[pictures]=picture(filepos_t(WIDTH[i]),pts_t(HEIGHT[i]),0,0,0,0,false,i+1);
      ++pictures;
    }  
    for (int i=nres; i<7; i++) {
      p[pictures]=picture(filepos_t(0),pts_t(0),0,0,0,0,false,i+1);
      ++pictures;
    }  
#endif
    if (size != pictures) {
      p=(picture*)realloc((void*)p,pictures*sizeof(picture));
    }
  }

  if (fd>=0 && pictureswritten<pictures) {
    int len=(pictures-pictureswritten)*sizeof(picture);
    if (::writer(fd,(void*)&p[pictureswritten],len)<0) {
      if (errorstring)
        *errorstring+=std::string("Write (")+savefilename+"): "+strerror(errno)+"\n";
      if (!usestdout)
        ::close(fd);
      fd=-1;
      } else
      pictureswritten=pictures;
    }

  if (!usestdout && fd>=0)
    ::close(fd);

#if 0
  if (pictures != 0)
    pictures-=7;  // subtract fake pictures
#endif
  fprintf(stderr, "Max. input bitrate of %d kbps detected at %s!\n", int(maxbitrate/1024), ptsstring(maxbitratepts-firstseqheaderpts).c_str());

  return check();
  }

int
index::save(int fd, std::string *errorstring, bool closeme) {
  int len = pictures * sizeof(picture);
  int res = 0;
  int save = 0;

  if (isatty(fd)) {
    if (errorstring)
      *errorstring += std::string("refusing to write index to a tty\n");
    errno = EINVAL;
    // Note: do NOT close it even if the caller said so
    return -1;
  }
  if (::writer(fd, (void*)p, len) < 0) {
    save = errno;
    if (errorstring)
      *errorstring += std::string("write: ") + strerror(errno) + "\n";
    res = -1;
  }
  if (closeme)
    ::close(fd);
  errno = save;
  return res;
}

int
index::save(const char *filename, std::string *errorstring) {
  int fd;

  fd = ::open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
  if (fd == -1) {
    if (errorstring)
      *errorstring += std::string(filename) + ": open: " + strerror(errno) + "\n";
    return -1;
  }
  std::string tmp;
  if (save(fd, &tmp, true) == -1) {
    if (errorstring)
      *errorstring += std::string(filename) + ": " + tmp;
    return -1;
  }
  return 0;
}

int index::load(const char *filename, std::string *errorstring)
  {
  int fd=::open(filename,O_RDONLY|O_BINARY,0666);
  if (fd<0) {
    if (errorstring) {
      int serrno=errno;
      *errorstring+=std::string("Open (")+filename+"): "+strerror(errno)+"\n";
      errno=serrno;
      }
    return fd;
    }

  int size=0;
  int len=0;
  uint8_t *data=0;

  for(;;) {
    if (len>=size) {
      size+=90000*sizeof(picture);
      data=(uint8_t*)realloc((void*)data,size);
      }

    int rd=::read(fd,data+len,size-len);

    if (rd<0) {
      int save_errno=errno;
      if (errorstring)
        *errorstring+=std::string("Read (")+filename+"): "+strerror(errno)+"\n";
      if (data)
        free(data);
      ::close(fd);
      errno=save_errno;
      return -1;
      }

    if (rd==0)
      break;

    len+=rd;
    }

  ::close(fd);

  pictures=len/sizeof(picture);
  if (pictures==0) {
    free(p);
    p=0;
    realpictures=0;
    return 0;
    }
  if (!((picture*)data)->getseqheader()) {
    free(p);
    p=0;
    pictures=0;
    realpictures=0;
    if (errorstring)
      *errorstring+=std::string("Invalid index file '")+filename+"'\n";
    fprintf(stderr,"Invalid index file: first frame no sequence header\n");
    return -2;
    }
  p=(picture*)realloc((void*)data,pictures*sizeof(picture));

  // 7 fake pictures at end contain resolution lookup table 
  // (if new type of index file, after svn-revision 131)
  if (p[0].getresolution()>0) {
    pictures-=7;
    int w, h, nres=0;
    for (int i=pictures; nres<7; i++) {
      w=int(p[i].getpos());
      h=int(p[i].getpts());    
      if (w==0 || h==0)
        break;
      nres++;  
      WIDTH.push_back(w);         
      HEIGHT.push_back(h);                
      //fprintf(stderr, "RESOLUTION[%d]: %d x %d\n", nres, w, h);
    }  
  }

  int seqnr[1<<10]={0};
  int seqpics=0;
  for(int i=0;;++i) {
    if (i==pictures || p[i].getseqheader()) {
      for(int j=0;j<seqpics;++j) {
        if (seqnr[j]!=1) // this sequence-number did not appear exactly once
          {
          if (errorstring)
            *errorstring+=std::string("Invalid index file (")+filename+")\n";
          fprintf(stderr,"Invalid index file: sequence number %u appears %u times\n",
			  j, seqnr[j]);
          fprintf(stderr, "Picture %u/%u, %u seqpics\n", i, pictures, seqpics);
          free(p);
          p=0;
          pictures=0;
          realpictures=0;
          return -2;
          }
        seqnr[j]=0;
        }
      if (i==pictures)
        break;
      seqpics=0;
      }
    ++seqnr[p[i].getsequencenumber()];
    ++seqpics;
    }

  for(int i=1;i<pictures;i<<=1) {
    while(i<pictures && !p[i].getseqheader())
      ++i;
    if (i==pictures)
      break;
    streamhandle s(p[i].getpos().packetposition());
    streamdata *sd=s.newstream(VIDEOSTREAM,streamtype::mpeg2video,mpg.istransportstream());
    unsigned int po=p[i].getpos().packetoffset();
    while (sd->inbytes()<po+4)
      if (mpg.streamreader(s)<=0)
        break;
    if ( (sd->inbytes()<po+4) || (*(const uint32_t*)((const uint8_t*)sd->getdata()+po) != mbo32(0x000001b3)) ) {
    fprintf(stderr,"index does not match (%08x)\n",(*(const uint32_t*)((const uint8_t*)sd->getdata()+po)));
      free(p);
      p=0;
      pictures=0;
      realpictures=0;
      if (errorstring)
        *errorstring+=std::string("Index file (")+filename+") does not correspond to MPEG file\n";
      return -3;
      }
    }
  
  return check();
  }

int index::check()
  {
  int firstiframe;

  for(firstiframe=0;firstiframe<pictures;++firstiframe)
    if (p[firstiframe].isiframe())
      break;

  if (firstiframe>=pictures) {
    realpictures=0;
    skipfirst=0;
    return 0;
    }

  int sequencebegin=0;
  for (int i=firstiframe;i>0;--i)
    if (p[i].getseqheader()) {
      sequencebegin=i;
      break;
      }

  skipfirst=sequencebegin;
  if (p[firstiframe].getsequencenumber()>0) {
    int fifseqnr=p[firstiframe].getsequencenumber();

    for(int i=sequencebegin;(i<pictures)&&(!p[i].getseqheader()||i==sequencebegin);++i)
      if (p[i].getsequencenumber()<fifseqnr)
        ++skipfirst;
    }

  realpictures=pictures-skipfirst;
  if (realpictures<1)
    return 0;

  while (realpictures>0)
    if (p[indexnr(realpictures-1)].isbframe())
      --realpictures;
    else
      break;

  return realpictures;
  }

