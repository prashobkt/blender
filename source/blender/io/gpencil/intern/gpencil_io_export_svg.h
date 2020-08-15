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
#pragma once

/** \file
 * \ingroup bgpencil
 */
#include "BLI_path_util.h"

#include "DNA_material_types.h"

#include "gpencil_io_export_base.h"
#include "pugixml.hpp"

struct GpencilExportParams;

#define SVG_EXPORTER_NAME "SVG Export for Grease Pencil"
#define SVG_EXPORTER_VERSION "v1.0"

namespace blender::io::gpencil {

class GpencilExporterSVG : public GpencilExporter {

 public:
  GpencilExporterSVG(const char *filename, const struct GpencilExportParams *iparams);
  bool add_newpage(void);
  bool add_body(void);
  bool write(std::string subfix);

 protected:
  static void add_rect(pugi::xml_node node,
                       float x,
                       float y,
                       float width,
                       float height,
                       float thickness,
                       std::string hexcolor);

  static void add_text(pugi::xml_node node,
                       float x,
                       float y,
                       std::string text,
                       const float size,
                       std::string hexcolor);

 private:
  /* XML doc. */
  pugi::xml_document main_doc;
  /* Main document node. */
  pugi::xml_node main_node;
  /** Frame node  */
  pugi::xml_node frame_node;
  void create_document_header(void);
  void export_gpencil_layers(void);

  void export_stroke_to_point(pugi::xml_node gpl_node);

  void export_stroke_to_path(pugi::xml_node gpl_node, const bool is_fill);

  void export_stroke_to_polyline(pugi::xml_node gpl_node, const bool is_fill);

  void color_string_set(pugi::xml_node gps_node, const bool is_fill);
};

}  // namespace blender::io::gpencil
