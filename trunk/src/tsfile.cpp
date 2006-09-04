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

#include "tsfile.h"
#include "streamhandle.h"
#include <list>
#include <utility>

#include <ffmpeg/avcodec.h>

tsfile::tsfile(const std::string &filename, inbuffer &b, int initial_offset)
    : mpgfile(filename, b, initial_offset)
  {
  int vid=-1;
  for(unsigned int i=0;i<8192;++i)
    streamnumber[i]=-1;

  int inpackets=buf.providedata(buf.getsize(),initialoffset)/TSPACKETSIZE;
  if (inpackets>40000) inpackets=40000;
  
  // Find video PID and audio PID(s)
  bool apid[8192]={};
  std::list<std::pair<int,int> > audios;
  for(int i=0;i<inpackets;++i) {
    const tspacket &p=((const tspacket*)buf.data())[i];
    int sid=p.sid();
    if (sid<0)
      continue;
    //     if ((sid==0xbd || sid==0xbf)&&p.payload_length()>9) // private stream
    //       {
    //       const uint8_t *payload=(const uint8_t*) p.payload();
    //       if (p.payload_length()>9+payload[8])
    //         sid=payload[9+payload[8]] | ((sid==0xbd)?0x100:0x200);
    //       }

    int pid=p.pid();
    if (((sid&0xe0) == 0xc0)&&(apid[pid]==false)) // mpeg audio stream
      {
      audios.push_back(std::pair<int,int>(sid,pid));
      apid[pid]=true;
      } else if ((sid==0xbd) && (apid[pid]==false)) // private stream 1, possibly AC3 audio stream
      {
      const uint8_t *payload=(const uint8_t*) p.payload();
      //       if (p.payload_length()>9+payload[8])
      //         sid=payload[9+payload[8]] | ((sid==0xbd)?0x100:0x200);
      if ((p.payload_length()>=11) && (p.payload_length()>9+payload[8]) &&
          (*(uint16_t*)&payload[9+payload[8]]==mbo16(0xb77)) )
        {
        audios.push_back(std::pair<int,int>(sid,pid));
        apid[pid]=true;
        }
      } else if (vid<0 && ((sid&0xf0)==0xe0)) // mpeg video stream
      vid=pid;
    }
  audios.sort();
  for (std::list<std::pair<int,int> >::iterator it=audios.begin();it!=audios.end();++it) {
    streamnumber[it->second]=audiostream(audiostreams);
    stream *S=&s[audiostream(audiostreams++)];
    S->id=it->second;
    if (it->first==0xbd) {
      S->dec=&ac3_decoder;
      S->enc=&ac3_encoder;
      S->type=streamtype::ac3audio;
      } else {
      S->dec=&mp2_decoder;
      S->enc=&mp2_encoder;
      S->type=streamtype::mpegaudio;
      }
    if (audiostreams>=MAXAUDIOSTREAMS)
      break;
    }

  if (vid>=0) {
    videostreams=1;
    streamnumber[vid]=VIDEOSTREAM;
    stream *S=&s[VIDEOSTREAM];
    S->id=vid;
    S->allocavcc();
    S->avcc->codec_type=CODEC_TYPE_VIDEO;
    S->avcc->codec_id=CODEC_ID_MPEG2VIDEO;
    S->dec=&mpeg2video_decoder;
    S->enc=&mpeg2video_encoder;
    S->type=streamtype::mpeg2video;
    }

  for (int i=0;i<audiostreams;++i)
    initaudiocodeccontext(i);
  }

tsfile::~tsfile()
  {}

