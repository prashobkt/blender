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

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_main.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"

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

void GpencilExporter::set_out_filename(struct bContext *C, char *filename)
{
  Main *bmain = CTX_data_main(C);
  BLI_strncpy(out_filename, filename, FILE_MAX);
  BLI_path_abs(out_filename, BKE_main_blendfile_path(bmain));

  //#ifdef WIN32
  //  UTF16_ENCODE(svg_filename);
  //#endif
}

/* Constructor. */
GpencilExporterSVG::GpencilExporterSVG(const struct GpencilExportParams *params)
{
  this->params.frame_start = params->frame_start;
  this->params.frame_end = params->frame_end;
  this->params.ob = params->ob;
  this->region = params->region;
  this->params.C = params->C;
  this->params.filename = params->filename;
  this->params.mode = params->mode;

  /* Prepare output filename with full path. */
  set_out_filename(params->C, params->filename);
}

/* Convert to screen space.
 * TODO: Cleanup using a more generic BKE function. */
static bool gpencil_3d_point_to_screen_space(ARegion *region,
                                             const float diff_mat[4][4],
                                             const float co[3],
                                             int r_co[2])
{
  float parent_co[3];
  mul_v3_m4v3(parent_co, diff_mat, co);
  int screen_co[2];
  eV3DProjTest test = (eV3DProjTest)(V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN);
  if (ED_view3d_project_int_global(region, parent_co, screen_co, test) == V3D_PROJ_RET_OK) {
    if (!ELEM(V2D_IS_CLIPPED, screen_co[0], screen_co[1])) {
      copy_v2_v2_int(r_co, screen_co);
      return true;
    }
  }
  r_co[0] = V2D_IS_CLIPPED;
  r_co[1] = V2D_IS_CLIPPED;
  return false;
}

/* Main write method for SVG format. */
bool GpencilExporterSVG::write(void)
{
  create_document_header();
  layers_loop();

  //// add description node with text child
  // pugi::xml_node descr = main_node.append_child("object");
  // descr.append_child(pugi::node_pcdata).set_value(ob->id.name + 2);

  //// add param node before the description
  // pugi::xml_node param = main_node.insert_child_before("param", descr);

  //// add attributes to param node
  // param.append_attribute("name") = "version";
  // param.append_attribute("value") = 1.1;
  // param.insert_attribute_after("type", param.attribute("name")) = "float";

  // end::code[]
  doc.save_file(out_filename);

  return true;
}

/* Create document header and main svg node. */
void GpencilExporterSVG::create_document_header(void)
{
  /* Add a custom document declaration node */
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
  main_node.append_attribute("width").set_value("841px");
  main_node.append_attribute("height").set_value("600px");
  main_node.append_attribute("viewBox").set_value("0 0 841 600");
}

/* Main layer loop. */
void GpencilExporterSVG::layers_loop(void)
{
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(params.C);
  Object *ob = params.ob;

  bGPdata *gpd = (bGPdata *)ob->data;
  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
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

    LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
      float diff_mat[4][4];
      BKE_gpencil_parent_matrix_get(depsgraph, ob, gpl, diff_mat);

      pugi::xml_node gps_node = gpl_node.append_child("path");
      gps_node.append_attribute("fill").set_value("#000000");
      gps_node.append_attribute("stroke").set_value("#000000");
      gps_node.append_attribute("stroke-width").set_value("0.5");

      for (int i = 0; i < gps->totpoints; i++) {
        bGPDspoint *pt = &gps->points[i];
        int screen_co[2];
        gpencil_3d_point_to_screen_space(region, diff_mat, &pt->x, screen_co);
      }
    }
  }
}

}  // namespace gpencil
}  // namespace io
}  // namespace blender
