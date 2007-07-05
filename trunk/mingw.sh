#! /bin/sh
make -C src -f Makefile.w32
exit
# original version (included for reference only):
TOP=$(pwd)
which gcc
if true; then
echo "*** Building FFMPEG..."
(cd ffmpeg.src;./configure --prefix=$TOP/ffmpeg --enable-mingw32 --enable-gpl --disable-decoders --enable-memalign-hack --disable-encoders --disable-ffplay --disable-ffserver --disable-vhook	--disable-zlib --disable-network --disable-dv1394 --disable-bktr --disable-v4l --disable-audio-beos --disable-audio-oss --enable-codec=mpeg2encoder --enable-codec=mp2_encoder  --enable-codec=ac3_decoder  --enable-codec=ac3_encoder --enable-a52 --disable-mmx --disable-debug)
make -C ffmpeg.src installlib
make -C ffmpeg.src clean
fi
cd src
#qt3to4.exe *.cpp *.h

UI="dvbcutbase.ui mplayererrorbase.ui progresswindowbase.ui exportdialogbase.ui"
MQT=../../qt-3
export PATH=$MQT/bin:$PATH
if true; then
for i in $UI;do 
BASE=$(basename $i .ui)
if ! uic -o $BASE.h $BASE.ui; then exit 1; fi
if ! uic -impl $BASE.h -o $BASE.cpp $BASE.ui; then exit 1; fi
done

for i in *.h; do 
echo $i 
moc -o moc_$(basename $i .h).cpp $i
done

#pour la partie qt on passe sous mingw
SOURCES="avframe.cpp differenceimageprovider.cpp buffer.cpp dvbcut.cpp eventlistitem.cpp exception.cpp exportdialog.cpp imageprovider.cpp  index.cpp  lavfmuxer.cpp  logoutput.cpp  main.cpp  mpegmuxer.cpp  mpgfile.cpp  playaudio.cpp  progressstatusbar.cpp  progresswindow.cpp  psfile.cpp  pts.cpp  streamdata.cpp  tsfile.cpp dvbcutbase.cpp mplayererrorbase.cpp progresswindowbase.cpp exportdialogbase.cpp settings.cpp $(ls moc*.cpp) ../import/stdlib.cpp"
OPT=-O2
for i in $SOURCES; do
OBJ=$(basename $i .cpp).o
echo "*** Compiling $i..."
if ! g++ $OPT -I ../ffmpeg/include -I ../import -I /usr/local/include -I . -DQT3_SUPPORT -DHAVE_LIB_MAD -DHAVE_LIB_A52 -DHAVE_LIB_AO -I $MQT/include/Qt -I $MQT/include/QtCore -I $MQT/include/QtGui  -I $MQT/include/Qt3Support -I $MQT/include $i  -c  ; then
exit 1;
fi


done
fi

echo "*** Linking..."
g++  $OPT *.o -o ../bin/dvbcut.exe -L $MQT/lib -L../ffmpeg/lib -L/usr/local/lib -lavformat -lavcodec -lavutil -lqt-mt -lmad -la52 -lao -lwinmm -lmpeg2

# make clean...(someone should write a real Makefile)
rm *.o
rm moc_*
for i in $UI;do 
 BASE=$(basename $i .ui)
 rm $BASE.h $BASE.cpp
done
