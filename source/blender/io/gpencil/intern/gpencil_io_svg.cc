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
#include <string>

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

#include "ED_gpencil.h"

#ifdef WIN32
#  include "utfconv.h"
#endif

#include "UI_view2d.h"

#include "ED_view3d.h"

#include "gpencil_io_exporter.h"
#include "gpencil_io_svg.h"

#include "pugixml.hpp"

namespace blender {
namespace io {
namespace gpencil {

/* Constructor. */
GpencilExporterSVG::GpencilExporterSVG(const struct GpencilExportParams *params)
{
  this->params.frame_start = params->frame_start;
  this->params.frame_end = params->frame_end;
  this->params.ob = params->ob;
  this->params.region = params->region;
  this->params.C = params->C;
  this->params.filename = params->filename;
  this->params.mode = params->mode;

  /* Prepare output filename with full path. */
  set_out_filename(params->C, params->filename);
}

/* Main write method for SVG format. */
bool GpencilExporterSVG::write(void)
{
  create_document_header();

  export_style_list();
  export_layers();

  doc.save_file(out_filename);

  return true;
}

/* Create document header and main svg node. */
void GpencilExporterSVG::create_document_header(void)
{
  int x = params.region->winx;
  int y = params.region->winy;

  /* Add a custom document declaration node. */
  pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";

  pugi::xml_node comment = doc.append_child(pugi::node_comment);
  comment.set_value(" Generator: Blender, SVG Export for Grease Pencil ");

  pugi::xml_node doctype = doc.append_child(pugi::node_doctype);
  doctype.set_value(
      "svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" "
      "\"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\"");

  main_node = doc.append_child("svg");
  main_node.append_attribute("version").set_value("1.0");
  main_node.append_attribute("x").set_value("0px");
  main_node.append_attribute("y").set_value("0px");

  std::string width = std::to_string(x) + "px";
  std::string height = std::to_string(y) + "px";
  main_node.append_attribute("width").set_value(width.c_str());
  main_node.append_attribute("height").set_value(height.c_str());
  std::string viewbox = "0 0 " + std::to_string(x) + " " + std::to_string(y);
  main_node.append_attribute("viewBox").set_value(viewbox.c_str());
}

/* Main layer loop. */
void GpencilExporterSVG::export_layers(void)
{
  RegionView3D *rv3d = (RegionView3D *)params.region->regiondata;

  float color[3] = {1.0f, 0.5f, 0.01f};
  std::string hex = rgb_to_hex(color);

  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(params.C);
  Object *ob = params.ob;

  bGPdata *gpd = (bGPdata *)ob->data;
  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    if (gpl->flag & GP_LAYER_HIDE) {
      continue;
    }

    /* Layer node. */
    std::string txt = "Layer: ";
    txt.append(gpl->info);
    main_node.append_child(pugi::node_comment).set_value(txt.c_str());
    pugi::xml_node gpl_node = main_node.append_child("g");
    gpl_node.append_attribute("id").set_value(gpl->info);

    bGPDframe *gpf = gpl->actframe;
    if (gpf == NULL) {
      continue;
    }

    float diff_mat[4][4];
    BKE_gpencil_parent_matrix_get(depsgraph, ob, gpl, diff_mat);

    LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
      MaterialGPencilStyle *gp_style = BKE_gpencil_material_settings(ob, gps->mat_nr + 1);
      bool is_stroke = ((gp_style->flag & GP_MATERIAL_STROKE_SHOW) &&
                        (gp_style->stroke_rgba[3] > GPENCIL_ALPHA_OPACITY_THRESH));
      bool is_fill = ((gp_style->flag & GP_MATERIAL_FILL_SHOW) &&
                      (gp_style->fill_rgba[3] > GPENCIL_ALPHA_OPACITY_THRESH));

      /* Fill. */
      if (is_fill) {
        export_stroke(gpl_node, gps, diff_mat);
      }

      /* Stroke. */
      if (is_stroke) {
        /* Create a duplicate to avoid any transformation. */
        bGPDstroke *gps_tmp = BKE_gpencil_stroke_duplicate(gps, true);
        bGPDstroke *gps_perimeter = BKE_gpencil_stroke_perimeter_from_view(
            rv3d, gpd, gpl, gps_tmp, 3, diff_mat);

        /* Reproject and sample stroke. */
        // ED_gpencil_project_stroke_to_view(params.C, gpl, gps_perimeter);
        BKE_gpencil_stroke_sample(gps_perimeter, 0.03f, false);

        export_stroke(gpl_node, gps_perimeter, diff_mat);

        BKE_gpencil_free_stroke(gps_perimeter);
        BKE_gpencil_free_stroke(gps_tmp);
      }
    }
  }
}

/**
 * Export a stroke
 * \param gpl_node: Node of the layer.
 * \param gps: Stroke to export.
 * \param ma: Material of the stroke.
 * \param diff_mat: Transformation matrix.
 */
void GpencilExporterSVG::export_stroke(pugi::xml_node gpl_node,
                                       struct bGPDstroke *gps,
                                       float diff_mat[4][4])
{
  pugi::xml_node gps_node = gpl_node.append_child("path");
  // gps_node.append_attribute("fill").set_value("#000000");
  // gps_node.append_attribute("stroke").set_value("#000000");

  gps_node.append_attribute("class").set_value(
      ("style" + std::to_string(gps->mat_nr + 1)).c_str());

  gps_node.append_attribute("stroke-width").set_value("1.0");

  std::string txt = "M";
  for (int i = 0; i < gps->totpoints; i++) {
    if (i > 0) {
      txt.append("L");
    }
    bGPDspoint *pt = &gps->points[i];
    float screen_co[2];
    gpencil_3d_point_to_screen_space(params.region, diff_mat, &pt->x, screen_co);
    /* Invert Y axis. */
    int y = params.region->winy - screen_co[1];
    txt.append(std::to_string(screen_co[0]) + "," + std::to_string(y));
  }
  /* Close patch (cyclic)*/
  if (gps->flag & GP_STROKE_CYCLIC) {
    txt.append("z");
  }

  gps_node.append_attribute("d").set_value(txt.c_str());
}

/**
 * Create Style (materials) list.
 */
void GpencilExporterSVG::export_style_list(void)
{
  Object *ob = this->params.ob;
  int mat_len = max_ii(1, ob->totcol);
  main_node.append_child(pugi::node_comment).set_value("List of materials");
  pugi::xml_node style_node = main_node.append_child("style");
  style_node.append_attribute("type").set_value("text/css");

  std::string txt;
  float col[3];

  for (int i = 0; i < mat_len; i++) {
    MaterialGPencilStyle *gp_style = BKE_gpencil_material_settings(ob, i + 1);

    bool is_stroke = ((gp_style->flag & GP_MATERIAL_STROKE_SHOW) &&
                      (gp_style->stroke_rgba[3] > GPENCIL_ALPHA_OPACITY_THRESH));
    bool is_fill = ((gp_style->flag & GP_MATERIAL_FILL_SHOW) &&
                    (gp_style->fill_rgba[3] > GPENCIL_ALPHA_OPACITY_THRESH));

    txt.append("\n\t.style");
    txt.append(std::to_string(i + 1).c_str());

    txt.append("{");
    if (is_fill) {
      linearrgb_to_srgb_v3_v3(col, gp_style->fill_rgba);

      txt.append("fill:" + rgb_to_hex(col) + ";");
      if (!is_stroke) {
        txt.append("stroke:" + rgb_to_hex(col) + ";");
      }
    }

    if (is_stroke) {
      linearrgb_to_srgb_v3_v3(col, gp_style->stroke_rgba);

      txt.append("stroke:" + rgb_to_hex(col) + ";");
      if (!is_fill) {
        txt.append("fill:" + rgb_to_hex(col) + ";");
      }
    }
    txt.append("}");
  }
  txt.append("\n\t");
  style_node.text().set(txt.c_str());
}

}  // namespace gpencil
}  // namespace io
}  // namespace blender
