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
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#include <list>
#include <utility>

#include "port.h"
#include "mpgfile.h"
#include "tsfile.h"
#include "psfile.h"
#include "index.h"
#include "avframe.h"
#include "streamhandle.h"
#include "playaudio.h"
#include "muxer.h"
#include "logoutput.h"

#include <stdio.h>

const int mpgfile::frameratescr[16]=
  {
    1080000,1126125,1125000,1080000,900900,900000,540000,450450,450000,
    1080000,1080000,1080000,1080000,1080000,1080000,1080000
  };

mpgfile::mpgfile(inbuffer &b, int initial_offset)
    : buf(b),
    videostreams(0),audiostreams(0),
    initialoffset(initial_offset),idx(*this),pictures(0)
{}

mpgfile::~mpgfile()
{}

/// Factory function
mpgfile*
mpgfile::open(inbuffer &b, std::string *errormessage) {
  if (errormessage)
    errormessage->clear();

  if (b.providedata(64 << 10) < (64 << 10)) {
    if (errormessage)
      *errormessage = "File too short";
    return 0;
  }

  int initialoffset;
  if ((initialoffset=tsfile::probe(b))>=0) // is this an mpeg transport stream?
    return new tsfile(b, initialoffset);
  if ((initialoffset=psfile::probe(b))>=0) // is this an mpeg program stream?
    return new psfile(b, initialoffset);

  if (errormessage)
    *errormessage = "Unknown file type";
  return 0;
}

void mpgfile::decodegop(int start, int stop, std::list<avframe*> &framelist)
{
  stream *S=&s[videostream()];
  if (!S->avcc)
    return;

  if (start<0)
    start=0;
  if (stop<0)
    stop=nextiframe(start+1)+1;
  else if (stop<=start)
    stop=start+1;
  if (stop>pictures)
    stop=pictures;

  if (stop<=start)
    return;
  int pic=lastseqheader(lastiframe(start));
  int streampic=idx.indexnr(pic);
  int seqnr=idx[streampic].getsequencenumber();

  streamhandle s(idx[streampic].getpos().packetposition());
  streamdata *sd=s.newstream(VIDEOSTREAM,streamtype::mpeg2video,istransportstream());

  if (idx[streampic].getpos().packetoffset()>0)
  {
    while( sd->inbytes()<idx[streampic].getpos().packetoffset() )
    {
      if (streamreader(s)<=0)
        return;
    }
    sd->discard(idx[streampic].getpos().packetoffset());
  }

  if (int rv=avcodec_open(S->avcc, S->dec))
  {
    fprintf(stderr,"avcodec_open returned %d\n",rv);
    return;
  }
  avframe avf;
  int last_cpn=-1;
  bool firstframe=true, firstsequence=true;

  while (pic<stop && streampic<idx.getpictures())
  {
    filepos_t tp(getfilesize(),0);
    if ((streampic+1)<idx.getpictures())
      tp=idx[streampic+1].getpos();
    while (sd->itemlist().empty() || s.fileposition<tp.fileposition())
      if (streamreader(s)<=0)
        break;

    int bytes=sd->inbytes();

    for(streamdata::itemlisttype::const_iterator it=sd->itemlist().begin();it!=sd->itemlist().end();++it)
      if (it->fileposition.packetposition()==tp.packetposition())
      {
        bytes=it->bufferposition-sd->getoffset()+tp.packetoffset()-it->fileposition.packetoffset();
        break;
      }
      else
        if (it->fileposition.packetposition()>tp.packetposition())
        {
          bytes=it->bufferposition-sd->getoffset();
          break;
        }

    if (!firstframe && idx[streampic].getseqheader())
      firstsequence=false;
    firstframe=false;

    if (!firstsequence || idx[streampic].getsequencenumber()>=seqnr)
    {
      const uint8_t *data=(const uint8_t*)sd->getdata();
      int frameFinished=0;

      int decodebytes=bytes;
      while (decodebytes>0)
      {
        frameFinished=0;
        int bytesDecoded=avcodec_decode_video(S->avcc, avf, &frameFinished,
                                              (uint8_t*) data, decodebytes);
        if (bytesDecoded<0)
        {
          fprintf(stderr,"libavcodec error while decoding frame #%d\n",pic);
          avcodec_close(S->avcc);
          return;
        }

        data+=bytesDecoded;
        decodebytes-=bytesDecoded;

        if (frameFinished)
        {
          if (last_cpn!=avf->coded_picture_number)
          {
            last_cpn=avf->coded_picture_number;
            if (pic>=start)
              framelist.push_back(new avframe(avf,S->avcc));
            ++pic;
            if (pic>=stop)
            {
              frameFinished=0;
              decodebytes=0;
              break;
            }
          }
          else
            frameFinished=0;
        }
      }
    }

    sd->discard(bytes);
    ++streampic;
  }

  if (pic < stop)
  {
    int frameFinished=0;
    avcodec_decode_video(S->avcc, avf, &frameFinished, NULL, 0);
    if (frameFinished)
    {
      if (last_cpn!=avf->coded_picture_number)
      {
        last_cpn=avf->coded_picture_number;
        if (pic>=start)
          framelist.push_back(new avframe(avf,S->avcc));
      }
    }
  }

  avcodec_close(S->avcc);
}