int tsfile::streamreader(streamhandle &s)
  {
  int returnvalue=0;
  bool lostsync(false);

  for(;;) {
    off_t packetpos=s.fileposition;
      {
      int pd=buf.providedata(TSPACKETSIZE,packetpos);
      if (pd<0)
        return pd;
      if (pd<TSPACKETSIZE)
        return returnvalue;
      }

    const tspacket *p=(const tspacket*) buf.data();

    if ((p->data[0]!=TSSYNCBYTE)||lostsync) // we lost sync
      {

        {
        int pd=buf.providedata(4<<10,packetpos);
        if (pd<(4<<10))
          {
          return returnvalue; // end of file, probably
          }
        }

      lostsync=true;
      const uint8_t *data = (const uint8_t*) buf.data();
      int ts;
      for (ts = 0; ts < 2048; ++ts)
        {
        int pos;
        for (pos = ts;pos < 4096;pos += TSPACKETSIZE)
          if (data[ pos ] != TSSYNCBYTE)
            break;
        if (pos >= 4096) // from here, we are in sync again
          { lostsync=false;
          break;
          }
        }

      s.fileposition+=ts;
      continue;
      }

    s.fileposition+=TSPACKETSIZE;

    // Abandon packets which have no payload or have invalid adaption field length
    if (p->payload_length()<=0)
      continue;
    int sn=streamnumber[p->pid()];
    if (sn<0)
      continue;
    streamdata *sd=s.stream[sn];
    if (!sd)
      continue;

    if (p->payload_unit_start_indicator()) {
      sd->header=std::string((const char *)p->payload(),p->payload_length());
      sd->nextfilepos=packetpos;
      } else if (!sd->header.empty())
      sd->header+=std::string((const char *)p->payload(),p->payload_length());
    else {
      if (!sd->itemlist().empty()) {
        returnvalue += p->payload_length();
        sd->append(p->payload(),p->payload_length());
        }
      continue;
      }

    if (sd->header.size()<9)
      continue;

    if (sd->header[0]!=0 || sd->header[1]!=0 || sd->header[2]!=1) {
      sd->header.clear();
      continue;
      }
    int sid=(uint8_t)sd->header[3];

    unsigned int payloadbegin=9u+(uint8_t)sd->header[8];

    if (sd->header.size()<payloadbegin)
      continue;
    //     if (sid==0xbd || sid==0xbf)
    //       {
    //       if (sd->header.size()<payloadbegin+1) continue;
    //         sid=((sid==0xbd)?0x100:0x200)|((unsigned char) sd->header[payloadbegin]);
    //         ++payloadbegin;
    //       }
    //     else
      {
      if (sid>=0xe0 && sid<0xf0 && sd->header.size()>=payloadbegin+4)
        if ( *(uint32_t*)(sd->header.c_str()+payloadbegin)==mbo32(0x00000001) )
          ++payloadbegin;
      }

    sd->appenditem(filepos_t(sd->nextfilepos,0), std::string(sd->header,6,payloadbegin-6),
                   sd->header.c_str()+payloadbegin, sd->header.size()-payloadbegin);
    returnvalue += sd->header.size()-payloadbegin;
    sd->header.clear();
    return returnvalue;
    }
  }

/// This function probes the data in the given inbuffer for an mpeg transport stream.
/// If it finds a syncbyte (ascii 'G') within the first 8kB which is then followed by
/// another syncbyte every 188 bytes (ts packet size) in the next 2kB, then
/// this data is assumed to be a transport stream.
/// It returns the buffer offset at which the transport stream starts, or -1 if
/// no transport stream was identified.
int tsfile::probe(inbuffer &buf)
  {
  int latestsync=buf.inbytes()-2048;
  if (latestsync>(8<<10))
    latestsync=8<<10;

  // Find TS syncbyte 'G' within the first 2048 bytes?
  const uint8_t *data = (const uint8_t*) buf.data();
  int ts;
  for (ts = 0; ts < latestsync; ++ts) {
    int testupto=ts+2048;
    int pos;
    for (pos = ts;pos < testupto;pos += TSPACKETSIZE)
      if (data[ pos ] != TSSYNCBYTE)
        break;
    if (pos >= testupto) // this is a MPEG TS file
      return ts;
    }
  return -1;
  }
