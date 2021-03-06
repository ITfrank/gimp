/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimpdrawable-stroke.c
 * Copyright (C) 2003 Simon Budig  <simon@gimp.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <cairo.h>
#include <gegl.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpmath/gimpmath.h"
#include "libgimpcolor/gimpcolor.h"

#include "core-types.h"

#include "gegl/gimp-gegl-apply-operation.h"
#include "gegl/gimp-gegl-utils.h"

#include "gimp.h"
#include "gimpbezierdesc.h"
#include "gimpboundary.h"
#include "gimpchannel.h"
#include "gimpcontext.h"
#include "gimpdrawable-stroke.h"
#include "gimperror.h"
#include "gimpimage.h"
#include "gimppattern.h"
#include "gimpscanconvert.h"
#include "gimpstrokeoptions.h"

#include "vectors/gimpstroke.h"
#include "vectors/gimpvectors.h"

#include "gimp-intl.h"


/*  local function prototypes  */

static GimpScanConvert * gimp_drawable_render_boundary     (GimpDrawable        *drawable,
                                                            const GimpBoundSeg  *bound_segs,
                                                            gint                 n_bound_segs,
                                                            gint                 offset_x,
                                                            gint                 offset_y);
static GimpScanConvert * gimp_drawable_render_vectors      (GimpDrawable        *drawable,
                                                            GimpVectors         *vectors,
                                                            gboolean             do_stroke,
                                                            GError             **error);
static void              gimp_drawable_stroke_scan_convert (GimpDrawable        *drawable,
                                                            GimpFillOptions     *options,
                                                            GimpScanConvert     *scan_convert,
                                                            gboolean             do_stroke,
                                                            gboolean             push_undo);


/*  public functions  */

void
gimp_drawable_fill_boundary (GimpDrawable       *drawable,
                             GimpFillOptions    *options,
                             const GimpBoundSeg *bound_segs,
                             gint                n_bound_segs,
                             gint                offset_x,
                             gint                offset_y,
                             gboolean            push_undo)
{
  GimpScanConvert *scan_convert;

  g_return_if_fail (GIMP_IS_DRAWABLE (drawable));
  g_return_if_fail (gimp_item_is_attached (GIMP_ITEM (drawable)));
  g_return_if_fail (GIMP_IS_STROKE_OPTIONS (options));
  g_return_if_fail (bound_segs == NULL || n_bound_segs != 0);
  g_return_if_fail (gimp_fill_options_get_style (options) !=
                    GIMP_FILL_STYLE_PATTERN ||
                    gimp_context_get_pattern (GIMP_CONTEXT (options)) != NULL);

  scan_convert = gimp_drawable_render_boundary (drawable,
                                                bound_segs, n_bound_segs,
                                                offset_x, offset_y);

  if (scan_convert)
    {
      gimp_drawable_stroke_scan_convert (drawable, options,
                                         scan_convert, FALSE, push_undo);
      gimp_scan_convert_free (scan_convert);
    }
}

void
gimp_drawable_stroke_boundary (GimpDrawable       *drawable,
                               GimpStrokeOptions  *options,
                               const GimpBoundSeg *bound_segs,
                               gint                n_bound_segs,
                               gint                offset_x,
                               gint                offset_y,
                               gboolean            push_undo)
{
  GimpScanConvert *scan_convert;

  g_return_if_fail (GIMP_IS_DRAWABLE (drawable));
  g_return_if_fail (gimp_item_is_attached (GIMP_ITEM (drawable)));
  g_return_if_fail (GIMP_IS_STROKE_OPTIONS (options));
  g_return_if_fail (bound_segs == NULL || n_bound_segs != 0);
  g_return_if_fail (gimp_fill_options_get_style (GIMP_FILL_OPTIONS (options)) !=
                    GIMP_FILL_STYLE_PATTERN ||
                    gimp_context_get_pattern (GIMP_CONTEXT (options)) != NULL);

  scan_convert = gimp_drawable_render_boundary (drawable,
                                                bound_segs, n_bound_segs,
                                                offset_x, offset_y);

  if (scan_convert)
    {
      gimp_drawable_stroke_scan_convert (drawable, GIMP_FILL_OPTIONS (options),
                                         scan_convert, TRUE, push_undo);
      gimp_scan_convert_free (scan_convert);
    }
}

