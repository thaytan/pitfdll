#ifndef AVIFILE_DMO_VIDEOENCODER_H
#define AVIFILE_DMO_VIDEOENCODER_H

typedef struct _DMO_VideoEncoder DMO_VideoEncoder;

DMO_VideoEncoder * DMO_VideoEncoder_Open (char * dllname, GUID * guid,
                                          BITMAPINFOHEADER * format,
                                          unsigned int dest_fourcc,
                                          unsigned long bitrate,
                                          float framerate);

void DMO_VideoEncoder_Destroy (DMO_VideoEncoder * this);

int DMO_VideoEncoder_GetOutputInfos (DMO_VideoEncoder * this, 
                                     unsigned long * out_buffer_size,
                                     unsigned long * out_align);

int DMO_VideoEncoder_EncodeInternal (DMO_VideoEncoder * this,
                                     const void * in_data,
                                     unsigned long in_size,
                                     void * out_data,
                                     unsigned long out_size);


#endif /* AVIFILE_DMO_VIDEOENCODER_H */
