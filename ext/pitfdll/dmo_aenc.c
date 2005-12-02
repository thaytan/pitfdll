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

#include <string.h>

#include "general.h"
#include "pitfdll.h"
#include "DMO_AudioEncoder.h"
#include "ldt_keeper.h"

typedef struct _DMOAudioEnc {
  GstElement parent;

  GstPad *srcpad, *sinkpad;
  
  GstBuffer *out_buffer;

  /* settings */
  gboolean vbr;
  gint quality;
  gint bitrate;
  gint channels;
  gint rate;
  gint block_align;
  gint depth;
  
  void *ctx;
  gulong out_buffer_size;
  gulong in_buffer_size;
  gulong lookahead;
  gulong out_align;
  gulong in_align;
  ldt_fs_t *ldt_fs;
} DMOAudioEnc;

typedef struct _DMOAudioEncClass {
  GstElementClass parent;

  const CodecEntry *entry;
} DMOAudioEncClass;

static void dmo_audioenc_dispose (GObject * obj);
static gboolean dmo_audioenc_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean dmo_audioenc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn dmo_audioenc_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn dmo_audioenc_change_state (GstElement * element,
    GstStateChange transition);
static void dmo_audioenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void dmo_audioenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static const CodecEntry *tmp;
static GstElementClass *parent_class = NULL;

enum
{
  ARG_0,
  ARG_BITRATE,
  ARG_QUALITY,
  ARG_VBR,
  /* FILL ME */
};

/*
 * object lifecycle.
 */

static void
dmo_audioenc_base_init (DMOAudioEncClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;
  GstPadTemplate *src, *snk;
  GstCaps *srccaps, *sinkcaps;

  /* element details */
  klass->entry = tmp;
  details.longname = g_strdup_printf ("DMO %s encoder version %d", tmp->dll,
                                      tmp->version);
  details.klass = "Codec/Encoder/Audio";
  details.description = g_strdup_printf ("DMO %s encoder version %d",
                                         tmp->friendly_name, tmp->version);
  details.author = "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (eklass, &details);
  g_free (details.description);
  g_free (details.longname);

  /* sink, simple */
  if (tmp->sinkcaps) {
    sinkcaps = gst_caps_from_string (tmp->sinkcaps);
  }
  else {
    sinkcaps = gst_caps_from_string ("audio/x-raw-int, signed = (boolean) true" \
                                     ", endianness = (int) 1234");
  }
  snk = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);
  
  /* source caps */
  srccaps = gst_caps_from_string (tmp->srccaps);
  gst_caps_set_simple (srccaps,
      "block_align", GST_TYPE_INT_RANGE, 0, G_MAXINT,
      "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT,
      NULL);
  src = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);
  
  /* register */
  gst_element_class_add_pad_template (eklass, src);
  gst_element_class_add_pad_template (eklass, snk);
}

static void
dmo_audioenc_class_init (DMOAudioEncClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  if (!parent_class)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  eklass->change_state = dmo_audioenc_change_state;
  oklass->dispose = dmo_audioenc_dispose;
  
  oklass->set_property = dmo_audioenc_set_property;
  oklass->get_property = dmo_audioenc_get_property;
  
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Audio bitrate",
          "Defines the audio bitrate the codec will try to reach.",
          0, 2048256, 128016, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_int ("quality", "Audio quality",
          "Defines the audio quality the codec will try to reach.",
          0, 100, 75, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR,
      g_param_spec_boolean ("vbr", "Variable BitRate",
          "Defines if the audio encoder should use a variable bitrate.",
          FALSE, G_PARAM_READWRITE));
}

static void
dmo_audioenc_init (DMOAudioEnc * enc)
{
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (enc);

  /* setup pads */
  enc->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "sink"), "sink");
  gst_pad_set_setcaps_function (enc->sinkpad, dmo_audioenc_sink_setcaps);
  gst_pad_set_event_function (enc->sinkpad, dmo_audioenc_sink_event);
  gst_pad_set_chain_function (enc->sinkpad, dmo_audioenc_chain);
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
                                                                                
  enc->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->ctx = NULL;
  enc->out_buffer = NULL;
  
  /* Usual defaults */
  enc->vbr = FALSE;
  enc->quality = 0;
  enc->channels = 2;
  enc->bitrate = 128016;
  enc->depth = 16;
  enc->rate = 44100;
  enc->block_align = 0;
}

