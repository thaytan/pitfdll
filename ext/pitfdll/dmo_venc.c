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
#include "DMO_VideoEncoder.h"
#include "ldt_keeper.h"

typedef struct _DMOVideoEnc {
  GstElement parent;

  GstPad *srcpad, *sinkpad;
  
  GstBuffer *out_buffer;

  /* settings */
  gint w, h;
  gboolean vbr;
  gint quality;
  gint bitrate;
  gdouble fps;
  
  void *ctx;
  gulong out_buffer_size;
  gulong out_align;
  ldt_fs_t *ldt_fs;
} DMOVideoEnc;

typedef struct _DMOVideoEncClass {
  GstElementClass parent;

  const CodecEntry *entry;
} DMOVideoEncClass;

static void dmo_videoenc_dispose (GObject * obj);
static GstFlowReturn dmo_videoenc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean dmo_videoenc_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean dmo_videoenc_sink_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn dmo_videoenc_change_state (GstElement * element,
    GstStateChange transition);
static void dmo_videoenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void dmo_videoenc_set_property (GObject * object, guint prop_id,
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
dmo_videoenc_base_init (DMOVideoEncClass * klass)
{
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;
  GstPadTemplate *src, *snk;
  GstCaps *srccaps, *sinkcaps;

  /* element details */
  klass->entry = tmp;
  details.longname = g_strdup_printf ("DMO %s encoder version %d", tmp->dll,
                                      tmp->version);
  details.klass = "Codec/Encoder/Video";
  details.description = g_strdup_printf ("DMO %s encoder version %d",
                                         tmp->friendly_name, tmp->version);
  details.author = "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (eklass, &details);
  g_free (details.description);
  g_free (details.longname);

  if (tmp->sinkcaps) {
    sinkcaps = gst_caps_from_string (tmp->sinkcaps);
  }
  else {
    sinkcaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
  }
  snk = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);
  
  /* src caps */
  srccaps = gst_caps_from_string (tmp->srccaps);
  gst_caps_set_simple (srccaps,
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL);
  src = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  /* register */
  gst_element_class_add_pad_template (eklass, src);
  gst_element_class_add_pad_template (eklass, snk);
}

static void
dmo_videoenc_class_init (DMOVideoEncClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);

  if (!parent_class)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  eklass->change_state = dmo_videoenc_change_state;
  
  oklass->set_property = dmo_videoenc_set_property;
  oklass->get_property = dmo_videoenc_get_property;
  
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Video bitrate",
          "Defines the video bitrate the codec will try to reach.",
          0, 2048256, 128016, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_int ("quality", "Video quality",
          "Defines the video quality the codec will try to reach.",
          0, 100, 75, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR,
      g_param_spec_boolean ("vbr", "Variable BitRate",
          "Defines if the video encoder should use a variable bitrate.",
          FALSE, G_PARAM_READWRITE));
}

static void
dmo_videoenc_init (DMOVideoEnc * enc)
{
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (enc);

  /* setup pads */
  enc->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "sink"), "sink");
  gst_pad_set_event_function (enc->sinkpad, dmo_videoenc_sink_event);
  gst_pad_set_setcaps_function (enc->sinkpad, dmo_videoenc_sink_setcaps);
  gst_pad_set_chain_function (enc->sinkpad, dmo_videoenc_chain);
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
                                                                                
  enc->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->ctx = NULL;
  enc->out_buffer = NULL;
  
  enc->vbr = FALSE;
  enc->quality = 0;
  enc->bitrate = 241000;
  enc->out_buffer_size = 0;
  enc->out_align = 0;
}

