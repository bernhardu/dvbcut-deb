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

#include "streamdata.h"

static const int mpegaudio_rates[]=
  {
    44100,48000,32000,16000
  };
static const int mpegaudio_bitrate[][16]=
  { {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, // undefined layer
    {0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320}, // layer 3
    {0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384}, // layer 2
    {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448}  // layer 1
  };

static int mpaframe(const void *data, int &pos, int len)
  {
  const unsigned char *d=(const unsigned char *)data;
  while ((pos+2)<len && (d[pos]!=0xff || (d[pos+1]&0xf0!=0xf0)))
    ++pos;
  if (pos+2>=len)
    return 0;

  int layer=(d[pos+1]>>1)&0x03;
  int samples=(layer==4-1)?384:1152;
  int samplingrate=mpegaudio_rates[(d[pos+2]>>2)&0x03];
  int bitratecode=(d[pos+2]>>4)&0x0f;

  int skipbytes=(mpegaudio_bitrate[layer][bitratecode]*125)*samples/samplingrate;
  if (skipbytes)
    pos+=skipbytes;
  else
    pos+=3;

  while ((pos+2)<len && (d[pos]!=0xff || (d[pos+1]&0xf0!=0xf0)))
    ++pos;
  return samples*90000/samplingrate; // Duration of MPEG audio frame in 90000Hz units
  }

//

#define AC3_SYNCWORD (mbo16(0x0b77))

static int ac3_framelength[256] = {
                                    128, 128, 160, 160, 192, 192, 224, 224,
                                    256, 256, 320, 320, 384, 384, 448, 448,
                                    512, 512, 640, 640, 768, 768, 896, 896,
                                    1024,1024,1280,1280,1536,1536,1792,1792,
                                    2048,2048,2304,2304,2560,2560,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    138, 140, 174, 176, 208, 210, 242, 244,
                                    278, 280, 348, 350, 416, 418, 486, 488,
                                    556, 558, 696, 698, 834, 836, 974, 976,
                                    1114,1116,1392,1394,1670,1672,1950,1952,
                                    2228,2230,2506,2508,2786,2788,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    192, 192, 240, 240, 288, 288, 336, 336,
                                    384, 384, 480, 480, 576, 576, 672, 672,
                                    768, 768, 960, 960,1152,1152,1344,1344,
                                    1536,1536,1920,1920,2304,2304,2688,2688,
                                    3072,3072,3456,3456,3840,3840,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2,
                                    2,   2,   2,   2,   2,   2,   2,   2};

static int ac3_frameduration[4]={270000*1536/480,270000*1536/441,270000*1536/320,270000*1536/480};

static int ac3frame(const void *data, int &pos, int len)
  {
  const unsigned char *d=(const unsigned char *)data;
  while ((pos+2)<len && (*(uint16_t*)(d+pos)!=AC3_SYNCWORD))
    ++pos;
  if (pos+2>=len)
    return 0;

  pos+=ac3_framelength[d[pos+4]];

  while ((pos+2)<len && (*(uint16_t*)(d+pos)!=AC3_SYNCWORD))
    ++pos;
  return ac3_frameduration[(d[pos+4]>>6)&3]/300; // Duration of AC3 audio frame in 90000Hz units
  }

void streamdata::audio_addpts(uint32_t startbufferpos, bool onepacket)
  {
  bool needsync=true;
  itemlisttype::iterator it=items.begin();

  while(it!=items.end())
    if (!it->headerhaspts())
      it=items.erase(it);
    else
      ++it;

  it=items.begin();
  while (it!=items.end() && it->bufferposition<startbufferpos)
    ++it;
  itemlisttype::iterator nx;
  uint32_t bufferposition=it->bufferposition;
  uint32_t stopbufferpos=0;
  pts_t pts=0;

  while (it!=items.end()) {
    if (needsync) {
      needsync=false;

      for(;it!=items.end();++it)
        if (it->headerhaspts())
          // header carries PTS
          break;

      if (it==items.end())
        break;
      if (onepacket) {
        if (stopbufferpos) {
          if (it->bufferposition>=stopbufferpos)
            break;
          } else {
          itemlisttype::iterator n=it;
          for(++n;n!=items.end();++n)
            if (it->headerhaspts())
              break;
          if (n!=items.end())
            stopbufferpos=n->bufferposition;
          else
            onepacket=false;
          }
        }

      pts=it->headerpts();

      nx=it;
      ++nx;
      }

    int pos=0;
    int len=inbytes()-(bufferposition-getoffset());
    int ptsplus;
    if (type==streamtype::ac3audio)
      ptsplus=ac3frame(getdata(bufferposition),pos,len);
    else
      ptsplus=mpaframe(getdata(bufferposition),pos,len);
    if (pos+2>=len)
      break;
    if (ptsplus==0) {
      needsync=true;
      continue;
      }

    bufferposition+=pos;
    pts+=ptsplus;

    if (nx==items.end() || bufferposition<nx->bufferposition) {
      it=items.insert(nx,item(it->fileposition+(bufferposition-it->bufferposition),
                              STREAM_ITEM_FLAG_HAS_PTS|STREAM_ITEM_FLAG_DATA_ALIGNMENT_INDICATOR,
                              pts,bufferposition));
      } else {
      it=nx;
      ++nx;
      pts=it->headerpts();
      }

    }
  }

/// Find the packet with the highest PTS less or equal than the given pts and returns its buffer position.
uint32_t streamdata::ptsbufferpos(pts_t pts)
  {
  uint32_t bufferpos=getoffset();
  streamdata::itemlisttype::const_iterator it=items.begin();

  while(it!=items.end())
    if (it->headerhaspts())
      break;
    else
      ++it;

  if (it!=items.end())
    for(;;) {
      bufferpos=it->bufferposition;
      pts_t itpts=it->headerpts(pts);
      if (itpts>=pts)
        break;
      streamdata::itemlisttype::const_iterator nx=it;
      ++nx;
      while (nx!=items.end() && !nx->headerhaspts())
        ++nx;
      if (nx==items.end())
        break;
      pts_t nxpts=nx->headerpts(pts);
      if (nxpts>pts)
        break;
      if (nxpts==pts) {
        bufferpos=nx->bufferposition;
        break;
        }

      it=nx;
      continue;
      }

  return bufferpos;
  }

/// Find the packet with its PTS closest to the given pts and returns its buffer position.
uint32_t streamdata::closestptsbufferpos(pts_t pts)
  {
  uint32_t bufferpos=getoffset();
  streamdata::itemlisttype::const_iterator it=items.begin();

  while(it!=items.end())
    if (it->headerhaspts())
      break;
    else
      ++it;

  for(;it!=items.end();) {
    pts_t itpts=it->headerpts(pts);
    if (itpts>=pts)
      break;
    streamdata::itemlisttype::const_iterator nx=it;
    ++nx;
    while (nx!=items.end() && !nx->headerhaspts())
      ++nx;
    if (nx==items.end())
      break;
    pts_t nxpts=nx->headerpts(pts);
    if (nxpts<pts) {
      bufferpos=nx->bufferposition;
      it=nx;
      continue;
      }
    if (nxpts==pts) {
      bufferpos=nx->bufferposition;
      break;
      }

    if (pts-itpts>=nxpts-pts)
      bufferpos=nx->bufferposition;

    break;
    }

  return bufferpos;
  }

void streamdata::appenditem(filepos_t fp, const std::string &header, const void *d, int s)
  {
  int flags(0);
  pts_t pts(0);
  const uint8_t *data=(const uint8_t*)d;

  if (header.size()>=3) {
    if (header[0]&0x04)
      flags|=STREAM_ITEM_FLAG_DATA_ALIGNMENT_INDICATOR;
    if ((header[1]&0x80) && (header.size()>=8)) {
      flags |= STREAM_ITEM_FLAG_HAS_PTS;
      pts=mpgfile::char2pts((const unsigned char *)header.c_str()+3);
      }
    }

#if 0	// --mr
  if (type==streamtype::mpegaudio) {
    if (s>=2)
      if (data[0]==0xff && (data[1]&0xf0)==0xf0)
        flags|=STREAM_ITEM_FLAG_DATA_ALIGNMENT_INDICATOR;
    if (!(flags&STREAM_ITEM_FLAG_DATA_ALIGNMENT_INDICATOR))
      flags &=~ STREAM_ITEM_FLAG_HAS_PTS;
    } else if (type==streamtype::ac3audio && !transportstream) {
    if (s<3)
      return;
    int ac3offset=((data[1]<<8)|data[2]);
    if (ac3offset+2>s)
      return;
    if (ac3offset>1)
      appenditem(fp,0,0,(const void*)(data+3),ac3offset-1);
    data+=ac3offset+2;
    s-=ac3offset+2;
    flags|=STREAM_ITEM_FLAG_DATA_ALIGNMENT_INDICATOR;
    }
#endif


  if (s>0) {
    appenditem(fp, flags, pts, (const void*)data, s);
    }
  }

