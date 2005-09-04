#ifndef AVIFILE_DMO_AUDIOENCODER_H
#define AVIFILE_DMO_AUDIOENCODER_H

typedef struct _DMO_AudioEncoder DMO_AudioEncoder;

int DMO_AudioEncoder_GetOutputInfos (DMO_AudioEncoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align);

int DMO_AudioEncoder_GetInputInfos (DMO_AudioEncoder * this, 
                                    unsigned long * in_buffer_size,
                                    unsigned long * in_align,
                                    unsigned long * lookahead);

DMO_AudioEncoder * DMO_AudioEncoder_Open (char * dllname, GUID * guid,
                                          WAVEFORMATEX * target_format,
                                          WAVEFORMATEX ** format);

void DMO_AudioEncoder_Destroy (DMO_AudioEncoder * this);

int DMO_AudioEncoder_ProcessInput (DMO_AudioEncoder * this,
                                   unsigned long long timestamp,
                                   unsigned long long duration,
                                   const void * in_data, unsigned int in_size,
                                   unsigned int * size_read);

int DMO_AudioEncoder_ProcessOutput (DMO_AudioEncoder * this,
                                    void * out_data, unsigned int out_size,
                                    unsigned int * size_written,
                                    unsigned long long * timestamp,
                                    unsigned long long * duration);

#endif // AVIFILE_DMO_AUDIOENCODER_H
