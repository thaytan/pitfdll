#ifndef AVIFILE_DMO_AUDIODECODER_H
#define AVIFILE_DMO_AUDIODECODER_H

typedef struct _DMO_AudioDecoder DMO_AudioDecoder;

int DMO_AudioDecoder_GetOutputInfos (DMO_AudioDecoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align);

int DMO_AudioDecoder_GetInputInfos (DMO_AudioDecoder * this, 
                                    unsigned long * in_buffer_size,
                                    unsigned long * in_align,
                                    unsigned long * lookahead);

DMO_AudioDecoder * DMO_AudioDecoder_Open (char * dllname, GUID * guid,
                                          WAVEFORMATEX * wf);

void DMO_AudioDecoder_Destroy (DMO_AudioDecoder * this);

int DMO_AudioDecoder_Convert (DMO_AudioDecoder * this, const void * in_data,
                              unsigned int in_size, void * out_data,
                              unsigned int out_size, unsigned int * size_read,
                              unsigned int * size_written);

int DMO_AudioDecoder_ProcessInput (DMO_AudioDecoder * this,
                                   unsigned long long timestamp,
                                   unsigned long long duration,
                                   const void * in_data, unsigned int in_size,
                                   unsigned int * size_read);
                                   
int DMO_AudioDecoder_ProcessOutput (DMO_AudioDecoder * this,
                                    void * out_data, unsigned int out_size,
                                    unsigned int * size_written,
                                    unsigned long long * timestamp,
                                    unsigned long long * duration);

#endif // AVIFILE_DMO_AUDIODECODER_H
