dnl autoconf configuration file for gst-ffmpeg 
AC_INIT(pitfdll, 0.9.1.1)
AC_CANONICAL_TARGET

dnl We disable static building for development, for time savings
dnl this goes before AS_LIBTOOL to appease autoconf
dnl *NOTE*: dnl this line before release, so release does static too
AM_DISABLE_STATIC

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION
AS_NANO(PITFDLL_GIT="no", PITFDLL_GIT="yes")

dnl we cheat here so we don't have to change the actual configure code bit
GST_PLUGINS_VERSION_NANO=$PITFDLL_VERSION_NANO

dnl required GStreamer version
GST_REQ=0.9.1

AM_INIT_AUTOMAKE([-Wno-portability])

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)
                                                                                
dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
AS_LIBTOOL(GST_PITFDLL, 1, 0, 0)
AM_PROG_LIBTOOL()

AC_CONFIG_SRCDIR(configure.ac)
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 $ACLOCAL_FLAGS")

AM_PROG_AS
AC_PROG_CC
AM_PROG_CC_STDC

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, GST_WALL="yes", GST_WALL="no")

if test "x$GST_WALL" = "xyes"; then
   GST_ERROR="$GST_ERROR -Wall"

   if test "x$PITFDLL_GIT" = "xyes"; then
     AS_COMPILER_FLAG(-Werror,GST_ERROR="$GST_ERROR -Werror",GST_ERROR="$GST_ERROR")
     GST_ERROR="$GST_ERROR -DG_DISABLE_DEPRECATED"
   fi
fi

AC_HEADER_STDC([])
AC_CHECK_HEADERS(sys/mman.h)

dnl check for gstreamer; uninstalled is selected preferentially -- see pkg-config(1)
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ \
		       gstreamer-base-$GST_MAJORMINOR >= $GST_REQ \
		       gstreamer-plugins-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST="yes", HAVE_GST="no")
                                                                                
if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(no GStreamer found)
fi

dnl =========================== win32 dll dir =================================

AC_ARG_WITH([dlldir],
   AC_HELP_STRING([--with-dlldir=DIR], [look for DLLs in DIR, default=LIBDIR/win32]),
   [dlldir="$withval"], [dlldir="\$(libdir)/win32"])
AC_SUBST(dlldir)

dnl ============================= gst plug-ins ================================

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

AC_CONFIG_FILES([
Makefile
ext/Makefile
ext/pitfdll/Makefile
gst-libs/Makefile
gst-libs/ext/Makefile
gst-libs/ext/loader/Makefile
gst-libs/ext/loader/dmo/Makefile
gst-libs/ext/loader/dshow/Makefile
gst-libs/ext/loader/wine/Makefile
gst-libs/ext/loader/qtx/Makefile
gst-libs/ext/loader/qtx/qtxsdk/Makefile
pitfdll.spec
])
AC_OUTPUT