void mpgfile::initaudiocodeccontext(int aud)
{
  stream &S=s[audiostream(aud)];
  S.infostring="Audio ";

  {
    char number[16];
    snprintf(number,16,"%d",aud);
    S.infostring+=number;
  }

  switch (S.type)
  {
  case streamtype::mpegaudio:
    S.infostring+=" (MPEG)";
    break;
  case streamtype::ac3audio:
    S.infostring+=" (AC3)";
    break;
  default:
    S.infostring+=" (unknown)";
    break;
  }
}

#ifdef HAVE_LIB_AO
void mpgfile::playaudio(int aud, int picture, int ms)
{
  if (aud>=audiostreams || ms==0)
    return;

  pts_t startpts=idx[idx.indexnr(picture)].getpts();
  pts_t stoppts=startpts;

  if (ms<0)
    startpts+=ms*90;
  else
    stoppts+=ms*90;

  int seekpic=idx.indexnr(picture);
  while (seekpic>0 && idx[seekpic].getpts()>=startpts-180000)
    --seekpic;
  int stopreadpic=idx.indexnr(picture);
  while (stopreadpic<pictures-1 && idx[stopreadpic].getpts()<stoppts+180000)
    ++stopreadpic;
  dvbcut_off_t stopreadpos=idx[stopreadpic].getpos().packetposition();

  streamhandle sh(idx[seekpic].getpos().packetposition());
  streamdata *sd=sh.newstream(audiostream(aud),s[audiostream(aud)].type,istransportstream());

  while (sd->empty())
  {
    if (sh.fileposition > stopreadpos || streamreader(sh)<=0)
      return; // data does not reach the point in time from which we like to start playing
    while (!sd->empty() && !sd->itemlist().begin()->headerhaspts())
      sd->pop();
  }

  for(;;)
  {
    if (sh.fileposition > stopreadpos || streamreader(sh)<=0)
      return; // data does not reach the point in time from which we like to start playing
    if (sd->empty())
      continue;

    streamdata::itemlisttype::const_iterator it=sd->itemlist().begin();
    int pop=1;
    pts_t pts=AV_NOPTS_VALUE;
    for(++it;it!=sd->itemlist().end();++it,++pop)
      if (it->headerhaspts()) //if (streamdata::headerhaspts(it->header))
      {
        pts=it->headerpts(startpts);
        break;
      }
    if (pts==(pts_t)AV_NOPTS_VALUE)
      continue;
    if (pts<=startpts)
      sd->pop(pop);
    if (pts>=startpts)
      break;
  }

  while (streamreader(sh)>0)
  {
    streamdata::itemlisttype::const_reverse_iterator it=sd->itemlist().rbegin();
    while(it!=sd->itemlist().rend())
      if (it->headerhaspts())
        break;
      else
        --it;

    if (it==sd->itemlist().rend())
      continue;

    if (it->headerpts(stoppts)>stoppts)
      break;
  }

  sd->audio_addpts();

  uint32_t startbufferpos=sd->closestptsbufferpos(startpts);
  uint32_t stopbufferpos=sd->closestptsbufferpos(stoppts);

  if (stopbufferpos>startbufferpos)
  {
    const stream &S=s[audiostream(aud)];

    if (S.type==streamtype::ac3audio)
      playaudio_ac3(sd->getdata(startbufferpos),stopbufferpos-startbufferpos);
    else
      playaudio_mp2(sd->getdata(startbufferpos),stopbufferpos-startbufferpos);
  }
}
#else // HAVE_LIB_AO
void mpgfile::playaudio(int, int, int)
{
}
#endif // HAVE_LIB_AO