static void
dmo_audioenc_get_property (GObject * object, guint prop_id, GValue * value,
                           GParamSpec * pspec)
{
  DMOAudioEnc * enc;
  
  enc = (DMOAudioEnc *) (object);

  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_int (value, enc->bitrate);
      break;
    case ARG_QUALITY:
      g_value_set_int (value, enc->quality);
      break;
    case ARG_VBR:
      g_value_set_boolean (value, enc->vbr);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
dmo_audioenc_set_property (GObject * object, guint prop_id,
                           const GValue * value, GParamSpec * pspec)
{
  DMOAudioEnc * enc;

  enc = (DMOAudioEnc *) (object);

  switch (prop_id) {
    case ARG_BITRATE:
      enc->bitrate = g_value_get_int (value);
      break;
    case ARG_QUALITY:
      enc->quality = g_value_get_int (value);
      break;
    case ARG_VBR:
      enc->vbr = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
dmo_audioenc_dispose (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

/*
 * processing.
 */

static gboolean
dmo_audioenc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  DMOAudioEnc *enc = (DMOAudioEnc *) gst_pad_get_parent (pad);
  DMOAudioEncClass *klass = (DMOAudioEncClass *) G_OBJECT_GET_CLASS (enc);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gchar *dll;
  WAVEFORMATEX * target_format = NULL, * format = NULL;
  GstCaps *out;
  const GValue *v;
  GstBuffer *extradata = NULL;
  gboolean ret = FALSE;

  Check_FS_Segment ();

  if (enc->ctx) {
    DMO_AudioDecoder_Destroy (enc->ctx);
    enc->ctx = NULL;
  }

  /* read data */
  if (!gst_structure_get_int (s, "rate", &enc->rate) ||
      !gst_structure_get_int (s, "channels", &enc->channels) ||
      !gst_structure_get_int (s, "depth", &enc->depth))
    goto beach;

  /* set up dll initialization */
  dll = g_strdup_printf ("%s.dll", klass->entry->dll);
  target_format = g_malloc0 (sizeof (WAVEFORMATEX));
  target_format->wFormatTag = klass->entry->format;
  target_format->nChannels = enc->channels;
  target_format->nSamplesPerSec = enc->rate;
  if (enc->vbr)
    target_format->nAvgBytesPerSec = enc->quality;
  else
    target_format->nAvgBytesPerSec = enc->bitrate / 8;
  target_format->wBitsPerSample = enc->depth;
  GST_DEBUG ("Will now open %s using %d Hz %d channels, %d depth to encode " \
             "at %d bps", dll, enc->rate, enc->channels, enc->depth,
             enc->bitrate);
  if (!(enc->ctx = DMO_AudioEncoder_Open (dll, &klass->entry->guid,
                                          target_format, &format, enc->vbr))) {
    GST_ERROR ("Failed to open DLL %s", dll);
    g_free (dll);
    g_free (target_format);
    goto beach;
  }
  g_free (dll);
  g_free (target_format);
  
  /* Adjust data */
  enc->bitrate = format->nAvgBytesPerSec * 8;
  enc->block_align = format->nBlockAlign;
  GST_DEBUG ("block_align is %d", enc->block_align);
  
  DMO_AudioEncoder_GetOutputInfos (enc->ctx, &enc->out_buffer_size,
                                   &enc->out_align);
  DMO_AudioEncoder_GetInputInfos (enc->ctx, &enc->in_buffer_size,
                                  &enc->in_align, &enc->lookahead);
  
  GST_DEBUG ("out_buffer_size is %d, out_align is %d", enc->out_buffer_size, enc->out_align);
  GST_DEBUG ("in_buffer_size is %d, in_align is %d, lookahead is %d", enc->in_buffer_size, enc->in_align, enc->lookahead);
  
  /* negotiate output */
  out = gst_caps_from_string (klass->entry->srccaps);
  
  if (format->cbSize) {
    extradata = gst_buffer_new_and_alloc (format->cbSize);
    memcpy ((char *) GST_BUFFER_DATA (extradata),
            (char *) format + sizeof (WAVEFORMATEX), format->cbSize);
    gst_caps_set_simple (out,
      "bitrate", G_TYPE_INT, enc->bitrate,
      "block_align", G_TYPE_INT, enc->block_align,
      "rate", G_TYPE_INT, enc->rate,
      "channels", G_TYPE_INT, enc->channels,
      "depth", G_TYPE_INT, enc->depth,
      "width", G_TYPE_INT, enc->depth,
      "codec_data", GST_TYPE_BUFFER, extradata, NULL);
  }
  else {
    gst_caps_set_simple (out,
      "bitrate", G_TYPE_INT, enc->bitrate,
      "block_align", G_TYPE_INT, enc->block_align,
      "rate", G_TYPE_INT, enc->rate,
      "channels", G_TYPE_INT, enc->channels,
      "depth", G_TYPE_INT, enc->depth,
      "width", G_TYPE_INT, enc->depth, NULL);
  }
  
  if (!gst_pad_set_caps (enc->srcpad, out)) {
    gst_caps_unref (out);
    GST_ERROR ("Failed to negotiate output");
    goto beach;
  }
  gst_caps_unref (out);

  ret = TRUE;
  
beach:
  gst_object_unref (enc);
  
  return ret;
}

static GstFlowReturn
dmo_audioenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  DMOAudioEnc *enc = (DMOAudioEnc *) gst_pad_get_parent (pad);
  GstBuffer *in_buffer = NULL;
  guint read = 0, wrote = 0, status = FALSE;

  Check_FS_Segment ();
  
  /* We are not ready yet ! */
  if (!enc->ctx) {
    ret = GST_FLOW_WRONG_STATE;
    goto beach;
  }
  
  /* encode */
  status = DMO_AudioEncoder_ProcessInput (enc->ctx,
                                          GST_BUFFER_TIMESTAMP (buffer),
                                          GST_BUFFER_DURATION (buffer),
                                          GST_BUFFER_DATA (buffer),
                                          GST_BUFFER_SIZE (buffer),
                                          &read);
  
  GST_DEBUG ("read %d out of %d, time %llu duration %llu", read,
             GST_BUFFER_SIZE (buffer),
             GST_BUFFER_TIMESTAMP (buffer),
             GST_BUFFER_DURATION (buffer));
  
  if (!enc->out_buffer) {
    ret = gst_pad_alloc_buffer (enc->srcpad, GST_BUFFER_OFFSET_NONE,
                                enc->out_buffer_size, GST_PAD_CAPS (enc->srcpad),
                                &(enc->out_buffer));
    if (ret != GST_FLOW_OK) {
      GST_DEBUG ("failed allocating a buffer of %d bytes from pad %p",
                 enc->out_buffer_size, enc->srcpad);
      goto beach;
    }
    /* If the DMO can not set the timestamp we do our best guess */
    GST_BUFFER_TIMESTAMP (enc->out_buffer) = GST_BUFFER_TIMESTAMP (buffer);
  }

  /* If the DMO can not set the duration we do our best guess */
  GST_BUFFER_DURATION (enc->out_buffer) += GST_BUFFER_DURATION (buffer);
  
  if (status == FALSE) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (enc->out_buffer);
    GST_DEBUG ("we have some output buffers to collect (size is %d)",
               GST_BUFFER_SIZE (enc->out_buffer));
    /* Loop until the last buffer (returns FALSE) */
    while ((status = DMO_AudioEncoder_ProcessOutput (enc->ctx,
                           GST_BUFFER_DATA (enc->out_buffer),
                           GST_BUFFER_SIZE (enc->out_buffer),
                           &wrote,
                           &(GST_BUFFER_TIMESTAMP (enc->out_buffer)),
                           &(GST_BUFFER_DURATION (enc->out_buffer)))) == TRUE) {
      GST_DEBUG ("there is another output buffer to collect, pushing %d bytes "\
                 "timestamp %llu duration %llu",
                 wrote, GST_BUFFER_TIMESTAMP (enc->out_buffer),
                 GST_BUFFER_DURATION (enc->out_buffer));
      GST_BUFFER_SIZE (enc->out_buffer) = wrote;
      ret = gst_pad_push (enc->srcpad, enc->out_buffer);
      enc->out_buffer = gst_buffer_new_and_alloc (enc->out_buffer_size);
      /* If the DMO can not set the timestamp we do our best guess */
      GST_BUFFER_TIMESTAMP (enc->out_buffer) = timestamp;
      GST_BUFFER_DURATION (enc->out_buffer) = 0;
    }
    GST_DEBUG ("pushing %d bytes timestamp %llu duration %llu", wrote,
               GST_BUFFER_TIMESTAMP (enc->out_buffer),
               GST_BUFFER_DURATION (enc->out_buffer));
    GST_BUFFER_SIZE (enc->out_buffer) = wrote;
    ret = gst_pad_push (enc->srcpad, enc->out_buffer);
    enc->out_buffer = NULL;
  }
  
beach:
  gst_buffer_unref (buffer);
  gst_object_unref (enc);
  
  return ret;
}

static GstStateChangeReturn
dmo_audioenc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  DMOAudioEnc *enc = (DMOAudioEnc *) element;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      enc->ldt_fs = Setup_LDT_Keeper ();
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      Restore_LDT_Keeper (enc->ldt_fs);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (enc->ctx) {
        Check_FS_Segment ();
        DMO_AudioEncoder_Destroy (enc->ctx);
        enc->ctx = NULL;
      }
      break;
    default:
      break;
  }

  res =  GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return res;
}

