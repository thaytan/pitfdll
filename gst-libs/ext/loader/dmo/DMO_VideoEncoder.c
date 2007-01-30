/********************************************************

	DirectShow Video encoder implementation
	Copyright 2000 Eugene Kuznetsov  (divx@euro.ru)

*********************************************************/

#include "dshow/guids.h"
#include "dshow/interfaces.h"
#include "registry.h"
#ifdef LDT_paranoia
#include "../ldt_keeper.h"
#endif

#ifndef NOAVIFILE_HEADERS
#include "videoencoder.h"
#else
#include "libwin32.h"
#endif
#include "DMO_Filter.h"

#include "DMO_VideoEncoder.h"

#include <gst/gst.h>

struct _DMO_VideoEncoder {
  DMO_Filter * m_pDMO_Filter;
  
  DMO_MEDIA_TYPE m_sOurType;
  DMO_MEDIA_TYPE m_sDestType;
  
  VIDEOINFOHEADER * m_sVhdr;
  VIDEOINFOHEADER * m_sVhdr2;
    
  unsigned long in_buffer_size;
  unsigned long out_buffer_size;
  unsigned long in_align;
  unsigned long out_align;
  unsigned long lookahead;
  unsigned long inputs;
  unsigned long outputs;
};

#include "../wine/winerror.h"

#ifndef NOAVIFILE_HEADERS
#define VFW_E_NOT_RUNNING               0x80040226
#include "fourcc.h"
#include "except.h"
#endif

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#define __MODULE__ "DirectShow_VideoEncoder"

#define false 0
#define true 1

typedef struct _ct {
  fourcc_t fcc;
  unsigned int bits;
  const GUID * subtype;
  int cap;
  const char * name;
} ct;
            
static ct check[] = {
    { fccI420, 12, &MEDIASUBTYPE_I420,   CAP_I420 , "I420" },
    { fccYV12, 12, &MEDIASUBTYPE_YV12,   CAP_YV12 , "YV12" },
    { fccYUY2, 16, &MEDIASUBTYPE_YUY2,   CAP_YUY2 , "YUY2" },
    { fccUYVY, 16, &MEDIASUBTYPE_UYVY,   CAP_UYVY , "UYVY" },
    { fccYVYU, 16, &MEDIASUBTYPE_YVYU,   CAP_YVYU , "YVYU" },
    { fccIYUV, 24, &MEDIASUBTYPE_IYUV,   CAP_IYUV , "IYUV" },
    {       8,  8, &MEDIASUBTYPE_RGB8,   CAP_NONE , "RGB8" },
    {      15, 16, &MEDIASUBTYPE_RGB555, CAP_NONE , "RGB555" },
    {      16, 16, &MEDIASUBTYPE_RGB565, CAP_NONE , "RGB565" },
    {      24, 24, &MEDIASUBTYPE_RGB24,  CAP_NONE , "RGB24" },
    {      32, 32, &MEDIASUBTYPE_RGB32,  CAP_NONE , "RGB32" },
    {       0,  0,                NULL,         0 , "NULL" }
};

int DMO_VideoEncoder_GetOutputInfos (DMO_VideoEncoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align)
{
  if (!this || !out_buffer_size || !out_align)
    return FALSE;
  
  *out_buffer_size = this->out_buffer_size;
  *out_align = this->out_align;
  
  return TRUE;
}

int DMO_VideoEncoder_GetInputInfos (DMO_VideoEncoder * this, 
                                    unsigned long * in_buffer_size,
                                    unsigned long * in_align,
                                    unsigned long * lookahead)
{
  if (!this || !in_buffer_size || !in_align || !lookahead)
    return FALSE;
  
  *in_buffer_size = this->in_buffer_size;
  *in_align = this->in_align;
  *lookahead = this->lookahead;
  
  return TRUE;
}

