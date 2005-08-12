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
  DMO_MEDIA_TYPE * m_pOurType;
  DMO_MEDIA_TYPE * m_pDestType;
} DMO_Filter;

typedef struct _CMediaBuffer CMediaBuffer;

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

int DMO_Filter_InspectPins (DMO_Filter * This, char ** error_message);

int DMO_Filter_SetInputType (DMO_Filter * This, unsigned long pin_id,
                             DMO_MEDIA_TYPE * in_fmt,
                             unsigned long * buffer_size,
                             unsigned long * lookahead,
                             unsigned long * align, char ** error_message);

int DMO_Filter_SetOutputType (DMO_Filter * This, unsigned long pin_id,
                              DMO_MEDIA_TYPE * out_fmt,
                              unsigned long * buffer_size,
                              unsigned long * align, char ** error_message);
                               
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
