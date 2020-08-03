/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup bgpencil
 */
#include <iostream>
#include <list>
#include <string>

#include "MEM_guardedalloc.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_main.h"
#include "BKE_material.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "ED_gpencil.h"

#ifdef WIN32
#  include "utfconv.h"
#endif

#include "UI_view2d.h"

#include "ED_view3d.h"

#include "gpencil_io_exporter.h"
#include "gpencil_io_svg.h"

#include "pugixml.hpp"

namespace blender ::io ::gpencil {

/* Constructor. */
GpencilExporterSVG::GpencilExporterSVG(const struct GpencilExportParams *iparams)
    : GpencilExporter(iparams)
{
  invert_axis_[0] = false;
  invert_axis_[1] = true;
}

/* Main write method for SVG format. */
bool GpencilExporterSVG::write(std::string actual_frame)
{
  create_document_header();

  export_layers();

  /* Add frame to filename. */
  std::string frame_file = out_filename_;
  size_t found = frame_file.find_last_of(".");
  if (found != std::string::npos) {
    frame_file.replace(found, 8, actual_frame + ".svg");
  }

  return doc.save_file(frame_file.c_str());
}

/* Create document header and main svg node. */
void GpencilExporterSVG::create_document_header(void)
{
  /* Add a custom document declaration node. */
  pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";

  pugi::xml_node comment = doc.append_child(pugi::node_comment);
  comment.set_value(SVG_EXPORTER_VERSION);

  pugi::xml_node doctype = doc.append_child(pugi::node_doctype);
  doctype.set_value(
      "svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
      "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\"");

  main_node = doc.append_child("svg");
  main_node.append_attribute("version").set_value("1.0");
  main_node.append_attribute("x").set_value("0px");
  main_node.append_attribute("y").set_value("0px");

  std::string width;
  std::string height;

  width = std::to_string(render_x_);
  height = std::to_string(render_y_);

  main_node.append_attribute("width").set_value((width + "px").c_str());
  main_node.append_attribute("height").set_value((height + "px").c_str());
  std::string viewbox = "0 0 " + width + " " + height;
  main_node.append_attribute("viewBox").set_value(viewbox.c_str());

  /* Camera clipping. */
  if (is_camera_mode() && ((params_.flag & GP_EXPORT_CLIP_CAMERA) != 0)) {
    pugi::xml_node clip_node = main_node.append_child("clipPath");
    clip_node.append_attribute("id").set_value("clip-path");
    create_rect(clip_node,
                0,
                0,
                (camera_rect_.xmax - camera_rect_.xmin) * camera_ratio_,
                (camera_rect_.xmax - camera_rect_.xmin) * camera_ratio_,
                0.0f,
                "#000000");
  }
}

/* Main layer loop. */
void GpencilExporterSVG::export_layers(void)
{
  for (ObjectZ &obz : ob_list_) {
    Object *ob = obz.ob;
    frame_node = main_node.append_child("g");
    std::string frametxt = " Frame_ " + std::to_string(params_.cfra);
    frame_node.append_attribute("id").set_value(frametxt.c_str());

    /* Test */
    rctf bb;
    get_select_boundbox(&bb);
    create_rect(frame_node,
                bb.xmin - offset_[0],
                bb.ymin - offset_[1],
                (bb.xmax - bb.xmin),
                (bb.ymax - bb.ymin),
                5.0f,
                "#FF0000");

    pugi::xml_node ob_node = frame_node.append_child("g");
    ob_node.append_attribute("id").set_value(ob->id.name + 2);

    /* Clip area. */
    if (is_camera_mode() && ((params_.flag & GP_EXPORT_CLIP_CAMERA) != 0)) {
      ob_node.append_attribute("clip-path").set_value("url(#clip-path)");
    }

    /* Use evaluated version to get strokes with modifiers. */
    Object *ob_eval_ = (Object *)DEG_get_evaluated_id(depsgraph, &ob->id);
    bGPdata *gpd_eval = (bGPdata *)ob_eval_->data;

    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd_eval->layers) {
      if (gpl->flag & GP_LAYER_HIDE) {
        continue;
      }
      gpl_current_set(gpl);

      /* Layer node. */
      std::string txt = "Layer: ";
      txt.append(gpl->info);
      main_node.append_child(pugi::node_comment).set_value(txt.c_str());
      pugi::xml_node gpl_node = ob_node.append_child("g");
      gpl_node.append_attribute("id").set_value(gpl->info);

      bGPDframe *gpf = gpl->actframe;
      if (gpf == NULL) {
        continue;
      }
      gpf_current_set(gpf);

      BKE_gpencil_parent_matrix_get(depsgraph, ob, gpl, diff_mat_);
      // if (is_bound_mode()) {
      //  diff_mat_[3][0] *= -1.0f;
      //}

      LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
        if (gps->totpoints == 0) {
          continue;
        }
        /* Duplicate the stroke to apply any layer thickness change. */
        bGPDstroke *gps_duplicate = BKE_gpencil_stroke_duplicate(gps, true);

        gps_current_set(ob, gps_duplicate, true);

        /* Apply layer thickness change. */
        gps_duplicate->thickness += gpl->line_change;
        CLAMP_MIN(gps_duplicate->thickness, 1.0f);

        if (gps_duplicate->totpoints == 1) {
          export_point(gpl_node);
        }
        else {
          bool is_normalized = ((params_.flag & GP_EXPORT_NORM_THICKNESS) != 0) ||
                               is_stroke_thickness_constant(gps);

          /* Fill. */
          if ((gp_style_is_fill()) && (params_.flag & GP_EXPORT_FILL)) {
            if (is_normalized) {
              export_stroke_polyline(gpl_node, true);
            }
            else {
              export_stroke_path(gpl_node, true);
            }
          }

          /* Stroke. */
          if (gp_style_is_stroke()) {
            if (is_normalized) {
              export_stroke_polyline(gpl_node, false);
            }
            else {
              bGPDstroke *gps_perimeter = BKE_gpencil_stroke_perimeter_from_view(
                  rv3d, gpd, gpl, gps_duplicate, 3, diff_mat_);

              gps_current_set(ob, gps_perimeter, false);

              /* Sample stroke. */
              if (params_.stroke_sample > 0.0f) {
                BKE_gpencil_stroke_sample(gps_perimeter, params_.stroke_sample, false);
              }

              export_stroke_path(gpl_node, false);

              BKE_gpencil_free_stroke(gps_perimeter);
            }
          }
        }

        BKE_gpencil_free_stroke(gps_duplicate);
      }
    }
  }
}

