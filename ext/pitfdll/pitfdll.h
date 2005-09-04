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

#ifndef __PITFDLL_H__
#define __PITFDLL_H__

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN (pitfdll_debug);
#define GST_CAT_DEFAULT pitfdll_debug

G_BEGIN_DECLS

extern gboolean dshow_vdec_register (GstPlugin * plugin);
extern gboolean dshow_adec_register (GstPlugin * plugin);

extern gboolean dmo_vdec_register (GstPlugin * plugin);
extern gboolean dmo_venc_register (GstPlugin * plugin);
extern gboolean dmo_adec_register (GstPlugin * plugin);
extern gboolean dmo_aenc_register (GstPlugin * plugin);

extern gboolean qt_adec_register (GstPlugin * plugin);

G_END_DECLS

#endif /* __PITFDLL_H__ */