DMO_VideoEncoder * DMO_VideoEncoder_Open (char * dllname, GUID * guid,
                                          BITMAPINFOHEADER * format,
                                          unsigned int dest_fourcc,
                                          unsigned int vbr,
                                          unsigned long bitrate,
                                          int fps_n, int fps_d,
                                          char ** data,
                                          unsigned long * data_length)
{
  DMO_VideoEncoder * this;
  HRESULT result;
  ct * c;
  char * error_message = NULL;
  VARIANT varg;
  
  this = malloc (sizeof (DMO_VideoEncoder));
  if (!this)
    return NULL;
  memset (this, 0, sizeof (DMO_VideoEncoder));

#ifdef LDT_paranoia
    Setup_LDT_Keeper ();
#endif
    
  /* Defining incoming type, raw video frames*/
  this->m_sVhdr = (VIDEOINFOHEADER *) malloc (sizeof (VIDEOINFOHEADER));
  memset (this->m_sVhdr, 0, sizeof (VIDEOINFOHEADER));
  
  this->m_sVhdr->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
  this->m_sVhdr->bmiHeader.biWidth = format->biWidth;
  this->m_sVhdr->bmiHeader.biHeight = format->biHeight;
  this->m_sVhdr->bmiHeader.biPlanes = 1;
  this->m_sVhdr->bmiHeader.biBitCount = format->biBitCount;
  this->m_sVhdr->bmiHeader.biCompression = format->biCompression;
  this->m_sVhdr->bmiHeader.biSizeImage = format->biWidth * format->biHeight * format->biBitCount / 8;

  this->m_sVhdr->dwBitRate = fps_n / fps_d * this->m_sVhdr->bmiHeader.biSizeImage * 8;
  this->m_sVhdr->AvgTimePerFrame = gst_util_uint64_scale_int (10000000,
      fps_d, fps_n);
  
  this->m_sVhdr->rcSource.left = this->m_sVhdr->rcSource.top = 0;
  this->m_sVhdr->rcSource.right = this->m_sVhdr->bmiHeader.biWidth;
  this->m_sVhdr->rcSource.bottom = this->m_sVhdr->bmiHeader.biHeight;
  this->m_sVhdr->rcTarget = this->m_sVhdr->rcSource;
  
  memset (&this->m_sOurType, 0, sizeof (this->m_sOurType));
  
  this->m_sOurType.majortype = MEDIATYPE_Video;
  /* Trying to find the GUID of our type */
  if (format->biCompression == 0) { /* This is RGB */
    if (format->biBitCount == 0) {
      printf ("we can't setup an encoder for RGB with 0 bpp\n");
      goto beach;
    }
    for (c = check; c->bits; c++) {
      if (c->fcc == format->biBitCount) {
        this->m_sOurType.subtype = *c->subtype;
      }
    }
  }
  else { /* This is YUV */
    for (c = check; c->bits; c++) {
      if (c->fcc == format->biCompression) {
        this->m_sOurType.subtype = *c->subtype;
        /* We trust our table more than the input format */
        this->m_sVhdr->bmiHeader.biBitCount = c->bits;
        format->biBitCount = c->bits;
      }
    }
  }
  this->m_sOurType.bFixedSizeSamples = true;
  this->m_sOurType.bTemporalCompression = false;
  this->m_sOurType.lSampleSize = this->m_sVhdr->bmiHeader.biSizeImage;
  this->m_sOurType.formattype = FORMAT_VideoInfo;
  this->m_sOurType.cbFormat = sizeof (VIDEOINFOHEADER);
  this->m_sOurType.pbFormat = (char*) this->m_sVhdr;
  this->m_sOurType.pUnk = NULL;
  
  /* Defining outgoing type compressed video bitstream */
  this->m_sVhdr2 = (VIDEOINFOHEADER *) malloc (sizeof (VIDEOINFOHEADER));
  memset (this->m_sVhdr2, 0, sizeof (VIDEOINFOHEADER));
  
  this->m_sVhdr2->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
  this->m_sVhdr2->bmiHeader.biWidth = format->biWidth;
  this->m_sVhdr2->bmiHeader.biHeight = format->biHeight;
  this->m_sVhdr2->bmiHeader.biPlanes = 1;
  this->m_sVhdr2->bmiHeader.biCompression = dest_fourcc;
  this->m_sVhdr2->bmiHeader.biBitCount = format->biBitCount;
  this->m_sVhdr2->bmiHeader.biSizeImage = format->biWidth * format->biHeight * format->biBitCount / 8;
  
  this->m_sVhdr2->rcSource = this->m_sVhdr->rcSource;
  this->m_sVhdr2->rcTarget = this->m_sVhdr->rcTarget;
  if (vbr)
    this->m_sVhdr2->dwBitRate = 128016; /* FIXME */
  else
    this->m_sVhdr2->dwBitRate = bitrate;
  this->m_sVhdr2->dwBitErrorRate = 0;
  this->m_sVhdr2->AvgTimePerFrame = gst_util_uint64_scale_int (10000000,
      fps_d, fps_n);
  
  memset (&this->m_sDestType, 0, sizeof (this->m_sDestType));
  
  this->m_sDestType.majortype = MEDIATYPE_Video;
  this->m_sDestType.subtype = MEDIATYPE_Video;
  this->m_sDestType.subtype.f1 = dest_fourcc;
  this->m_sDestType.formattype = FORMAT_VideoInfo;
  this->m_sDestType.bFixedSizeSamples = false;
  this->m_sDestType.bTemporalCompression = true;
  this->m_sDestType.cbFormat = sizeof (VIDEOINFOHEADER);
  this->m_sDestType.pbFormat = (char *) this->m_sVhdr2;
  this->m_sDestType.pUnk = NULL;
  
  /* print_video_header (this->m_sVhdr); */
 
  /* Creating DMO Filter */
  this->m_pDMO_Filter = DMO_Filter_Create (dllname, guid, &this->inputs,
                                           &this->outputs, &error_message);
  if (!this->m_pDMO_Filter)
    goto beach;
  
  if (vbr) { /* VBR 1 pass */
    varg.n1.n2.vt = VT_BOOL;
    varg.n1.n2.n3.boolVal = TRUE;
  
    if (!DMO_Filter_SetProperty (this->m_pDMO_Filter,
                                 (const WCHAR *) g_wszWMVCVBREnabled, &varg,
                                 &error_message))
      goto beach;
  
    varg.n1.n2.vt = VT_I4;
    varg.n1.n2.n3.lVal = 1;
  
    if (!DMO_Filter_SetProperty (this->m_pDMO_Filter,
                                 (const WCHAR *) g_wszWMVCPassesUsed, &varg,
                                 &error_message))
      goto beach;
    
    /* Setting Quality */
    varg.n1.n2.vt = VT_I4;
    varg.n1.n2.n3.lVal = bitrate;
    
    if (!DMO_Filter_SetProperty (this->m_pDMO_Filter,
                                 (const WCHAR *) g_wszWMVCVBRQuality, &varg,
                                 &error_message))
      goto beach;
  }
  
  /* Input first and then output as we are an encoder */
  if (!DMO_Filter_SetInputType (this->m_pDMO_Filter, 0, &this->m_sOurType,
                                &error_message))
    goto beach;
  
  /* Try a set partial output */
  if (!DMO_Filter_SetPartialOutputType (this->m_pDMO_Filter, data_length,
                                        data, &this->m_sDestType,
                                        &error_message))
    goto beach;
  
  /* Add additional data to our structure */
  if (*data_length) {
    this->m_sDestType.cbFormat = sizeof (VIDEOINFOHEADER) + *data_length;
    this->m_sVhdr2->bmiHeader.biSize += *data_length;
    this->m_sVhdr2 = (VIDEOINFOHEADER *) realloc (this->m_sVhdr2,
                                                  this->m_sDestType.cbFormat);
    memcpy ((char *) this->m_sVhdr2 + sizeof (VIDEOINFOHEADER), *data,
            *data_length);
  }
  
  /* print_video_header (this->m_sVhdr2); */
    
  /* Now use that data in dest type and set the output type definitely */
  if (!DMO_Filter_SetOutputType (this->m_pDMO_Filter, 0, &this->m_sDestType,
                                 &error_message))
    goto beach;
  
  /* Getting informations about buffers */
  if (!DMO_Filter_GetOutputSizeInfo (this->m_pDMO_Filter, 0,
                                     &this->out_buffer_size, &this->out_align,
                                     &error_message))
    goto beach;
  
  if (!DMO_Filter_GetInputSizeInfo (this->m_pDMO_Filter, 0,
                                    &this->in_buffer_size, &this->lookahead,
                                    &this->in_align, &error_message))
    goto beach;
  
  if (!DMO_Filter_Discontinuity (this->m_pDMO_Filter, &error_message))
    goto beach;
  
  return this;
  
beach:
  if (this->m_pDMO_Filter)
    DMO_Filter_Destroy (this->m_pDMO_Filter);
  if (error_message) {
    printf ("Failed creating a video encoder: %s\n", error_message);
    free (error_message);
  }
  free(this->m_sVhdr);
  free(this->m_sVhdr2);
  free(this);
  return NULL;
}

