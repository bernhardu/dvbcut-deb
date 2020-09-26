# configure.ac - Configure template for dvbcut.
# Process this file with autoconf to produce a configure script.
# Copyright (C) 2007 - 2009 Michael Riepe

#@(#) $Id$

AC_INIT(src/dvbcut.cpp)

AC_PREREQ(2.13)

AC_CANONICAL_SYSTEM

CONFIGURE_ARGS="$ac_configure_args"
AC_SUBST(CONFIGURE_ARGS)

dnl Checks for programs.
AC_PROG_MAKE_SET
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_CHECK_TOOL(STRIP, strip, :)
AC_CHECK_TOOL(PKG_CONFIG, pkg-config, :)

AC_CYGWIN
AC_MINGW32
AC_EXEEXT
AC_OBJEXT

if test "$MINGW32" = yes; then
  CPPFLAGS="$CPPFLAGS -I/usr/local/include"
  LDFLAGS="$LDFLAGS -L/usr/local/lib"
fi

dnl check for library dir
mr_libdirname=lib
set -- `LC_ALL=C $CC -print-search-dirs | sed -n -e 's,^libraries: ,,p' | sed -e 's,^=,,' | sed -e 's,:, ,'g`
for ac_dir; do
  case `cd $ac_dir 2>/dev/null && pwd` in
    */lib32 | */lib32/) mr_libdirname=lib32; break;;
    */lib64 | */lib64/) mr_libdirname=lib64; break;;
  esac
done

dnl Checks for libraries.

FFMPEG_LIBS='-lavformat -lavcodec -lavutil'
AC_SUBST(FFMPEG_LIBS)

AC_CHECK_LIB(swscale, main,
  [AC_DEFINE(HAVE_LIB_SWSCALE, 1, [Define this if you have libswscale.])
  FFMPEG_LIBS="$FFMPEG_LIBS -lswscale"])

AC_CHECK_LIB(mad, mad_decoder_init,
  [AC_DEFINE(HAVE_LIB_MAD, 1, [Define this if you have libmad.])
  LIBS="$LIBS -lmad"],
  [AC_MSG_WARN([Did not find mad library])])
AC_CHECK_LIB(a52, a52_init,
  [AC_DEFINE(HAVE_LIB_A52, 1, [Define this if you have liba52.])
  LIBS="$LIBS -la52 -lm"],
  [AC_MSG_ERROR([Did not find required a52 library])],
  [-lm])

mr_other_libs=
if test "$MINGW32" = yes; then
  mr_other_libs=-lwinmm
fi

AC_CHECK_LIB(ao, ao_initialize,
  [AC_DEFINE(HAVE_LIB_AO, 1, [Define this if you have libao.])
  LIBS="$LIBS -lao $mr_other_libs"],
  [AC_MSG_WARN([Did not find libao - dvbcut will not play audio])],
  [$mr_other_libs])

STDLIB=
AC_SUBST(STDLIB)

if test "$MINGW32" = yes; then
  CPPFLAGS="-I\$(top_srcdir)/import $CPPFLAGS"
  STDLIB='$(top_srcdir)/import/stdlib.cpp'
fi

dnl AC_CHECK_LIB(m, sqrt, [LIBS="$LIBS -lm"])

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(ao/ao.h mad.h stdint.h a52dec/a52.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

dnl Checks for library functions.
AC_FUNC_MMAP

dnl external stuff
PKG_CHECK_MODULES([QT5], [Qt5Core >= 5.3 Qt5Gui >= 5.3 Qt5Xml >= 5.3 Qt5Widgets >= 5.3])
AC_SUBST([QT5_CFLAGS])
AC_SUBST([QT5_LIBS])

#error "You must build your code with position independent code if Qt was built with -reduce-relocations. " \
#      "Compile your code with -fPIC (-fPIE is not enough)."
#
# https://bugreports.qt.io/browse/QTBUG-50156
# With Qt5 pkg-config reports no '-fPIC', but header file complains that it is needed.
#
CPPFLAGS="$CPPFLAGS -fPIC $QT5_CFLAGS"
LIBS="$LIBS $QT5_LIBS"

AC_CHECK_PROGS(MOC, [moc-qt5 moc])
AC_CHECK_PROGS(UIC, [uic-qt5 uic])
AC_CHECK_PROGS(RCC, [rcc-qt5 rcc])
AC_CHECK_PROGS(LRELEASE, [lrelease-qt5 lrelease])
if test -z "$MOC" || test -z "$UIC" || test -z "$RCC" || test -z "$LRELEASE"; then
   AC_MSG_ERROR([Qt utility programs moc, uic, rcc and lrelease are required.])
fi

AC_DEFINE(__STDC_LIMIT_MACROS, 1, [Required for C++])
AC_DEFINE(__STDC_CONSTANT_MACROS, 1, [Required for C++])
AC_DEFINE(_FILE_OFFSET_BITS, 64, [We are always using large files])
dnl AC_DEFINE(_LARGEFILE_SOURCE)

AC_OUTPUT([Makefile src/Makefile dvbcut.desktop])

# vi: set ts=8 sw=2 :
