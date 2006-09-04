#
# libavcodec Makefile
# (c) 2000-2005 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libavcodec

# NOTE: -I.. is needed to include config.h
CFLAGS=$(OPTFLAGS) -DHAVE_AV_CONFIG_H -I.. -I$(SRC_PATH)/libavutil -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE $(AMR_CFLAGS)

OBJS= bitstream.o utils.o mem.o allcodecs.o \
      mpegvideo.o jrevdct.o jfdctfst.o jfdctint.o\
      mpegaudio.o ac3enc.o mjpeg.o resample.o resample2.o dsputil.o \
      motion_est.o imgconvert.o imgresample.o \
      mpeg12.o mpegaudiodec.o pcm.o simple_idct.o \
      ratecontrol.o adpcm.o eval.o error_resilience.o \
      fft.o mdct.o raw.o golomb.o cabac.o\
      dpcm.o adx.o faandct.o parser.o g726.o \
      vp3dsp.o h264idct.o rangecoder.o pnm.o h263.o msmpeg4.o h263dec.o \
      opt.o

# currently using liba52 for ac3 decoding
ifeq ($(CONFIG_AC3),yes)
OBJS+= a52dec.o

# using builtin liba52 or runtime linked liba52.so.0
ifneq ($(CONFIG_A52BIN),yes)
OBJS+= liba52/bit_allocate.o liba52/bitstream.o liba52/downmix.o \
	liba52/imdct.o  liba52/parse.o liba52/crc.o liba52/resample.o
endif
endif

EXTRALIBS += -L../libavutil -lavutil$(BUILDSUF)

# currently using libdts for dts decoding
ifeq ($(CONFIG_DTS),yes)
OBJS+= dtsdec.o
CFLAGS += $(DTS_INC)
EXTRALIBS += -ldts
endif

ifeq ($(TARGET_GPROF),yes)
CFLAGS+=-p
LDFLAGS+=-p
endif

# i386 mmx specific stuff
ifeq ($(TARGET_MMX),yes)
OBJS += i386/fdct_mmx.o i386/cputest.o \
	i386/dsputil_mmx.o i386/mpegvideo_mmx.o \
	i386/idct_mmx.o i386/motion_est_mmx.o \
	i386/simple_idct_mmx.o i386/fft_sse.o i386/vp3dsp_mmx.o \
	i386/vp3dsp_sse2.o
ifeq ($(CONFIG_GPL),yes)
OBJS += i386/idct_mmx_xvid.o
endif
ifdef TARGET_BUILTIN_VECTOR
i386/fft_sse.o: CFLAGS+= -msse
depend: CFLAGS+= -msse
endif
endif

# armv4l specific stuff
ifeq ($(TARGET_ARCH_ARMV4L),yes)
ASM_OBJS += armv4l/jrevdct_arm.o armv4l/simple_idct_arm.o armv4l/dsputil_arm_s.o
OBJS += armv4l/dsputil_arm.o armv4l/mpegvideo_arm.o
ifeq ($(TARGET_IWMMXT),yes)
OBJS += armv4l/dsputil_iwmmxt.o armv4l/mpegvideo_iwmmxt.o
endif
endif

# sun mediaLib specific stuff
# currently only works when libavcodec is used in mplayer
ifeq ($(HAVE_MLIB),yes)
OBJS += mlib/dsputil_mlib.o
CFLAGS += $(MLIB_INC)
endif

# Intel IPP specific stuff
# currently only works when libavcodec is used in mplayer
ifeq ($(HAVE_IPP),yes)
CFLAGS += $(IPP_INC)
endif

# alpha specific stuff
ifeq ($(TARGET_ARCH_ALPHA),yes)
OBJS += alpha/dsputil_alpha.o alpha/mpegvideo_alpha.o \
	alpha/simple_idct_alpha.o alpha/motion_est_alpha.o
ASM_OBJS += alpha/dsputil_alpha_asm.o alpha/motion_est_mvi_asm.o
CFLAGS += -fforce-addr
endif

ifeq ($(TARGET_ARCH_POWERPC),yes)
OBJS += ppc/dsputil_ppc.o ppc/mpegvideo_ppc.o
endif

ifeq ($(TARGET_MMI),yes)
OBJS += ps2/dsputil_mmi.o ps2/idct_mmi.o ps2/mpegvideo_mmi.o
endif

ifeq ($(TARGET_ALTIVEC),yes)
OBJS += ppc/dsputil_altivec.o ppc/mpegvideo_altivec.o ppc/idct_altivec.o \
        ppc/fft_altivec.o ppc/gmc_altivec.o ppc/fdct_altivec.o \
        ppc/dsputil_h264_altivec.o