static gboolean
dmo_audioenc_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  DMOAudioEnc *enc;

  enc = (DMOAudioEnc *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG ("flush ! implement me !");
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  return res;
}

/*
 * Register codecs.
 */

static const CodecEntry codecs[] = {
  { "wmadmoe", { 0x70f598e9, 0xf4ab, 0x495a,
    0x99, 0xe2, 0xa7, 0xc4, 0xd3, 0xd8, 0x9a, 0xbf },
    0x00000160, 1, "Windows Media Audio",
    "audio/x-raw-int, " \
    "depth = (int) 16, width = (int) 16, "
    "signed = (boolean) true , endianness = (int) 1234",
    "audio/x-wma, wmaversion = (int) 1" },
  { "wmadmoe", { 0x70f598e9, 0xf4ab, 0x495a,
    0x99, 0xe2, 0xa7, 0xc4, 0xd3, 0xd8, 0x9a, 0xbf },
    0x00000161, 2, "Windows Media Audio",
    "audio/x-raw-int, channels = (int) [ 1, 2 ], " \
    "depth = (int) 16, width = (int) 16, "
    "signed = (boolean) true , endianness = (int) 1234",
    "audio/x-wma, wmaversion = (int) 2, depth = (int) 16, width = (int) 16" },
  { "wmadmoe", { 0x70f598e9, 0xf4ab, 0x495a,
    0x99, 0xe2, 0xa7, 0xc4, 0xd3, 0xd8, 0x9a, 0xbf },
    0x00000162, 3, "Windows Media Audio",
    "audio/x-raw-int, channels = (int) [ 2, 8 ], " \
    "depth = (int) 24, width = (int) 24, "
    "signed = (boolean) true , endianness = (int) 1234",
    "audio/x-wma, wmaversion = (int) 3" },
  { "wmspdmoe", { 0x67841b03, 0xc689, 0x4188,
    0xad, 0x3f, 0x4c, 0x9e, 0xbe, 0xec, 0x71, 0x0b },
    0x0000000a, 1, "Windows Media Speech",
    "audio/x-raw-int, rate = (int) [ 8000, 22050 ], " \
    "depth = (int) 16, width = (int) 16, channels = (int) 1, "
    "signed = (boolean) true , endianness = (int) 1234",
    "audio/x-wmsp" },
  { NULL }
};

