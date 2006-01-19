#include "DMO_Filter.h"
#include "driver.h"
#include "com.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32.h" // printf macro
#include "../wine/winerror.h"

typedef long STDCALL (*GETCLASS) (const GUID *, const GUID *, void **);

void print_wave_header(WAVEFORMATEX *h){
  printf("======= WAVE Format =======\n");
  printf("Format Tag: %d (0x%X)\n",h->wFormatTag,h->wFormatTag);
  printf("Channels: %d\n",h->nChannels);
  printf("Samplerate: %ld\n",h->nSamplesPerSec);
  if ((h->nAvgBytesPerSec & 0x7FFFFF00) == 0x7FFFFF00)
    printf("VBR Quality: %ld%%\n",h->nAvgBytesPerSec & 0x000000FF);
  else    
    printf("avg byte/sec: %ld\n",h->nAvgBytesPerSec);
  printf("Block align: %d\n",h->nBlockAlign);
  printf("bits/sample: %d\n",h->wBitsPerSample);
  printf("cbSize: %d\n",h->cbSize);
  if (h->cbSize > 0)
  {
    int i;
    uint8_t* p = ((uint8_t*)h) + sizeof(WAVEFORMATEX);
    printf("Unknown extra header dump: ");
    for (i = 0; i < h->cbSize; i++)
	    printf("[%x] ", p[i]);
    printf("\n");
  }
  printf("===========================\n");
}

void print_video_header(VIDEOINFOHEADER *h){
  printf("======= VIDEO Format ======\n");
  printf("  rcSource: %d,%d x %d,%d\n", h->rcSource.left, h->rcSource.top,
         h->rcSource.right, h->rcSource.bottom);
  printf("  rcTarget: %d,%d x %d,%d\n", h->rcTarget.left, h->rcTarget.top,
         h->rcTarget.right, h->rcTarget.bottom);
  printf("  dwBitRate: %d\n", h->dwBitRate);
  printf("  dwBitErrorRate: %d\n", h->dwBitErrorRate);
  printf("  AvgTimePerFrame: %llu\n", h->AvgTimePerFrame);
	printf("  biSize: %d\n", h->bmiHeader.biSize);
	printf("  biWidth: %d\n", h->bmiHeader.biWidth);
	printf("  biHeight: %d\n", h->bmiHeader.biHeight);
	printf("  biPlanes: %d\n", h->bmiHeader.biPlanes);
	printf("  biBitCount: %d\n", h->bmiHeader.biBitCount);
	printf("  biCompression: %d='%.4s'\n", h->bmiHeader.biCompression,
         (char *)&h->bmiHeader.biCompression);
	printf("  biSizeImage: %d\n", h->bmiHeader.biSizeImage);
  printf("  biXPelsPerMeter: %ld\n", h->bmiHeader.biXPelsPerMeter);
  printf("  biYPelsPerMeter: %ld\n", h->bmiHeader.biYPelsPerMeter);
  printf("  biClrUsed: %d\n", h->bmiHeader.biClrUsed);
  printf("  biClrImportant: %d\n", h->bmiHeader.biClrImportant);
  if (h->bmiHeader.biSize > sizeof(BITMAPINFOHEADER))
  {
    int i;
    uint8_t* p = ((uint8_t*)&h->bmiHeader) + sizeof(BITMAPINFOHEADER);
    printf("Unknown extra header dump: ");
    for (i = 0; i < h->bmiHeader.biSize-sizeof(BITMAPINFOHEADER); i++)
	    printf("[%x] ", *(p+i));
    printf("\n");
  }
  printf("===========================\n");
}

void DMO_Filter_Destroy (DMO_Filter * This)
{
  if (!This)
    return;
  
  if (This->m_pPropertyBag)
    This->m_pPropertyBag->vt->Release ((IUnknown *) This->m_pPropertyBag);
  if (This->m_pPrivateData)
    This->m_pPrivateData->vt->Release ((IUnknown *) This->m_pPrivateData);
  if (This->m_pOptim)
    This->m_pOptim->vt->Release ((IUnknown *) This->m_pOptim);
  if (This->m_pInPlace)
	  This->m_pInPlace->vt->Release ((IUnknown *) This->m_pInPlace);
  if (This->m_pMedia)
	  This->m_pMedia->vt->Release ((IUnknown *) This->m_pMedia);

  free (This);
  CodecRelease ();
}

