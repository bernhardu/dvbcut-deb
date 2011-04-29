/*  dvbcut
    Copyright (c) 2005-2007 Sven Over <svenover@svenover.de>
 
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
#include <stdint.h>
#include <algorithm>
#include <string>
#include <sstream>

#include "playaudio.h"
#include "exception.h"

#ifdef HAVE_LIB_AO
#include <ao/ao.h>

/// Simple class for audio output through libao
class audioout
{
protected:
  ao_device *m_device;
  int m_channels, m_samplerate;
public:
  audioout() : m_device(NULL), m_channels(0), m_samplerate(0) {}
  ~audioout()
  {
    if (m_device)
      ao_close(m_device);
  }

  void play(int channels, int samplerate, int16_t *data, int bytes)
  {
    if ((not m_device) or channels!=m_channels or samplerate!=m_samplerate)
    {
      if (m_device)
      {
        ao_close(m_device);
        m_device=NULL;
      }

      ao_sample_format format;
      // zero-initialize
      memset(&format, 0, sizeof(format));

      format.bits = 16;
      format.channels = channels;
      format.rate = samplerate;
      format.byte_format = AO_FMT_LITTLE;

      m_channels=channels;
      m_samplerate=samplerate;

      m_device = ao_open_live(ao_default_driver_id(), &format, NULL /* no options */ );
      if (!m_device)
        throw dvbcut_exception("Error setting up audio output");
    }

    ao_play(m_device, (char*) data, bytes);
  }
};


#ifdef HAVE_LIB_A52
extern "C"
{
#include <a52dec/a52.h>
#include <a52dec/mm_accel.h>
}

/// Simple class for AC3 decoding
class a52dec
{
protected:
  a52_state_t *m_state;
public:
  a52dec() : m_state(0)
  {
    m_state=a52_init(MM_ACCEL_DJBFFT);

    if (not m_state)
      throw dvbcut_exception("Error setting up AC3 decoder");
  }
  ~a52dec()
  {
    a52_free(m_state);
  }
  const sample_t *samples()
  {
    return a52_samples(m_state);
  }
  int syncinfo(uint8_t *buffer, int &flags, int &sample_rate, int &bitrate) const
  {
    return a52_syncinfo(buffer,&flags,&sample_rate,&bitrate);
  }
  int frame(uint8_t *buffer, int &flags, sample_t &level, sample_t bias)
  {
    return a52_frame(m_state, buffer, &flags, &level, bias);
  }
  int block()
  {
    return a52_block(m_state);
  }
};

void playaudio_ac3(const void *data, uint32_t len)
{
  uint8_t *d=const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(data));

  a52dec dec;
  audioout aout;
  int flags=0, sample_rate=0, bit_rate=0;
  sample_t level;

  while (len>=7)
  {
    int bytes=dec.syncinfo(d,flags,sample_rate,bit_rate);

    if (bytes==0)
    {
      ++d;
      --len;
      continue;
    }

    if (bytes>signed(len))
      break;

    flags=A52_STEREO | A52_ADJUST_LEVEL;
    level=1;
    if (dec.frame(d,flags,level,0)!=0)
      throw dvbcut_exception("Error while decoding AC3 data");
    if ((flags&A52_CHANNEL_MASK)!=A52_STEREO)
      throw dvbcut_exception("Error while decoding AC3 data (non-stereo output)");
    for (int i=0;i<6;++i)
    {
      if (dec.block()!=0)
        dvbcut_exception("Error while decoding AC3 data");
      int16_t samples[256*2];
      const sample_t *decoded=dec.samples();
      for(int j=0;j<256;++j)
      {
        samples[j*2+0]=int16_t(decoded[j]*sample_t(32767));
        samples[j*2+1]=int16_t(decoded[j+256]*sample_t(32767));
      }
      aout.play(2,sample_rate,samples,256*2*2); //dec.samples();
    }

    len-=bytes;
    d+=bytes;
  }

  //   throw dvbcut_exception("AC3 playback not yet implemented");
}
#else // HAVE_LIB_A52
void playaudio_ac3(const void *, uint32_t)
{
  throw dvbcut_exception("DVBCUT was built without AC3 support");
}
#endif // ifdef HAVE_LIB_A52


