export DESTDIR

build:
	scons $(if $(FFMPEG),FFMPEG=$(FFMPEG)) \
		$(if $(DEBUG),DEBUG=$(DEBUG))

clean:
	scons --clean
	
install:
	scons $(if $(FFMPEG),FFMPEG=$(FFMPEG)) \
		$(if $(PREFIX),PREFIX=$(PREFIX)) \
		$(if $(BINDIR),BINDIR=$(BINDIR)) \
		$(if $(MANPATH),MANPATH=$(MANPATH)) \
		$(if $(DEBUG),DEBUG=$(DEBUG)) \
		install

distclean: clean
	@rm -Rfv *~ */*~ .sconsign* */.sconsign* .sconf_temp config.log ffmpeg tags
ifeq ($(wildcard ffmpeg.src/),ffmpeg.src/)
	@touch ffmpeg.src/config.mak
	@make -C ffmpeg.src/libavformat distclean
	@make -C ffmpeg.src/libavutil distclean
	@make -C ffmpeg.src/ distclean
	@rm -Rfv ffmpeg.src/config.log ffmpeg.src/*.pc ffmpeg.src/*/*.a \
		latex html xml dvbcut.tag
endif
