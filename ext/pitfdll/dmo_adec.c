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
#include <gst/audio/multichannel.h>
#include "pitfdll.h"
#include "DMO_AudioDecoder.h"
#include "ldt_keeper.h"

typedef struct _DMOAudioDec {
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
} DMOAudioDec;

typedef struct _DMOAudioDecClass {
  GstElementClass parent;

  const CodecEntry *entry;
} DMOAudioDecClass;

static void dmo_audiodec_dispose (GObject * obj);
static GstPadLinkReturn dmo_audiodec_link (GstPad * pad, const GstCaps * caps);
static void dmo_audiodec_chain (GstPad * pad, GstData * data);
static GstElementStateReturn dmo_audiodec_change_state (GstElement * element);

static const CodecEntry *tmp;
static GstElementClass *parent_class = NULL;

/*
 * object lifecycle.
 */

static void
dmo_audiodec_base_init (DMOAudioDecClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;
  GstPadTemplate *src, *snk;
  GstCaps *srccaps, *sinkcaps;

  /* element details */
  klass->entry = tmp;
  details.longname = g_strdup_printf ("DMO %s decoder version %d", tmp->dll,
                                      tmp->version);
  details.klass = "Codec/Decoder/Audio";
  details.description = g_strdup_printf ("DMO %s decoder version %d",
                                         tmp->friendly_name, tmp->version);;
  details.author = "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (eklass, &details);
  g_free (details.description);
  g_free (details.longname);

  /* sink caps */
  sinkcaps = gst_caps_from_string (tmp->caps);
  gst_caps_set_simple (sinkcaps,
      "block_align", GST_TYPE_INT_RANGE, 0, G_MAXINT,
      "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT,
      NULL);
  snk = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);

  /* source, simple */
  srccaps = gst_caps_from_string ("audio/x-raw-int, " \
                                  "width = (int) { 1, 8, 16, 24, 32 }, " \
                                  "depth = (int) { 1, 8, 16, 24, 32 }, " \
                                  "signed = (boolean) true, " \
                                  "endianness = (int) 1234");
  src = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  /* register */
  gst_element_class_add_pad_template (eklass, src);
  gst_element_class_add_pad_template (eklass, snk);
}

static void
dmo_audiodec_class_init (DMOAudioDecClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  if (!parent_class)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  eklass->change_state = dmo_audiodec_change_state;
  oklass->dispose = dmo_audiodec_dispose;
}

static void
dmo_audiodec_init (DMOAudioDec * dec)
{
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (dec);

  /* setup pads */
  dec->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "sink"), "sink");
  gst_pad_set_link_function (dec->sinkpad, dmo_audiodec_link);
  gst_pad_set_chain_function (dec->sinkpad, dmo_audiodec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
                                                                                
  dec->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_pad_use_explicit_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->ctx = NULL;
  dec->out_buffer = NULL;
}

