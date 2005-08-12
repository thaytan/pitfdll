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

DMO_VideoEncoder * DMO_VideoEncoder_Open (char * dllname, GUID * guid,
                                          BITMAPINFOHEADER * format,
                                          unsigned int dest_fourcc,
                                          unsigned long bitrate,
                                          float framerate)
{
  DMO_VideoEncoder * this;
  HRESULT result;
  ct * c;
  char * error_message = NULL;
                        
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
  this->m_sVhdr->bmiHeader.biSizeImage = labs (format->biWidth * format->biHeight
                                               * ((format->biBitCount + 7) / 8));

  this->m_sVhdr->dwBitRate = framerate * this->m_sVhdr->bmiHeader.biSizeImage * 8;
  this->m_sVhdr->AvgTimePerFrame = 10000 / framerate;
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
        printf ("using %s\n", c->name);
      }
    }
  }
  else { /* This is YUV */
    for (c = check; c->bits; c++) {
      if (c->fcc == format->biCompression) {
        this->m_sOurType.subtype = *c->subtype;
        /* We trust our table more than the input format */
        this->m_sVhdr->bmiHeader.biBitCount = c->bits;
        printf ("using %s\n", c->name);
      }
    }
  }
  this->m_sOurType.bFixedSizeSamples = true;
  this->m_sOurType.bTemporalCompression = false;
  this->m_sOurType.lSampleSize = this->m_sVhdr->bmiHeader.biSizeImage;
  this->m_sOurType.formattype = FORMAT_VideoInfo;
  this->m_sOurType.cbFormat = sizeof (VIDEOINFOHEADER);
  this->m_sOurType.pbFormat = (char*) this->m_sVhdr;
  
  /* Defining outgoing type compressed video bitstream */
  this->m_sVhdr2 = (VIDEOINFOHEADER *) malloc (sizeof (VIDEOINFOHEADER));
  memset (this->m_sVhdr2, 0, sizeof (VIDEOINFOHEADER));
  
  this->m_sVhdr2->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
  this->m_sVhdr2->bmiHeader.biWidth = format->biWidth;
  this->m_sVhdr2->bmiHeader.biHeight = format->biHeight;
  this->m_sVhdr2->bmiHeader.biPlanes = 1;
  this->m_sVhdr2->bmiHeader.biCompression = dest_fourcc;

  this->m_sVhdr2->dwBitRate = bitrate;
  this->m_sVhdr2->dwBitErrorRate = 0;
  this->m_sVhdr2->AvgTimePerFrame = 10000 / framerate;
  this->m_sVhdr2->rcSource.left = this->m_sVhdr2->rcSource.top = 0;
  this->m_sVhdr2->rcSource.right = format->biWidth;
  this->m_sVhdr2->rcSource.bottom = format->biHeight;
  this->m_sVhdr2->rcTarget = this->m_sVhdr2->rcSource;
  
  memset (&this->m_sDestType, 0, sizeof (this->m_sDestType));
  
  this->m_sDestType.majortype = MEDIATYPE_Video;
  this->m_sDestType.subtype = MEDIATYPE_Video;
  this->m_sDestType.subtype.f1 = dest_fourcc;
  this->m_sDestType.formattype = FORMAT_VideoInfo;
  this->m_sDestType.bFixedSizeSamples = false;
  this->m_sDestType.bTemporalCompression = true;
  this->m_sDestType.cbFormat = sizeof (VIDEOINFOHEADER);
  this->m_sDestType.pbFormat = (char *) this->m_sVhdr2;
 
  /* Creating DMO Filter */
  this->m_pDMO_Filter = DMO_Filter_Create (dllname, guid, &this->inputs,
                                           &this->outputs, &error_message);
  if (!this->m_pDMO_Filter)
    goto beach;
  
  /* Output first and then input as we are an encoder */
  if (!DMO_Filter_SetInputType (this->m_pDMO_Filter, 0, &this->m_sOurType,
                                &this->in_buffer_size, &this->lookahead,
                                &this->in_align, &error_message))
    goto beach;
  if (!DMO_Filter_SetOutputType (this->m_pDMO_Filter, 0, &this->m_sDestType,
                                 &this->out_buffer_size, &this->out_align,
                                 &error_message))
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

int DMO_VideoEncoder_EncodeInternal (DMO_VideoEncoder * this,
                                     const void * in_data,
                                     unsigned long in_size,
                                     void * out_data,
                                     unsigned long out_size)
{
  unsigned long result, index, status;
  DMO_OUTPUT_DATA_BUFFER * db = NULL;
  CMediaBuffer * bufferin = NULL;

#ifdef LDT_paranoia
    Setup_FS_Segment();
#endif

  bufferin = CMediaBufferCreate (in_size, (void *) in_data, in_size, 0);
  result = this->m_pDMO_Filter->m_pMedia->vt->ProcessInput (
                this->m_pDMO_Filter->m_pMedia, 0,
                (IMediaBuffer *) bufferin, 0, 0, 0);
                
  ((IMediaBuffer *) bufferin)->vt->Release ((IUnknown *) bufferin);

  if (result != S_OK) {
    /* something to process */
	  if (result != S_FALSE)
	    printf ("ProcessInputError  r:0x%x=%d\n", result, result);
	  else
	    printf ("ProcessInputError  FALSE ??\n");
	  return in_size;
  }

  db = malloc (sizeof (DMO_OUTPUT_DATA_BUFFER) * this->outputs);
  if (!db)
    return 0;
  
  for (index = 0; index < this->outputs; index++) {
    db[index].rtTimestamp = 0;
    db[index].rtTimelength = 0;
    db[index].dwStatus = 0;
    if (index == 0)
      db[index].pBuffer = (IMediaBuffer *) CMediaBufferCreate (out_size,
                                                               (void *) out_data,
                                                                0, 0);
    else
      db[index].pBuffer = NULL;
  }
  
  result = this->m_pDMO_Filter->m_pMedia->vt->ProcessOutput (
             this->m_pDMO_Filter->m_pMedia,
             DMO_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER,
             this->outputs, db, &status);

  if ((unsigned)result == DMO_E_NOTACCEPTING)
	  printf("ProcessOutputError: Not accepting\n");
  else if (result)
  	printf("ProcessOutputError: r:0x%x=%d  %ld\n", result, result, status);

  /* Only the first buffer was not null. The others were discardable */
  ((IMediaBuffer *) db[0].pBuffer)->vt->Release ((IUnknown *) db[0].pBuffer);
  
  free (db);

  return 0;
}