/**
 * Export a point
 * \param gpl_node: Node of the layer.
 * \param gps: Stroke to export.
 * \param diff_mat_: Transformation matrix.
 */
void GpencilExporterSVG::export_point(pugi::xml_node gpl_node)
{
  bGPDstroke *gps = gps_current_get();

  BLI_assert(gps->totpoints == 1);
  float screen_co[2];

  pugi::xml_node gps_node = gpl_node.append_child("circle");

  color_string_set(gps_node, false);

  bGPDspoint *pt = &gps->points[0];
  gpencil_3d_point_to_screen_space(&pt->x, screen_co);

  gps_node.append_attribute("cx").set_value(screen_co[0]);
  gps_node.append_attribute("cy").set_value(screen_co[1]);

  /* Radius. */
  float radius = stroke_point_radius_get(gps);
  gps_node.append_attribute("r").set_value(radius);
}

/**
 * Export a stroke using path
 * \param gpl_node: Node of the layer.
 * \param gps: Stroke to export.
 * \param diff_mat_: Transformation matrix.
 * \param is_fill: True if the stroke is only fill
 */
void GpencilExporterSVG::export_stroke_path(pugi::xml_node gpl_node, const bool is_fill)
{
  bGPDlayer *gpl = gpl_current_get();
  bGPDstroke *gps = gps_current_get();

  pugi::xml_node gps_node = gpl_node.append_child("path");

  float col[3];
  std::string stroke_hex;
  if (is_fill) {
    gps_node.append_attribute("fill-opacity").set_value(fill_color_[3] * gpl->opacity);

    interp_v3_v3v3(col, fill_color_, gpl->tintcolor, gpl->tintcolor[3]);
  }
  else {
    gps_node.append_attribute("fill-opacity")
        .set_value(stroke_color_[3] * stroke_average_opacity() * gpl->opacity);

    interp_v3_v3v3(col, stroke_color_, gpl->tintcolor, gpl->tintcolor[3]);
  }
  if ((params_.flag & GP_EXPORT_GRAY_SCALE) != 0) {
    rgb_to_grayscale(col);
  }

  linearrgb_to_srgb_v3_v3(col, col);
  stroke_hex = rgb_to_hex(col);

  gps_node.append_attribute("fill").set_value(stroke_hex.c_str());
  gps_node.append_attribute("stroke").set_value("none");

  std::string txt = "M";
  for (int i = 0; i < gps->totpoints; i++) {
    if (i > 0) {
      txt.append("L");
    }
    bGPDspoint *pt = &gps->points[i];
    float screen_co[2];
    gpencil_3d_point_to_screen_space(&pt->x, screen_co);
    txt.append(std::to_string(screen_co[0]) + "," + std::to_string(screen_co[1]));
  }
  /* Close patch (cyclic)*/
  if (gps->flag & GP_STROKE_CYCLIC) {
    txt.append("z");
  }

  gps_node.append_attribute("d").set_value(txt.c_str());
}

/**
 * Export a stroke using polyline or polygon
 * \param gpl_node: Node of the layer.
 * \param gps: Stroke to export.
 * \param diff_mat_: Transformation matrix.
 * \param is_fill: True if the stroke is only fill
 */