void mpgfile::savempg(muxer &mux, int start, int stop, int savedpics, int savepics, logoutput *log)
{
  if (start<0)
    start=0;
  if (start>pictures)
    start=pictures;
  if (stop<0)
    stop=0;
  if (stop>pictures)
    stop=pictures;
  if (start==stop)
    return;
  if (stop<start)
  {
    int x=start;
    start=stop;
    stop=x;
  }

  int seekpic=idx.indexnr(start);
  int framerate = mpgfile::frameratescr[idx[seekpic].getframerate()];
  bool fixedstart=true;

  if (mux.isempty())
  {
    fixedstart=false;
    mux.unsetempty();
    pts_t startpts = framerate / 300;
    for (int i=0;i<MAXAVSTREAMS;++i)
      mux.setpts(i,startpts);
  }

  pts_t videostartpts=idx[seekpic].getpts();
  pts_t videostoppts=idx[idx.indexnr(stop)].getpts();
  pts_t videooffset=videostartpts-mux.getpts(VIDEOSTREAM);
  pts_t audiopts[MAXAUDIOSTREAMS];
  pts_t audiostartpts[MAXAUDIOSTREAMS];
  pts_t audiooffset[MAXAUDIOSTREAMS];
  for(int a=0;a<MAXAUDIOSTREAMS;++a)
  {
    audiooffset[a]=videooffset;
    audiostartpts[a]=audiopts[a]=videostartpts;
  }
  pts_t shift=0;

  {
    dvbcut_off_t start_pos = idx[idx.indexnr(start)].getpos().fileposition();
    dvbcut_off_t stop_pos = idx[idx.indexnr(stop)].getpos().fileposition();
    dvbcut_off_t bytes = stop_pos - start_pos;
    pts_t delta_pts = (pts_t)(stop - start) * framerate / 300;
    double mux_rate = (double)bytes * 9e4 / (double)delta_pts;
    if (log)
      log->printinfo("Estimated mux rate: %.2f MB/s", mux_rate * 1e-6);
  }

  while (seekpic>0 && idx[seekpic].getpts()>=videostartpts-180000)
    --seekpic;

  dvbcut_off_t tpos;
  {
    int stoppic=idx.indexnr(start);
    while (stoppic<pictures &&
           (idx[stoppic].getpts()<videostartpts+180000 || !idx[stoppic].getseqheader()))
      ++stoppic;
    tpos=idx[stoppic].getpos().packetposition();
  }

  streamhandle sh(idx[seekpic].getpos().packetposition());
  streamdata *vsd=sh.newstream(VIDEOSTREAM,s[VIDEOSTREAM].type,istransportstream());
  for (int a=0;a<MAXAUDIOSTREAMS;++a)
    if (mux.streampresent(audiostream(a)))
      sh.newstream(audiostream(a),s[audiostream(a)].type,istransportstream());

  while (sh.fileposition<tpos)
    if (streamreader(sh)<0)
      break;

  for (int a=0;a<MAXAUDIOSTREAMS;++a)
    if (streamdata *sd=sh.stream[audiostream(a)])
    {
      pts_t tpts=videostartpts-mux.getpts(VIDEOSTREAM)+mux.getpts(audiostream(a));
      sd->audio_addpts();
      uint32_t startbufferpos=sd->ptsbufferpos(tpts);
      if (startbufferpos>sd->getoffset())
        sd->discard(startbufferpos-sd->getoffset());
      sd->audio_addpts(0,true);
      startbufferpos=sd->closestptsbufferpos(tpts);
      if (startbufferpos>sd->getoffset())
        sd->discard(startbufferpos-sd->getoffset());

      pts_t apts=sd->itemlist().front().headerpts(tpts);
      audiostartpts[a]=apts;
      if (apts>=0)
      {
        if (fixedstart)
          audiooffset[a]=videooffset-tpts+apts;
        else if (tpts-apts>shift)
          shift=tpts-apts;
      }

    }

  if (!fixedstart)
  {
    videooffset-=shift;
    for(int a=0;a<MAXAUDIOSTREAMS;++a)
      audiooffset[a]=videooffset;
  }

  pts_t audiostoppts[MAXAUDIOSTREAMS];
  for(int a=0;a<MAXAUDIOSTREAMS;++a)
    audiostoppts[a]=videostoppts-videooffset+audiooffset[a];

  int firstseqhdr=nextseqheader(start);
  {
    filepos_t copystart=idx[idx.indexnr(firstseqhdr)].getpos();
    vsd->discard(vsd->fileposbufferpos(copystart)-vsd->getoffset());
  }
  bool isfirstpic=true, isfirstseq=true;
  int firstseqnr=idx[idx.indexnr(firstseqhdr)].getsequencenumber();

  if (firstseqhdr>start)
  {
    recodevideo(mux,start,firstseqhdr,videooffset,savedpics,savepics,log);
    savedpics+=firstseqhdr-start;
  }

  int copystop=stop; // first picture not to write to stream
  while (copystop<pictures && idx[idx.indexnr(copystop)].isbframe())
    ++copystop;
  copystop=idx.indexnr(copystop);

  int streampic=idx.indexnr(firstseqhdr);

  while (!log || !log->cancelled())
  {
    int packetsread;
    for (packetsread=0;packetsread<20;++packetsread)
      if (streamreader(sh)<=0)
        break;
    if (packetsread==0)
      break;

    // copy video
    if (vsd)
      for(;;)
      {
        if (streampic>=copystop)
        {
          vsd=0;
          sh.delstream(VIDEOSTREAM);
          break;
        }

        uint32_t picsize=vsd->fileposbufferpos(idx[streampic+1].getpos())-vsd->getoffset();
        if (picsize>=vsd->inbytes())
          break;

        if (!isfirstpic && idx[streampic].getseqheader())
          isfirstseq=false;
        isfirstpic=false;

        int seqoff=0;
        if (!isfirstseq || idx[streampic].getsequencenumber()>=firstseqnr)
        {
          if (isfirstseq && firstseqnr>0) // need to subtract offset from picture sequence number
          {
            uint8_t *d=(uint8_t*) vsd->getdata();

            for (unsigned int j=0;j+5<picsize;)
            {
              if (d[2]&0xfe)
                j+=3;
              else
                if (*(u_int32_t*)&d[j]==mbo32(0x00000100))
                {
                  int seqpic=(d[j+4]<<2)|((d[j+5]>>6)&0x03);
                  seqpic-=firstseqnr;
                  d[j+4]=seqpic>>2;
                  d[j+5]=(d[j+5]&0x3f)|((seqpic<<6)&0xc0);
                  break;
                }
                else
                  ++j;
            }
            seqoff=firstseqnr;
          }
          pts_t vidpts=idx[streampic].getpts()-videooffset;
          pts_t viddts=vidpts;
          if (!idx[streampic].isbframe())
          {
            viddts=mux.getdts(VIDEOSTREAM);
            mux.setdts(VIDEOSTREAM,vidpts);
          }
          if (idx[streampic].getseqheader())
          {
            int tcpic=streampic;
            while (tcpic < copystop && idx[tcpic].getsequencenumber() != seqoff)
              ++tcpic;
            pts_t tcpts=idx[tcpic].getpts()-videooffset;
            fixtimecode((uint8_t*)vsd->getdata(),picsize,tcpts);
          }
          if (!mux.putpacket(VIDEOSTREAM,vsd->getdata(),picsize,vidpts,viddts,
                             idx[streampic].isiframe() ? MUXER_FLAG_KEY:0  ))
            if (log)
              log->printwarning("putpacket(streampic=%d) returned false",streampic);
            else
              fprintf(stderr,"WARN: putpacket(streampic=%d) returned false\n",streampic);
        }

        vsd->discard(picsize);
        ++streampic;

        if (log && savepics>0)
          log->setprogress(++savedpics*1000/savepics);
      }

    bool haveaudio=false;

    for (int a=0;a<MAXAUDIOSTREAMS;++a)
      if (streamdata * const sd=sh.stream[audiostream(a)])
      {
        bool stopped=false;
        sd->audio_addpts();
        streamdata::itemlisttype::const_iterator nx,it;

	while ((it = sd->itemlist().begin())!=sd->itemlist().end() &&
	       !it->is_frame())
	  sd->pop();
	if (it!=sd->itemlist().end())
        while(!stopped)
        {
          audiopts[a]=it->headerpts(audiopts[a]);
          if (audiopts[a]>=audiostoppts[a])
          {
            audiostoppts[a]=audiopts[a];
            stopped=true;
            break;
          }
          nx=it;
          ++nx;
          while (nx!=sd->itemlist().end() && !nx->is_frame())
            ++nx;
          if (nx==sd->itemlist().end())
            break;
          uint32_t bytes=nx->bufferposition-it->bufferposition;
          pts_t nxheaderpts=nx->headerpts(audiopts[a]);

          if (nxheaderpts>=audiostoppts[a])
          {
            if (nxheaderpts-audiostoppts[a]>audiostoppts[a]-audiopts[a])
            {
              bytes=0;
              audiostoppts[a]=audiopts[a];
            }
            else
              audiostoppts[a]=nxheaderpts;
            stopped=true;
          }

          if (nx->bufferposition<it->bufferposition)
          {

            for(it=sd->itemlist().begin();it!=sd->itemlist().end();++it)
              fprintf(stderr," fileposition:%lld/%d bufferposition:%d flags:%x pts:%s\n",
                      it->fileposition.packetposition(),it->fileposition.packetoffset(),
                      it->bufferposition,it->flags,ptsstring(it->pts).c_str());

            fprintf(stderr,"nx->bufferposition:%d it->bufferposition:%d\n",
                    nx->bufferposition,it->bufferposition);

            for(int i=0;i<MAXAVSTREAMS;++i)
              if (sh.stream[i])
                fprintf(stderr,"stream %d%s, itemlist.size():%d\n",
                        i,(sh.stream[i]==sd)?"*":"",sh.stream[i]->itemlist().size());

            abort();
          }

          if (bytes>0)
          {
            pts_t pts=audiopts[a]-audiooffset[a];
	    //fprintf(stderr, "mux.put audio %d %lld\n", bytes, pts);
            mux.putpacket(audiostream(a),sd->getdata(),bytes,pts,pts,MUXER_FLAG_KEY);

            sd->discard(bytes);
          }
          it=nx;
        }

        if (stopped)
          sh.delstream(audiostream(a));
        else
          haveaudio=true;
      }

    if (!vsd &&!haveaudio)
      break;
  }

  if ((stop>nextseqheader(start)) && idx[idx.indexnr(stop-1)].isbframe())
    // we didn't catch the last picture(s) yet
  {
    int startrecode=stop-1;
    while (startrecode>0 && idx[idx.indexnr(startrecode-1)].isbframe())
      --startrecode;
    recodevideo(mux,startrecode,stop,videooffset,savedpics,savepics,log);
  }

  // output info on audio stream timings
  if (log)
    for (int a=0;a<MAXAUDIOSTREAMS;++a)
      if (mux.streampresent(audiostream(a)))
      {
        float starts=float(audiostartpts[a]-videostartpts)/90.;
        float stops=float(audiostoppts[a]-videostoppts)/90.;
        float shift=float(audiooffset[a]-videooffset)/90.;
        log->printinfo("Audio channel %d: starts %.3f milliseconds %s video",
          a+1, fabsf(starts-shift), (starts>=shift) ? "after":"before");
        log->printinfo("Audio channel %d: stops %.3f milliseconds %s video",
          a+1, fabsf(stops-shift), (stops>=shift) ? "after":"before");
	log->printinfo("Audio channel %d: delayed %.3f milliseconds",
          a+1, shift);
	log->print("");
      }

  mux.setpts(VIDEOSTREAM, videostoppts-videooffset);
  for(int a=0;a<MAXAUDIOSTREAMS;++a)
    mux.setpts(audiostream(a), audiostoppts[a]-audiooffset[a]);
}

