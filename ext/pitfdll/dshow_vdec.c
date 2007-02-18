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
#include "DS_VideoDecoder.h"
#include "ldt_keeper.h"

typedef struct _DSVideoDec {
  GstElement parent;

  GstPad *srcpad, *sinkpad;

  /* settings */
  gint w, h;
  gint fps_d, fps_n;
  void *ctx;
  ldt_fs_t *ldt_fs;
} DSVideoDec;

typedef struct _DSVideoDecClass {
  GstElementClass parent;

  const CodecEntry *entry;
} DSVideoDecClass;

static void dshow_videodec_dispose (GObject * obj);
static gboolean dshow_videodec_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean dshow_videodec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn dshow_videodec_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn dshow_videodec_change_state (GstElement * element,
    GstStateChange transition);

static const CodecEntry *tmp;
static GstElementClass *parent_class = NULL;

/*
 * object lifecycle.
 */

static void
dshow_videodec_base_init (DSVideoDecClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;
  GstPadTemplate *src, *snk;
  GstCaps *srccaps, *sinkcaps;

  /* element details */
  klass->entry = tmp;
  details.longname = g_strdup_printf ("DS %s decoder version %d", tmp->dll,
                                      tmp->version);
  details.klass = "Codec/Decoder/Video";
  details.description = g_strdup_printf ("DS %s decoder version %d",
                                         tmp->friendly_name, tmp->version);
  details.author = "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (eklass, &details);
  g_free (details.description);
  g_free (details.longname);

  /* sink caps */
  sinkcaps = gst_caps_from_string (tmp->sinkcaps);
  gst_caps_set_simple (sinkcaps,
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
  snk = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);

  /* source, simple */
  if (tmp->srccaps) {
    srccaps = gst_caps_from_string (tmp->srccaps);
  }
  else {
    srccaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
  }
  src = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  /* register */
  gst_element_class_add_pad_template (eklass, src);
  gst_element_class_add_pad_template (eklass, snk);
}

static void
dshow_videodec_class_init (DSVideoDecClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  if (!parent_class)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  eklass->change_state = dshow_videodec_change_state;
  oklass->dispose = dshow_videodec_dispose;
}

static void
dshow_videodec_init (DSVideoDec * dec)
{
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (dec);

  /* setup pads */
  dec->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "sink"), "sink");
  gst_pad_set_setcaps_function (dec->sinkpad, dshow_videodec_sink_setcaps);
  gst_pad_set_event_function (dec->sinkpad, dshow_videodec_sink_event);
  gst_pad_set_chain_function (dec->sinkpad, dshow_videodec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
  
  dec->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->ctx = NULL;
}

