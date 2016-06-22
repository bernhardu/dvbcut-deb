
QT += core gui xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = dvbcut
TEMPLATE = app

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
    types.h

FORMS += \
    dvbcutbase.ui \
    exportdialogbase.ui \
    mplayererrorbase.ui \
    progresswindowbase.ui

RESOURCES += \
    ../icons/icons.qrc

TRANSLATIONS += \
    dvbcut.ts \
    dvbcut_cs.ts \
    dvbcut_de.ts


qtPrepareTool(LRELEASE, lrelease)

l10n.commands = $$LRELEASE ${QMAKE_FILE_IN} -qm ${QMAKE_FILE_OUT}
l10n.input = TRANSLATIONS
l10n.output = ${QMAKE_FILE_BASE}.qm
l10n.CONFIG += no_link target_predeps
l10n.variable_out = l10ninst.files
QMAKE_EXTRA_COMPILERS += l10n


CONFIG += link_pkgconfig


system(pkg-config --exists libavformat) {
    PKGCONFIG += libavformat
} else { error(Please install development package libavformat-dev) }

system(pkg-config --exists libavcodec) {
    PKGCONFIG += libavcodec
} else { error(Please install development package libavcodec-dev) }

system(pkg-config --exists libavutil) {
    PKGCONFIG += libavutil
} else { error(Please install development package libavutil-dev) }

system(pkg-config --exists libswscale) {
    QMAKE_CXXFLAGS += -DHAVE_LIB_SWSCALE
    PKGCONFIG += libswscale
} else { error(Please install development package libswscale-dev) }

system(pkg-config --exists ao) {
    QMAKE_CXXFLAGS += -DHAVE_LIB_AO
    PKGCONFIG += ao
} else { error(Please install development package libao-dev) }

system(pkg-config --exists mad) {
    QMAKE_CXXFLAGS += -DHAVE_LIB_MAD
    PKGCONFIG += mad
} else { error(Please install development package libmad0-dev) }

QMAKE_CXXFLAGS += -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -D_FILE_OFFSET_BITS=64
