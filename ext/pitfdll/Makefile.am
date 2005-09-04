plugin_LTLIBRARIES = libpitfdll.la

INCLUDES = \
	-DWIN32_PATH=\"/usr/lib/win32\" \
	-I$(top_srcdir)/gst-libs/ext/loader/dmo \
	-I$(top_srcdir)/gst-libs/ext/loader/dshow \
	-I$(top_srcdir)/gst-libs/ext/loader/wine

libpitfdll_la_SOURCES = \
	pitfdll.c \
	dshow_vdec.c \
	dshow_adec.c \
	dmo_vdec.c \
	dmo_venc.c \
	dmo_adec.c \
	dmo_aenc.c \
	qt_adec.c

libpitfdll_la_CFLAGS = \
	$(GST_CFLAGS)
libpitfdll_la_LIBADD = \
	$(top_builddir)/gst-libs/ext/loader/wine/libloader.la \
	$(top_builddir)/gst-libs/ext/loader/dshow/libdshow.la \
	$(top_builddir)/gst-libs/ext/loader/dmo/libdmo.la
libpitfdll_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lstdc++

noinst_HEADERS = \
	general.h \
	pitfdll.h