static void
dshow_videodec_dispose (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

/*
 * processing.
 */

static gboolean
dshow_videodec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  DSVideoDec *dec = (DSVideoDec *) gst_pad_get_parent (pad);
  DSVideoDecClass *klass = (DSVideoDecClass *) G_OBJECT_GET_CLASS (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  BITMAPINFOHEADER *hdr;
  const GValue *v;
  GstBuffer *extradata = NULL;
  gchar *dll;
  gint size;
  GstCaps *out;
  const GValue *fps;

  Check_FS_Segment ();

  if (dec->ctx) {
    DS_VideoDecoder_Destroy (dec->ctx);
    dec->ctx = NULL;
  }

  /* read data */
  if (!gst_structure_get_int (s, "width", &dec->w) ||
      !gst_structure_get_int (s, "height", &dec->h)) {
    return FALSE;
  }
  
  fps = gst_structure_get_value (s, "framerate");
  if (!fps) {
    return FALSE;
  }
  
  dec->fps_n = gst_value_get_fraction_numerator (fps);
  dec->fps_d = gst_value_get_fraction_denominator (fps);
  
  if ((v = gst_structure_get_value (s, "codec_data")))
    extradata = gst_value_get_buffer (v);

  /* set up dll initialization */
  dll = g_strdup_printf ("%s.dll", klass->entry->dll);
  size = sizeof (BITMAPINFOHEADER) +
      (extradata ? GST_BUFFER_SIZE (extradata) : 0);
  hdr = g_malloc0 (size);
  if (extradata) {
    memcpy (((gchar *)hdr)+sizeof(BITMAPINFOHEADER),
  	    GST_BUFFER_DATA (extradata), GST_BUFFER_SIZE (extradata));
  }
  hdr->width = dec->w;
  hdr->height = dec->h;
  hdr->size = size;
  hdr->image_size = dec->w * dec->h;
  hdr->planes = 1;
  hdr->bit_cnt = 16;
  hdr->compression = klass->entry->format;
  GST_DEBUG ("Will now open %s using %dx%d@%d/%dfps",
             dll, dec->w, dec->h, dec->fps_n, dec->fps_d);
  if (!(dec->ctx = DS_VideoDecoder_Open (dll, &klass->entry->guid,
                                         hdr, 0, 0))) {
    GST_ERROR ("Failed to open DLL %s", dll);
    g_free (dll);
    g_free (hdr);
    return FALSE;
  }
  g_free (dll);
  g_free (hdr);

  /* negotiate output */
  out = gst_caps_new_simple ("video/x-raw-yuv",
      "width", G_TYPE_INT, dec->w,
      "height", G_TYPE_INT, dec->h,
      "framerate", GST_TYPE_FRACTION, dec->fps_n, dec->fps_d,
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y','U','Y','2'), NULL);
  if (!gst_pad_set_caps (dec->srcpad, out)) {
    gst_caps_unref (out);
    GST_ERROR ("Failed to negotiate output");
    return FALSE;
  }
  gst_caps_unref (out);

  /* start */
  DS_VideoDecoder_SetDestFmt (dec->ctx, 16,
                              GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'));
  DS_VideoDecoder_StartInternal (dec->ctx);

  return TRUE;
}

#define ALIGN_2(x) ((x+1)&~1)

static GstFlowReturn
dshow_videodec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  DSVideoDec *dec = (DSVideoDec *) gst_pad_get_parent (pad);
  GstBuffer *in, *out;

  Check_FS_Segment ();

  GST_DEBUG ("Receive data");

  /* decode */
  ret = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE,
                              ALIGN_2 (dec->w) * dec->h * 2,
                              GST_PAD_CAPS (dec->srcpad), &(out));
  if (ret != GST_FLOW_OK) {
    GST_DEBUG ("failed allocating a buffer of %d bytes from pad %p",
               ALIGN_2 (dec->w) * dec->h * 2, dec->srcpad);
    goto beach;
  }
  gst_buffer_stamp (out, buffer);
  DS_VideoDecoder_DecodeInternal (dec->ctx,
      GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), 1, GST_BUFFER_DATA (out));
  gst_buffer_unref (buffer);
  ret = gst_pad_push (dec->srcpad, out);
  
beach:
  return ret;
}

static GstStateChangeReturn
dshow_videodec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  DSVideoDec *dec = (DSVideoDec *) element;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      dec->ldt_fs = Setup_LDT_Keeper ();
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (dec->ctx) {
        Check_FS_Segment ();
        DS_VideoDecoder_Destroy (dec->ctx);
        dec->ctx = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      Restore_LDT_Keeper (dec->ldt_fs);
      break;
    default:
      break;
  }
  return res;
}

static gboolean
dshow_videodec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  DSVideoDec *dec;

  dec = (DSVideoDec *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG ("flush ! implement me !");
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 segment_start, segment_stop, segment_base;
      gdouble segment_rate;
      GstFormat format;
      gboolean update;

      gst_event_parse_new_segment (event, &update, &segment_rate, &format,
                                  &segment_start, &segment_stop, &segment_base);

      if (format == GST_FORMAT_TIME) {
        GST_DEBUG ("newsegment ! implement me !");
      }

      res = gst_pad_event_default (pad, event);
      break;
    }
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
  { "ir50_32", { 0x30355649, 0x0000, 0x0010,
		 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 },
    GST_MAKE_FOURCC ('I', 'V', '5', '0'), 5, "Indeo Video",
    "video/x-intel, ivversion=(int)5",
    NULL },
  { "ir41_32", { 0x31345649, 0x0000, 0x0010,
                 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 },
    GST_MAKE_FOURCC ('I', 'V', '4', '1'), 4, "Indeo Video",
    "video/x-indeo, indeoversion=(int)4",
    NULL },
  { NULL }
};

gboolean
dshow_vdec_register (GstPlugin * plugin)
{
  GTypeInfo info = {
    sizeof (DSVideoDecClass),
    (GBaseInitFunc) dshow_videodec_base_init,
    NULL,
    (GClassInitFunc) dshow_videodec_class_init,
    NULL,
    NULL,
    sizeof (DSVideoDec),
    0,
    (GInstanceInitFunc) dshow_videodec_init,
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

    element_name = g_strdup_printf ("dshowdec_%sv%d", codecs[n].dll,
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
