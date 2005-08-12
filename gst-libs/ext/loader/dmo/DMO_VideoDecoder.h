#ifndef AVIFILE_DMO_VIDEODECODER_H
#define AVIFILE_DMO_VIDEODECODER_H

typedef struct _DMO_VideoDecoder DMO_VideoDecoder;

DMO_VideoDecoder * DMO_VideoDecoder_Open (char * dllname, GUID * guid, 
                                          BITMAPINFOHEADER * format);

void DMO_VideoDecoder_Destroy (DMO_VideoDecoder * this);

int DMO_VideoDecoder_DecodeInternal (DMO_VideoDecoder * this, const void * src,
                                     int size, int is_keyframe, char * pImage);

#endif /* AVIFILE_DMO_VIDEODECODER_H */
