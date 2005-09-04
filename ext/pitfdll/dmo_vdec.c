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
#include "DMO_VideoDecoder.h"
#include "ldt_keeper.h"

typedef struct _DMOVideoDec {
  GstElement parent;

  GstPad *srcpad, *sinkpad;

  GstBuffer *out_buffer;
  
  /* settings */
  gint w, h;
  gdouble fps;
  
  void *ctx;
  gulong out_buffer_size;
  gulong in_buffer_size;
  gulong lookahead;
  gulong out_align;
  gulong in_align;
  ldt_fs_t *ldt_fs;
} DMOVideoDec;

typedef struct _DMOVideoDecClass {
  GstElementClass parent;

  const CodecEntry *entry;
} DMOVideoDecClass;

static void dmo_videodec_dispose (GObject * obj);
static GstPadLinkReturn dmo_videodec_link (GstPad * pad, const GstCaps * caps);
static void dmo_videodec_chain (GstPad * pad, GstData * data);
static GstElementStateReturn dmo_videodec_change_state (GstElement * element);

static const CodecEntry *tmp;
static GstElementClass *parent_class = NULL;

/*
 * object lifecycle.
 */

static void
dmo_videodec_base_init (DMOVideoDecClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;
  GstPadTemplate *src, *snk;
  GstCaps *srccaps, *sinkcaps;

  /* element details */
  klass->entry = tmp;
  details.longname = g_strdup_printf ("DMO %s decoder version %d", tmp->dll,
                                      tmp->version);
  details.klass = "Codec/Decoder/Video";
  details.description = g_strdup_printf ("DMO %s decoder version %d",
                                         tmp->friendly_name, tmp->version);
  details.author = "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (eklass, &details);
  g_free (details.description);
  g_free (details.longname);

  /* sink caps */
  sinkcaps = gst_caps_from_string (tmp->caps);
  gst_caps_set_simple (sinkcaps,
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL);
  snk = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);

  /* source, simple */
  srccaps = gst_caps_from_string ("video/x-raw-yuv, format=(fourcc)YUY2");
  src = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  /* register */
  gst_element_class_add_pad_template (eklass, src);
  gst_element_class_add_pad_template (eklass, snk);
}

static void
dmo_videodec_class_init (DMOVideoDecClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  if (!parent_class)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  eklass->change_state = dmo_videodec_change_state;
  oklass->dispose = dmo_videodec_dispose;
}

static void
dmo_videodec_init (DMOVideoDec * dec)
{
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (dec);

  /* setup pads */
  dec->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "sink"), "sink");
  gst_pad_set_link_function (dec->sinkpad, dmo_videodec_link);
  gst_pad_set_chain_function (dec->sinkpad, dmo_videodec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
                                                                                
  dec->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_pad_use_explicit_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->ctx = NULL;
  dec->out_buffer = NULL;
}

static void
dmo_videodec_dispose (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

/*
 * processing.
 */

static GstPadLinkReturn
dmo_videodec_link (GstPad * pad, const GstCaps * caps)
{
  DMOVideoDec *dec = (DMOVideoDec *) gst_pad_get_parent (pad);
  DMOVideoDecClass *klass = (DMOVideoDecClass *) G_OBJECT_GET_CLASS (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  BITMAPINFOHEADER *hdr;
  const GValue *v;
  GstBuffer *extradata = NULL;
  gchar *dll;
  gint size;
  GstCaps *out;

  Check_FS_Segment ();

  if (dec->ctx) {
    DMO_VideoDecoder_Destroy (dec->ctx);
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
  hdr->compression = klass->entry->format;
  GST_DEBUG ("Will now open %s using %dx%d@%lffps",
	     dll, dec->w, dec->h, dec->fps);
  if (!(dec->ctx = DMO_VideoDecoder_Open (dll, &klass->entry->guid, hdr))) {
    GST_ERROR ("Failed to open DLL %s", dll);
    g_free (dll);
    g_free (hdr);
    return GST_PAD_LINK_REFUSED;
  }
  g_free (dll);
  g_free (hdr);
  
  DMO_VideoDecoder_GetOutputInfos (dec->ctx, &dec->out_buffer_size,
                                   &dec->out_align);
  DMO_VideoDecoder_GetInputInfos (dec->ctx, &dec->in_buffer_size,
                                  &dec->in_align, &dec->lookahead);

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

  return GST_PAD_LINK_OK;
}

#define ALIGN_2(x) ((x+1)&~1)

static void
dmo_videodec_chain (GstPad * pad, GstData * data)
{
  DMOVideoDec *dec = (DMOVideoDec *) gst_pad_get_parent (pad);
  GstBuffer *in_buffer = NULL;
  guint read = 0, wrote = 0, status = FALSE;

  Check_FS_Segment ();

  in_buffer = GST_BUFFER (data);
  
  /* encode */
  status = DMO_VideoDecoder_ProcessInput (dec->ctx,
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
    while ((status = DMO_VideoDecoder_ProcessOutput (dec->ctx,
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
dmo_videodec_change_state (GstElement * element)
{
  DMOVideoDec *dec = (DMOVideoDec *) element;

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
        DMO_VideoDecoder_Destroy (dec->ctx);
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
  { "wmv9dmod", { 0x724bb6a4, 0xe526, 0x450f,
		  0xaf, 0xfa, 0xab, 0x9b, 0x45, 0x12, 0x91, 0x11 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '3'), 3, "Windows Media Video",
    "video/x-wmv, wmvversion=(int)3" },
  { "wmvdmod", { 0x82d353df, 0x90bd, 0x4382,
		 0x8b, 0xc2, 0x3f, 0x61, 0x92, 0xb7, 0x6e, 0x34 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '1'), 1, "Windows Media Video",
    "video/x-wmv, wmvversion = (int) 1" },
  { "wmvdmod", { 0x82d353df, 0x90bd, 0x4382,
		 0x8b, 0xc2, 0x3f, 0x61, 0x92, 0xb7, 0x6e, 0x34 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '2'), 2, "Windows Media Video",
    "video/x-wmv, wmvversion = (int) 2" },
  { "wmvdmod", { 0x82d353df, 0x90bd, 0x4382,
		 0x8b, 0xc2, 0x3f, 0x61, 0x92, 0xb7, 0x6e, 0x34 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '3'), 3, "Windows Media Video",
    "video/x-wmv, wmvversion = (int) 3" },
  { NULL }
};

gboolean
dmo_vdec_register (GstPlugin * plugin)
{
  GTypeInfo info = {
    sizeof (DMOVideoDecClass),
    (GBaseInitFunc) dmo_videodec_base_init,
    NULL,
    (GClassInitFunc) dmo_videodec_class_init,
    NULL,
    NULL,
    sizeof (DMOVideoDec),
    0,
    (GInstanceInitFunc) dmo_videodec_init,
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
