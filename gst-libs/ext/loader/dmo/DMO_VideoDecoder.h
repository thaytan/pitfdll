#ifndef AVIFILE_DMO_VIDEODECODER_H
#define AVIFILE_DMO_VIDEODECODER_H

typedef struct _DMO_VideoDecoder DMO_VideoDecoder;

DMO_VideoDecoder * DMO_VideoDecoder_Open (char * dllname, GUID * guid, 
                                          BITMAPINFOHEADER * format);

void DMO_VideoDecoder_Destroy (DMO_VideoDecoder * this);

void DMO_VideoDecoder_Flush (DMO_VideoDecoder * this);

int DMO_VideoDecoder_GetOutputInfos (DMO_VideoDecoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align);

int DMO_VideoDecoder_GetInputInfos (DMO_VideoDecoder * this, 
                                    unsigned long * in_buffer_size,
                                    unsigned long * in_align,
                                    unsigned long * lookahead);

int DMO_VideoDecoder_ProcessInput (DMO_VideoDecoder * this,
                                   int is_keyframe,
                                   unsigned long long timestamp,
                                   unsigned long long duration,
                                   const void * in_data, unsigned int in_size,
                                   unsigned int * size_read);

int DMO_VideoDecoder_ProcessOutput (DMO_VideoDecoder * this,
                                    void * out_data, unsigned int out_size,
                                    unsigned int * size_written,
                                    unsigned long long * timestamp,
                                    unsigned long long * duration);

#endif /* AVIFILE_DMO_VIDEODECODER_H */
