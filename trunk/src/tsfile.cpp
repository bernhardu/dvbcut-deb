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
#include <algorithm>

tsfile::tsfile(inbuffer &b, int initial_offset) : mpgfile(b, initial_offset)
{
  for(unsigned int i=0;i<8192;++i)
    streamnumber[i]=-1;

  // check the "header" for receiver specific bookmarks
  const uint8_t *header = (const uint8_t*) buf.data();
  const char *model[7]={"???","TF4000","TF5000","TF5010","TF5000c","TF5010c","TF7700"};
  int irc;
  bmtype = none;  
  if((irc=isTOPFIELD(header, initial_offset, buf.getfilename(0)))>0)
    fprintf(stderr,"Found Topfield %s recording (%d bytes header, %d bookmarks)\n",model[irc/100],initial_offset,irc%100);  
  else 
    fprintf(stderr,"Analyzed transport stream, %d bytes of initial data (IRC=%d)\n",initial_offset,irc);

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
      const uint16_t plen = p.payload_length();
      if (plen >= 9
       && plen >= 11 + payload[8]
       && payload[9 + payload[8]] == 0x0b
       && payload[10 + payload[8]] == 0x77) {
        audios.push_back(std::pair<int,int>(sid,pid));
        apid[pid]=true;
      }
      else if (plen == 184
       && payload[8] == 0x24
       && (payload[45] & 0xf0) == 0x10
       && payload[47] == 0x2c) {
	// probably VBI/teletext data
      }
      else if (plen >= 9
       && plen >= 12 + payload[8]
       && payload[9 + payload[8]] == 0x20
       && payload[10 + payload[8]] == 0x00
       && payload[11 + payload[8]] == 0x0f) {
	// probably dvb subtitles
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

// test for Topfield TF4000PVR & TF5xxxPVR and read bookmarks from the proprietary header
// return codes: <type>*100+<number of bookmarks> or 
//               -1: header far to short for topfield/bookmarks, -2: magic mismatch, -3: version mismatch                                              
int 
tsfile::isTOPFIELD(const uint8_t *header, int len, std::string recfile) {  
  unsigned int magic, version, unit=0;
  unsigned int frequency, symbolrate, modulation;
  int boff=len, off=0, type=0, hlen=0, verbose=0, irc;

  if(verbose) fprintf(stderr,"Header length of %s: %d\n",recfile.c_str(),len);
  
  // topfield receiver with additional info file?
  if((irc=isTF7700HDPVR(recfile))>0) return irc;
  else if(verbose) fprintf(stderr,"No TF7700HDPVR! IRC=%d\n",irc);

  // just in case there's a corrupted TS packet at the beginning 
  if(len<TSPACKETSIZE) return -1;

  // identify the specific header via the magic number 'TFrc'
  magic = (header[0]<<24)|(header[1]<<16)|(header[2]<<8)|header[3];
  if(magic==TF5XXXPVR_MAGIC) {           // all newer models of type TF5???PVR and TF6???PVR
      hlen = TF5XXXPVR_LEN;
      unit = (1<<9)*TSPACKETSIZE;
      // There are 2 different versions, one with 4 bytes larger ServiceInfoStructure (UK models?)
      version = (header[4]<<8)|header[5];
      if(verbose) fprintf(stderr,"magic: %x version: %x\n",magic,version);
      if(version==TF5000PVR_VERSION) {  
        off = 0;  
        boff = TF5000PVR_POS;
        type = 2;
      } else if(version==TF5010PVR_VERSION) {
        off = 4;
        boff = TF5010PVR_POS;
        type = 3;
      } else 
        return -3;          
      // DVB-C boxes have a 4bytes smaller TransponderInfoStructure... grmbl!!!
      // And there's no identifier or version number to indicate this... arghhhh!! 
      // ==> check a few transponder parameters (which are stored at different positions if DVB-S or -T!?!) 
      frequency = (header[52+off]<<24)|(header[53+off]<<16)|(header[54+off]<<8)|header[55+off];
      symbolrate = (header[56+off]<<8)|header[57+off];
      modulation = header[62+off];
      if(verbose) fprintf(stderr,"DVB-C? freq=%d symb=%d mod=%d\n",frequency,symbolrate,modulation);
      if(frequency>=47000 && frequency<=862000 && symbolrate>=2000 && symbolrate<=30000 && modulation<=4) {
        boff-=4;
        type+=2;
      }  
  } else { 
      // the old TF4000PVR don't writes a simple magic/version number at the beginning...
      // but start/stop time (MJD/hour/time) ==> check for consistency!
      // start can be equal to stop (if recording is COPY/CUT), but should be year 2000+ (MJD=51543)!
      version = (header[4]<<24)|(header[5]<<16)|(header[6]<<8)|header[7];
      unsigned int duration = (header[8]<<8)|header[9]; // in minutes
      unsigned int servicetype = (header[12]<<8)|header[13]; // 0:TV, 1:Radio
      unsigned int polarity = header[53]; // 0x80, 0x08, 0x88, 0x00 (hi/lo, hor/ver)
      frequency = (header[54]<<8)|header[55];
      symbolrate = (header[56]<<8)|header[57];
      if(verbose) fprintf(stderr,"TF4000? start=%x stop=%x dur=%d serv=%d freq=%d symb=%d pol=%d\n",
                                        magic,version,duration,servicetype,frequency,symbolrate,polarity);
      if(magic<=version && (magic>>16)>51543 && 
         header[2]<24 && header[3]<60 && header[6]<24 && header[7]<60 &&
         duration>0 && duration<1440 && servicetype==0 &&    // up to one full day TV recording should be enough!
         frequency>=10000 && frequency<=13000 &&             // there's only a DVB-S device, AFAIK!
         symbolrate>=2000 && symbolrate<=30000 && (polarity&0x77)==0) {
        // the above should be sufficient... together with the required length of the header!
        hlen = TF4000PVR_LEN;
        unit = (1<<17);
        boff = TF4000PVR_POS;
        type = 1;
      } else 
        return -2;
  }    
   
  // OK,... we identified a receiver model!
  int bnum = 0; 
  //if(len>=hlen) // discard ALL (slightly) to small headers...? No, only require enough space for the bookmarks...
  if(len>=boff+4*TF5XXXPVR_MAX) { 
      // Seems to be a Topfield TF4000PVR/TF5xxxPVR TS-header with 576/3760bytes total length
      // and up to 64 bookmarks (BUT can be shorter/corrupted due to COPY/CUT-procedure on reveiver)
      dvbcut_off_t bookmark;
      while ((bookmark=(header[boff]<<24)|(header[boff+1]<<16)|(header[boff+2]<<8)|header[boff+3])
              && bnum<TF5XXXPVR_MAX) {
          // bookmark is stored in 128 resp. 94kbyte units
          bookmark*=unit;
          if(verbose) fprintf(stderr,"BOOKMARK[%d] = %lld\n",bnum,bookmark);
          // fill bookmark vector with byte positions
          byte_bookmarks.push_back(bookmark);
          bnum++;
          boff+=4;
      }  
  } else // receiver model identified but header to short!
      fprintf(stderr,"Header probabely corrupted (%dbytes to short), discarding bookmarks!\n",hlen-len);

  // this routine stores bookmarks as byte positions! 
  bmtype = byte;
  
  return type*100+bnum;
}

// newest topfield receivers (i.e. TF7700HDPVR) with additional ADD-file instead of a TS-header
int 
tsfile::isTF7700HDPVR(std::string recfile) {  
  int verbose=0;
  std::string addfile;
  
  // contruct the expected file name of the additonal info file
  if (!recfile.empty()) {
    int lastdot = recfile.rfind('.');
    int lastslash = recfile.rfind('/');
    if (lastdot >= 0 && lastdot >= lastslash)
      addfile = recfile.substr(0,lastdot) + ".add";
    else
      return -20;  
  } else
    return -10;  

  uint8_t *buffer;  
  ssize_t len = readfile(addfile, &buffer);
  if(len<0) return -30;

  /* the whole file (normally 2636 bytes) is now loaded in the memory buffer. */

  int bnum = 0, boff = TF7700HDPVR_POS+4*TF7700HDPVR_MAX, unit=90000; 
  // there has to be space for up to 48 bookmarks magic number!
  if(len>=boff+4) { 
      // is it a topfield file?
      unsigned int magic = (buffer[boff]<<24)|(buffer[boff+1]<<16)|(buffer[boff+2]<<8)|buffer[boff+3];
      if(magic!=TF5XXXPVR_MAGIC) {
        free(buffer);
        return -40;
      }
      else if(verbose) fprintf(stderr,"Found Topfield ADD-file: %s\n",addfile.c_str());
      
      boff = TF7700HDPVR_POS;
      pts_t bookmark;
      // changed byte order compared to old receivers!?!
      while ((bookmark=(buffer[boff+3]<<24)|(buffer[boff+2]<<16)|(buffer[boff+1]<<8)|buffer[boff])
              && bnum<TF7700HDPVR_MAX) {
          if(verbose) fprintf(stderr,"BOOKMARK[%d] = %lld\n",bnum,bookmark);
          // bookmark is stored in seconds now, but we'll use full pts!
          bookmark*=unit;
          // fill bookmark vector with times
          time_bookmarks.push_back(bookmark);
          bnum++;
          boff+=4;
      }  
  } else // receiver model identified but file to short!
      fprintf(stderr,"ADD-File probabely corrupted (%dbytes to short), discarding bookmarks!\n",TF7700HDPVR_LEN-len);

  // terminate
  free(buffer);

  // this routine stores bookmarks as times! 
  bmtype = time;
  
  return 600+bnum;
}

size_t
tsfile::get_si_table(uint8_t *tbl, size_t max, size_t &index, int pid, int tid) {
  const uint8_t *d = (uint8_t*)buf.data();
  size_t len = buf.inbytes();
  uint8_t cc = 0xff;
  size_t size = 0;
  while (index + TSPACKETSIZE <= len) {
    const tspacket *p = (const tspacket*)&d[index];
    index += TSPACKETSIZE;
    // check packet
    if (p->transport_error_indicator()
     || p->pid() != pid
     || !p->contains_payload()) {
      continue;
    }
    if (p->payload_unit_start_indicator()) {
      // first packet of table
      cc = p->continuity_counter();
      size = 0;
    }
    else if (size != 0) {
      // additional packet, check continuity
      uint8_t ccdelta = (p->continuity_counter() - cc) & 0x0f;
      switch (ccdelta) {
	default: size = 0; continue;	// sequence error, retry
	case 0: continue;		// repeated packet
	case 1: break;			// correct sequence
      }
      cc = p->continuity_counter();
    }
    else {
      continue;
    }
    if (p->payload_length() < 1) {	// payload too short
      continue;
    }
    const uint8_t *payload = (uint8_t*)p->payload();
    size_t amount = p->payload_length();
    unsigned n = 0;
    if (p->payload_unit_start_indicator()) {
      // evaluate pointer field
      n = payload[0] + 1;
      if (n >= amount) {	// too short
	continue;
      }
      amount -= n;
      if (payload[n] != tid) {	// wrong table
	continue;
      }
    }
    if (amount > max - size) {
      amount = max - size;
    }
    if (amount == 0) {
      // table too long, discard it
      size = 0;
      continue;
    }
    memcpy(tbl + size, payload + n, amount);
    size += amount;
    if (size >= 3) {
      size_t tmp = 3 + (((tbl[1] << 8) | tbl[2]) & 0x0fff);
      if (tmp <= size) {
	return tmp;	// table complete
      }
    }
  }
  return 0;
}

static const uint8_t*
get_stream_descriptor(const uint8_t *d, unsigned len) {
  while (len >= 2) {
    unsigned x = d[1] + 2;
    if (len < x)	// descriptor truncated
      break;
    switch (d[0]) {
      default:
	break;
      /* audio */
      case 0x6a:	// AC-3 descriptor
      /* in the future, maybe also:
      case 0x73:	// DTS audio descriptor
      case 0x79:	// AAC descriptor
      case 0x7a:	// enhanced AC-3 descriptor
      */
	return d;
      /* teletext/subtitles */
      case 0x45:	// VBI data descriptor
      case 0x46:	// VBI teletext descriptor
      case 0x56:	// teletext descriptor
      case 0x59:	// subtitling descriptor
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
  uint8_t pat[1024];
  size_t patlen;
  size_t index = 0;
  for (;;) {
    patlen = get_si_table(pat, sizeof(pat), index, 0x0000, 0x00);
    if (patlen == 0)			// not found
      return false;
    if (patlen < 12			// too short
     || (pat[1] & 0xc0) != 0x80		// bad syntax
     || !(pat[5] & 0x01))		// not current
      continue;
    // XXX: CRC32 check omitted
    if (pat[6] != 0 || pat[7] != 0) {
      /* this is NOT an error, but currently unhandled */
      fprintf(stderr, "can not handle segmented PAT; guessing streams\n");
      return false;
    }
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
    uint8_t pmt[1024];
    size_t pmtlen;
    index = 0;
    for (;;) {
      pmtlen = get_si_table(pmt, sizeof(pmt), index, *it, 0x02);
      if (pmtlen == 0)			// not found
	break;
      if (pmtlen < 16			// too short
       || (pmt[1] & 0xc0) != 0x80	// bad syntax
       || !(pmt[5] & 0x01)		// not current
       || pmt[6] != 0 || pmt[7] != 0)	// not allowed
	continue;
      // XXX: CRC32 check omitted
      break;
    }
    if (pmtlen == 0)
      continue;

    // iterate through streams
    unsigned i = ((pmt[10] << 8) | pmt[11]) & 0x0fff;
    i = 12 + i;
    while (i + 9 <= pmtlen) {
      uint8_t stream_type = pmt[i];
      int pid = ((pmt[i + 1] << 8) | pmt[i + 2]) & 0x1fff;
      size_t dlen = ((pmt[i + 3] << 8) | pmt[i + 4]) & 0x0fff;
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
	  case 0x81:	// legacy AC-3 audio
	  case 0x83:	// legacy LPCM audio
	  case 0x85:	// legacy DTS audio
	    apids.push_back(std::pair<int, int>(stream_type, pid));
	    break;
	  case 0x06:	// PES packets containing private data
	    const uint8_t *desc = get_stream_descriptor(pmt + i + 5, dlen);
	    if (desc)
	      apids.push_back(std::pair<int, int>(256 + *desc, pid));
	    break;
	}
      }
      i += 5 + dlen;
    }

    // did we find at least a video stream?
    if (vpid != -1) {
      fprintf(stderr, "PMT: found video stream at pid %d\n", vpid);
      if (apids.empty()) {
	fprintf(stderr, "but I have to guess the audio streams :-(\n");
	return false;
      }
      streamnumber[vpid] = VIDEOSTREAM;
      std::list<std::pair<int, int> >::iterator ait = apids.begin();
      while (ait != apids.end()) {
	streamtype::type t = streamtype::unknown;
	switch (ait->first) {
	  case 0x03:
	  case 0x04:
	    t = streamtype::mpegaudio;
	    break;
	  case 0x81:	// legacy AC-3 audio
	  case 0x16a:	// AC-3 descriptor
	    t = streamtype::ac3audio;
	    break;
	  /* in the future, maybe also:
	  case 0x83:	// legacy LPCM audio
	    t = streamtype::pcmaudio;
	    break;
	  case 0x85:	// legacy DTS audio
	  case 0x173:	// DTS audio descriptor
	    t = streamtype::dtsaudio;
	    break;
	  case 0x179:	// AAC descriptor
	    t = streamtype::aacaudio;
	    break;
	  case 0x17a:	// enhanced AC-3 descriptor
	    t = streamtype::eac3audio;
	    break;
	  */
	  case 0x145:	// VBI data descriptor
	  case 0x146:	// VBI teletext descriptor
	  case 0x156:	// teletext descriptor
	    // t = streamtype::vbisub;
	    fprintf(stderr, "PMT: can't handle teletext stream at pid %d\n", ait->second);
	    break;
	  case 0x159:	// subtitling descriptor
	    // t = streamtype::dvbsub;
	    fprintf(stderr, "PMT: can't handle subtitle stream at pid %d\n", ait->second);
	    break;
	  default:
	    break;
	}
	if (t != streamtype::unknown) {
	  fprintf(stderr, "PMT: found audio stream at pid %d\n", ait->second);
	  streamnumber[ait->second] = audiostream(audiostreams);
	  stream *S = &s[audiostream(audiostreams++)];
	  S->id = ait->second;
	  S->type = t;
	  if (audiostreams >= MAXAUDIOSTREAMS)
	    break;
	}
	++ait;
      }
      initcodeccontexts(vpid);
      return true;
    }
    // start over
    apids.clear();
  }
  return false;
}
