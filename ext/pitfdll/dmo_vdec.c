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
  gint fps_n, fps_d;
  
  void *ctx;
  gulong out_buffer_size;
  gulong in_buffer_size;
  gulong lookahead;
  gulong out_align;
  gulong in_align;
  ldt_fs_t *ldt_fs;

  GstSegment segment;
  gboolean waiting_for_key;
  gdouble proportion;
  GstClockTime earliest_time;
} DMOVideoDec;

typedef struct _DMOVideoDecClass {
  GstElementClass parent;

  const CodecEntry *entry;
} DMOVideoDecClass;

static void dmo_videodec_dispose (GObject * obj);
static gboolean dmo_videodec_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean dmo_videodec_sink_event (GstPad * pad, GstEvent * event);
static GstPadLinkReturn dmo_videodec_link (GstPad * pad, const GstCaps * caps);
static GstFlowReturn dmo_videodec_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn dmo_videodec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean dmo_videodec_src_event (GstPad * pad, GstEvent * event);

static void dmo_videodec_reset_qos (DMOVideoDec * dmo_videodec);
static void dmo_videodec_read_qos (DMOVideoDec * dmo_videodec, gdouble * proportion, GstClockTime * time);
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
  const CodecEntry *tmp;

  /* element details */
  tmp = klass->entry = (CodecEntry *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
                                           PITFDLL_CODEC_QDATA);
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
    srccaps = gst_caps_from_string ("video/x-raw-yuv, format=(fourcc)YUY2");
  }
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
  gst_pad_set_setcaps_function (dec->sinkpad, dmo_videodec_sink_setcaps);
  gst_pad_set_event_function (dec->sinkpad, dmo_videodec_sink_event);
  gst_pad_set_chain_function (dec->sinkpad, dmo_videodec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
  
  dec->srcpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (eklass, "src"), "src");
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_pad_set_event_function (dec->srcpad, dmo_videodec_src_event);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->ctx = NULL;
  dec->out_buffer = NULL;

  gst_segment_init (&dec->segment, GST_FORMAT_TIME);
}

static void
dmo_videodec_dispose (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

/*
 * processing.
 */

static gboolean
dmo_videodec_sink_setcaps (GstPad * pad, GstCaps * caps)
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
  const GValue *fps;
  gboolean ret = FALSE;
  GUID tmp_guid = klass->entry->guid;
  guint32 fourcc;
  
  GST_DEBUG_OBJECT (dec, "setcaps called with %" GST_PTR_FORMAT, caps);

  Check_FS_Segment ();

  if (dec->ctx) {
    DMO_VideoDecoder_Destroy (dec->ctx);
    dec->ctx = NULL;
  }

  /* read data */
  if (!gst_structure_get_int (s, "width", &dec->w) ||
      !gst_structure_get_int (s, "height", &dec->h)) {
    goto beach;
  }
  
  fps = gst_structure_get_value (s, "framerate");
  if (!fps) {
    goto beach;
  }
  
  dec->fps_n = gst_value_get_fraction_numerator (fps);
  dec->fps_d = gst_value_get_fraction_denominator (fps);
  
  if ((v = gst_structure_get_value (s, "codec_data")))
    extradata = gst_value_get_buffer (v);

  if (!gst_structure_get_fourcc (s, "format", &fourcc))
    fourcc = klass->entry->format;

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
  hdr->compression = fourcc;
  GST_DEBUG ("Will now open %s using %dx%d@%d/%dfps",
	     dll, dec->w, dec->h, dec->fps_n, dec->fps_d);
  if (!(dec->ctx = DMO_VideoDecoder_Open (dll, &tmp_guid, hdr))) {
    GST_ERROR ("Failed to open DLL %s", dll);
    g_free (dll);
    g_free (hdr);
    goto beach;
  }
  g_free (dll);
  g_free (hdr);
  
  DMO_VideoDecoder_GetOutputInfos (dec->ctx, &dec->out_buffer_size,
                                   &dec->out_align);
  DMO_VideoDecoder_GetInputInfos (dec->ctx, &dec->in_buffer_size,
                                  &dec->in_align, &dec->lookahead);

  /* negotiate output */
  out = gst_caps_from_string (klass->entry->srccaps);
  gst_caps_set_simple (out,
      "width", G_TYPE_INT, dec->w,
      "height", G_TYPE_INT, dec->h,
      "framerate", GST_TYPE_FRACTION, dec->fps_n, dec->fps_d, NULL);
  if (!gst_pad_set_caps (dec->srcpad, out)) {
    gst_caps_unref (out);
    GST_ERROR ("Failed to negotiate output");
    goto beach;
  }
  gst_caps_unref (out);
  
  dmo_videodec_reset_qos (dec);

  ret = TRUE;
  
beach:
  gst_object_unref (dec);
  
  return ret;
}