void DMO_VideoEncoder_Destroy (DMO_VideoEncoder * this)
{
  if (!this)
    return;
  
  if (this->m_sVhdr) {
    free(this->m_sVhdr);
    this->m_sVhdr = NULL;
  }
  if (this->m_sVhdr2) {
    free(this->m_sVhdr2);
    this->m_sVhdr2 = NULL;
  }
  if (this->m_pDMO_Filter) {
    DMO_Filter_Destroy (this->m_pDMO_Filter);
    this->m_pDMO_Filter = NULL;
  }
}

int DMO_VideoEncoder_ProcessInput (DMO_VideoEncoder * this,
                                   unsigned long long timestamp,
                                   unsigned long long duration,
                                   const void * in_data, unsigned int in_size,
                                   unsigned int * size_read)
{
  CMediaBuffer * bufferin;
  unsigned long read = 0;
  int r = 0;
  
  if (!in_data)
	  return -1;

#ifdef LDT_paranoia
    Setup_FS_Segment ();
#endif

  /* REFERENCETIME is in 100 nanoseconds */
  timestamp /= 100;
  duration /= 100;  
  
  /* Creating the IMediaBuffer containing input data */
  bufferin = CMediaBufferCreate (in_size, (void *) in_data, in_size, 1);
  
  if (duration) {
    r = this->m_pDMO_Filter->m_pMedia->vt->ProcessInput (
        this->m_pDMO_Filter->m_pMedia, 0, (IMediaBuffer *) bufferin,
				DMO_INPUT_DATA_BUFFERF_TIME |
        DMO_INPUT_DATA_BUFFERF_TIMELENGTH |
        DMO_INPUT_DATA_BUFFERF_SYNCPOINT,
        timestamp, duration);
  }
  else {
    r = this->m_pDMO_Filter->m_pMedia->vt->ProcessInput (
          this->m_pDMO_Filter->m_pMedia, 0, (IMediaBuffer *) bufferin,
				  DMO_INPUT_DATA_BUFFERF_SYNCPOINT, 0, 0);
  }

 ((IMediaBuffer *) bufferin)->vt->GetBufferAndLength ((IMediaBuffer *) bufferin,
                                                      0, &read);

  ((IMediaBuffer *) bufferin)->vt->Release((IUnknown *) bufferin);

  if (size_read)
	  *size_read = read;
  
  if (r == S_OK || (unsigned) r == DMO_E_NOTACCEPTING) {
    /* Output data waiting for us to process it, so we won't accept more. */
    return FALSE;
  }
  else {
    /* Send us more data please */
    return TRUE;
  }
}