void mpgfile::recodevideo(muxer &mux, int start, int stop, pts_t offset,int savedpics,int savepics, logoutput *log)
{
  if (log)
    log->printinfo("Recoding %d pictures", stop-start);

  std::list<avframe*> framelist;
  decodegop(start,stop,framelist);

  AVCodecContext *avcc=s[VIDEOSTREAM].avcc;
  if (!avcc)
    return;
  s[VIDEOSTREAM].setvideoencodingparameters();

  if (int rv=avcodec_open(avcc, s[VIDEOSTREAM].enc))
  {
    if (log)
      log->printerror("avcodec_open(mpeg2video_encoder) returned %d",rv);
    return ;
  }

  buffer m2v(4<<20);

  int p=0;
  int outpicture=start;
  pts_t startpts=idx[idx.indexnr(start)].getpts();
  while (outpicture<stop)
  {
    u_int8_t *buf=(u_int8_t*)m2v.writeptr();
    int out;

    if (!framelist.empty())
    {
      avframe &f=*framelist.front();

      f->pts=idx[idx.indexnr(start+p)].getpts()-startpts;
      f->coded_picture_number=f->display_picture_number=p;
      f->key_frame=(p==0)?1:0;
      f->pict_type=(p==0)?FF_I_TYPE:FF_P_TYPE;

      out = avcodec_encode_video(avcc, buf,
                                 m2v.getsize(), f);

      delete framelist.front();
      framelist.pop_front();
      ++p;

      if (out<=0)
        continue;
    }
    else
    {
      fprintf(stderr,"trying to call avcodec_encode_video with frame=0\n");
      out = avcodec_encode_video(avcc, buf,
                                 m2v.getsize(), 0);
      fprintf(stderr,"...back I am.\n");

      if (out<=0)
        break;
    }

    pts_t vidpts=idx[idx.indexnr(outpicture)].getpts()-offset;
    pts_t viddts=mux.getdts(VIDEOSTREAM);
    mux.setdts(VIDEOSTREAM,vidpts);
    fixtimecode(buf,out,vidpts);
    mux.putpacket(VIDEOSTREAM,buf,out,vidpts,viddts,
                  (avcc->coded_frame && avcc->coded_frame->key_frame)?MUXER_FLAG_KEY:0 );
    ++outpicture;

    if (log && savepics>0)
      log->setprogress(++savedpics*1000/savepics);
  }

  for(std::list<avframe*>::iterator fit=framelist.begin();fit!=framelist.end();++fit)
    delete *fit;
  avcodec_close(avcc);
}