void GpencilExporterSVG::export_stroke_polyline(pugi::xml_node gpl_node, const bool is_fill)
{
  bGPDstroke *gps = gps_current_get();

  const bool is_thickness_const = is_stroke_thickness_constant(gps);
  const bool cyclic = ((gps->flag & GP_STROKE_CYCLIC) != 0);

  bGPDspoint *pt = &gps->points[0];
  float avg_pressure = pt->pressure;
  if (!is_thickness_const) {
    avg_pressure = stroke_average_pressure_get(gps);
  }

  /* Get the thickness in pixels using a simple 1 point stroke. */
  bGPDstroke *gps_temp = BKE_gpencil_stroke_duplicate(gps, false);
  gps_temp->totpoints = 1;
  gps_temp->points = (bGPDspoint *)MEM_callocN(sizeof(bGPDspoint), "gp_stroke_points");
  bGPDspoint *pt_src = &gps->points[0];
  bGPDspoint *pt_dst = &gps_temp->points[0];
  copy_v3_v3(&pt_dst->x, &pt_src->x);
  pt_dst->pressure = avg_pressure;

  float radius = stroke_point_radius_get(gps_temp);

  BKE_gpencil_free_stroke(gps_temp);

  pugi::xml_node gps_node = gpl_node.append_child(is_fill || cyclic ? "polygon" : "polyline");

  color_string_set(gps_node, is_fill);

  if (gp_style_is_stroke() && !is_fill) {
    gps_node.append_attribute("stroke-width").set_value(radius);
  }

  std::string txt;
  for (int i = 0; i < gps->totpoints; i++) {
    if (i > 0) {
      txt.append(" ");
    }
    bGPDspoint *pt = &gps->points[i];
    float screen_co[2];
    gpencil_3d_point_to_screen_space(&pt->x, screen_co);
    txt.append(std::to_string(screen_co[0]) + "," + std::to_string(screen_co[1]));
  }

  gps_node.append_attribute("points").set_value(txt.c_str());
}

void GpencilExporterSVG::color_string_set(pugi::xml_node gps_node, const bool is_fill)
{
  bGPDlayer *gpl = gpl_current_get();
  bGPDstroke *gps = gps_current_get();

  const bool round_cap = (gps->caps[0] == GP_STROKE_CAP_ROUND ||
                          gps->caps[1] == GP_STROKE_CAP_ROUND);

  float col[3];
  if (is_fill) {
    interp_v3_v3v3(col, fill_color_, gpl->tintcolor, gpl->tintcolor[3]);
    if ((params_.flag & GP_EXPORT_GRAY_SCALE) != 0) {
      rgb_to_grayscale(col);
    }
    linearrgb_to_srgb_v3_v3(col, col);
    std::string stroke_hex = rgb_to_hex(col);
    gps_node.append_attribute("fill").set_value(stroke_hex.c_str());
    gps_node.append_attribute("stroke").set_value("none");
    gps_node.append_attribute("fill-opacity").set_value(fill_color_[3] * gpl->opacity);
  }
  else {
    interp_v3_v3v3(col, stroke_color_, gpl->tintcolor, gpl->tintcolor[3]);
    if ((params_.flag & GP_EXPORT_GRAY_SCALE) != 0) {
      rgb_to_grayscale(col);
    }
    linearrgb_to_srgb_v3_v3(col, col);
    std::string stroke_hex = rgb_to_hex(col);
    gps_node.append_attribute("stroke").set_value(stroke_hex.c_str());
    gps_node.append_attribute("stroke-opacity")
        .set_value(stroke_color_[3] * stroke_average_opacity() * gpl->opacity);

    if (gps->totpoints > 1) {
      gps_node.append_attribute("fill").set_value("none");
      gps_node.append_attribute("stroke-linecap").set_value(round_cap ? "round" : "square");
    }
    else {
      gps_node.append_attribute("fill").set_value(stroke_hex.c_str());
      gps_node.append_attribute("fill-opacity").set_value(fill_color_[3] * gpl->opacity);
    }
  }
}

void GpencilExporterSVG::create_rect(pugi::xml_node node,
                                     float x,
                                     float y,
                                     float width,
                                     float height,
                                     float thickness,
                                     std::string hexcolor)
{
  pugi::xml_node rect_node = node.append_child("rect");
  rect_node.append_attribute("x").set_value(x);
  rect_node.append_attribute("y").set_value(y);
  rect_node.append_attribute("width").set_value(width);
  rect_node.append_attribute("height").set_value(height);
  rect_node.append_attribute("fill").set_value("none");
  if (thickness > 0.0f) {
    rect_node.append_attribute("stroke").set_value(hexcolor.c_str());
    rect_node.append_attribute("stroke-width").set_value(thickness);
  }
}

}  // namespace blender::io::gpencil
