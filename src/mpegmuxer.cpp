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
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include "mpegmuxer.h"

#ifndef O_BINARY
#define O_BINARY    0
#endif /* O_BINARY */

/// video bitrate in bit/s
#define VIDEOBITRATE (9500000)
/// vbv buffer size in units of 1024 bytes
#define VBVBUFFERSIZE (224)

struct streamlistsort
  {
  bool operator()(mpegmuxer::stream *a, mpegmuxer::stream *b)
    {
    return *a<*b;
    }
  };

struct systemhdr_s
  {
  uint32_t system_header_start_code;
  uint16_t header_length;
  // unsigned marker_bit1 : 1;
  // unsigned rate_bound : 22;
  // unsigned marker_bit2 : 1;
  // unsigned audio_bound : 6;
  // unsigned fixed_flag : 1;
  // unsigned CSPS_flag : 1;
  uint32_t rate_etc;
  // unsigned system_audio_lock_flag : 1;
  // unsigned system_video_lock_flag : 1;
  // unsigned marker_bit3 : 1;
  // unsigned video_bound : 5;
  uint8_t video_bound_etc;
  // unsigned packet_rate_restriction_flag : 1;
  // unsigned reserved_byte : 7;
  uint8_t reserved_byte;
  uint8_t stream_id1;
  uint16_t stream1;
  uint8_t stream_id2;
  uint16_t stream2;
  uint8_t stream_id3;
  uint16_t stream3;
  uint8_t stream_id4;
  uint16_t stream4;
  }
__attribute__((packed));


// **************************************************************************
// ***  mpegmuxer

mpegmuxer::mpegmuxer(uint32_t audiostreammask, mpgfile &mpg, const char *filename, bool dvd,
                     int packsize_bytes, int muxrate_bitsps) :
    fd(-1), st(), muxrate(muxrate_bitsps/400), packsize(packsize_bytes), ptsoffset(0), aucounter(0),
    systemhdr(0), systemhdrlen(0), pespacket_setlength(true),scr(0)
  {
  if (packsize<MINPACKSIZE)
    packsize=0;
  scrpack=int(27.e6/double(muxrate*50)*packsize+0.9999);

  st[VIDEOSTREAM]=new stream(streamtype::mpeg2video,0xe0,232<<10,232<<10,true);
  strpres[VIDEOSTREAM]=true;

  int audiobuffersize=dvd?(4<<10):(48<<10);
  for (int i=0;i<mpg.getaudiostreams();++i)
    if (audiostreammask & (1u<<i)) {
      if (mpg.getstreamtype(audiostream(i))==streamtype::ac3audio)
        st[audiostream(i)]=new stream(mpg.getstreamtype(audiostream(i)),
                                      0x180+i,
                                      audiobuffersize,58<<10,true);
      else
        st[audiostream(i)]=new stream(mpg.getstreamtype(audiostream(i)),
                                      0xc0+i,
                                      audiobuffersize,audiobuffersize,false);
      strpres[audiostream(i)]=true;
      }

  systemhdrlen=dvd ? 2034 : 24; // include DVD navigation packets if dvd set
  systemhdr=malloc(systemhdrlen);
  bzero(systemhdr,systemhdrlen);

  *(systemhdr_s *)systemhdr = (systemhdr_s) {
                                mbo32(0x000001bb),mbo16(18),
                                //1,muxrate,1,mpg.getaudiostreams(),0,0,
                                htom32(0x80000100 | ((muxrate&0x3fffff)<<9) | (mpg.getaudiostreams()<<2)),
                                //1,1,1,1,
                                0xe1,
                                //0,0x7f,
                                0x7f,
                                0xb9,mbo16(0xe000|232),0xb8,mbo16(0xc000|32),
                                0xbd,mbo16(0xe000|58),0xbf,mbo16(0xe000|2)
                                };

  if (dvd) { // dvd nav packets
    *(uint32_t*)((char*)systemhdr+24)=mbo32(0x000001bf);
    *(uint16_t*)((char*)systemhdr+28)=mbo16(980);
    *(uint32_t*)((char*)systemhdr+1010)=mbo32(0x000001bf);
    *(uint16_t*)((char*)systemhdr+1014)=mbo16(1018);
    }

  // total size of all buffers: 232kB video + 4kB per mpeg audio stream
  double allbuffers(double((232<<10)+mpg.getaudiostreams()*(4<<10)));
  allbuffers*=1.05; // 5% assumed muxing overhead
  ptsoffset=pts_t(90000.*(allbuffers/50./muxrate))+90;

  fd=::open(filename,O_WRONLY|O_CREAT|O_TRUNC|O_BINARY,0666);
  }

