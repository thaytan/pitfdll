#ifndef DMO_FILTER_H
#define DMO_FILTER_H

#include "dmo_guids.h"
#include "dmo_interfaces.h"
#include "libwin32.h"
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _DMO_Filter {
  int m_iHandle;
  IDMOVideoOutputOptimizations * m_pOptim;
  IMediaObject * m_pMedia;
  IMediaObjectInPlace * m_pInPlace;
  IWMCodecPrivateData * m_pPrivateData;
  IPropertyBag * m_pPropertyBag;
  DMO_MEDIA_TYPE * m_pOurType;
  DMO_MEDIA_TYPE * m_pDestType;
} DMO_Filter;

typedef struct _CMediaBuffer CMediaBuffer;

void print_wave_header(WAVEFORMATEX *h);
void print_video_header(VIDEOINFOHEADER *h);

static const char g_wszWMVCPassesUsed[] = {'_','\0','P','\0','A','\0','S','\0','S','\0','E','\0','S','\0','U','\0','S','\0','E','\0','D','\0','\0','\0'};
static const char g_wszWMACAvgBytesPerSec[] = {'A','\0','v','\0','g','\0','B','\0','y','\0','t','\0','e','\0','s','\0','P','\0','e','\0','r','\0','S','\0','e','\0','c','\0','\0','\0'};
static const char g_wszWMVCVBREnabled[] = {'_','\0','V','\0','B','\0','R','\0','E','\0','N','\0','A','\0','B','\0','L','\0','E','\0','D','\0','\0','\0'};
static const char g_wszWMVCVBRQuality[] = {'_','\0','V','\0','B','\0','R','\0','Q','\0','U','\0','A','\0','L','\0','I','\0','T','\0','Y','\0','\0','\0'};

/**
 * Create DMO_Filter object.
 */
DMO_Filter * DMO_Filter_Create (const char * dllname, const GUID * id,
			                          unsigned long * input_pins,
                                unsigned long * output_pins,
                                char ** error_message);
/**
 * Destroy DMO_Filter object - release all allocated resources
 */
void DMO_Filter_Destroy (DMO_Filter * This);

int DMO_Filter_LookupAudioEncoderType (DMO_Filter * This, WAVEFORMATEX * target,
                                       DMO_MEDIA_TYPE * type, int vbr,
                                       char ** error_message);

int DMO_Filter_InspectPins (DMO_Filter * This, char ** error_message);

int DMO_Filter_SetInputType (DMO_Filter * This, unsigned long pin_id,
                             DMO_MEDIA_TYPE * in_fmt,
                             char ** error_message);
                             
int DMO_Filter_GetInputSizeInfo (DMO_Filter * This, unsigned long pin_id,
                                 unsigned long * buffer_size,
                                 unsigned long * lookahead,
                                 unsigned long * align, char ** error_message);

int DMO_Filter_SetOutputType (DMO_Filter * This, unsigned long pin_id,
                              DMO_MEDIA_TYPE * out_fmt,
                              char ** error_message);
                              
int DMO_Filter_GetOutputSizeInfo (DMO_Filter * This, unsigned long pin_id,
                                  unsigned long * buffer_size,
                                  unsigned long * align, char ** error_message);
                              
int DMO_Filter_SetPartialOutputType (DMO_Filter * This,
                                     unsigned long * data_length,
                                     char ** data,
                                     DMO_MEDIA_TYPE * out_fmt,
                                     char ** error_message);

int DMO_Filter_SetProperty (DMO_Filter * This, const WCHAR * name,
                            VARIANT * value, char ** error_message);
int DMO_Filter_GetProperty (DMO_Filter * This, const WCHAR * name,
                            VARIANT * value, char ** error_message);
                                     
int DMO_Filter_Discontinuity (DMO_Filter * This, char ** error_message);

/**
 * Create IMediaBuffer object - to pass/receive data from DMO_Filter
 *
 * maxlen - maximum size for this buffer
 * mem - initial memory  0 - creates memory
 * len - initial size of used portion of the buffer
 * copy - make a local copy of data
 */
CMediaBuffer * CMediaBufferCreate (unsigned long maxlen, void * mem,
                                   unsigned long len, int copy);

#if defined(__cplusplus)
}
#endif

#endif /* DMO_FILTER_H */