int DMO_Filter_LookupAudioEncoderType (DMO_Filter * This, WAVEFORMATEX * target,
                                       DMO_MEDIA_TYPE * type, int vbr,
                                       char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
  WAVEFORMATEX * format = NULL;
  unsigned long dwType = 0, found = 0;
  
  if (!This || !This->m_pMedia || !This->m_pMedia->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p", This);
    goto beach;
  }

  /* We are leaking all that memory, sorry but i don't know how to free that
     correctly using the wrapped DMO functions (FIXME) */
  do {
    hr = This->m_pMedia->vt->GetOutputType (This->m_pMedia, 0, dwType, type);
    if (hr != S_OK)
      break;
    
    format = (WAVEFORMATEX *) type->pbFormat;
    
    if (format && format->wFormatTag == target->wFormatTag &&
        format->nSamplesPerSec == target->nSamplesPerSec &&
        format->wBitsPerSample == target->wBitsPerSample &&
        format->nChannels == target->nChannels) {
      if (vbr && ((format->nAvgBytesPerSec & 0x7FFFFF00) == 0x7FFFFF00)) {
        int quality = format->nAvgBytesPerSec & 0x000000FF;
        if (quality == target->nAvgBytesPerSec)
          found = TRUE;
      }
      else {
        if (format->nAvgBytesPerSec == target->nAvgBytesPerSec)
          found = TRUE;
      }
    }
    
    dwType++;
  } while (found == FALSE);
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;
}

