/* DLL loader
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pitfdll.h"

#include "win32.h"
#include "windef.h"
#include "ldt_keeper.h"

HMODULE WINAPI LoadLibraryA(LPCSTR);
FARPROC WINAPI GetProcAddress(HMODULE,LPCSTR);
int WINAPI FreeLibrary(HMODULE);

typedef struct _SoundComponentData {
  long flags;
  int format;
  short numChannels;
  short sampleSize;
  unsigned long sampleRate;
  long sampleCount;
  guint8 *buffer;
  long reserved;
} SoundComponentData;

typedef gpointer SoundConverter;
typedef int (__cdecl* LPFUNC1)(long flag);
typedef int (__cdecl* LPFUNC2)(const SoundComponentData *, 
			       const SoundComponentData *,
			       SoundConverter *);
typedef int (__cdecl* LPFUNC3)(SoundConverter sc);
typedef int (__cdecl* LPFUNC4)(void);
typedef int (__cdecl* LPFUNC5)(SoundConverter sc, int selector, void * infoPtr);
typedef int (__cdecl* LPFUNC6)(SoundConverter sc, 
			       unsigned long inputBytesTarget,
			       unsigned long *inputFrames,
			       unsigned long *inputBytes,
			       unsigned long *outputBytes );
typedef int (__cdecl* LPFUNC7)(SoundConverter sc, 
			       const void    *inputPtr, 
			       unsigned long inputFrames,
			       void          *outputPtr,
			       unsigned long *outputFrames,
			       unsigned long *outputBytes );
typedef int (__cdecl* LPFUNC8)(SoundConverter sc,
			       void      *outputPtr,
			       unsigned long *outputFrames,
			       unsigned long *outputBytes);
typedef int (__cdecl* LPFUNC9)(SoundConverter sc) ;  

typedef struct _QtAudioDec {
  GstElement parent;

  GstPad *srcpad, *sinkpad;

  /* settings */
  gint channels, rate;
  void *ctx;
  unsigned long in_framesize, out_framesize;
  GstBuffer *cache;
  ldt_fs_t *ldt_fs;
  GMutex *lock;
  HINSTANCE dll, qts;

  /* funcs */
  LPFUNC1 InitializeQTML;
  LPFUNC2 SoundConverterOpen;
  LPFUNC3 SoundConverterClose;
  LPFUNC4 TerminateQTML;
  LPFUNC5 SoundConverterSetInfo;
  LPFUNC6 SoundConverterGetBufferSizes;
  LPFUNC7 SoundConverterConvertBuffer;
  LPFUNC8 SoundConverterEndConversion;
  LPFUNC9 SoundConverterBeginConversion;
} QtAudioDec;

typedef struct _QtAudioDecClass {
  GstElementClass parent;
} QtAudioDecClass;

static void qt_audiodec_finalize (GObject * obj);
static GstPadLinkReturn qt_audiodec_link (GstPad * pad, const GstCaps * caps);
static void qt_audiodec_chain (GstPad * pad, GstData * data);
static GstElementStateReturn qt_audiodec_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*
 * object lifecycle.
 */

static void
qt_audiodec_base_init (QtAudioDecClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  static GstElementDetails details =
  GST_ELEMENT_DETAILS ("quicktime binary audio decoder",
      "Codec/Decoder/Audio",
      "Decodes quicktime audio using binary dlls",
      "Ronald S. Bultje <rbultje@ronald.bitfreak.net>");
  GstPadTemplate *src, *snk;
  GstCaps *srccaps, *sinkcaps;

  /* element details */
  gst_element_class_set_details (eklass, &details);

  /* sink caps */
  sinkcaps = gst_caps_new_simple ("audio/x-qdm2",
      "samplerate", GST_TYPE_INT_RANGE, 8000, 96000,
      "channels", GST_TYPE_INT_RANGE, 1, 8,
      "framesize", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);
  snk = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);

  /* source, simple */
  srccaps = gst_caps_from_string ("audio/x-raw-int");
  src = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  /* register */
  gst_element_class_add_pad_template (eklass, src);
  gst_element_class_add_pad_template (eklass, snk);
}

static void
qt_audiodec_class_init (QtAudioDecClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  eklass->change_state = qt_audiodec_change_state;
  oklass->finalize = qt_audiodec_finalize;
}

static void
qt_audiodec_init (QtAudioDec * dec)
{
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (dec);

  /* setup pads */
  dec->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "sink"), "sink");
  gst_pad_set_link_function (dec->sinkpad, qt_audiodec_link);
  gst_pad_set_chain_function (dec->sinkpad, qt_audiodec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
                                                                                
  dec->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_pad_use_explicit_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->dll = NULL;
  dec->ctx = NULL;
  dec->cache = NULL;
  dec->lock = g_mutex_new ();
}

