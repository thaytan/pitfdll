noinst_LTLIBRARIES = libdshow.la

libdshow_la_SOURCES = \
	allocator.c \
	cmediasample.c \
	DS_AudioDecoder.c \
	DS_Filter.c \
	DS_VideoDecoder.c \
	guids.c \
	inputpin.c \
	outputpin.c

noinst_HEADERS = \
	allocator.h \
	cmediasample.h \
	DS_AudioDecoder.h \
	DS_Filter.h \
	DS_VideoDecoder.h \
	guids.h \
	inputpin.h \
	interfaces.h \
	iunk.h \
	outputpin.h

INCLUDES = \
	-DNOAVIFILE_HEADERS \
	-I$(srcdir)/../wine \
	-I$(srcdir)/..
