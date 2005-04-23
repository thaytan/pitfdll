noinst_LTLIBRARIES = libdmo.la

libdmo_la_SOURCES = \
	DMO_AudioDecoder.c \
	DMO_VideoDecoder.c \
	buffer.c \
	dmo.c \
	dmo_guids.c

noinst_HEADERS = \
	DMO_AudioDecoder.h \
	DMO_VideoDecoder.h \
	DMO_Filter.h \
	dmo.h \
	dmo_guids.h \
	dmo_interfaces.h

INCLUDES = \
	-DNOAVIFILE_HEADERS \
	-I$(srcdir)/.. \
	-I$(srcdir)/../wine