#ifdef HAVE_LIB_MAD
#include <mad.h>

/// Simple class for MP2 decoding and output throught the audioout class
class mp2dec
{
protected:
  mad_decoder m_decoder;
  const void *m_data;
  uint32_t m_len;
  audioout &m_aout;
  std::string m_error;
  
  static enum mad_flow input(void *data,struct mad_stream *stream)
  {
    mp2dec *This=reinterpret_cast<mp2dec*>(data);

    if (This->m_len==0)
      return MAD_FLOW_STOP;

    mad_stream_buffer(stream, reinterpret_cast<const unsigned char*>(This->m_data), This->m_len);
    This->m_len=0;

    return MAD_FLOW_CONTINUE;
  }
    
  static enum mad_flow output(void *data,
                              struct mad_header const *header,
                              struct mad_pcm *pcm)
  {
    mp2dec *This=reinterpret_cast<mp2dec*>(data);
    
    unsigned int channels=pcm->channels;
    if (channels>2)
      channels=2;
    
    const unsigned int samples=pcm->length;
    
    mad_fixed_t *sample[channels];
    for (unsigned int i=0;i<channels;++i)
      sample[i]=pcm->samples[i];
    
    int16_t buffer[samples*channels];
    
    // Transform the data delivered by libmad into samples accepted by libao.
    for (unsigned int s=0;s<samples;++s)
      for (unsigned int c=0;c<channels;++c)
    {
      mad_fixed_t S=*(sample[c]++);
      
      S+=(1L << (MAD_F_FRACBITS - 16)); // round

      // clip
      S=std::max(std::min(S,MAD_F_ONE-1),-MAD_F_ONE);
      
      // ...and convert to 16-bit signed integer
      buffer[s*channels+c]=S >> (MAD_F_FRACBITS + 1 - 16);
    }
    
    This->m_aout.play(channels,pcm->samplerate,buffer,samples*channels*2);
    
    return MAD_FLOW_CONTINUE;
  }
  
  static enum mad_flow error(void *data,
                                  struct mad_stream *stream,
                                  struct mad_frame *frame)
  {
    mp2dec *This=reinterpret_cast<mp2dec*>(data);
    
    std::ostringstream out;
    
    out << "MP2 decoding error " << int(stream->error) << " (" << mad_stream_errorstr(stream)
        << ") at byte offset " << int(stream->this_frame - reinterpret_cast<const unsigned char*>(This->m_data));
    
    This->m_error=out.str();
    return MAD_FLOW_CONTINUE;
  }

public:
  mp2dec(const void *data, uint32_t len, audioout &aout) : m_data(data),m_len(len),m_aout(aout)
  {
    mad_decoder_init(&m_decoder,this,input,0,0,output,error,0);
  }
  ~mp2dec()
  {
    mad_decoder_finish(&m_decoder);
  }
  void run()
  {
    if (mad_decoder_run(&m_decoder,MAD_DECODER_MODE_SYNC)!=0)
    {
      if (m_error.empty())
        throw dvbcut_exception("Error decoding/playing MP2 audio");
      else
        throw dvbcut_exception("Error decoding/playing MP2 audio: "+m_error);
    }
  }
};

void playaudio_mp2(const void *data, uint32_t len)
{
  audioout aout;
  mp2dec(data,len,aout).run();
}
#else // ifdef HAVE_LIB_MAD
void playaudio_mp2(const void *data, uint32_t len)
{
  throw dvbcut_exception("DVBCUT was built without MP2 support");
}
#endif // ifdef HAVE_LIB_MAD

// If DVBCUT is compiled without audio support, these dummy functions
// will just throw an exception. However, they should never get called, as
// the Play Audio actions should be disabled anyhow.
#else // ifdef HAVE_LIB_AO
void playaudio_ac3(const void *, uint32_t)
{
  throw dvbcut_exception("DVBCUT was built without support for audio output");
}
void playaudio_mp2(const void *, uint32_t)
{
  throw dvbcut_exception("DVBCUT was built without support for audio output");
}
#endif // ifdef HAVE_LIB_AO
