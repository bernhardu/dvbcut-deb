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

#include "port.h"
#include "tsfile.h"
#include "streamhandle.h"
#include <list>
#include <utility>

tsfile::tsfile(inbuffer &b, int initial_offset) : mpgfile(b, initial_offset)
{
  for(unsigned int i=0;i<8192;++i)
    streamnumber[i]=-1;

  // Find video PID and audio PID(s)
  buf.providedata(buf.getsize(), initialoffset);
  if (check_si_tables())
    return;

  // there are no SI tables, so we have to guess
  int vid=-1;
  bool apid[8192]={};
  std::list<std::pair<int,int> > audios;
  int inpackets=buf.inbytes()/TSPACKETSIZE;
  if (inpackets>40000)
    inpackets=40000;
  for(int i=0;i<inpackets;++i) {
    const tspacket &p=((const tspacket*)buf.data())[i];
    if (p.transport_error_indicator())
      continue;	// drop invalid packet --mr
    int pid=p.pid();
    if (apid[pid])
      continue;	// already had this pid --mr
    int sid=p.sid();
    if (sid<0)
      continue;

    if ((sid&0xe0) == 0xc0) {	// mpeg audio stream
      audios.push_back(std::pair<int,int>(sid,pid));
      apid[pid]=true;
    }
    else if (sid==0xbd) {	// private stream 1, possibly AC3 audio stream
      const uint8_t *payload=(const uint8_t*) p.payload();
      if ((p.payload_length()>=9) && (p.payload_length()>=11+payload[8])
       && (*(uint16_t*)&payload[9+payload[8]]==mbo16(0xb77)) ) {
        audios.push_back(std::pair<int,int>(sid,pid));
        apid[pid]=true;
      }
    }
    else if (vid<0 && ((sid&0xf0)==0xe0)) { // mpeg video stream
      vid=pid;
      streamnumber[vid]=VIDEOSTREAM;
      apid[pid]=true;
    }
  }
  audios.sort();
  for (std::list<std::pair<int,int> >::iterator it=audios.begin();it!=audios.end();++it) {
    streamnumber[it->second]=audiostream(audiostreams);
    stream *S=&s[audiostream(audiostreams++)];
    S->id=it->second;
    if (it->first==0xbd) {
      S->type=streamtype::ac3audio;
    }
    else {
      S->type=streamtype::mpegaudio;
    }
    if (audiostreams>=MAXAUDIOSTREAMS)
      break;
  }
  initcodeccontexts(vid);
}

tsfile::~tsfile()
  {}