int DMO_Filter_InspectPins (DMO_Filter * This, char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
  unsigned long input_pins, output_pins, index;
  
  if (!This || !This->m_pMedia || !This->m_pMedia->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p", This);
    goto beach;
  }
  
  /* Now let's get in DMO object discovery */
  hr = This->m_pMedia->vt->GetStreamCount (This->m_pMedia, &input_pins,
                                           &output_pins);
  printf ("DMO has %ld input pins and %ld output pins\n", input_pins,
          output_pins);
  
  for (index = 0; index < input_pins; index++) {
    DMO_MEDIA_TYPE mt;
    unsigned long dwType = 0;
    printf ("Input pin %ld supports:\n", index);
    hr = This->m_pMedia->vt->GetInputType (This->m_pMedia, index, dwType, &mt);
    while (hr == S_OK) {
      printf ("Mediatype { %lx, %lx, %lx, %lx } Subtype { %lx, %lx, %lx, %lx }"\
              " format struct length %ld (normal size would be %ld)\n",
              mt.majortype.f1, mt.majortype.f2, mt.majortype.f3,
              mt.majortype.f4, mt.subtype.f1, mt.subtype.f2, mt.subtype.f3,
              mt.subtype.f4, mt.cbFormat, sizeof (VIDEOINFOHEADER));
      dwType++;
      hr = This->m_pMedia->vt->GetInputType (This->m_pMedia, index, dwType, &mt);
    }
    printf ("\n");
  }
  
  for (index = 0; index < output_pins; index++) {
    DMO_MEDIA_TYPE mt;
    unsigned long dwType = 0;
    printf ("Output pin %ld supports:\n", index);
    hr = This->m_pMedia->vt->GetOutputType (This->m_pMedia, index, dwType, &mt);
    while (hr == S_OK) {
      printf ("Mediatype { %lx, %lx, %lx, %lx } Subtype { %lx, %lx, %lx, %lx }"\
              " format type { %lx, %lx, %lx, %lx }\n",
              mt.majortype.f1, mt.majortype.f2, mt.majortype.f3,
              mt.majortype.f4, mt.subtype.f1, mt.subtype.f2, mt.subtype.f3,
              mt.subtype.f4, mt.formattype.f1, mt.formattype.f2, 
              mt.formattype.f3, mt.formattype.f4);
      dwType++;
      hr = This->m_pMedia->vt->GetOutputType (This->m_pMedia, index, dwType, &mt);
    }
    printf ("\n");
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;
}

/* Sets a format on a specific input pin. If the format is NULL we are trying
   to clear the format on that pin. An error message will be set
   indicating what happened except if error_message is NULL */
int DMO_Filter_SetInputType (DMO_Filter * This, unsigned long pin_id,
                             DMO_MEDIA_TYPE * in_fmt,
                             char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
      
  if (!This || !This->m_pMedia || !This->m_pMedia->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p", This);
    goto beach;
  }
  
  if (!in_fmt) { /* Clearing type on input pin */
    hr = This->m_pMedia->vt->SetInputType (This->m_pMedia, pin_id, in_fmt,
                                           DMO_SET_TYPEF_CLEAR);
    if (hr != S_OK) {
      asprintf (&local_error, "failed clearing type on input pin %ld", pin_id);
      goto beach;
    }
  }
  else { /* Trying to set a specific type */
    hr = This->m_pMedia->vt->SetInputType (This->m_pMedia, pin_id, in_fmt, 0);
    if (hr != S_OK) {
      if (hr == DMO_E_INVALIDSTREAMINDEX) {
        asprintf (&local_error, "pin %ld is not a valid input pin", pin_id);
        goto beach;
      }
      else if (hr == DMO_E_TYPE_NOT_ACCEPTED) {
        asprintf (&local_error, "type was not accepted on input pin %ld",
                  pin_id);
        goto beach;
      }
      else if (hr == S_FALSE) {
        asprintf (&local_error, "type is unacceptable on input pin %ld",
                  pin_id);
        goto beach;
      }
      else {
        asprintf (&local_error, "unexpected error when trying to set type on " \
                  "input pin %ld : 0x%lx", pin_id, hr);
        goto beach;
      }
    }
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;
}

int DMO_Filter_GetInputSizeInfo (DMO_Filter * This, unsigned long pin_id,
                                 unsigned long * buffer_size,
                                 unsigned long * lookahead,
                                 unsigned long * align, char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
      
  if (!This || !This->m_pMedia || !This->m_pMedia->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p", This);
    goto beach;
  }
  
  if (buffer_size && lookahead && align) { /* Getting buffer infos */
    hr = This->m_pMedia->vt->GetInputSizeInfo (This->m_pMedia, pin_id,
                                               buffer_size, lookahead, align);
    if (hr != S_OK) {
      if (hr == DMO_E_INVALIDSTREAMINDEX) {
        asprintf (&local_error, "pin %ld is not a valid input pin", pin_id);
        goto beach;
      }
      else if (hr == DMO_E_TYPE_NOT_SET) {
        asprintf (&local_error, "type not set on input pin %ld, can't get " \
                  "buffer infos", pin_id);
        goto beach;
      }
      else {
        asprintf (&local_error, "unexpected error when trying to get infos " \
                  "on input pin %ld : 0x%lx", pin_id, hr);
        goto beach;
      }
    }
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;
}

/* Sets a format on a specific output pin. If the format is NULL we are trying
   to clear the format on that pin. An error message will be set
   indicating what happened except if error_message is NULL */
int DMO_Filter_SetOutputType (DMO_Filter * This, unsigned long pin_id,
                              DMO_MEDIA_TYPE * out_fmt,
                              char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
      
  if (!This || !This->m_pMedia || !This->m_pMedia->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p", This);
    goto beach;
  }
  
  if (!out_fmt) { /* Clearing type on output pin */
    hr = This->m_pMedia->vt->SetOutputType (This->m_pMedia, pin_id, out_fmt,
                                            DMO_SET_TYPEF_CLEAR);
    if (hr != S_OK) {
      asprintf (&local_error, "failed clearing type on output pin %ld", pin_id);
      goto beach;
    }
  }
  else { /* Trying to set a specific type */
    hr = This->m_pMedia->vt->SetOutputType (This->m_pMedia, pin_id, out_fmt, 0);
    if (hr != S_OK) {
      if (hr == DMO_E_INVALIDSTREAMINDEX) {
        asprintf (&local_error, "pin %ld is not a valid output pin", pin_id);
        goto beach;
      }
      else if (hr == DMO_E_TYPE_NOT_ACCEPTED) {
        asprintf (&local_error, "type was not accepted on output pin %ld",
                  pin_id);
        goto beach;
      }
      else if (hr == S_FALSE) {
        asprintf (&local_error, "type is unacceptable on output pin %ld",
                  pin_id);
        goto beach;
      }
      else {
        asprintf (&local_error, "unexpected error when trying to set type on " \
                  "output pin %ld : 0x%lx", pin_id, hr);
        goto beach;
      }
    }
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;
}

int DMO_Filter_GetOutputSizeInfo (DMO_Filter * This, unsigned long pin_id,
                                  unsigned long * buffer_size,
                                  unsigned long * align, char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
      
  if (!This || !This->m_pMedia || !This->m_pMedia->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p", This);
    goto beach;
  }
  
  if (buffer_size && align) { /* Getting buffer infos */
    hr = This->m_pMedia->vt->GetOutputSizeInfo (This->m_pMedia, pin_id,
                                                buffer_size, align);
    if (hr != S_OK) {
      if (hr == DMO_E_INVALIDSTREAMINDEX) {
        asprintf (&local_error, "pin %ld is not a valid output pin", pin_id);
        goto beach;
      }
      else if (hr == DMO_E_TYPE_NOT_SET) {
        asprintf (&local_error, "type not set on output pin %ld, can't get " \
                  "buffer infos", pin_id);
        goto beach;
      }
      else {
        asprintf (&local_error, "unexpected error when trying to get infos " \
                  "on output pin %ld : 0x%lx", pin_id, hr);
        goto beach;
      }
    }
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;
}

int DMO_Filter_SetPartialOutputType (DMO_Filter * This,
                                     unsigned long * data_length,
                                     char ** data,
                                     DMO_MEDIA_TYPE * out_fmt,
                                     char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
      
  if (!This || !This->m_pPrivateData || !This->m_pPrivateData->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p or this " \
              "DMO does not support the IWMCodecPrivateData interface", This);
    goto beach;
  }
  
  hr = This->m_pPrivateData->vt->SetPartialOutputType (This->m_pPrivateData,
                                                       out_fmt);
  
  if (hr != S_OK) {
    asprintf (&local_error, "unexpected error when trying to set partial " \
              "output type: 0x%lx", hr);
    goto beach;
  }
  
  hr = This->m_pPrivateData->vt->GetPrivateData (This->m_pPrivateData, NULL,
                                                 data_length);
  if (hr != S_OK) {
    asprintf (&local_error, "unexpected error when trying to get private " \
              "data length: 0x%lx", hr);
    goto beach;
  }
  
  if (*data_length == 0)
    goto beach;
  
  *data = malloc (*data_length);
  
  hr = This->m_pPrivateData->vt->GetPrivateData (This->m_pPrivateData,
                                                 *data,
                                                 data_length);
  if (hr != S_OK) {
    asprintf (&local_error, "unexpected error when trying to get private " \
              "data: 0x%lx", hr);
    goto beach;
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE; 
}

int DMO_Filter_Discontinuity (DMO_Filter * This, char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
      
  if (!This || !This->m_pMedia || !This->m_pMedia->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p", This);
    goto beach;
  }
  
  hr = This->m_pMedia->vt->Discontinuity (This->m_pMedia, 0);
  if (hr != S_OK) {
    asprintf (&local_error, "error when sending discontinuity: 0x%lx", hr);
    goto beach;
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;   
}

int DMO_Filter_Flush (DMO_Filter * This, char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
      
  if (!This || !This->m_pMedia || !This->m_pMedia->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p", This);
    goto beach;
  }
  
  hr = This->m_pMedia->vt->Flush (This->m_pMedia);
  if (hr != S_OK) {
    asprintf (&local_error, "error when sending flush: 0x%lx", hr);
    goto beach;
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;   
}

int DMO_Filter_SetProperty (DMO_Filter * This, const WCHAR * name,
                            VARIANT * value, char ** error_message)
{
  HRESULT hr = 1;
  char * local_error = NULL;
  
  if (!This || !This->m_pPropertyBag || !This->m_pPropertyBag->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p or this " \
              "DMO does not support the IPropertyBag interface", This);
    goto beach;
  }
  
  hr = This->m_pPropertyBag->vt->Write (This->m_pPropertyBag,
                                        (LPCOLESTR) name, value);
  if (hr != S_OK) {
    asprintf (&local_error, "unexpected error when trying to set property " \
              "named %ls: 0x%lx", name, hr);
    goto beach;
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;    
}

int DMO_Filter_GetProperty (DMO_Filter * This, const WCHAR * name,
                            VARIANT * value, char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
  
  if (!This || !This->m_pPropertyBag || !This->m_pPropertyBag->vt) {
    asprintf (&local_error, "invalid reference to the DMO object %p or this " \
              "DMO does not support the IPropertyBag interface", This);
    goto beach;
  }
  
  hr = This->m_pPropertyBag->vt->Read (This->m_pPropertyBag,
                                       (LPCOLESTR) name, value, NULL);
  if (hr != S_OK) {
    asprintf (&local_error, "unexpected error when trying to get property " \
              "named %ls: 0x%lx", name, hr);
    goto beach;
  }
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
    return FALSE;
  }
  return TRUE;    
}

DMO_Filter * DMO_Filter_Create (const char * dllname, const GUID* id,
                                unsigned long * input_pins,
                                unsigned long * output_pins,
                                char ** error_message)
{
  HRESULT hr = 0;
  char * local_error = NULL;
  GETCLASS func;
  struct IClassFactory * factory = NULL;
  struct IUnknown * object = NULL;
  DMO_Filter * This = NULL;
  
  This = (DMO_Filter *) malloc (sizeof (DMO_Filter));
  if (!This)
    return NULL;

  memset (This, 0, sizeof (DMO_Filter));
  
  CodecAlloc();

  This->m_iHandle = LoadLibraryA (dllname);
  if (!This->m_iHandle) {
    asprintf (&local_error, "could not open DMO filter from DLL %s", dllname);
    goto beach;
  }
  
  func = (GETCLASS) GetProcAddress ((unsigned) This->m_iHandle,
                                    "DllGetClassObject");
  if (!func) {
    asprintf (&local_error, "unable to get DLL entry point, corrupted file ?");
    goto beach;
  }

  hr = func (id, &IID_IClassFactory, (void **) &factory);
  if (hr || !factory) {            
    asprintf (local_error, "call to DllGetClassObject failed to generate a class factory");
    goto beach;
  }
  
  hr = factory->vt->CreateInstance (factory, 0, &IID_IUnknown,
                                    (void **) &object);
  factory->vt->Release ((IUnknown *) factory);
  if (hr || !object) {
    asprintf (&local_error, "unable to instantiate from this class factory");
    goto beach;
  }
  
  hr = object->vt->QueryInterface (object, &IID_IMediaObject,
                                   (void **) &This->m_pMedia);
  if (hr == 0) {
    /* query for some extra available interface */
    HRESULT r = object->vt->QueryInterface (object, &IID_IMediaObjectInPlace,
                                            (void **) &This->m_pInPlace);
    
    r = object->vt->QueryInterface (object, &IID_IDMOVideoOutputOptimizations,
                                    (void**) &This->m_pOptim);
    if (r == 0 && This->m_pOptim) {
      unsigned long flags;
      r = This->m_pOptim->vt->QueryOperationModePreferences (This->m_pOptim, 0,
                                                             &flags);
      printf("DMO dll supports VO Optimizations %ld %lx\n", r, flags);
      if (flags & DMO_VOSF_NEEDS_PREVIOUS_SAMPLE)
        printf("DMO dll might use previous sample when requested\n");
    }
    
    r = object->vt->QueryInterface (object, &IID_IWMCodecPrivateData,
                                    (void **) &This->m_pPrivateData);
    r = object->vt->QueryInterface (object, &IID_IPropertyBag,
                                    (void **) &This->m_pPropertyBag);
  }
  
  object->vt->Release ((IUnknown *) object);
  if (hr || !This->m_pMedia) {
    asprintf (&local_error, "object does not provide the IMediaObject interface,"
                           " are you sure this is a DMO ?");
    goto beach;
  }
  
  /* Now let's get in DMO object discovery */
  hr = This->m_pMedia->vt->GetStreamCount (This->m_pMedia, input_pins,
                                           output_pins);
  
beach:
  if (error_message && local_error) {
    *error_message = local_error;
  }
  if (local_error) {
    DMO_Filter_Destroy (This);
    This = NULL;
  }
  return This;
}
