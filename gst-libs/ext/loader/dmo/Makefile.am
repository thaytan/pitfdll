noinst_LTLIBRARIES = libdmo.la

libdmo_la_SOURCES = \
	DMO_AudioDecoder.c \
	DMO_AudioEncoder.c \
	DMO_VideoDecoder.c \
	DMO_VideoEncoder.c \
	buffer.c \
	dmo.c \
	dmo_guids.c

libdmo_la_CFLAGS = $(GST_CFLAGS)
libdmo_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = \
	DMO_AudioDecoder.h \
	DMO_AudioEncoder.h \
	DMO_VideoDecoder.h \
	DMO_VideoEncoder.h \
	DMO_Filter.h \
	dmo.h \
	dmo_guids.h \
	dmo_interfaces.h \
	variant.h

INCLUDES = \
	-DNOAVIFILE_HEADERS \
	-I$(srcdir)/.. \
	-I$(srcdir)/../wine