static gboolean
dmo_videodec_do_qos (DMOVideoDec *dec, GstClockTime timestamp)
{
  GstClockTimeDiff diff;
  gdouble proportion;
  GstClockTime qostime, earliest_time;

  /* no timestamp, can't do QoS */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp)))
    goto no_qos;

  /* get latest QoS observation values */
  dmo_videodec_read_qos (dec, &proportion, &earliest_time);

  /* skip qos if we have no observation (yet) */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (earliest_time))) {
    goto no_qos;
  }

  /* qos is done on running time of the timestamp */
  qostime = gst_segment_to_running_time (&dec->segment, GST_FORMAT_TIME,
      timestamp);

  /* timestamp can be out of segment, then we don't do QoS */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (qostime)))
    goto no_qos;

  /* see how our next timestamp relates to the latest qos timestamp. negative
   * values mean we are early, positive values mean we are too late. */
  diff = GST_CLOCK_DIFF (qostime, earliest_time);

  GST_DEBUG_OBJECT (dec, "QOS: qostime %" GST_TIME_FORMAT
      ", earliest %" GST_TIME_FORMAT, GST_TIME_ARGS (qostime),
      GST_TIME_ARGS (earliest_time));

  /* if we using less than 40% of the available time, we can try to
   * speed up again when we were slow. */
  if (proportion < 0.4 && diff < 0) {
    goto no_qos;
  } else {
    /* if we're more than two seconds late, switch to the next keyframe */
    /* FIXME, let the demuxer decide what's the best since we might be dropping
     * a lot of frames when the keyframe is far away or we even might not get a new
     * keyframe at all.. */
    if (diff > ((GstClockTimeDiff) GST_SECOND * 2)
        && !dec->waiting_for_key) {
      goto skip_to_keyframe;
    } else if (diff >= GST_SECOND/30) {
      goto skipping;
    }
  }

no_qos:
  return TRUE;

skipping:
  GST_DEBUG_OBJECT (dec, "QOS: Skipping frame. diff %" G_GINT64_FORMAT, diff);
  return FALSE;
skip_to_keyframe:
  {
    // ->waiting_for_key = TRUE;
    GST_DEBUG_OBJECT (dec,
        "QOS: keyframe, %" G_GINT64_FORMAT " > GST_SECOND/2", diff);
    /* we can skip the current frame */
    return FALSE;
  }
}

