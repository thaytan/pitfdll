/********************************************************

         DirectShow audio decoder
	 Copyright 2001 Eugene Kuznetsov  (divx@euro.ru)

*********************************************************/

#ifndef NOAVIFILE_HEADERS
#include "audiodecoder.h"
#include "except.h"
#else
#include "libwin32.h"
#ifdef LDT_paranoia
#include "ldt_keeper.h"
#endif
#endif

#include "DMO_Filter.h"

#include "DMO_AudioDecoder.h"

struct _DMO_AudioDecoder
{ 
  DMO_Filter * m_pDMO_Filter;
  
  DMO_MEDIA_TYPE m_sOurType;
  DMO_MEDIA_TYPE m_sDestType;
    
  WAVEFORMATEX * m_sAhdr;
  WAVEFORMATEX * m_sAhdr2;
  
  int m_iFlushed;
  
  unsigned long in_buffer_size;
  unsigned long out_buffer_size;
  unsigned long in_align;
  unsigned long out_align;
  unsigned long lookahead;
  unsigned long inputs;
  unsigned long outputs;
};

#include "../wine/winerror.h"
#include "../wine/msacm.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define __MODULE__ "DirectShow audio decoder"

typedef long STDCALL (*GETCLASS) (GUID*, GUID*, void**);

int DMO_AudioDecoder_GetOutputInfos (DMO_AudioDecoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align)
{
  if (!this || !out_buffer_size || !out_align)
    return FALSE;
  
  *out_buffer_size = this->out_buffer_size;
  *out_align = this->out_align;
  
  return TRUE;
}

int DMO_AudioDecoder_GetInputInfos (DMO_AudioDecoder * this, 
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

DMO_AudioDecoder * DMO_AudioDecoder_Open (char * dllname, GUID * guid,
                                          WAVEFORMATEX * format)
{
  DMO_AudioDecoder * this = NULL;
  char * error_message = NULL;
  unsigned int bihs;
  
#ifdef LDT_paranoia
  Setup_LDT_Keeper();
  Setup_FS_Segment();
#endif
  
  /* print_wave_header (format); */
        
  this = malloc (sizeof (DMO_AudioDecoder));
  if (!this)
    return NULL;
  memset (this, 0, sizeof (DMO_AudioDecoder));
  
  this->m_iFlushed=1;
    
  bihs = sizeof (WAVEFORMATEX) + format->cbSize;
  
  /* Defining incoming type, encoded audio samples */
  this->m_sAhdr = (WAVEFORMATEX *) malloc (bihs);
  
  memcpy (this->m_sAhdr, format, bihs);
  memset (&this->m_sOurType, 0, sizeof (this->m_sOurType));
  
  this->m_sOurType.majortype = MEDIATYPE_Audio;
  this->m_sOurType.subtype = MEDIASUBTYPE_PCM;
  this->m_sOurType.subtype.f1 = this->m_sAhdr->wFormatTag;
  this->m_sOurType.formattype = FORMAT_WaveFormatEx;
  this->m_sOurType.lSampleSize = this->m_sAhdr->nBlockAlign;
  this->m_sOurType.bFixedSizeSamples = 1;
  this->m_sOurType.bTemporalCompression = 0;
  this->m_sOurType.cbFormat = bihs;
  this->m_sOurType.pbFormat = (char *) this->m_sAhdr;
  
  /* Defining outcoming type, raw audio samples */
  this->m_sAhdr2 = (WAVEFORMATEX *) malloc (sizeof (WAVEFORMATEX));
  memset (this->m_sAhdr2, 0, sizeof (WAVEFORMATEX));
 
  this->m_sAhdr2->wFormatTag = WAVE_FORMAT_PCM;
  this->m_sAhdr2->wBitsPerSample = format->wBitsPerSample;
  this->m_sAhdr2->nChannels = format->nChannels;
  this->m_sAhdr2->nBlockAlign = this->m_sAhdr2->nChannels *
                                (this->m_sAhdr2->wBitsPerSample / 8);
  this->m_sAhdr2->nSamplesPerSec = format->nSamplesPerSec;
  this->m_sAhdr2->nAvgBytesPerSec = this->m_sAhdr2->nBlockAlign *
                                    this->m_sAhdr2->nSamplesPerSec;
  
  memset (&this->m_sDestType, 0, sizeof (this->m_sDestType));
  
  this->m_sDestType.majortype = MEDIATYPE_Audio;
  this->m_sDestType.subtype = MEDIASUBTYPE_PCM;
  this->m_sDestType.formattype = FORMAT_WaveFormatEx;
  this->m_sDestType.bFixedSizeSamples = 1;
  this->m_sDestType.bTemporalCompression = 0;
  this->m_sDestType.lSampleSize = this->m_sAhdr2->nBlockAlign;
  this->m_sDestType.cbFormat = sizeof (WAVEFORMATEX);
  this->m_sDestType.pbFormat = (char *) this->m_sAhdr2;
  
  /* print_wave_header (this->m_sAhdr);
     print_wave_header (this->m_sAhdr2); */
  
  /* Creating DMO Filter */
  this->m_pDMO_Filter = DMO_Filter_Create (dllname, guid, &this->inputs,
                                           &this->outputs, &error_message);
  if (!this->m_pDMO_Filter)
    goto beach;
  
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
    printf ("Failed creating an audio decoder: %s\n", error_message);
    free (error_message);
  }
  free(this->m_sAhdr);
  free(this->m_sAhdr2);
  free(this);
  return NULL;

}