static void
dmo_audiodec_dispose (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

/*
 * processing.
 */

static GstPadLinkReturn
dmo_audiodec_link (GstPad * pad, const GstCaps * caps)
{
  DMOAudioDec *dec = (DMOAudioDec *) gst_pad_get_parent (pad);
  DMOAudioDecClass *klass = (DMOAudioDecClass *) G_OBJECT_GET_CLASS (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gchar *dll;
  gint size;
  WAVEFORMATEX * hdr = NULL;
  const GValue *v;
  GstBuffer *extradata = NULL;
  GstCaps *out;

  Check_FS_Segment ();

  if (dec->ctx) {
    DMO_AudioDecoder_Destroy (dec->ctx);
    dec->ctx = NULL;
  }

  /* read data */
  if (!gst_structure_get_int (s, "bitrate", &dec->bitrate) ||
      !gst_structure_get_int (s, "block_align", &dec->block_align) ||
      !gst_structure_get_int (s, "rate", &dec->rate) ||
      !gst_structure_get_int (s, "channels", &dec->channels) ||
      !gst_structure_get_int (s, "depth", &dec->depth))
    return GST_PAD_LINK_REFUSED;
  if ((v = gst_structure_get_value (s, "codec_data")))
    extradata = g_value_get_boxed (v);

  /* set up dll initialization */
  dll = g_strdup_printf ("%s.dll", klass->entry->dll);
  size = sizeof (WAVEFORMATEX) +
      (extradata ? GST_BUFFER_SIZE (extradata) : 0);
  hdr = g_malloc0 (size);
  if (extradata) { /* Codec data is appended after our header */
    memcpy (((guchar *) hdr) + sizeof (WAVEFORMATEX),
  	        GST_BUFFER_DATA (extradata), GST_BUFFER_SIZE (extradata));
    hdr->cbSize = GST_BUFFER_SIZE (extradata);
  }
  hdr->wFormatTag = klass->entry->format;
  hdr->nChannels = dec->channels;
  hdr->nSamplesPerSec = dec->rate;
  hdr->nAvgBytesPerSec = dec->bitrate / 8;
  hdr->nBlockAlign = dec->block_align;
  hdr->wBitsPerSample = dec->depth;
  GST_DEBUG ("Will now open %s using %d bps %d channels", dll, dec->bitrate,
             dec->channels);
  if (!(dec->ctx = DMO_AudioDecoder_Open (dll, &klass->entry->guid, hdr))) {
    GST_ERROR ("Failed to open DLL %s", dll);
    g_free (dll);
    g_free (hdr);
    return GST_PAD_LINK_REFUSED;
  }
  g_free (dll);
  g_free (hdr);
  
  DMO_AudioDecoder_GetOutputInfos (dec->ctx, &dec->out_buffer_size,
                                   &dec->out_align);
  DMO_AudioDecoder_GetInputInfos (dec->ctx, &dec->in_buffer_size,
                                  &dec->in_align, &dec->lookahead);
  
  /* negotiate output */
  out = gst_caps_new_simple ("audio/x-raw-int",
      "width", G_TYPE_INT, dec->depth,
      "depth", G_TYPE_INT, dec->depth,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "rate", G_TYPE_INT, dec->rate,
      "endianness", G_TYPE_INT, 1234,
      "channels", G_TYPE_INT, dec->channels,
      NULL);
  if (dec->channels > 2 && dec->channels <= 11) {
    GstAudioChannelPosition pos[] = {
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_LFE,
      GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
      GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT
    };
    gst_audio_set_channel_positions (gst_caps_get_structure (out, 0), pos);
  }
  if (!gst_pad_set_explicit_caps (dec->srcpad, out)) {
    gst_caps_free (out);
    GST_ERROR ("Failed to negotiate output");
    return GST_PAD_LINK_REFUSED;
  }
  gst_caps_free (out);

  return GST_PAD_LINK_OK;
}

static void
dmo_audiodec_chain (GstPad * pad, GstData * data)
{
  DMOAudioDec *dec = (DMOAudioDec *) gst_pad_get_parent (pad);
  GstBuffer *in_buffer = NULL;
  guint read = 0, wrote = 0, status = FALSE;

  Check_FS_Segment ();

  in_buffer = GST_BUFFER (data);
  
  /* encode */
  status = DMO_AudioDecoder_ProcessInput (dec->ctx,
                                          GST_BUFFER_TIMESTAMP (in_buffer),
                                          GST_BUFFER_DURATION (in_buffer),
                                          GST_BUFFER_DATA (in_buffer),
                                          GST_BUFFER_SIZE (in_buffer),
                                          &read);
  
  GST_DEBUG ("read %d out of %d, time %llu duration %llu", read,
             GST_BUFFER_SIZE (in_buffer),
             GST_BUFFER_TIMESTAMP (in_buffer),
             GST_BUFFER_DURATION (in_buffer));
  
  if (!dec->out_buffer) {
    dec->out_buffer = gst_buffer_new_and_alloc (dec->out_buffer_size);
    /* If the DMO can not set the timestamp we do our best guess */
    GST_BUFFER_TIMESTAMP (dec->out_buffer) = GST_BUFFER_TIMESTAMP (in_buffer);
  }

  /* If the DMO can not set the duration we do our best guess */
  GST_BUFFER_DURATION (dec->out_buffer) += GST_BUFFER_DURATION (in_buffer);
  
  gst_buffer_unref (in_buffer);
  in_buffer = NULL;
  
  if (status == FALSE) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (dec->out_buffer);
    GST_DEBUG ("we have some output buffers to collect (size is %d)",
               GST_BUFFER_SIZE (dec->out_buffer));
    /* Loop until the last buffer (returns FALSE) */
    while ((status = DMO_AudioDecoder_ProcessOutput (dec->ctx,
                           GST_BUFFER_DATA (dec->out_buffer),
                           GST_BUFFER_SIZE (dec->out_buffer),
                           &wrote,
                           &(GST_BUFFER_TIMESTAMP (dec->out_buffer)),
                           &(GST_BUFFER_DURATION (dec->out_buffer)))) == TRUE) {
      GST_DEBUG ("there is another output buffer to collect, pushing %d bytes timestamp %llu duration %llu",
                 wrote, GST_BUFFER_TIMESTAMP (dec->out_buffer), GST_BUFFER_DURATION (dec->out_buffer));
      GST_BUFFER_SIZE (dec->out_buffer) = wrote;
      gst_pad_push (dec->srcpad, GST_DATA (dec->out_buffer));
      dec->out_buffer = gst_buffer_new_and_alloc (dec->out_buffer_size);
      /* If the DMO can not set the timestamp we do our best guess */
      GST_BUFFER_TIMESTAMP (dec->out_buffer) = timestamp;
      GST_BUFFER_DURATION (dec->out_buffer) = 0;
    }
    GST_DEBUG ("pushing %d bytes timestamp %llu duration %llu", wrote,
               GST_BUFFER_TIMESTAMP (dec->out_buffer),
               GST_BUFFER_DURATION (dec->out_buffer));
    GST_BUFFER_SIZE (dec->out_buffer) = wrote;
    gst_pad_push (dec->srcpad, GST_DATA (dec->out_buffer));
    dec->out_buffer = NULL;
  }
}

static GstElementStateReturn
dmo_audiodec_change_state (GstElement * element)
{
  DMOAudioDec *dec = (DMOAudioDec *) element;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      dec->ldt_fs = Setup_LDT_Keeper ();
      break;
    case GST_STATE_READY_TO_NULL:
      Restore_LDT_Keeper (dec->ldt_fs);
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (dec->ctx) {
        Check_FS_Segment ();
        DMO_AudioDecoder_Destroy (dec->ctx);
        dec->ctx = NULL;
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
  { "wmadmod", { 0x2eeb4adf, 0x4578, 0x4d10,
		  0xbc, 0xa7, 0xbb, 0x95, 0x5f, 0x56, 0x32, 0x0a },
    0x00000160, 1, "Windows Media Audio",
    "audio/x-wma, wmaversion = (int) 1" },
  { "wmadmod", { 0x2eeb4adf, 0x4578, 0x4d10,
		  0xbc, 0xa7, 0xbb, 0x95, 0x5f, 0x56, 0x32, 0x0a },
    0x00000161, 2, "Windows Media Audio",
    "audio/x-wma, wmaversion = (int) 2" },
  { "wmadmod", { 0x2eeb4adf, 0x4578, 0x4d10,
		  0xbc, 0xa7, 0xbb, 0x95, 0x5f, 0x56, 0x32, 0x0a },
    0x00000162, 3, "Windows Media Audio",
    "audio/x-wma, wmaversion = (int) 3" },
  { "wmspdmod", { 0x874131CB, 0x4ecc, 0x443b,
		  0x89, 0x48, 0x74, 0x6b, 0x89, 0x59, 0x5d, 0x20 },
    0x0000000a, 1, "Windows Media Speech",
    "audio/x-wmsp" },
  { NULL }
};

gboolean
dmo_adec_register (GstPlugin * plugin)
{
  GTypeInfo info = {
    sizeof (DMOAudioDecClass),
    (GBaseInitFunc) dmo_audiodec_base_init,
    NULL,
    (GClassInitFunc) dmo_audiodec_class_init,
    NULL,
    NULL,
    sizeof (DMOAudioDec),
    0,
    (GInstanceInitFunc) dmo_audiodec_init,
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

    element_name = g_strdup_printf ("dmodec_%sv%d", codecs[n].dll,
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
