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

#include <gst/gst.h>

#include "pitfdll.h"

GST_DEBUG_CATEGORY (pitfdll_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (pitfdll_debug, "pitfdll", 0, "PitfDLL elements");

  gst_plugin_add_dependency_simple (plugin, NULL, WIN32_PATH, NULL, GST_PLUGIN_DEPENDENCY_FLAG_NONE);
  dshow_vdec_register (plugin);
  dshow_adec_register (plugin);

  dmo_vdec_register (plugin);
  dmo_venc_register (plugin);
  dmo_adec_register (plugin);
  dmo_aenc_register (plugin);

  qt_adec_register (plugin);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "pitfdll",
    "DLL-loader elements",
    plugin_init,
    PACKAGE_VERSION, "GPL", "PitfDLL", "http://ronald.bitfreak.net/pitfdll/")
