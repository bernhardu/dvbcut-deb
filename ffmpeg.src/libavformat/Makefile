#
# libavformat Makefile
# (c) 2000-2003 Fabrice Bellard
#
include ../config.mak

VPATH=$(SRC_PATH)/libavformat

CFLAGS=$(OPTFLAGS) -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavutil -I$(SRC_PATH)/libavcodec -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

OBJS= utils.o cutils.o os_support.o allformats.o
PPOBJS=

# mux and demuxes
OBJS+=mpeg.o mpegts.o mpegtsenc.o 

# file I/O
OBJS+= avio.o aviobuf.o file.o 

EXTRALIBS += -L../libavutil -lavutil$(BUILDSUF)

ifeq ($(TARGET_ARCH_SPARC64),yes)
CFLAGS+= -mcpu=ultrasparc -mtune=ultrasparc
endif

LIB= $(LIBPREF)avformat$(LIBSUF)
ifeq ($(BUILD_SHARED),yes)
SLIB= $(SLIBPREF)avformat$(SLIBSUF)

AVCLIBS+=-lavcodec$(BUILDSUF) -L../libavcodec
ifeq ($(CONFIG_MP3LAME),yes)
AVCLIBS+=-lmp3lame
endif
endif

SRCS := $(OBJS:.o=.c) $(PPOBJS:.o=.cpp)

all: $(LIB) $(SLIB)

$(LIB): $(OBJS) $(PPOBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS) $(PPOBJS)
	$(RANLIB) $@

$(SLIB): $(OBJS)
ifeq ($(CONFIG_WIN32),yes)
	$(CC) $(SHFLAGS) -Wl,--output-def,$(@:.dll=.def) -o $@ $(OBJS) $(PPOBJS) $(AVCLIBS) $(EXTRALIBS)
	-lib /machine:i386 /def:$(@:.dll=.def)
else
	$(CC) $(SHFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(PPOBJS) $(AVCLIBS) $(EXTRALIBS)
endif

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

ifeq ($(BUILD_SHARED),yes)
install: all install-headers
ifeq ($(CONFIG_WIN32),yes)
	install $(INSTALLSTRIP) -m 755 $(SLIB) "$(prefix)"
else
	install -d $(libdir)
	install $(INSTALLSTRIP) -m 755 $(SLIB) $(libdir)/libavformat-$(VERSION).so
	ln -sf libavformat-$(VERSION).so $(libdir)/libavformat.so
	$(LDCONFIG) || true
endif
else
install:
endif

installlib: all install-headers
	install -m 644 $(LIB) "$(libdir)"

install-headers:
	mkdir -p "$(prefix)/include/ffmpeg"
	install -m 644 $(SRC_PATH)/libavformat/avformat.h $(SRC_PATH)/libavformat/avio.h \
                "$(prefix)/include/ffmpeg"
#                $(SRC_PATH)/libavformat/rtp.h $(SRC_PATH)/libavformat/rtsp.h 
#                $(SRC_PATH)/libavformat/rtspcodes.h 
	install -d $(libdir)/pkgconfig
	install -m 644 ../libavformat.pc $(libdir)/pkgconfig

%.o: %.c
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $< 

# BeOS: remove -Wall to get rid of all the "multibyte constant" warnings
%.o: %.cpp
	g++ $(subst -Wall,,$(CFLAGS)) -c -o $@ $< 

distclean clean: 
	rm -f *.o *.d .depend *~ *.a *.so $(LIB)

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