gboolean
gimp_drawable_fill_vectors (GimpDrawable     *drawable,
                            GimpFillOptions  *options,
                            GimpVectors      *vectors,
                            gboolean          push_undo,
                            GError          **error)
{
  GimpScanConvert *scan_convert;

  g_return_val_if_fail (GIMP_IS_DRAWABLE (drawable), FALSE);
  g_return_val_if_fail (gimp_item_is_attached (GIMP_ITEM (drawable)), FALSE);
  g_return_val_if_fail (GIMP_IS_FILL_OPTIONS (options), FALSE);
  g_return_val_if_fail (GIMP_IS_VECTORS (vectors), FALSE);
  g_return_val_if_fail (gimp_fill_options_get_style (options) !=
                        GIMP_FILL_STYLE_PATTERN ||
                        gimp_context_get_pattern (GIMP_CONTEXT (options)) != NULL,
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  scan_convert = gimp_drawable_render_vectors (drawable, vectors, FALSE, error);

  if (scan_convert)
    {
      gimp_drawable_stroke_scan_convert (drawable, options,
                                         scan_convert, FALSE, push_undo);
      gimp_scan_convert_free (scan_convert);

      return TRUE;
    }

  return FALSE;
}

gboolean
gimp_drawable_stroke_vectors (GimpDrawable       *drawable,
                              GimpStrokeOptions  *options,
                              GimpVectors        *vectors,
                              gboolean            push_undo,
                              GError            **error)
{
  GimpScanConvert *scan_convert;

  g_return_val_if_fail (GIMP_IS_DRAWABLE (drawable), FALSE);
  g_return_val_if_fail (gimp_item_is_attached (GIMP_ITEM (drawable)), FALSE);
  g_return_val_if_fail (GIMP_IS_STROKE_OPTIONS (options), FALSE);
  g_return_val_if_fail (GIMP_IS_VECTORS (vectors), FALSE);
  g_return_val_if_fail (gimp_fill_options_get_style (GIMP_FILL_OPTIONS (options)) !=
                        GIMP_FILL_STYLE_PATTERN ||
                        gimp_context_get_pattern (GIMP_CONTEXT (options)) != NULL,
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  scan_convert = gimp_drawable_render_vectors (drawable, vectors, TRUE, error);

  if (scan_convert)
    {
      gimp_drawable_stroke_scan_convert (drawable, GIMP_FILL_OPTIONS (options),
                                         scan_convert, TRUE, push_undo);
      gimp_scan_convert_free (scan_convert);

      return TRUE;
    }

  return FALSE;
}


/*  private functions  */

static GimpScanConvert *
gimp_drawable_render_boundary (GimpDrawable       *drawable,
                               const GimpBoundSeg *bound_segs,
                               gint                n_bound_segs,
                               gint                offset_x,
                               gint                offset_y)
{
  if (bound_segs)
    {
      GimpBoundSeg *stroke_segs;
      gint          n_stroke_segs;

      stroke_segs = gimp_boundary_sort (bound_segs, n_bound_segs,
                                        &n_stroke_segs);

      if (stroke_segs)
        {
          GimpBezierDesc *bezier;

          bezier = gimp_bezier_desc_new_from_bound_segs (stroke_segs,
                                                         n_bound_segs,
                                                         n_stroke_segs);

          g_free (stroke_segs);

          if (bezier)
            {
              GimpScanConvert *scan_convert;

              scan_convert = gimp_scan_convert_new ();

              gimp_bezier_desc_translate (bezier, offset_x, offset_y);
              gimp_scan_convert_add_bezier (scan_convert, bezier);

              gimp_bezier_desc_free (bezier);

              return scan_convert;
            }
        }
    }

  return NULL;
}

static GimpScanConvert *
gimp_drawable_render_vectors (GimpDrawable  *drawable,
                              GimpVectors   *vectors,
                              gboolean       do_stroke,
                              GError       **error)
{
  const GimpBezierDesc *bezier;

  bezier = gimp_vectors_get_bezier (vectors);

  if (bezier && (do_stroke ? bezier->num_data >= 2 : bezier->num_data > 4))
    {
      GimpScanConvert *scan_convert;

      scan_convert = gimp_scan_convert_new ();
      gimp_scan_convert_add_bezier (scan_convert, bezier);

      return scan_convert;
    }

  g_set_error_literal (error, GIMP_ERROR, GIMP_FAILED,
                       do_stroke ?
                       _("Not enough points to stroke") :
                       _("Not enough points to fill"));

  return NULL;
}

static void
gimp_drawable_stroke_scan_convert (GimpDrawable    *drawable,
                                   GimpFillOptions *options,
                                   GimpScanConvert *scan_convert,
                                   gboolean         do_stroke,
                                   gboolean         push_undo)
{
  GimpContext *context = GIMP_CONTEXT (options);
  GimpImage   *image   = gimp_item_get_image (GIMP_ITEM (drawable));
  GeglBuffer  *base_buffer;
  GeglBuffer  *mask_buffer;
  gint         x, y, w, h;
  gint         off_x;
  gint         off_y;

  /*  must call gimp_channel_is_empty() instead of relying on
   *  gimp_item_mask_intersect() because the selection pretends to
   *  be empty while it is being stroked, to prevent masking itself.
   */
  if (gimp_channel_is_empty (gimp_image_get_mask (image)))
    {
      x = 0;
      y = 0;
      w = gimp_item_get_width  (GIMP_ITEM (drawable));
      h = gimp_item_get_height (GIMP_ITEM (drawable));
    }
  else if (! gimp_item_mask_intersect (GIMP_ITEM (drawable), &x, &y, &w, &h))
    {
      return;
    }

  if (do_stroke)
    {
      GimpStrokeOptions *stroke_options = GIMP_STROKE_OPTIONS (options);
      gdouble            width;
      GimpUnit           unit;

      width = gimp_stroke_options_get_width (stroke_options);
      unit  = gimp_stroke_options_get_unit (stroke_options);

      if (unit != GIMP_UNIT_PIXEL)
        {
          gdouble xres;
          gdouble yres;

          gimp_image_get_resolution (image, &xres, &yres);

          gimp_scan_convert_set_pixel_ratio (scan_convert, yres / xres);

          width = gimp_units_to_pixels (width, unit, yres);
        }

      gimp_scan_convert_stroke (scan_convert, width,
                                gimp_stroke_options_get_join_style (stroke_options),
                                gimp_stroke_options_get_cap_style (stroke_options),
                                gimp_stroke_options_get_miter_limit (stroke_options),
                                gimp_stroke_options_get_dash_offset (stroke_options),
                                gimp_stroke_options_get_dash_info (stroke_options));
    }

  /* fill a 1-bpp GeglBuffer with black, this will describe the shape
   * of the stroke.
   */
  mask_buffer = gegl_buffer_new (GEGL_RECTANGLE (0, 0, w, h),
                                 babl_format ("Y u8"));

  /* render the stroke into it */
  gimp_item_get_offset (GIMP_ITEM (drawable), &off_x, &off_y);

  gimp_scan_convert_render (scan_convert, mask_buffer,
                            x + off_x, y + off_y,
                            gimp_fill_options_get_antialias (options));

  base_buffer = gegl_buffer_new (GEGL_RECTANGLE (0, 0, w, h),
                                 gimp_drawable_get_format_with_alpha (drawable));

  switch (gimp_fill_options_get_style (options))
    {
    case GIMP_FILL_STYLE_SOLID:
      {
        GimpRGB    fg;
        GeglColor *color;

        gimp_context_get_foreground (context, &fg);

        color = gimp_gegl_color_new (&fg);
        gegl_buffer_set_color (base_buffer, NULL, color);
        g_object_unref (color);
      }
      break;

    case GIMP_FILL_STYLE_PATTERN:
      {
        GimpPattern *pattern = gimp_context_get_pattern (context);
        GeglBuffer  *pattern_buffer;

        pattern_buffer = gimp_pattern_create_buffer (pattern);
        gegl_buffer_set_pattern (base_buffer, NULL, pattern_buffer, 0, 0);
        g_object_unref (pattern_buffer);
      }
      break;
    }

  gimp_gegl_apply_opacity (base_buffer, NULL, NULL, base_buffer,
                           mask_buffer, 0, 0, 1.0);
  g_object_unref (mask_buffer);

  /* Apply to drawable */
  gimp_drawable_apply_buffer (drawable, base_buffer,
                              GEGL_RECTANGLE (0, 0, w, h),
                              push_undo, C_("undo-type", "Render Stroke"),
                              gimp_context_get_opacity (context),
                              gimp_context_get_paint_mode (context),
                              NULL, x, y);

  g_object_unref (base_buffer);

  gimp_drawable_update (drawable, x, y, w, h);
}