static void
dmo_videoenc_get_property (GObject * object, guint prop_id, GValue * value,
                           GParamSpec * pspec)
{
  DMOVideoEnc * enc;
  
  enc = (DMOVideoEnc *) (object);

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
dmo_videoenc_set_property (GObject * object, guint prop_id,
                           const GValue * value, GParamSpec * pspec)
{
  DMOVideoEnc * enc;

  enc = (DMOVideoEnc *) (object);

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

/*
 * processing.
 */

static gboolean
dmo_videoenc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  DMOVideoEnc *enc = (DMOVideoEnc *) gst_pad_get_parent (pad);
  DMOVideoEncClass *klass = (DMOVideoEncClass *) G_OBJECT_GET_CLASS (enc);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  BITMAPINFOHEADER *hdr;
  gint size;
  GstCaps *out;
  const gchar *structure_name;
  char * data = NULL, * dll = NULL;
  unsigned long data_length = 0;
  GstBuffer * extradata = NULL;
  
  
  Check_FS_Segment ();

  if (enc->ctx) {
    DMO_VideoEncoder_Destroy (enc->ctx);
    enc->ctx = NULL;
  }

  /* read data */ 
  if (!gst_structure_get_int (s, "width", &enc->w) ||
      !gst_structure_get_int (s, "height", &enc->h) ||
      !gst_structure_get_double (s, "framerate", &enc->fps))
    return FALSE;

  /* set up dll initialization */
  dll = g_strdup_printf ("%s.dll", klass->entry->dll);
  size = sizeof (BITMAPINFOHEADER);
  hdr = g_malloc0 (size);
  hdr->width = enc->w;
  hdr->height = enc->h;
  hdr->size = sizeof (BITMAPINFOHEADER);
  
  structure_name = gst_structure_get_name (s);
  /* if (gst_structure_has_name (s, "video/x-raw-rgb")) {*/
  if (strcmp (structure_name, "video/x-raw-rgb") == 0) {
    gint bpp;
    if (!gst_structure_get_int (s, "bpp", &bpp))
      return GST_PAD_LINK_REFUSED;
    hdr->bit_cnt = bpp;
    GST_DEBUG ("Using RGB%d", hdr->bit_cnt);
    hdr->compression = 0;
  }
  /* else if (gst_structure_has_name (s, "video/x-raw-yuv")) {*/
  else if (strcmp (structure_name, "video/x-raw-yuv") == 0) {
    if (!gst_structure_get_fourcc (s, "format", (guint32 *) &hdr->compression))
      return GST_PAD_LINK_REFUSED;
    GST_DEBUG ("Using YUV with fourcc");
  }
  GST_DEBUG ("Will now open %s using %dx%d@%lffps",
	           dll, enc->w, enc->h, enc->fps);
  if (enc->vbr) {
    if (!(enc->ctx = DMO_VideoEncoder_Open (dll, &(klass->entry->guid), hdr, 
                                            klass->entry->format, enc->vbr,
                                            enc->quality, enc->fps,
                                            &data, &data_length))) {
      GST_ERROR ("Failed to open DLL %s", dll);
      g_free (dll);
      g_free (hdr);
      return GST_PAD_LINK_REFUSED;
    }
  }
  else {
    if (!(enc->ctx = DMO_VideoEncoder_Open (dll, &(klass->entry->guid), hdr, 
                                            klass->entry->format, enc->vbr,
                                            enc->bitrate, enc->fps,
                                            &data, &data_length))) {
      GST_ERROR ("Failed to open DLL %s", dll);
      g_free (dll);
      g_free (hdr);
      return GST_PAD_LINK_REFUSED;
    }
  }
  g_free (dll);

  DMO_VideoEncoder_GetOutputInfos (enc->ctx, &enc->out_buffer_size,
                                   &enc->out_align);
  
  /* negotiate output */
  out = gst_caps_from_string (klass->entry->srccaps);
  if (data_length) {
    extradata = gst_buffer_new_and_alloc (data_length);
    memcpy ((char *) GST_BUFFER_DATA (extradata), (char *) data, data_length);
    g_free (data);
    gst_caps_set_simple (out,
      "width", G_TYPE_INT, enc->w,
      "height", G_TYPE_INT, enc->h,
      "framerate", G_TYPE_DOUBLE, enc->fps,
      "bitrate", G_TYPE_INT, enc->bitrate,
      "bpp", G_TYPE_INT, hdr->bit_cnt,
      "codec_data", GST_TYPE_BUFFER, extradata, NULL);
  }
  else {
    gst_caps_set_simple (out,
      "width", G_TYPE_INT, enc->w,
      "height", G_TYPE_INT, enc->h,
      "bitrate", G_TYPE_INT, enc->bitrate,
      "bpp", G_TYPE_INT, hdr->bit_cnt,
      "framerate", G_TYPE_DOUBLE, enc->fps, NULL);
  }
  
  g_free (hdr);
  
  if (!gst_pad_set_caps (enc->srcpad, out)) {
    gst_caps_unref (out);
    GST_ERROR ("Failed to negotiate output");
    return FALSE;
  }
  gst_caps_unref (out);

  return TRUE;
}

static GstFlowReturn
dmo_videoenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  DMOVideoEnc *enc = (DMOVideoEnc *) gst_pad_get_parent (pad);
  GstBuffer *in_buffer = NULL;
  guint read = 0, wrote = 0, status = FALSE;

  Check_FS_Segment ();

  /* We are not ready yet ! */
  if (!enc->ctx) {
    gst_buffer_unref (buffer);
    GST_ELEMENT_ERROR (enc, CORE, NEGOTIATION, (NULL),
          ("encoder not initialized (input is not video?)"));
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }
  
  /* encode */
  status = DMO_VideoEncoder_ProcessInput (enc->ctx,
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
  
  gst_buffer_unref (buffer);
  
  if (status == FALSE) {
    gboolean key_frame = FALSE;
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (enc->out_buffer);
    GST_DEBUG ("we have some output buffers to collect (size is %d)",
               GST_BUFFER_SIZE (enc->out_buffer));
    /* Loop until the last buffer (returns FALSE) */
    while ((status = DMO_VideoEncoder_ProcessOutput (enc->ctx,
                           GST_BUFFER_DATA (enc->out_buffer),
                           GST_BUFFER_SIZE (enc->out_buffer),
                           &wrote,
                           &(GST_BUFFER_TIMESTAMP (enc->out_buffer)),
                           &(GST_BUFFER_DURATION (enc->out_buffer)),
                           &key_frame)) == TRUE) {
      if (wrote) {
        GST_DEBUG ("there is another output buffer to collect, pushing %d " \
                   "bytes timestamp %llu duration %llu", wrote,
                   GST_BUFFER_TIMESTAMP (enc->out_buffer),
                   GST_BUFFER_DURATION (enc->out_buffer));
        GST_BUFFER_SIZE (enc->out_buffer) = wrote;
        if (!key_frame) {
          GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
        }
        ret = gst_pad_push (enc->srcpad, enc->out_buffer);
      }
      else {
        gst_buffer_unref (enc->out_buffer);
      }
      enc->out_buffer = gst_buffer_new_and_alloc (enc->out_buffer_size);
      /* If the DMO can not set the timestamp we do our best guess */
      GST_BUFFER_TIMESTAMP (enc->out_buffer) = timestamp;
      GST_BUFFER_DURATION (enc->out_buffer) = 0;
    }
    if (wrote) {
      GST_DEBUG ("pushing %d bytes timestamp %llu duration %llu", wrote,
                 GST_BUFFER_TIMESTAMP (enc->out_buffer),
                 GST_BUFFER_DURATION (enc->out_buffer));
      GST_BUFFER_SIZE (enc->out_buffer) = wrote;
      ret = gst_pad_push (enc->srcpad, enc->out_buffer);
    }
    else {
      gst_buffer_unref (enc->out_buffer);
    }
    enc->out_buffer = NULL;
  }

beach:
  return ret;
}

static GstStateChangeReturn
dmo_videoenc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  DMOVideoEnc *enc = (DMOVideoEnc *) element;

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
        DMO_VideoEncoder_Destroy (enc->ctx);
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
dmo_videoenc_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  DMOVideoEnc *enc;

  enc = (DMOVideoEnc *) gst_pad_get_parent (pad);

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
  { "wmvdmoe2", { 0x96b57cdd, 0x8966, 0x410c,
		 0xbb, 0x1f, 0xc9, 0x7e, 0xea, 0x76, 0x5c, 0x04 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '1'), 1, "Windows Media Video",
    "video/x-raw-yuv",
    "video/x-wmv, wmvversion = (int) 1" },
  { "wmvdmoe2", { 0x96b57cdd, 0x8966, 0x410c,
		 0xbb, 0x1f, 0xc9, 0x7e, 0xea, 0x76, 0x5c, 0x04 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '2'), 2, "Windows Media Video",
    "video/x-raw-yuv",
    "video/x-wmv, wmvversion = (int) 2" },
  { "wmvdmoe2", { 0x96b57cdd, 0x8966, 0x410c,
		 0xbb, 0x1f, 0xc9, 0x7e, 0xea, 0x76, 0x5c, 0x04 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '3'), 3, "Windows Media Video",
    "video/x-raw-yuv",
    "video/x-wmv, wmvversion = (int) 3" },
  { NULL }
};

gboolean
dmo_venc_register (GstPlugin * plugin)
{
  GTypeInfo info = {
    sizeof (DMOVideoEncClass),
    (GBaseInitFunc) dmo_videoenc_base_init,
    NULL,
    (GClassInitFunc) dmo_videoenc_class_init,
    NULL,
    NULL,
    sizeof (DMOVideoEnc),
    0,
    (GInstanceInitFunc) dmo_videoenc_init,
  };
  gint n;

  for (n = 0; codecs[n].dll != NULL; n++) {
    gchar *full_path, *element_name;
    GType type;

    full_path = g_strdup_printf (WIN32_PATH "/%s.dll", codecs[n].dll);
    if (!g_file_test (full_path, G_FILE_TEST_EXISTS)) {
      GST_DEBUG ("DLL file %s was not found", full_path);
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
