#ifndef __GENERAL_H__
#define __GENERAL_H__

#include <glib.h>

typedef struct _GUID {
  guint32 a;
  guint16 b, c;
  guint8 d1, d2, d3, d4, d5, d6, d7, d8;
} GUID;

typedef struct __attribute__((__packed__)) _BITMAPINFOHEADER {
  long size;
  long width;
  long height;
  short planes;
  short bit_cnt;
  long compression;
  long image_size;
  long xpels_meter;
  long ypels_meter;
  long num_colors;        /* used colors */
  long imp_colors;        /* important colors */
  /* may be more for some codecs */
} BITMAPINFOHEADER;

typedef struct __attribute__((__packed__)) _WAVEFORMATEX { 
  unsigned short  wFormatTag; 
  unsigned short  nChannels; 
  unsigned long nSamplesPerSec; 
  unsigned long nAvgBytesPerSec; 
  unsigned short  nBlockAlign; 
  unsigned short  wBitsPerSample; 
  unsigned short  cbSize; 
} WAVEFORMATEX; 
 
typedef struct _CodecEntry {
  /* dll */
  gchar *dll;
  GUID guid;
  guint32 format;
  guint32 version;
  gchar *friendly_name;

  /* gst */
  gchar *caps;
} CodecEntry;

#endif /* __GENERAL_H__ */