int tsfile::streamreader(streamhandle &s)
  {
  int returnvalue=0;
  bool lostsync(false);

  for(;;) {
    dvbcut_off_t packetpos=s.fileposition;
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

    // Abandon invalid packets --mr
    if (p->transport_error_indicator())
      continue;
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
        if ( *(uint32_t*)(&sd->header[payloadbegin])==mbo32(0x00000001) )
          ++payloadbegin;
      }

    sd->appenditem(filepos_t(sd->nextfilepos,0), std::string(sd->header,6,payloadbegin-6),
                   &sd->header[payloadbegin], sd->header.size()-payloadbegin);
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
int tsfile::probe(inbuffer &buf) {
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

const uint8_t*
tsfile::get_si_table(const uint8_t *d, size_t len, int pid, unsigned *tlen) {
  for (size_t i = 0; i + TSPACKETSIZE <= len; i += TSPACKETSIZE) {
    const tspacket *p = (const tspacket*)&d[i];
    // check packet
    if (p->transport_error_indicator()
     || p->pid() != pid
     || !p->payload_unit_start_indicator()
     || p->payload_length() < 1)
      continue;
    const uint8_t *buf = (uint8_t*)p->payload();
    // evaluate pointer field
    unsigned n = buf[0] + 1;
    if (n >= (unsigned)p->payload_length())	// too short
      continue;
    if (tlen)
      *tlen = p->payload_length() - n;
    return buf + n;
  }
  return 0;
}

static const uint8_t*
get_audio_descriptor(const uint8_t *d, unsigned len) {
  while (len >= 2) {
    unsigned x = d[1] + 2;
    if (len < x)	// descriptor truncated
      break;
    switch (d[0]) {
      default:
	break;
      case 0x6a:	// AC-3 descriptor
      /* in the future, maybe also:
      case 0x73:	// DTS audio descriptor
      case 0x79:	// AAC descriptor
      case 0x7a:	// enhanced AC-3 descriptor
      */
	return d;
    }
    d += x;
    len -= x;
  }
  return 0;
}

bool
tsfile::check_si_tables() {
  unsigned len = buf.inbytes();
  const uint8_t *d = (uint8_t*)buf.data();
  bool pids[8192] = {};

  // limit buffer size
  if (len > 40000 * TSPACKETSIZE)
    len = 40000 * TSPACKETSIZE;

  // find all PIDs
  for (unsigned i = 0; i + TSPACKETSIZE <= len; i += TSPACKETSIZE) {
    const tspacket *p = (const tspacket*)&d[i];
    if (!p->transport_error_indicator())
      pids[p->pid()] = true;
  }

  // drop out if PAT not present
  if (!pids[0])
    return false;

  // read PAT
  const uint8_t *pat;
  unsigned patlen;
  for (pat = d; ; pat += TSPACKETSIZE) {
    if (pat >= d + len)
      return false;	// no valid PAT found
    pat = get_si_table(pat, d + len - pat, 0x0000, &patlen);
    if (pat == 0)
      return false;	// no valid PAT found
    if (pat[0] != 0x00			// wrong TID
     || (pat[1] & 0xc0) != 0x80)	// bad syntax
      continue;
    // XXX: CRC32 check omitted
    unsigned tlen = ((pat[1] << 8) | pat[2]) & 0xfff;
    if (tlen + 3 < 12			// too short
     || !(pat[5] & 0x01))		// not current
      continue;
    // valid PAT found
    if (pat[6] != 0 || pat[7] != 0 || patlen < tlen + 3) {
      fprintf(stderr, "PAT segmented or truncated\n");
      return false;
    }
    patlen = tlen + 3;
    break;
  }

  // get PMT PIDs
  std::list<int> pmts;
  for (unsigned i = 8; i + 8 <= patlen; i += 4) {
    if (pat[i] || pat[i + 1]) {
      int pid = ((pat[i + 2] << 8) | pat[i + 3]) & 0x1fff;
      if (pids[pid]) {
	fprintf(stderr, "PAT: found PMT at pid %d\n", pid);
	pmts.push_back(pid);
      }
    }
  }

  // drop out if no PMT is present
  if (pmts.empty())
    return false;

  // process PMTs until we find a valid one
  int vpid = -1;
  std::list<std::pair<int, int> > apids;
  for (std::list<int>::iterator it = pmts.begin(); it != pmts.end(); ++it) {
    // read PMT
    const uint8_t *pmt;
    unsigned pmtlen;
    for (pmt = d; pmt < d + len; pmt += TSPACKETSIZE) {
      pmt = get_si_table(pmt, d + len - pmt, *it, &pmtlen);
      if (pmt == 0)
	break;
      if (pmt[0] != 0x02		// wrong TID
       || (pmt[1] & 0xc0) != 0x80)	// bad syntax
	continue;
      // XXX: CRC32 check omitted
      unsigned tlen = ((pmt[1] << 8) | pmt[2]) & 0xfff;
      if (tlen + 3 < 16			// too short
       || !(pmt[5] & 0x01))		// not current
	continue;
      // valid PMT found
      if (pmt[6] != 0 || pmt[7] != 0 || pmtlen < tlen + 3) {
	fprintf(stderr, "PMT segmented or truncated\n");
	continue;
      }
      pmtlen = tlen + 3;
      break;
    }
    if (pmt == 0 || pmt >= d + len)
      continue;

    // iterate through streams
    unsigned i = ((pmt[10] << 8) | pmt[11]) & 0x0fff;
    i = 12 + i;
    while (i + 9 <= pmtlen) {
      uint8_t stream_type = pmt[i];
      int pid = ((pmt[i + 1] << 8) | pmt[i + 2]) & 0x1fff;
      unsigned dlen = ((pmt[i + 3] << 8) | pmt[i + 4]) & 0x0fff;
      if (i + 5 + dlen + 4 > len)
	break;
      if (pids[pid]) {	// is the PID actually present?
	switch (stream_type) {
	  default:
	    break;
	  case 0x01:	// MPEG-1 video
	  case 0x02:	// MPEG-2 video
	    if (vpid == -1)
	      vpid = pid;
	    break;
	  case 0x03:	// MPEG-1 audio
	  case 0x04:	// MPEG-2 audio
	    apids.push_back(std::pair<int, int>(stream_type, pid));
	    break;
	  case 0x06:	// PES packets containing private data
	    const uint8_t *desc = get_audio_descriptor(pmt + i + 5, dlen);
	    if (desc)
	      apids.push_back(std::pair<int, int>(stream_type, pid));
	    break;
	}
      }
      i += 5 + dlen;
    }

    // did we find at least a video stream?
    if (vpid != -1) {
      fprintf(stderr, "PMT: found video stream at pid %d\n", vpid);
      streamnumber[vpid] = VIDEOSTREAM;
      std::list<std::pair<int, int> >::iterator ait = apids.begin();
      while (ait != apids.end()) {
	fprintf(stderr, "PMT: found audio stream at pid %d\n", ait->second);
	streamnumber[ait->second] = audiostream(audiostreams);
	stream *S = &s[audiostream(audiostreams++)];
	s->id = ait->second;
	if (ait->first == 0x06) {
	  S->type = streamtype::ac3audio;
	}
	else {
	  S->type = streamtype::mpegaudio;
	}
	if (audiostreams >= MAXAUDIOSTREAMS)
	  break;
	++ait;
      }
      initcodeccontexts(vpid);
      return true;
    }
  }
  return false;
}
