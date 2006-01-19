/********************************************************

	DirectShow Video decoder implementation
	Copyright 2000 Eugene Kuznetsov  (divx@euro.ru)

*********************************************************/

#include "dshow/guids.h"
#include "dshow/interfaces.h"
#include "registry.h"
#ifdef LDT_paranoia
#include "../ldt_keeper.h"
#endif

#ifndef NOAVIFILE_HEADERS
#include "videodecoder.h"
#else
#include "libwin32.h"
#endif
#include "DMO_Filter.h"

#include "DMO_VideoDecoder.h"

struct _DMO_VideoDecoder
{
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
#include <stdlib.h>  // labs

#define __MODULE__ "DirectShow_VideoDecoder"

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

int DMO_VideoDecoder_GetOutputInfos (DMO_VideoDecoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align)
{
  if (!this || !out_buffer_size || !out_align)
    return FALSE;
  
  *out_buffer_size = this->out_buffer_size;
  *out_align = this->out_align;
  
  return TRUE;
}

int DMO_VideoDecoder_GetInputInfos (DMO_VideoDecoder * this, 
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

DMO_VideoDecoder * DMO_VideoDecoder_Open (char * dllname, GUID* guid,
                                          BITMAPINFOHEADER * format)
{
  DMO_VideoDecoder * this;
  HRESULT result;
  ct * c;
  char * error_message = NULL;
  unsigned int bihs;
                        
  this = malloc(sizeof (DMO_VideoDecoder));
  if (!this)
    return NULL;
  
  memset (this, 0, sizeof (DMO_VideoDecoder));

#ifdef LDT_paranoia
    Setup_LDT_Keeper();
#endif

  /* Defining incoming type compressed video bitstream */
  
  bihs = (format->biSize < (int) sizeof(BITMAPINFOHEADER)) ?
          sizeof(BITMAPINFOHEADER) : format->biSize;
  bihs += sizeof(VIDEOINFOHEADER) - sizeof(BITMAPINFOHEADER);
  
  this->m_sVhdr = (VIDEOINFOHEADER *) malloc (bihs);
  memset (this->m_sVhdr, 0, bihs);
  memcpy (&this->m_sVhdr->bmiHeader, format, format->biSize);

  this->m_sVhdr->rcSource.left = this->m_sVhdr->rcSource.top = 0;
  this->m_sVhdr->rcSource.right = this->m_sVhdr->bmiHeader.biWidth;
  this->m_sVhdr->rcSource.bottom = this->m_sVhdr->bmiHeader.biHeight;
  this->m_sVhdr->rcTarget = this->m_sVhdr->rcSource;
  
  memset (&this->m_sOurType, 0, sizeof (this->m_sOurType));
  
  this->m_sOurType.majortype = MEDIATYPE_Video;
  this->m_sOurType.subtype = MEDIATYPE_Video;
  this->m_sOurType.subtype.f1 = format->biCompression;
  this->m_sOurType.formattype = FORMAT_VideoInfo;
  this->m_sOurType.bFixedSizeSamples = false;
  this->m_sOurType.bTemporalCompression = true;
  this->m_sOurType.lSampleSize = 0;
  this->m_sOurType.cbFormat = bihs;
  this->m_sOurType.pbFormat = (char *) this->m_sVhdr;
 
  /* Defining outgoing type, raw video YUY2 frames*/
  this->m_sVhdr2 = (VIDEOINFOHEADER *) malloc (sizeof (VIDEOINFOHEADER));
  memset (this->m_sVhdr2, 0, sizeof (VIDEOINFOHEADER));
  
  this->m_sVhdr2->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
  this->m_sVhdr2->bmiHeader.biWidth = format->biWidth;
  this->m_sVhdr2->bmiHeader.biHeight = format->biHeight;
  this->m_sVhdr2->bmiHeader.biPlanes = 1;
  this->m_sVhdr2->bmiHeader.biBitCount = 16;
  this->m_sVhdr2->bmiHeader.biCompression = fccYUY2;
  /*
  this->m_sVhdr2->bmiHeader.biSizeImage = labs (format->biWidth * format->biHeight
                                                * ((format->biBitCount + 7) / 8));*/
  this->m_sVhdr2->bmiHeader.biSizeImage = format->biWidth * format->biHeight * format->biBitCount / 8;

  this->m_sVhdr2->rcSource.left = this->m_sVhdr->rcSource.top = 0;
  this->m_sVhdr2->rcSource.right = this->m_sVhdr->bmiHeader.biWidth;
  this->m_sVhdr2->rcSource.bottom = this->m_sVhdr->bmiHeader.biHeight;
  this->m_sVhdr2->rcTarget = this->m_sVhdr->rcSource;
  
  this->m_sDestType.majortype = MEDIATYPE_Video;
	this->m_sDestType.subtype = MEDIASUBTYPE_YUY2;
  this->m_sDestType.bFixedSizeSamples = true;
  this->m_sDestType.bTemporalCompression = false;
  this->m_sDestType.lSampleSize = this->m_sVhdr2->bmiHeader.biSizeImage;
  this->m_sDestType.formattype = FORMAT_VideoInfo;
  this->m_sDestType.cbFormat = sizeof (VIDEOINFOHEADER);
  this->m_sDestType.pbFormat = (char*) this->m_sVhdr2;

  /* Creating DMO Filter */
  this->m_pDMO_Filter = DMO_Filter_Create (dllname, guid, &this->inputs,
                                           &this->outputs, &error_message);
  if (!this->m_pDMO_Filter)
    goto beach;

	/* Input first and then output as we are a decoder */
  if (!DMO_Filter_SetInputType (this->m_pDMO_Filter, 0, &this->m_sOurType,
                                &error_message))
    goto beach;
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
  
  return this;
    
beach:
  if (this->m_pDMO_Filter)
    DMO_Filter_Destroy (this->m_pDMO_Filter);
  if (error_message) {
    printf ("Failed creating a video decoder: %s\n", error_message);
    free (error_message);
  }
  free(this->m_sVhdr);
  free(this->m_sVhdr2);
  free(this);
  return NULL;
}

void DMO_VideoDecoder_Destroy (DMO_VideoDecoder * this)
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

void DMO_VideoDecoder_Flush (DMO_VideoDecoder * this)
{
  char * error_message = NULL;
  
  DMO_Filter_Flush (this->m_pDMO_Filter, &error_message);
}

int DMO_VideoDecoder_ProcessInput (DMO_VideoDecoder * this,
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

int DMO_VideoDecoder_ProcessOutput (DMO_VideoDecoder * this,
                                    void * out_data, unsigned int out_size,
                                    unsigned int * size_written,
                                    unsigned long long * timestamp,
                                    unsigned long long * duration)
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
