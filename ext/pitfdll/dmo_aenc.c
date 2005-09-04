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
static GstPadLinkReturn dmo_audioenc_link (GstPad * pad, const GstCaps * caps);
static void dmo_audioenc_chain (GstPad * pad, GstData * data);
static GstElementStateReturn dmo_audioenc_change_state (GstElement * element);
static void dmo_audioenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void dmo_audioenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static const CodecEntry *tmp;
static GstElementClass *parent_class = NULL;

enum
{
  ARG_0,
  ARG_BITRATE
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
  sinkcaps = gst_caps_from_string ("audio/x-raw-int, signed = (boolean) true" \
                                   ", endianness = (int) 1234");
  snk = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);
  
  /* source caps */
  srccaps = gst_caps_from_string (tmp->caps);
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
          G_MININT, G_MAXINT, 128016, G_PARAM_READWRITE));
}

static void
dmo_audioenc_init (DMOAudioEnc * enc)
{
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (enc);

  /* setup pads */
  enc->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "sink"), "sink");
  gst_pad_set_link_function (enc->sinkpad, dmo_audioenc_link);
  gst_pad_set_chain_function (enc->sinkpad, dmo_audioenc_chain);
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
                                                                                
  enc->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_pad_use_explicit_caps (enc->srcpad);
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->ctx = NULL;
  enc->out_buffer = NULL;
  
  /* Usual defaults */
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

static GstPadLinkReturn
dmo_audioenc_link (GstPad * pad, const GstCaps * caps)
{
  DMOAudioEnc *enc = (DMOAudioEnc *) gst_pad_get_parent (pad);
  DMOAudioEncClass *klass = (DMOAudioEncClass *) G_OBJECT_GET_CLASS (enc);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gchar *dll;
  WAVEFORMATEX * target_format = NULL, * format = NULL;
  GstCaps *out;
  const GValue *v;
  GstBuffer *extradata = NULL;

  Check_FS_Segment ();

  if (enc->ctx) {
    DMO_AudioDecoder_Destroy (enc->ctx);
    enc->ctx = NULL;
  }

  /* read data */
  if (!gst_structure_get_int (s, "rate", &enc->rate) ||
      !gst_structure_get_int (s, "channels", &enc->channels) ||
      !gst_structure_get_int (s, "depth", &enc->depth))
    return GST_PAD_LINK_REFUSED;

  /* set up dll initialization */
  dll = g_strdup_printf ("%s.dll", klass->entry->dll);
  target_format = g_malloc0 (sizeof (WAVEFORMATEX));
  target_format->wFormatTag = klass->entry->format;
  target_format->nChannels = enc->channels;
  target_format->nSamplesPerSec = enc->rate;
  target_format->nAvgBytesPerSec = enc->bitrate / 8;
  target_format->wBitsPerSample = enc->depth;
  GST_DEBUG ("Will now open %s using %d Hz %d channels, %d depth to encode " \
             "at %d bps", dll, enc->rate, enc->channels, enc->depth,
             enc->bitrate);
  if (!(enc->ctx = DMO_AudioEncoder_Open (dll, &klass->entry->guid,
                                          target_format, &format))) {
    GST_ERROR ("Failed to open DLL %s", dll);
    g_free (dll);
    g_free (target_format);
    return GST_PAD_LINK_REFUSED;
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
  out = gst_caps_from_string (klass->entry->caps);
  
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
  
  if (!gst_pad_set_explicit_caps (enc->srcpad, out)) {
    gst_caps_free (out);
    GST_ERROR ("Failed to negotiate output");
    return GST_PAD_LINK_REFUSED;
  }
  gst_caps_free (out);

  return GST_PAD_LINK_OK;
}

static void
dmo_audioenc_chain (GstPad * pad, GstData * data)
{
  DMOAudioEnc *enc = (DMOAudioEnc *) gst_pad_get_parent (pad);
  GstBuffer *in_buffer = NULL;
  guint read = 0, wrote = 0, status = FALSE;

  Check_FS_Segment ();

  in_buffer = GST_BUFFER (data);
  
  /* encode */
  status = DMO_AudioEncoder_ProcessInput (enc->ctx,
                                          GST_BUFFER_TIMESTAMP (in_buffer),
                                          GST_BUFFER_DURATION (in_buffer),
                                          GST_BUFFER_DATA (in_buffer),
                                          GST_BUFFER_SIZE (in_buffer),
                                          &read);
  
  GST_DEBUG ("read %d out of %d, time %llu duration %llu", read,
             GST_BUFFER_SIZE (in_buffer),
             GST_BUFFER_TIMESTAMP (in_buffer),
             GST_BUFFER_DURATION (in_buffer));
  
  if (!enc->out_buffer) {
    enc->out_buffer = gst_buffer_new_and_alloc (enc->out_buffer_size);
    /* If the DMO can not set the timestamp we do our best guess */
    GST_BUFFER_TIMESTAMP (enc->out_buffer) = GST_BUFFER_TIMESTAMP (in_buffer);
  }

  /* If the DMO can not set the duration we do our best guess */
  GST_BUFFER_DURATION (enc->out_buffer) += GST_BUFFER_DURATION (in_buffer);
  
  gst_buffer_unref (in_buffer);
  in_buffer = NULL;
  
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
      gst_pad_push (enc->srcpad, GST_DATA (enc->out_buffer));
      enc->out_buffer = gst_buffer_new_and_alloc (enc->out_buffer_size);
      /* If the DMO can not set the timestamp we do our best guess */
      GST_BUFFER_TIMESTAMP (enc->out_buffer) = timestamp;
      GST_BUFFER_DURATION (enc->out_buffer) = 0;
    }
    GST_DEBUG ("pushing %d bytes timestamp %llu duration %llu", wrote,
               GST_BUFFER_TIMESTAMP (enc->out_buffer),
               GST_BUFFER_DURATION (enc->out_buffer));
    GST_BUFFER_SIZE (enc->out_buffer) = wrote;
    gst_pad_push (enc->srcpad, GST_DATA (enc->out_buffer));
    enc->out_buffer = NULL;
  }
}