gboolean
dmo_aenc_register (GstPlugin * plugin)
{
  GTypeInfo info = {
    sizeof (DMOAudioEncClass),
    (GBaseInitFunc) dmo_audioenc_base_init,
    NULL,
    (GClassInitFunc) dmo_audioenc_class_init,
    NULL,
    NULL,
    sizeof (DMOAudioEnc),
    0,
    (GInstanceInitFunc) dmo_audioenc_init,
  };
  gint n;

  for (n = 0; codecs[n].dll != NULL; n++) {
    gchar *full_path, *element_name;
    GType type;

    full_path = g_strdup_printf (WIN32_PATH "/%s.dll", codecs[n].dll);
    if (!g_file_test (full_path, G_FILE_TEST_EXISTS)) {
      g_free (full_path);
      continue;
    }
    GST_DEBUG ("Registering %s (%s)", full_path, codecs[n].dll);
    g_free (full_path);

    element_name = g_strdup_printf ("dmoenc_%sv%d", codecs[n].dll,
                                    codecs[n].version);
    tmp = &codecs[n];
    type = g_type_register_static (GST_TYPE_ELEMENT, element_name, &info, 0);
    if (!gst_element_register (plugin, element_name, GST_RANK_SECONDARY, type)) {
      g_free (element_name);
      return FALSE;
    }
    GST_DEBUG ("Registered %s", element_name);
    g_free (element_name);
  }

  return TRUE;
}
