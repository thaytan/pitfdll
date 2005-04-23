#ifndef __GENERAL_H__
#define __GENERAL_H__

#include <glib.h>

typedef struct _GUID {
  guint32 a;
  guint16 b, c;
  guint8 d1, d2, d3, d4, d5, d6, d7, d8;
} GUID;

typedef struct _BITMAPINFOHEADER {
  guint32 size;
  guint32 width;
  guint32 height;
  guint16 planes;
  guint16 bit_cnt;
  guint32 compression;
  guint32 image_size;
  guint32 xpels_meter;
  guint32 ypels_meter;
  guint32 num_colors;        /* used colors */
  guint32 imp_colors;        /* important colors */
  /* may be more for some codecs */
} BITMAPINFOHEADER;

typedef struct _CodecEntry {
  /* dll */
  gchar *dll;
  GUID guid;
  guint32 fourcc;

  /* gst */
  gchar *caps;
} CodecEntry;

#endif /* __GENERAL_H__ */