int DMO_VideoEncoder_ProcessOutput (DMO_VideoEncoder * this,
                                    void * out_data, unsigned int out_size,
                                    unsigned int * size_written,
                                    unsigned long long * timestamp,
                                    unsigned long long * duration,
                                    unsigned int * key_frame)
{
  DMO_OUTPUT_DATA_BUFFER * db = NULL;
  unsigned long written = 0, status = 0, index;
  int r = 0;
  
  if (!out_data)
	  return -1;

#ifdef LDT_paranoia
    Setup_FS_Segment ();
#endif
  
  db = malloc (sizeof (DMO_OUTPUT_DATA_BUFFER) * this->outputs);
  if (!db)
    return 0;
  
  for (index = 0; index < this->outputs; index++) {
    db[index].rtTimestamp = 0;
    db[index].rtTimelength = 0;
    db[index].dwStatus = 0;
    if (index == 0)
      db[index].pBuffer = (IMediaBuffer *) CMediaBufferCreate (out_size,
                                                               out_data, 0, 0);
    else
      db[index].pBuffer = NULL;
  }
  
	r = this->m_pDMO_Filter->m_pMedia->vt->ProcessOutput (
                                     this->m_pDMO_Filter->m_pMedia,
                                     DMO_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER,
                                     this->outputs, db, &status);
    
  /* printf ("dwStatus is %d r is %d 0x%X\n", db[0].dwStatus, r, r); */

	((IMediaBuffer *) db[0].pBuffer)->vt->GetBufferAndLength (
                                       (IMediaBuffer *) db[0].pBuffer, NULL,
                                       &written);
	((IMediaBuffer *) db[0].pBuffer)->vt->Release ((IUnknown *) db[0].pBuffer);
  
  
  if (size_written)
	  *size_written = written;
  
  if (timestamp && duration &&
      (db[0].dwStatus & DMO_OUTPUT_DATA_BUFFERF_TIME) &&
      (db[0].dwStatus & DMO_OUTPUT_DATA_BUFFERF_TIMELENGTH)) {
    *timestamp = db[0].rtTimestamp * 100;
    *duration = db[0].rtTimelength * 100;
  }
  
  /* Is that a key frame ? */
  if (key_frame && (db[0].dwStatus & DMO_OUTPUT_DATA_BUFFERF_SYNCPOINT)) {
    *key_frame = TRUE;
  }
  else if (key_frame) {
    *key_frame = FALSE;
  }
  
  if ((db[0].dwStatus & DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE) != 0) {
    /* printf ("I have more data to output\n"); */
    free (db);
    return TRUE;
  }
  else {
    free (db);
    return FALSE;
  }
}
