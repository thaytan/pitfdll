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
  gdouble fps;
  void *ctx;
  ldt_fs_t *ldt_fs;
} DSVideoDec;

typedef struct _DSVideoDecClass {
  GstElementClass parent;

  const CodecEntry *entry;
} DSVideoDecClass;

static void dshow_videodec_dispose (GObject * obj);
static GstPadLinkReturn dshow_videodec_link (GstPad * pad, const GstCaps * caps);
static void dshow_videodec_chain (GstPad * pad, GstData * data);
static GstElementStateReturn dshow_videodec_change_state (GstElement * element);

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
  details.longname = g_strdup_printf ("DS %s decoder", tmp->dll);
  details.klass = "Codec/Decoder/Video";
  details.description = details.longname;
  details.author = "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (eklass, &details);
  g_free (details.longname);

  /* sink caps */
  sinkcaps = gst_caps_from_string (tmp->caps);
  gst_caps_set_simple (sinkcaps,
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL);
  snk = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);

  /* source, simple */
  srccaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
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
  gst_pad_set_link_function (dec->sinkpad, dshow_videodec_link);
  gst_pad_set_chain_function (dec->sinkpad, dshow_videodec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
                                                                                
  dec->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_pad_use_explicit_caps (dec->srcpad);
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

static GstPadLinkReturn
dshow_videodec_link (GstPad * pad, const GstCaps * caps)
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

  Check_FS_Segment ();

  if (dec->ctx) {
    DS_VideoDecoder_Destroy (dec->ctx);
    dec->ctx = NULL;
  }

  /* read data */
  if (!gst_structure_get_int (s, "width", &dec->w) ||
      !gst_structure_get_int (s, "height", &dec->h) ||
      !gst_structure_get_double (s, "framerate", &dec->fps))
    return GST_PAD_LINK_REFUSED;
  if ((v = gst_structure_get_value (s, "codec_data")))
    extradata = g_value_get_boxed (v);

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
  hdr->compression = klass->entry->fourcc;
  GST_DEBUG ("Will now open %s using %dx%d@%lffps",
	     dll, dec->w, dec->h, dec->fps);
  if (!(dec->ctx = DS_VideoDecoder_Open (dll, &klass->entry->guid,
					  hdr, 0, 0))) {
    g_free (dll);
    g_free (hdr);
    GST_ERROR ("Failed to open DLL %s", dll);
    return GST_PAD_LINK_REFUSED;
  }
  g_free (dll);
  g_free (hdr);

  /* negotiate output */
  out = gst_caps_new_simple ("video/x-raw-yuv",
      "width", G_TYPE_INT, dec->w,
      "height", G_TYPE_INT, dec->h,
      "framerate", G_TYPE_DOUBLE, dec->fps,
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y','U','Y','2'), NULL);
  if (!gst_pad_set_explicit_caps (dec->srcpad, out)) {
    gst_caps_free (out);
    GST_ERROR ("Failed to negotiate output");
    return GST_PAD_LINK_REFUSED;
  }
  gst_caps_free (out);

  /* start */
  DS_VideoDecoder_SetDestFmt (dec->ctx, 16,
			       GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'));
  DS_VideoDecoder_StartInternal (dec->ctx);

  return GST_PAD_LINK_OK;
}

#define ALIGN_2(x) ((x+1)&~1)

static void
dshow_videodec_chain (GstPad * pad, GstData * data)
{
  DSVideoDec *dec = (DSVideoDec *) gst_pad_get_parent (pad);
  GstBuffer *in, *out;

  Check_FS_Segment ();

  GST_DEBUG ("Receive data");

  /* decode */
  in = GST_BUFFER (data);
  out = gst_buffer_new_and_alloc (ALIGN_2 (dec->w) * dec->h * 2);
  GST_BUFFER_TIMESTAMP (out) = GST_BUFFER_TIMESTAMP (in);
  GST_BUFFER_DURATION (out) = GST_BUFFER_DURATION (in);
  DS_VideoDecoder_DecodeInternal (dec->ctx,
      GST_BUFFER_DATA (in), GST_BUFFER_SIZE (in), 1, GST_BUFFER_DATA (out));
  gst_data_unref (data);
  gst_pad_push (dec->srcpad, GST_DATA (out));
}

static GstElementStateReturn
dshow_videodec_change_state (GstElement * element)
{
  DSVideoDec *dec = (DSVideoDec *) element;

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
        DS_VideoDecoder_Destroy (dec->ctx);
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
  { "ir50_32", { 0x30355649, 0x0000, 0x0010,
		 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 },
    GST_MAKE_FOURCC ('I', 'V', '5', '0'),
    "video/x-intel, ivversion=(int)5" },
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

    element_name = g_strdup_printf ("dshowdec_%s", codecs[n].dll);
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