void DMO_AudioDecoder_Destroy(DMO_AudioDecoder * this)
{
  if (!this)
    return;
  
  if (this->m_sAhdr) {
    free(this->m_sAhdr);
    this->m_sAhdr = NULL;
  }
  if (this->m_sAhdr2) {
    free(this->m_sAhdr2);
    this->m_sAhdr2 = NULL;
  }
  if (this->m_pDMO_Filter) {
    DMO_Filter_Destroy (this->m_pDMO_Filter);
    this->m_pDMO_Filter = NULL;
  }
}

void DMO_AudioDecoder_Flush (DMO_AudioDecoder * this)
{
  char * error_message = NULL;
  
  DMO_Filter_Flush (this->m_pDMO_Filter, &error_message);
}

int DMO_AudioDecoder_Convert (DMO_AudioDecoder * this, const void * in_data,
                              unsigned int in_size, void * out_data,
                              unsigned int out_size, unsigned int * size_read,
                              unsigned int * size_written)
{
  DMO_OUTPUT_DATA_BUFFER db;
  CMediaBuffer * bufferin;
  unsigned long written = 0;
  unsigned long read = 0;
  int r = 0;

  if (!in_data || !out_data)
	  return -1;

#ifdef LDT_paranoia
    Setup_FS_Segment ();
#endif

  /* Creating the IMediaBuffer containing input data */
  bufferin = CMediaBufferCreate (in_size, (void *) in_data, in_size, 1);
  r = this->m_pDMO_Filter->m_pMedia->vt->ProcessInput (
        this->m_pDMO_Filter->m_pMedia, 0, (IMediaBuffer *) bufferin,
				(this->m_iFlushed) ? DMO_INPUT_DATA_BUFFERF_SYNCPOINT : 0, 0, 0);
  
  if (r == S_OK) {
	  ((IMediaBuffer *) bufferin)->vt->GetBufferAndLength (
      (IMediaBuffer *) bufferin, 0, &read);
	  this->m_iFlushed = 0;
  }

  ((IMediaBuffer *) bufferin)->vt->Release((IUnknown *) bufferin);

  /* Output data waiting for us to process it */
  if (r == S_OK || (unsigned) r == DMO_E_NOTACCEPTING) {
	  unsigned long status = 0;
    
	  db.rtTimestamp = 0;
	  db.rtTimelength = 0;
	  db.dwStatus = 0;
	  db.pBuffer = (IMediaBuffer *) CMediaBufferCreate (out_size, out_data, 0, 0);
	  
	  r = this->m_pDMO_Filter->m_pMedia->vt->ProcessOutput (
          this->m_pDMO_Filter->m_pMedia, 0, 1, &db, &status);

	  ((IMediaBuffer *) db.pBuffer)->vt->GetBufferAndLength (
      (IMediaBuffer *) db.pBuffer, NULL, &written);
	  ((IMediaBuffer *) db.pBuffer)->vt->Release ((IUnknown *) db.pBuffer);
  }
  else if (in_size > 0)
	  printf ("ProcessInputError  r:0x%x=%d\n", r, r);

  if (size_read)
	  *size_read = read;
  if (size_written)
	  *size_written = written;
  
  return r;
}

int DMO_AudioDecoder_ProcessInput (DMO_AudioDecoder * this,
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

int DMO_AudioDecoder_ProcessOutput (DMO_AudioDecoder * this,
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
    free (db);
    return TRUE;
  }
  else {
    free (db);
    return FALSE;
  }
}
