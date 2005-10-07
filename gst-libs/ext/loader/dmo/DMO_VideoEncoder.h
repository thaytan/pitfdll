#ifndef AVIFILE_DMO_VIDEOENCODER_H
#define AVIFILE_DMO_VIDEOENCODER_H

typedef struct _DMO_VideoEncoder DMO_VideoEncoder;

DMO_VideoEncoder * DMO_VideoEncoder_Open (char * dllname, GUID * guid,
                                          BITMAPINFOHEADER * format,
                                          unsigned int dest_fourcc,
                                          unsigned int vbr,
                                          unsigned long bitrate,
                                          float framerate,
                                          char ** data,
                                          unsigned long * data_length);

void DMO_VideoEncoder_Destroy (DMO_VideoEncoder * this);

int DMO_VideoEncoder_GetOutputInfos (DMO_VideoEncoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align);

int DMO_VideoEncoder_GetInputInfos (DMO_VideoEncoder * this, 
                                    unsigned long * in_buffer_size,
                                    unsigned long * in_align,
                                    unsigned long * lookahead);

int DMO_VideoEncoder_ProcessInput (DMO_VideoEncoder * this,
                                   unsigned long long timestamp,
                                   unsigned long long duration,
                                   const void * in_data, unsigned int in_size,
                                   unsigned int * size_read);

int DMO_VideoEncoder_ProcessOutput (DMO_VideoEncoder * this,
                                    void * out_data, unsigned int out_size,
                                    unsigned int * size_written,
                                    unsigned long long * timestamp,
                                    unsigned long long * duration,
                                    unsigned int * key_frame);


#endif /* AVIFILE_DMO_VIDEOENCODER_H */