mpegmuxer::~mpegmuxer()
  {
  if (fd>=0) {
    flush(true);
    ::close(fd);
    }

  for(int i=0;i<MAXAVSTREAMS;++i)
    if (st[i])
      delete st[i];

  if (systemhdr)
    free(systemhdr);
  }

bool mpegmuxer::putpacket(int str, const void *data, int len, pts_t pts, pts_t dts, uint32_t flags)
  {
  stream * const s=st[str];
  if (!s)
    return false;
  if (len == 0) {
	// I'm not sure why this happens, but it does. --mr
	fprintf(stderr, "mpegmuxer::putpacket called with zero length, str=%d\n", str);
	return false;
  }
  pts+=ptsoffset;
  dts+=ptsoffset;
  au *newau=new au(data,len,pts,dts,flags);

  if (s->type==streamtype::mpeg2video) {
    uint8_t *audata=(uint8_t*) newau->getdata();

    //     if (0) //
    for (int j=0;j+11<len;) {
      uint8_t *d=audata+j;
      if (d[2]&0xfe)
        j+=3;
      else
        if (((*(u_int32_t*)&d[0])&mbo32(0xffffff00))==mbo32(0x00000100)) {
          if (d[3]==0x00) // picture header
            {
            // vbv delay := 0xffff
            d[5]|=0x07;
            d[6]=0xff;
            d[7]|=0xf8;
            break;
            } else if (d[3]==0xb3) // sequence header
            {
            d[8]=(VIDEOBITRATE/400) >> 10;
            d[9]=((VIDEOBITRATE/400) >> 2) & 0xff;
            d[10]=(((VIDEOBITRATE/400) << 6)&0xc0)|0x20|(((VBVBUFFERSIZE/2)>>5)&0x1f);
            d[11]=(d[11]&0x07)|((VBVBUFFERSIZE/2)<<3);
            j+=12;
            } else if (d[3]==0xb5) // extension
            {
            if ((d[4]&0xf0) == 0x10) // sequence extension
              {
              // set bitrate_extension and vbv_buffer_size_extension to 0
              d[6]&=0xe0;
              d[7]=0x01;
              d[8]=0;
              j+=10;
              } else
              j+=5;
            } else
            j+=4;
          } else
          ++j;
      }
    }

  s->aulist.push_back(newau);
  s->bufferremovals.push_back(bufferremoval(pts2scr(dts),len));

  if (++aucounter>=300) {
    aucounter=0;
    return flush(false);
    }

  return true;
  }

