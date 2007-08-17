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

#include "psfile.h"
#include "streamhandle.h"
#include "stream.h"

psfile::psfile(inbuffer &b, int initial_offset)
    : mpgfile(b, initial_offset)
{
  int vid=-1;
  for(unsigned int i=0;i<0x300;++i)
    streamnumber[i]=-1;

  int inbytes=buf.providedata(buf.getsize(),initialoffset);
  const uint8_t* data=(const uint8_t*) buf.data();

  bool streamfound[0x300]={};
  int sid;

  while (inbytes>=9) {
    if (data[2]&0xfe) {
      data+=3;
      inbytes-=3;
      continue;
    }

    if (data[0]!=0 || data[1]!=0 || data[2]!=1 || data[3]<0xb9) {
      // sync lost
      ++data;
      --inbytes;
      continue;
    }

    sid=data[3];
    int len=6;

    if (sid==0xba) {	// pack header
      if (inbytes<14)
        break;
      len=14+(data[13]&0x07);
    }
    else if (sid==0xb9) {	// program end
      break;
    }
    else {
      len=((data[4]<<8)|data[5])+6;
    }

    if (sid>=0xe0 && sid<=0xef) {
      if (vid<0) {
        vid=sid;
	streamnumber[vid]=VIDEOSTREAM;
      }
      inbytes-=len;
      data+=len;
      continue;
    }

    if (sid==0xbd || sid==0xbf) {	// private stream
      if (inbytes<(10+data[8]))
        break;
      int ssid=data[9+data[8]];
      sid=((sid==0xbd)?0x100:0x200) | ssid;
    }

    if (!streamfound[sid]) {	// first occurrence of this stream
      streamfound[sid] = true;
      streamtype::type t = streamtype::unknown;
      if (sid >= 0xc0 && sid <= 0xdf)
	t = streamtype::mpegaudio;
      else if (sid >= 0x180 && sid <= 0x187)
	t = streamtype::ac3audio;
      /* not supported yet:
      else if (sid >= 0x188 && sid <= 0x18f)
	t = streamtype::dtsaudio;
      else if (sid >= 0x120 && sid <= 0x13f)
	t = streamtype::vobsub;
      if (t == streamtype::vobsub) {
	// TODO
      }
      else
      */
      if (t != streamtype::unknown
       && audiostreams < MAXAUDIOSTREAMS) {
	streamnumber[sid] = audiostream(audiostreams);
	stream *S = &s[audiostream(audiostreams++)];
	S->id = sid;
	S->type = t;
      }
    }
    inbytes-=len;
    data+=len;
    continue;
  }

  initcodeccontexts(vid);
}


psfile::~psfile()
  {}

int psfile::streamreader(streamhandle &sh)
{
  int bytes=0;
  const uint8_t *data=0;
  int skipped=0;
  int minbytes=14;

  for(;;) {
    if (bytes<minbytes) {
      sh.fileposition+=skipped;
      skipped=0;
      bytes=buf.providedata(minbytes,sh.fileposition);
      if (bytes<0)
        return bytes;
      if (bytes<minbytes)
        return 0;
      data=(const uint8_t*) buf.data();
    }

    minbytes=14;

    if (data[2]&0xfe) {	//shortcut
      skipped+=3;
      data+=3;
      bytes-=3;
      continue;
    }

    if (data[0]!=0 || data[1]!=0 || data[2]!=1 || data[3]<0xb9) {
      // sync lost
      ++skipped;
      ++data;
      --bytes;
      continue;
    }

    int sid=data[3];
    int len=6;

    if (sid==0xba) {	// packheader
      len=14+(data[13]&0x07);
    }
    else if (sid==0xb9) // program end
      return 0;
    else
      len=((data[4]<<8)|data[5])+6;

    if (sid==0xbd || sid==0xbf) {	// private stream
      if (bytes<(10+data[8])) {
        minbytes=10+data[8];
        continue;
      }
      int ssid=data[9+data[8]];
      sid=((sid==0xbd)?0x100:0x200) | ssid;
    }

    int sn=streamnumber[sid];
    if (sn>=0) {
      if (bytes<len) {
        minbytes=len;
        continue;
      }
      sh.fileposition+=skipped;
      skipped=0;

      int payloadbegin=data[8]+9;
      if (sid >= 0x180 && sid <= 0x18f)	// ac3audio or dtsaudio
	payloadbegin += 4;
      else if (sid&0x300)
        ++payloadbegin;
      else if (sid&0xf0==0xe0) {
        if ( *(uint32_t*)(data+payloadbegin)==mbo32(0x00000001) )
          ++payloadbegin;
      }

      streamdata *sd=sh.stream[sn];
      if (len>payloadbegin && sd) {

        sd->appenditem(filepos_t(sh.fileposition,0), std::string((const char*)data+6,payloadbegin-6),
                       data+payloadbegin, len-payloadbegin);
        int returnvalue = len-payloadbegin;

        sh.fileposition+=len;
        return returnvalue;
      }
    }

    skipped+=len;
    data+=len;
    bytes-=len;
  }

}

/// This function probes the data in the given inbuffer for an mpeg program stream.
/// It returns the buffer offset at which the program stream starts, or -1 if
/// no program stream was identified.
int psfile::probe(inbuffer &buf)
{
  int latestsync=buf.inbytes()-2048-16;
  if (latestsync>(8<<10))
    latestsync=8<<10;

  const uint8_t *data = (const uint8_t*) buf.data();
  int ps;
  for (ps = 0; ps < latestsync; ++ps) {
    if (data[ps+2]&0xfe) {
      ps+=3;
      continue;
    }

    int testupto=buf.inbytes()-16;
    int pos=ps;
    while(pos < testupto) {
      const uint8_t *d=&data[pos];

      if (d[0]!=0 || d[1]!=0 || d[2]!=1 || d[3]<0xba)
        break;
      if (d[3]==0xba)
        pos+=14+(d[13]&0x07);
      else
        pos+=((d[4]<<8)|d[5])+6;
    }
    if (pos >= testupto) {	// this is a MPEG PS file
      return ps;
    }
  }

  return -1;
}