endif

ifeq ($(TARGET_ARCH_SH4),yes)
OBJS+= sh4/idct_sh4.o sh4/dsputil_sh4.o sh4/dsputil_align.o
endif

ifeq ($(TARGET_ARCH_SPARC),yes)
OBJS+=sparc/dsputil_vis.o
sparc/%.o: sparc/%.c
	$(CC) -mcpu=ultrasparc -mtune=ultrasparc $(CFLAGS) -c -o $@ $< 
endif
ifeq ($(TARGET_ARCH_SPARC64),yes)
CFLAGS+= -mcpu=ultrasparc -mtune=ultrasparc
endif

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S)
OBJS := $(OBJS) $(ASM_OBJS)

LIB= $(LIBPREF)avcodec$(LIBSUF)
LIBAVUTIL= $(SRC_PATH)/libavutil/$(LIBPREF)avutil$(LIBSUF)
ifeq ($(BUILD_SHARED),yes)
SLIB= $(SLIBPREF)avcodec$(SLIBSUF)
endif
TESTS= imgresample-test dct-test motion-test fft-test

all: $(LIB) $(SLIB)

amrlibs:
	$(MAKE) -C amr spclib fipoplib

tests: apiexample cpuid_test $(TESTS)

$(LIB): $(OBJS) $(AMRLIBS)
	rm -f $@
	$(AR) rc $@ $(OBJS) $(AMREXTRALIBS)
	$(RANLIB) $@

$(SLIB): $(OBJS)
ifeq ($(CONFIG_WIN32),yes)
	$(CC) $(SHFLAGS) -Wl,--output-def,$(@:.dll=.def) -o $@ $(OBJS) $(EXTRALIBS) $(AMREXTRALIBS)
	-lib /machine:i386 /def:$(@:.dll=.def)
else
	$(CC) $(SHFLAGS) -o $@ $(OBJS) $(EXTRALIBS) $(AMREXTRALIBS) $(LDFLAGS)
endif

dsputil.o: dsputil.c dsputil.h

%.o: %.c
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $< 

%.o: %.S
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

dep:	depend

clean: $(CLEANAMR)
	rm -f *.o *.d *~ .depend $(LIB) $(SLIB) *.so i386/*.o i386/*~ \
	   armv4l/*.o armv4l/*~ \
	   mlib/*.o mlib/*~ \
	   alpha/*.o alpha/*~ \
	   ppc/*.o ppc/*~ \
	   ps2/*.o ps2/*~ \
	   sh4/*.o sh4/*~ \
	   sparc/*.o sparc/*~ \
	   liba52/*.o liba52/*~ \
	   apiexample $(TESTS)

distclean: clean
	rm -f Makefile.bak .depend

cleanamr:
	$(MAKE) -C amr clean

cleanamrfloat:
	rm -f amr_float/*.o

cleanamrwbfloat:
	$(MAKE) -C amrwb_float -f makefile.gcc clean

# api example program
apiexample: apiexample.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) $(LIBAVUTIL) $(EXTRALIBS) -lm

# cpuid test
cpuid_test: i386/cputest.c
	$(CC) $(CFLAGS) -D__TEST__ -o $@ $<

# testing progs

imgresample-test: imgresample.c
	$(CC) $(CFLAGS) -DTEST -o $@ $^ -lm

dct-test: dct-test.o fdctref.o $(LIB)
	$(CC) -o $@ $^ -lm $(LIBAVUTIL)

motion-test: motion_test.o $(LIB)
	$(CC) -o $@ $^ -lm

fft-test: fft-test.o $(LIB)
	$(CC) -o $@ $^ $(LIBAVUTIL) -lm

ifeq ($(BUILD_SHARED),yes)
install: all install-headers
ifeq ($(CONFIG_WIN32),yes)
	install $(INSTALLSTRIP) -m 755 $(SLIB) "$(prefix)"
else
	install -d $(libdir)
	install $(INSTALLSTRIP) -m 755 $(SLIB) $(libdir)/libavcodec-$(VERSION).so
	ln -sf libavcodec-$(VERSION).so $(libdir)/libavcodec.so
	$(LDCONFIG) || true
endif
else
install:
endif

installlib: all install-headers
	install -m 644 $(LIB) "$(libdir)"

install-headers:
	mkdir -p "$(prefix)/include/ffmpeg"
	install -m 644 $(SRC_PATH)/libavcodec/avcodec.h \
                "$(prefix)/include/ffmpeg"
	install -d $(libdir)/pkgconfig
	install -m 644 ../libavcodec.pc $(libdir)/pkgconfig

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
