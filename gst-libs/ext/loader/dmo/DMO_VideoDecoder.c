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
    
  unsigned long out_buffer_size;
  unsigned long out_align;
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
  this->m_sVhdr2->bmiHeader.biSizeImage = labs (format->biWidth * format->biHeight
                                                * ((format->biBitCount + 7) / 8));

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
                                NULL, NULL, NULL, &error_message))
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

int DMO_VideoDecoder_DecodeInternal (DMO_VideoDecoder * this, const void * src,
                                     int size, int is_keyframe, char * imdata)
{
  int result;
  unsigned long status;
  DMO_OUTPUT_DATA_BUFFER db;
  CMediaBuffer* bufferin;
    
#ifdef LDT_paranoia
    Setup_FS_Segment();
#endif

  bufferin = CMediaBufferCreate (size, (void *) src, size, 0);
  result = this->m_pDMO_Filter->m_pMedia->vt->ProcessInput (
             this->m_pDMO_Filter->m_pMedia, 0,
				     (IMediaBuffer *) bufferin,
				     (is_keyframe) ? DMO_INPUT_DATA_BUFFERF_SYNCPOINT : 0,
				     0, 0);
  ((IMediaBuffer *) bufferin)->vt->Release ((IUnknown *) bufferin);

  if (result != S_OK) {
    /* something for process */
	  if (result != S_FALSE)
	    printf ("ProcessInputError  r:0x%x=%d (keyframe: %d)\n", result, result, is_keyframe);
	  else
	      printf ("ProcessInputError  FALSE ?? (keyframe: %d)\n", is_keyframe);
	  return size;
  }

  db.rtTimestamp = 0;
  db.rtTimelength = 0;
  db.dwStatus = 0;
  db.pBuffer = (IMediaBuffer *) CMediaBufferCreate (
                                  this->m_sDestType.lSampleSize,
                                  imdata, 0, 0);
  result = this->m_pDMO_Filter->m_pMedia->vt->ProcessOutput (
             this->m_pDMO_Filter->m_pMedia,
						 (imdata) ? 0 : DMO_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER,
						 1, &db, &status);
  
  if ((unsigned) result == DMO_E_NOTACCEPTING)
	  printf("ProcessOutputError: Not accepting\n");
  else if (result)
	  printf("ProcessOutputError: r:0x%x=%d  %ld  stat:%ld\n", result, result, status, db.dwStatus);

  ((IMediaBuffer *) db.pBuffer)->vt->Release ((IUnknown *) db.pBuffer);

  return 0;
}