static GstElementStateReturn
dmo_audioenc_change_state (GstElement * element)
{
  DMOAudioEnc *enc = (DMOAudioEnc *) element;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      enc->ldt_fs = Setup_LDT_Keeper ();
      break;
    case GST_STATE_READY_TO_NULL:
      Restore_LDT_Keeper (enc->ldt_fs);
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (enc->ctx) {
        Check_FS_Segment ();
        DMO_AudioEncoder_Destroy (enc->ctx);
        enc->ctx = NULL;
      }
      break;
    default:
      break;
  }

  return parent_class->change_state (element);
}

/*
 * Register codecs.
 */

static const CodecEntry codecs[] = {
  { "wmadmoe", { 0x70f598e9, 0xF4ab, 0x495a,
		  0x99, 0xe2, 0xa7, 0xc4, 0xd3, 0xd8, 0x9a, 0xbf },
    0x00000160, 1, "Windows Media Audio",
    "audio/x-wma, wmaversion = (int) 1" },
  { "wmadmoe", { 0x70f598e9, 0xF4ab, 0x495a,
		  0x99, 0xe2, 0xa7, 0xc4, 0xd3, 0xd8, 0x9a, 0xbf },
    0x00000161, 2, "Windows Media Audio",
    "audio/x-wma, wmaversion = (int) 2" },
  { "wmadmoe", { 0x70f598e9, 0xF4ab, 0x495a,
		  0x99, 0xe2, 0xa7, 0xc4, 0xd3, 0xd8, 0x9a, 0xbf },
    0x00000162, 3, "Windows Media Audio",
    "audio/x-wma, wmaversion = (int) 3" },
  { "wmspdmoe", { 0x67841b03, 0xc689, 0x4188,
		  0xad, 0x3f, 0x4c, 0x9e, 0xbe, 0xec, 0x71, 0x0b },
    0x0000000a, 1, "Windows Media Speech",
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