static GstFlowReturn
dmo_videodec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  DMOVideoDec *dec = (DMOVideoDec *) gst_pad_get_parent (pad);
  GstBuffer *in_buffer = NULL;
  guint read = 0, wrote = 0, status;
  gboolean is_keyframe;

  Check_FS_Segment ();

  /* We are not ready yet ! */
  if (!dec->ctx) {
    ret = GST_FLOW_WRONG_STATE;
    goto beach;
  }
  
  is_keyframe = !GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  /* pass encoded data to decoder */
  status = DMO_VideoDecoder_ProcessInput (dec->ctx,
                                          is_keyframe,
                                          GST_BUFFER_TIMESTAMP (buffer),
                                          GST_BUFFER_DURATION (buffer),
                                          GST_BUFFER_DATA (buffer),
                                          GST_BUFFER_SIZE (buffer),
                                          &read);
  
  GST_DEBUG ("read %d out of %d, result %d time %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT,
      read, GST_BUFFER_SIZE (buffer), status,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)));
  
  if (!dec->out_buffer) {
    GST_DEBUG ("allocating a buffer of %d bytes from pad %" GST_PTR_FORMAT,
                 dec->out_buffer_size, dec->srcpad);
    ret = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE,
                                dec->out_buffer_size, GST_PAD_CAPS (dec->srcpad),
                                &(dec->out_buffer));
    if (ret != GST_FLOW_OK) {
      GST_DEBUG ("failed allocating a buffer of %d bytes from pad %" GST_PTR_FORMAT,
                 dec->out_buffer_size, dec->srcpad);
      goto beach;
    }
    /* If the DMO can not set the timestamp we do our best guess */
    GST_BUFFER_TIMESTAMP (dec->out_buffer) = GST_BUFFER_TIMESTAMP (buffer);
  }

  /* If the DMO can not set the duration we do our best guess */
  GST_BUFFER_DURATION (dec->out_buffer) += GST_BUFFER_DURATION (buffer);
  
  if (status == FALSE) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);
    GstClockTime duration = GST_BUFFER_DURATION (buffer);
    GST_DEBUG ("we have some output buffers to collect (size is %d)",
               GST_BUFFER_SIZE (dec->out_buffer));
    /* Loop until the last buffer (returns FALSE) */
    do {
      if (dmo_videodec_do_qos (dec, timestamp)) {
        status = DMO_VideoDecoder_ProcessOutput (dec->ctx,
                             GST_BUFFER_DATA (dec->out_buffer),
                             GST_BUFFER_SIZE (dec->out_buffer),
                             &wrote,
                             &(GST_BUFFER_TIMESTAMP (dec->out_buffer)),
                             &(GST_BUFFER_DURATION (dec->out_buffer)));

        if (status) {
          GST_DEBUG ("there is another output buffer to collect, pushing %d bytes timestamp %llu duration %llu",
                     wrote, GST_BUFFER_TIMESTAMP (dec->out_buffer), GST_BUFFER_DURATION (dec->out_buffer));
        }
        GST_BUFFER_SIZE (dec->out_buffer) = wrote;
        GST_DEBUG ("pushing %d bytes timestamp %llu duration %llu", wrote,
                   GST_BUFFER_TIMESTAMP (dec->out_buffer),
                   GST_BUFFER_DURATION (dec->out_buffer));
        ret = gst_pad_push (dec->srcpad, dec->out_buffer);
        dec->out_buffer = NULL;
  
        if (status) {
          dec->out_buffer = gst_buffer_new_and_alloc (dec->out_buffer_size);
          /* If the DMO can not set the timestamp we do our best guess */
          GST_BUFFER_TIMESTAMP (dec->out_buffer) = timestamp;
          GST_BUFFER_DURATION (dec->out_buffer) = 0;
        }
      }
      else {
        /* Skip the frame */
        status = DMO_VideoDecoder_ProcessOutput (dec->ctx, NULL, 0, NULL, &timestamp, &duration);
        GST_DEBUG_OBJECT (dec, "skipped frame with ts %" GST_TIME_FORMAT " dur %" GST_TIME_FORMAT,
            GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration));
        if (GST_CLOCK_TIME_IS_VALID (timestamp) && GST_CLOCK_TIME_IS_VALID (duration))
          timestamp += duration;
      }
    } while (status);
  }
  
beach:
  gst_buffer_unref (buffer);
  gst_object_unref (dec);
  
  return ret;
}

