
QT += core gui xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = dvbcut
TEMPLATE = app


QMAKE_CXXFLAGS += \
    -DHAVE_LIB_SWSCALE=1 \
    -DHAVE_LIB_MAD=1 \
    -DHAVE_LIB_A52=1 \
    -DHAVE_LIB_AO=1 \
    -DSTDC_HEADERS=1 \
    -DHAVE_AO_AO_H=1 \
    -DHAVE_MAD_H=1 \
    -DHAVE_STDINT_H=1 \
    -DHAVE_A52DEC_A52_H=1 \
    -DHAVE_UNISTD_H=1 \
    -DHAVE_GETPAGESIZE=1 \
    -DHAVE_MMAP=1 \
    -D__STDC_LIMIT_MACROS=1 \
    -D__STDC_CONSTANT_MACROS=1 \
    -D_FILE_OFFSET_BITS=64 \
    -D_FORTIFY_SOURCE=2 \
    -DQT_SHARED

QMAKE_CXXFLAGS += \
    -I/usr/include \
    -I/usr/include/libavcodec \
    -I/usr/include/libavformat \
    -I/usr/include/libswscale \
    -I/include

LIBS += \
    -lavcodec \
    -lavformat \
    -lswscale \
    -lavutil \
    -lao \
    -la52 \
    -lmad

SOURCES += \
    avframe.cpp \
    buffer.cpp \
    differenceimageprovider.cpp \
    dvbcut.cpp \
    eventlistitem.cpp \
    exception.cpp \
    exportdialog.cpp \
    imageprovider.cpp \
    index.cpp \
    lavfmuxer.cpp \
    logoutput.cpp \
    main.cpp \
    mpegmuxer.cpp \
    mpgfile.cpp \
    mplayererrorbase.cpp \
    playaudio.cpp \
    progressstatusbar.cpp \
    progresswindow.cpp \
    psfile.cpp \
    pts.cpp \
    settings.cpp \
    streamdata.cpp \
    tsfile.cpp

HEADERS += \
    avframe.h \
    buffer.h \
    busyindicator.h \
    defines.h \
    differenceimageprovider.h \
    dvbcut.h \
    eventlistitem.h \
    exception.h \
    exportdialog.h \
    imageprovider.h \
    index.h \
    lavfmuxer.h \
    logoutput.h \
    mpegmuxer.h \
    mpgfile.h \
    mplayererrorbase.h \
    muxer.h \
    playaudio.h \
    port.h \
    progressstatusbar.h \
    progresswindow.h \
    psfile.h \
    pts.h \
    settings.h \
    streamdata.h \
    stream.h \
    streamhandle.h \
    tsfile.h \
    types.h \
    version.h

FORMS += \
    dvbcutbase.ui \
    exportdialogbase.ui \
    mplayererrorbase.ui \
    progresswindowbase.ui

RESOURCES += \
    ../icons/icons.qrc

TRANSLATIONS += \
    dvbcut_cs.ts \
    dvbcut_en.ts