bool mpegmuxer::flush(bool flushall)
  {
  const pts_t dts_safetymargin=2*90000; // 2 seconds
  pts_t maxdts=-1;

  if (!flushall) {
    maxdts=0;
    for(int i=0;i<MAXAVSTREAMS;++i)
      if (st[i] && !st[i]->aulist.empty() && st[i]->aulist.back()->getdts()>maxdts )
        maxdts=st[i]->aulist.back()->getdts();

    // maxdts is the maximal DTS in all access units available by now
    // we subtract a safety margin from that value
    maxdts-=dts_safetymargin;
    // maxdts is now the max DTS value up to which we process access units

    // we want to have at least one second worth of material
    if (maxdts<90000)
      return true;
    }

  packetizer(VIDEOSTREAM,maxdts);

  for(int i=audiostream(0);i<=audiostream(MAXAUDIOSTREAMS-1);++i)
    if (st[i])
      packetizer(i,maxdts);

  for(int i=0;i<MAXAVSTREAMS;++i) {
    stream * const s=st[i];
    if (!s || st[i]->packlist.empty() )
      continue;

    scr_t maxscr=s->packlist.back()->getmaxscr();
    std::list<pack*>::reverse_iterator it=s->packlist.rbegin();
    for(++it;it!=s->packlist.rend();++it) {
      maxscr-=packsize?scrpack:int(27.e6/double(muxrate*50)*(*it)->getsize()+0.9999);
      (*it)->setmaxscr(maxscr);
      maxscr=(*it)->getmaxscr();
      }
    }

  // access units have been put into packs, fine.
  // now we multiplex these packs into a program stream. Again, we
  // have a safety margin (one second this time).
  scr_t stopscr=maxdts<0?-1:pts2scr(maxdts-90000);
  std::list<stream*> streamlist;

  for(int i=0;i<MAXAVSTREAMS;++i)
    if (st[i] && !st[i]->packlist.empty()) // && st[i]->packlist.front()->getmaxscr()<stopscr)
      streamlist.push_back(st[i]);

  streamlist.sort(streamlistsort());

  while (!streamlist.empty() &&
         (*(streamlist.front())<stopscr || (stopscr<0 && !streamlist.front()->packlist.empty()))) {
    std::list<stream*>::iterator it=streamlist.begin();
    std::list<stream*>::iterator minscrit=streamlist.end();
    scr_t minscr=streamlist.front()->packlist.front()->getminscr();

    for(;it!=streamlist.end() && !(*it)->packlist.empty();++it)
      if ((*it)->packlist.front()->getminscr()<=scr)
        break;
      else if ((*it)->packlist.front()->getminscr()<minscr) {
        minscr=(*it)->packlist.front()->getminscr();
        minscrit=it;
        }

    if ((it==streamlist.end()) || ((*it)->packlist.empty())) {
      while (minscr-scr>13500000) {
        pack p(packsize,0,muxrate,0);
        scr+=13500000;
        p.setscr(scr);
        ::write(fd,p.getdata(),p.getsize());
        }

      scr=minscr;
      if (minscrit!=streamlist.end())
        it=minscrit;
      else
        continue;
      }

    stream *s=*it;
    assert(!s->packlist.empty());
    pack *p=s->packlist.front();

    if (s->getfill()<0) {
      fprintf(stderr,"stream %d filllevel: %d\n",s->getid(),s->getfill());
      }

    if (!s->bufferremovals.empty()) {
      if (s->bufferremovals.back().scr()<=scr) {
        s->zerofill();
        s->bufferremovals.clear();
        } else
        while (!s->bufferremovals.empty()) {
          bufferremoval &r=s->bufferremovals.front();
          if (r.scr()>scr)
            break;
          s->unfill(r.bytes());
          s->bufferremovals.pop_front();
          }


      // Buffer full?
      if (p->getaupayloadlen()>s->getbuffree()) {

        int freespace=s->getbuffree();

        //scr_t minscr=p->getminscr();

        for(std::list<bufferremoval>::iterator it=s->
            bufferremovals.begin();
            it!=s->bufferremovals.end();
            ++it) {
          scr_t stepminscr=it->scr()+2700; //-scr_t(27.e6*freespace/double(muxrate*50));

          p->setminscr(stepminscr);
          freespace+=it->bytes();
          if (freespace>=p->getaupayloadlen())
            break;
          }

        if (p->getminscr()
            >scr)
          continue;
        }
      }

    p->setscr(scr);
    scr+=packsize?scrpack:int(27.e6/double(muxrate*50)*p->getsize()+0.9999);
    if (scr>p->getmaxscr())
      fprintf(stderr,"Muxer problem: %s > %s (dts:%s) s->getbuffree():%d\n",
              ptsstring(scr2pts(scr)).c_str(),
              ptsstring(scr2pts(p->getmaxscr())).c_str(),
              ptsstring(p->getdts()).c_str(),
              s->getbuffree() );
    ::write(fd,p->getdata(),p->getsize());
    if (p->getaupayloadlen()>0) {
      s->fill(p->getaupayloadlen());
      }
    s->packlist.pop_front();
    delete p;

    streamlist.erase(it);
    for(it=streamlist.begin();it!=streamlist.end();++it)
      if (*s<**it)
        break;
    streamlist.insert(it,s);
    }

  return true;
  }