static GstStateChangeReturn
dmo_videodec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  DMOVideoDec *dec = (DMOVideoDec *) element;

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
        GST_DEBUG_OBJECT (dec, "cleaning up DMO decoder");
        Check_FS_Segment ();
        DMO_VideoDecoder_Destroy (dec->ctx);
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
dmo_videodec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  DMOVideoDec *dec;

  dec = (DMOVideoDec *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      DMO_VideoDecoder_Flush (dec->ctx);
      dmo_videodec_reset_qos (dec);
      gst_segment_init (&dec->segment, GST_FORMAT_TIME);
      res = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 start, stop, time;
      gdouble rate, arate;
      GstFormat format;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
                                  &start, &stop, &time);

      if (format != GST_FORMAT_TIME) {
        goto wrong_format;
      }

      GST_DEBUG_OBJECT (dec,
          "NEWSEGMENT in time start %" GST_TIME_FORMAT " -- stop %"
          GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

      /* and store the values */
      gst_segment_set_newsegment_full (&dec->segment, update,
          rate, arate, format, start, stop, time);

      res = gst_pad_event_default (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (dec);
  return res;

wrong_format:
  GST_ELEMENT_ERROR (dec, CORE, NEGOTIATION, (NULL), ("Input segment is not in TIME format"));

  gst_object_unref (dec);
  gst_event_unref (event);
  return FALSE;
}

static void
dmo_videodec_update_qos (DMOVideoDec * dmo_videodec, gdouble proportion,
    GstClockTime time)
{
  GST_OBJECT_LOCK (dmo_videodec);
  dmo_videodec->proportion = proportion;
  dmo_videodec->earliest_time = time;
  GST_OBJECT_UNLOCK (dmo_videodec);
}

static void
dmo_videodec_reset_qos (DMOVideoDec * dmo_videodec)
{
  dmo_videodec_update_qos (dmo_videodec, 0.5, GST_CLOCK_TIME_NONE);
}

static void
dmo_videodec_read_qos (DMOVideoDec * dmo_videodec, gdouble * proportion,
    GstClockTime * time)
{
  GST_OBJECT_LOCK (dmo_videodec);
  *proportion = dmo_videodec->proportion;
  *time = dmo_videodec->earliest_time;
  GST_OBJECT_UNLOCK (dmo_videodec);
}

static gboolean
dmo_videodec_src_event(GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  DMOVideoDec *dec = (DMOVideoDec *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      /* update our QoS values */
      dmo_videodec_update_qos (dec, proportion, timestamp + diff);

      /* forward upstream */
      res = gst_pad_push_event (dec->sinkpad, event);
      break;
    }
    default:
      /* forward upstream */
      res = gst_pad_push_event (dec->sinkpad, event);
      break;
  }

  gst_object_unref (dec);

  return res;
}

/*
 * Register codecs.
 */

static const CodecEntry codecs[] = {
  { "wmv9dmod", { 0x724bb6a4, 0xe526, 0x450f,
		  0xaf, 0xfa, 0xab, 0x9b, 0x45, 0x12, 0x91, 0x11 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '3'), 3, "Windows Media Video",
    "video/x-wmv, wmvversion=(int)3",
    "video/x-raw-yuv, format= (fourcc) YUY2" },
  { "wmvdmod", { 0x82d353df, 0x90bd, 0x4382,
		 0x8b, 0xc2, 0x3f, 0x61, 0x92, 0xb7, 0x6e, 0x34 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '1'), 1, "Windows Media Video",
    "video/x-wmv, wmvversion = (int) 1",
    "video/x-raw-yuv, format= (fourcc) YUY2" },
  { "wmvdmod", { 0x82d353df, 0x90bd, 0x4382,
		 0x8b, 0xc2, 0x3f, 0x61, 0x92, 0xb7, 0x6e, 0x34 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '2'), 2, "Windows Media Video",
    "video/x-wmv, wmvversion = (int) 2",
    "video/x-raw-yuv, format= (fourcc) YUY2" },
  { "wmvdmod", { 0x82d353df, 0x90bd, 0x4382,
		 0x8b, 0xc2, 0x3f, 0x61, 0x92, 0xb7, 0x6e, 0x34 },
    GST_MAKE_FOURCC ('W', 'M', 'V', '3'), 3, "Windows Media Video",
    "video/x-wmv, wmvversion = (int) 3, format = (fourcc) WMV3",
    "video/x-raw-yuv, format= (fourcc) YUY2" },
  { "wvc1dmod", { 0xc9bfbccf, 0xe60e, 0x4588,
                 0xa3, 0xdf, 0x5a, 0x03, 0xb1, 0xfd, 0x95, 0x85 },
    GST_MAKE_FOURCC ('W', 'V', 'C', '1'), 3, "Windows Media Video",
    "video/x-wmv, wmvversion = (int) 3, format = (fourcc) WVC1",
    "video/x-raw-yuv, format= (fourcc) YUY2" },
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

  return pitfdll_register_codecs (plugin, "dmodec_%sv%d", codecs, &info);
}
