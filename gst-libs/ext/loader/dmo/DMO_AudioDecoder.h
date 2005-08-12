#ifndef AVIFILE_DMO_AUDIODECODER_H
#define AVIFILE_DMO_AUDIODECODER_H

typedef struct _DMO_AudioDecoder DMO_AudioDecoder;

int DMO_AudioDecoder_GetOutputInfos (DMO_AudioDecoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align);

DMO_AudioDecoder * DMO_AudioDecoder_Open (char * dllname, GUID * guid,
                                          WAVEFORMATEX * wf);

void DMO_AudioDecoder_Destroy (DMO_AudioDecoder * this);

int DMO_AudioDecoder_Convert (DMO_AudioDecoder * this, const void * in_data,
                              unsigned int in_size, void * out_data,
                              unsigned int out_size, unsigned int * size_read,
                              unsigned int * size_written);

int DMO_AudioDecoder_GetSrcSize (DMO_AudioDecoder *this, int dest_size);

#endif // AVIFILE_DMO_AUDIODECODER_H