void mpegmuxer::packetizer(int str,pts_t maxdts)
  {
  bool const video=(str==VIDEOSTREAM);
  stream * const s=st[str];

  while (!s->aulist.empty() && (maxdts<0 || s->aulist.front()->getdts()<maxdts)) {
    au *a=s->aulist.front();
    bool headerpts(false), headerdts(false), headerext(false);

    if (video) {
      if (!a->incomplete() && (a->getflags()&MUXER_FLAG_KEY)) // key frame
        {
        const void *data=systemhdr;
        int slen=systemhdrlen;
        while (slen>0)
          {
          pack * const p=new pack(packsize, slen, muxrate, a->getdts());
          memcpy(p->getpayload(),data,p->getpayloadlen());
          slen-=p->getpayloadlen();
          data=(const void*)((const char*)data+p->getpayloadlen());
          s->packlist.push_back(p);
          p->nopayload();
          }
        headerpts=true;
        if (a->getdts() != a->getpts())
          headerdts=true;
        headerext=true;
        }
      } else if (s->type==streamtype::mpegaudio) { // audio
      if ((!a->incomplete())||(++s->aulist.begin()!=s->aulist.end()))
        headerpts=true;
      if (!a->incomplete())
        headerext=true;
      } else if (s->type==streamtype::ac3audio) { // audio
      if ((!a->incomplete())||(++s->aulist.begin()!=s->aulist.end()))
        headerpts=true;
      headerext=((s->packet%1000)==0);
      }

    int headerlen=0;
    if (headerpts)
      headerlen+=5;
    if (headerdts)
      headerlen+=5;
    if (headerext)
      headerlen+=3;
    bool isprivatestream(s->id>=0x100 && s->id<0x300);
    if (isprivatestream)
      ++headerlen;
    if (s->type==streamtype::ac3audio)
      headerlen+=3;
    int len=9+headerlen+a->getsize();
    if (pespacket_setlength)
      len+=9*((len-10)/65532);

    if (!video || packsize) {
      int maxsize=9+headerlen+s->getbufsize()*3/4;
      if (pespacket_setlength)
        maxsize+=9*((maxsize-10)/65532);
      if (packsize && maxsize>pack::maxpayload(packsize))
        maxsize=pack::maxpayload(packsize);
      if (pespacket_setlength && maxsize>65541 && maxsize%65541<=9)
        maxsize-=maxsize%65541;

      std::list<au*>::iterator it=s->aulist.begin();
      ++it;

      while (it!=s->aulist.end() && len<maxsize-16) { // -16
        au * const aa=*it;
        ++it;
        // don't start next sequence within this pack
        if (video&&(aa->getflags()&MUXER_FLAG_KEY))
          break;
        // no DTS in this packet shall be 0.7s or more after the first DTS
        if (aa->getdts()-a->getdts()>=63000)
          break;
        int newlen;
        if (pespacket_setlength) {
          newlen=len-9*((len-10)/65541)+aa->getsize();
          newlen+=9*((newlen-10)/65532);
          } else
          newlen=len+aa->getsize();
        if (s->type==streamtype::mpegaudio && (newlen>maxsize) && (len>maxsize-50))
          break;
        len=newlen;
        }

      if (packsize && len>pack::maxpayload(packsize))
        len=pack::maxpayload(packsize);
      }
    //        else {
    //       int maxsize=9+headerlen+s->getbufsize()*3/4;
    //       if (packsize && maxsize>pack::maxpayload(packsize))
    //         maxsize=pack::maxpayload(packsize);
    //       std::list<au*>::iterator it=s->aulist.begin();
    //       ++it;
    //
    //       while (it!=s->aulist.end()) {
    //         au * const aa=*it;
    //         // no DTS in this packet shall be 0.7s or more after the first DTS
    //         if (aa->getdts()-a->getdts()>=63000)
    //           break;
    //         if (len+aa->getsize()>maxsize)
    //           break;
    //           if (pespacket_setlength) {
    //             len=len-9*((len-6)/65545)+aa->getsize();
    //             len+=9*((len-6)/65536);
    //             } else
    //             len+=aa->getsize();
    //         }
    //       }


    int pes_padding = 0;
    if (packsize) {
      int maxpayload = pack::maxpayload(packsize);
      assert(len <= maxpayload);
      if (maxpayload - len < 8)
        pes_padding = maxpayload - len;
      headerlen += pes_padding;
      len += pes_padding;
      }

    pack * const p=new pack(packsize,len,muxrate,a->getdts());
    s->packlist.push_back(p);
    ++s->packet;
    len=p->getpayloadlen();
    char *data=(char*)p->getpayload();

    while (a && (len>9+headerlen)) {
      int plen=len-6;
      if (pespacket_setlength && plen>65535)
        plen=65535;

      *(uint32_t*)data=s->getstartcode();
      data+=4;
      *(uint16_t*)data=pespacket_setlength ? htom16(plen) : 0;
      data+=2;
      len-=6;

      //       *(data++)=((!a->incomplete())&&(s->type!=streamtype::mpeg2video || (a->getflags()&MUXER_FLAG_KEY)))?0x85:0x81;
      *(data++)=a->incomplete()?0x81:0x85;
      *(data++)=(headerpts?0x80:0)|(headerdts?0x40:0)|(headerext?0x01:0);
      int officialheaderlen=headerlen;
      if (isprivatestream)
        --officialheaderlen;
      if (s->type==streamtype::ac3audio)
        officialheaderlen-=3;
      *(data++)=officialheaderlen;
      len-=3;
      plen-=3;

      if (headerpts) {
        pts_t pts=a->getpts();
        if (a->incomplete()) {
          std::list<au*>::iterator it=++s->aulist.begin();
          if (it!=s->aulist.end())
            pts=(*it)->getpts();
          }
        uint32_t pts32=pts&0xffffffff;
        *(data++)=(headerdts?0x31:0x21)|(uint32_t(pts>>29)&0x0e);
        *(data++)=pts32>>22;
        *(data++)=(pts32>>14)|1;
        *(data++)=pts32>>7;
        *(data++)=(pts32<<1)|1;
        plen-=5;
        len-=5;
        headerpts=false;
        headerlen-=5;
        }
      if (headerdts) {
        pts_t dts=a->getdts();
        uint32_t dts32=dts&0xffffffff;
        *(data++)=0x11|(uint32_t(dts>>29)&0x0e);
        *(data++)=dts32>>22;
        *(data++)=(dts32>>14)|1;
        *(data++)=dts32>>7;
        *(data++)=(dts32<<1)|1;
        plen-=5;
        len-=5;
        headerdts=false;
        headerlen-=5;
        }
      if (headerext) {
        *(data++)=0x10; // P-STD_buffer_flag
        *(uint16_t*)data=s->getpstdbuffer();
        data+=2;
        plen-=3;
        len-=3;
        headerext=false;
        headerlen-=3;
        }
      if (pes_padding > 0) {
        memset(data, 0xff, pes_padding);
        data += pes_padding;
        headerlen -= pes_padding;
        plen -= pes_padding;
        len -= pes_padding;
        pes_padding = 0;
        }
      if (isprivatestream) {
        *(data++)=s->id & 0xff;
        --plen;
        --len;
        }

      uint8_t *framestarts=0;
      uint16_t *framestartoffset=0;
      if (s->type==streamtype::ac3audio) {
        framestarts=(uint8_t*)data;
        *(data++)=0;
        framestartoffset=(uint16_t*)data;
        *(data++)=0;
        *(data++)=0;
        plen-=3;
        len-=3;
        }

      while (plen>0) {
        int copy=a->getsize();
        if (copy>plen)
          copy=plen;

        if (framestarts && !a->incomplete()) {
          if (*framestarts==0 && framestartoffset)
            *framestartoffset=htom16((data-(char*)framestartoffset)-1);
          ++*framestarts;
          }
        memcpy(data,a->getdata(),copy);
        data+=copy;
        len-=copy;
        plen-=copy;
        a->addpos(copy);
        p->addaupayload(copy);

        if (video)
          p->setmaxscr(pts2scr(a->getdts()));

        if (a->getsize()==0) {
          s->aulist.pop_front();
          delete a;
          if (s->aulist.empty()) {
            a=0;
            break;
            }
          a=s->aulist.front();
          p->setlastdts(a->getdts());
          }
        }
      }

    if (len) {
      fprintf(stderr,"str=%d len=%d aulist.size=%d packlist.size=%d\n",
	str,len,s->aulist.size(),s->packlist.size());
      assert(len==0);
      }
    }
  }