static void
qt_audiodec_finalize (GObject * obj)
{
  QtAudioDec *dec = (QtAudioDec *) obj;

  if (dec->lock) {
    g_mutex_free (dec->lock);
    dec->lock = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/*
 * processing.
 */

#define FOUR_CHAR_CODE(a,b,c,d) \
    ((uint32_t)(a)<<24 | (uint32_t)(b)<<16 | (uint32_t)(c)<<8 | (uint32_t)(d))

static GstPadLinkReturn
qt_audiodec_link (GstPad * pad, const GstCaps * caps)
{
  QtAudioDec *dec = (QtAudioDec *) gst_pad_get_parent (pad);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *mime = gst_structure_get_name (s);
  SoundComponentData input, output;
  const GValue *v;
  GstBuffer *extradata = NULL;
  int samplesize;
  unsigned long in_frames, in_bytes, out_bytes, size;
  GstCaps *out;

  g_mutex_lock (dec->lock);

  Check_FS_Segment ();

  if (!dec->dll) {
    g_mutex_unlock (dec->lock);
    return GST_PAD_LINK_DELAYED;
  }

  if (dec->ctx) {
    unsigned long x, y;

    dec->SoundConverterEndConversion (dec->ctx, NULL, &x, &y);
    dec->SoundConverterClose (dec->ctx);
    dec->ctx = NULL;
  }

  /* read data */
  if (!gst_structure_get_int (s, "channels", &dec->channels) ||
      !gst_structure_get_int (s, "rate", &dec->rate) ||
      !gst_structure_get_int (s, "samplesize", &samplesize))
    goto failed;
  if ((v = gst_structure_get_value (s, "codec_data")))
    extradata = g_value_get_boxed (v);

  /* set up dll initialization */
  memset (&input, 0, sizeof (input));
  memset (&output, 0, sizeof (output));
  input.sampleRate = output.sampleRate = dec->rate;
  output.numChannels = input.numChannels = dec->channels;
  output.sampleSize = 16;
  input.sampleSize = samplesize;
  output.format = FOUR_CHAR_CODE ('N', 'O', 'N', 'E');
  if (!strcmp (mime, "audio/x-qdm2")) {
    input.format = FOUR_CHAR_CODE ('Q', 'D', 'M', '2');
  } /* add other codecs here */

  if (dec->SoundConverterOpen (&input, &output, &dec->ctx)) {
    GST_ERROR ("Failed to open qt context");
    goto failed;
  }

  if (extradata) {
    GST_DEBUG ("Setting decoder-specific-data (len=0x%x)",
        GST_BUFFER_SIZE (extradata));
    if (dec->SoundConverterSetInfo (dec->ctx,
				    FOUR_CHAR_CODE ('w', 'a', 'v', 'e'),
				    GST_BUFFER_DATA (extradata))) {
      GST_ERROR ("Failed to read decoderspecific data");
      goto failed;
    }
  }

  /* negotiate output */
  out = gst_caps_new_simple ("audio/x-raw-int",
      "signed", G_TYPE_BOOLEAN, TRUE,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "channels", G_TYPE_INT, dec->channels,
      "rate", G_TYPE_INT, dec->rate, NULL);
  if (!gst_pad_set_explicit_caps (dec->srcpad, out)) {
    gst_caps_free (out);
    GST_ERROR ("Failed to negotiate output");
    goto failed;
  }
  gst_caps_free (out);

  /* start */
  size = dec->channels * dec->rate * 2;
  dec->SoundConverterGetBufferSizes (dec->ctx, size, &in_frames,
				     &in_bytes, &out_bytes);
  dec->in_framesize = ceil (in_bytes / in_frames);
  dec->out_framesize = ceil (out_bytes / in_frames);
  dec->SoundConverterBeginConversion (dec->ctx);

  GST_LOG ("done, req=%ld (f: %ld, in: %ld, out: %ld), in_f=%ld, out_f=%ld",
	   size, in_frames, in_bytes, out_bytes,
	   dec->in_framesize, dec->out_framesize);

  g_mutex_unlock (dec->lock);

  return GST_PAD_LINK_OK;

failed:
  g_mutex_unlock (dec->lock);

  return GST_PAD_LINK_REFUSED;
}

static void
qt_audiodec_chain (GstPad * pad, GstData * da)
{
  QtAudioDec *dec = (QtAudioDec *) gst_pad_get_parent (pad);
  GstBuffer *in, *out;
  guint8 *data;
  gint size;
  GstClockTime time;

  g_mutex_lock (dec->lock);

  /* merge */
  in = GST_BUFFER (da);
  time = GST_BUFFER_TIMESTAMP (in);
  if (dec->cache) {
    in = gst_buffer_merge (dec->cache, in);
    dec->cache = NULL;
  }
  data = GST_BUFFER_DATA (in);
  size = GST_BUFFER_SIZE (in);

  Check_FS_Segment ();

  GST_DEBUG ("Receive data");

  /* decode */
  while (size > 0) {
    unsigned long out_frames, out_bytes;
    int frames = size / dec->in_framesize;

    if (!frames)
      break;

    out = gst_buffer_new_and_alloc (dec->rate * dec->channels * 2);
    if (dec->SoundConverterConvertBuffer (dec->ctx, data, frames,
					  GST_BUFFER_DATA (out),
					  &out_frames, &out_bytes)) {
      GST_ERROR ("Failed to decode");
      gst_buffer_unref (out);
      g_mutex_unlock (dec->lock);
      return;
    }

    data += frames * dec->in_framesize;
    size -= frames * dec->in_framesize;

    GST_BUFFER_TIMESTAMP (out) = time;
    GST_BUFFER_SIZE (out) = out_bytes;
    GST_BUFFER_DURATION (out) = GST_SECOND * out_frames / dec->rate;
    time += GST_BUFFER_DURATION (out);

    gst_pad_push (dec->srcpad, GST_DATA (out));
  }

  /* keep */
  if (size) {
    dec->cache = gst_buffer_create_sub (in, GST_BUFFER_SIZE (in) - size, size);
  }
  gst_buffer_unref (in);

  g_mutex_unlock (dec->lock);
}

static GstElementStateReturn
qt_audiodec_change_state (GstElement * element)
{
  QtAudioDec *dec = (QtAudioDec *) element;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      g_mutex_lock (dec->lock);
      dec->ldt_fs = Setup_LDT_Keeper ();
#if 0
      GST_DEBUG ("loading qts");
      if (!(dec->qts = LoadLibraryA ("QuickTime.qts"))) {
        GST_ELEMENT_ERROR (element, LIBRARY, INIT, (NULL),
            ("Failed to load QuickTime.qts"));
        Restore_LDT_Keeper (dec->ldt_fs);
        goto failed;
      }
#endif
      GST_DEBUG ("Loading dll");
      if (!(dec->dll = LoadLibraryA ("qtmlClient.dll"))) {
        GST_ELEMENT_ERROR (element, LIBRARY, INIT, (NULL),
            ("Failed to load qtmlClient.dll"));
        //FreeLibrary (dec->qts);
        Restore_LDT_Keeper (dec->ldt_fs);
        goto failed;
      }

      GST_DEBUG ("Loading symbols");
#define find(x,y) \
    (dec->x = (gpointer) GetProcAddress (dec->dll, y))
      /* retrieve function addresses */
      if (!find (InitializeQTML, "InitializeQTML") ||
          !find (SoundConverterOpen, "SoundConverterOpen") ||
          !find (SoundConverterClose, "SoundConverterClose") ||
          !find (TerminateQTML, "TerminateQTML") ||
          !find (SoundConverterSetInfo, "SoundConverterSetInfo") ||
          !find (SoundConverterGetBufferSizes, "SoundConverterGetBufferSizes") ||
          !find (SoundConverterConvertBuffer, "SoundConverterConvertBuffer") ||
          !find (SoundConverterEndConversion, "SoundConverterEndConversion") ||
          !find (SoundConverterBeginConversion, "SoundConverterBeginConversion")) {
        GST_ELEMENT_ERROR (element, LIBRARY, INIT, (NULL),
            ("Failed to retrieve symbols"));
        //FreeLibrary (dec->qts);
        FreeLibrary (dec->dll);
        dec->dll = NULL;
        Restore_LDT_Keeper (dec->ldt_fs);
        goto failed;
      }

      GST_DEBUG ("Have addies, call %p", dec->InitializeQTML);
      dec->InitializeQTML (6 + 16);
      GST_DEBUG ("Found all addies");
      g_mutex_unlock (dec->lock);
      break;
    failed:
      g_mutex_unlock (dec->lock);
      return GST_STATE_FAILURE;
    case GST_STATE_READY_TO_NULL:
      g_mutex_lock (dec->lock);
      Check_FS_Segment ();
      //FreeLibrary (dec->qts);
      FreeLibrary (dec->dll);
      dec->dll = NULL;
      Restore_LDT_Keeper (dec->ldt_fs);
      g_mutex_unlock (dec->lock);
      break;
    case GST_STATE_PAUSED_TO_READY:
      g_mutex_lock (dec->lock);
      Check_FS_Segment ();
      if (dec->ctx) {
        unsigned long x, y;

        dec->SoundConverterEndConversion (dec->ctx, NULL, &x, &y);
        dec->SoundConverterClose (dec->ctx);
        dec->ctx = NULL;
      }
      if (dec->cache) {
        gst_buffer_unref (dec->cache);
        dec->cache = NULL;
      }
      g_mutex_unlock (dec->lock);
      break;
    default:
      break;
  }

  return parent_class->change_state (element);
}

/*
 * Register codecs.
 */

gboolean
qt_adec_register (GstPlugin * plugin)
{
  GTypeInfo info = {
    sizeof (QtAudioDecClass),
    (GBaseInitFunc) qt_audiodec_base_init,
    NULL,
    (GClassInitFunc) qt_audiodec_class_init,
    NULL,
    NULL,
    sizeof (QtAudioDec),
    0,
    (GInstanceInitFunc) qt_audiodec_init,
  };
  GType type;

  if (!g_file_test (WIN32_PATH "/qtmlClient.dll", G_FILE_TEST_EXISTS))
    return FALSE;
  type = g_type_register_static (GST_TYPE_ELEMENT, "QtAudioDec", &info, 0);
  return gst_element_register (plugin, "qtadec_bin", GST_RANK_SECONDARY, type);
}