void mpgfile::fixtimecode(uint8_t *buf, int len, pts_t pts)
{
  int frc=-1;
  int i=0;
  for (;;)
  {
    if (i+8>len)
      return;
    else if (buf[i+2]&0xfe)
      i+=3;
    else if (buf[i]!=0 || buf[i+1]!=0 || buf[i+2]!=1)
      i+=1;
    else if (buf[i+3]==0xb3)
    {	// sequence header
      frc=buf[i+7]&0x0f;
      i+=12;
    }
    else if (buf[i+3]==0xb8)	// GOP header
      break;
    else
      i+=4;
  }
  buf+=i;
  buf[4]=0x00;
  buf[5]=0x00;
  buf[6]=0x08;
  buf[7]&=0x7f;
  if (frc==-1)
    return;
  if (frc==1 || frc==4 || frc==7)
    ++frc;	// use nearest integer
  int framerate=27000000/frameratescr[frc];
  int ss=pts/90000;
  int mm=ss/60; ss %= 60;
  int hh=mm/60; mm %= 60;
  int pp=pts%90000;
  pp=(pp*framerate)/90000;
  buf[4] = (hh<<2) & 0x7c | (mm>>4) & 0x03;
  buf[5] = (mm<<4) & 0xf0 | (ss>>3) & 0x07 | 0x08;
  buf[6] = (ss<<5) & 0xe0 | (pp>>1) & 0x1f;
  buf[7] |= (pp<<7) & 0x80;
}