// **************************************************************************
// ***  mpegmuxer::au (access units)

mpegmuxer::au::au(const void *_data,int _size, pts_t _pts, pts_t _dts, int _flags) :
    data(0), size(_size), pts(_pts), dts(_dts), flags(_flags), pos(0)
  {
  data=malloc(size);
  memcpy(data,_data,size);
  }
mpegmuxer::au::~au()
  {
  if (data)
    free(data);
  }

// **************************************************************************
// ***  mpegmuxer::pack

mpegmuxer::pack::pack(int packsize, int payloadsize, int muxrate, pts_t _dts) :
    data(0), size(packsize), minscr(pts2scr(_dts-DTSMAXDELAY)), maxscr(pts2scr(_dts)),
    dts(_dts),
    payloadpos(14),payloadlen(payloadsize),aupayloadlen(0)
  {
  if (size<MINPACKSIZE) // considered as unspecified packsize
    {
    if (payloadlen<0)
      payloadlen=0;
    size=payloadlen+14; // big enough to host payload
    } else // fixed packsize
    if (payloadlen>size-14)
      payloadlen=size-14;
    else
      if (payloadlen<0)
        payloadlen=0;

  if (size-payloadlen<21)
    payloadpos=size-payloadlen;
  else
    payloadpos=14;

  data=malloc(size);

  *(uint32_t*)data=mbo32(0x000001ba); // pack_start_code
  *(uint32_t*)((char*)data+10)=htom32((muxrate<<10)|0x00000300|(payloadpos-14)); // program_mux_rate and pack_stuffing_length
  for(int i=14;i<payloadpos;++i)
    ((unsigned char*)data)[i]=0xff;

  int pos=payloadpos+payloadlen;
  while (pos+6<=size) // add padding packet
    {
    void *pad=(void*)((char*)data+pos);
    int padlen=size-pos-6;
    if (padlen>65535)
      {
      if (padlen>=65541)
        padlen=65535;
      else
        padlen-=6;
      }
    *(uint32_t*)pad=mbo32(0x000001be);
    *(uint16_t*)((char*)pad+4)=htom16(padlen);
    memset((char*)pad+6,0xff,padlen);
    pos+=padlen+6;
    }
  while (pos<size) // should never happen
    ((unsigned char*)data)[pos++]=0xff;
  }

mpegmuxer::pack::~pack()
  {
  if (data)
    free(data);
  }

void mpegmuxer::pack::setscr(scr_t scr)
  {
  if (!data || size<10)
    return;
  uint8_t *d=(uint8_t*)data+4;
  uint64_t scrb=scr/300;
  uint32_t scrx=scr%300;
  uint32_t scrb32=scrb&0xffffffff;

  d[0]=(uint32_t(scrb>>27)&0x38)|((scrb32>>28)&0x03)|0x44;
  d[1]=scrb32>>20;
  d[2]=((scrb32>>12)&0xf8)|0x04|((scrb32>>13)&0x03);
  d[3]=scrb32>>5;
  d[4]=((scrb32<<3)&0xf8)|0x04|((scrx>>7)&0x03);
  d[5]=scrx<<1|0x01;
  }
