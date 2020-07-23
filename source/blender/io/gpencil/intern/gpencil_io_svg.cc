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
#include "BKE_main.h"

#include "BLI_path_util.h"
#include "BLI_string.h"

#include "DNA_object_types.h"

#ifdef WIN32
#  include "utfconv.h"
#endif

#include <fstream>

#include "gpencil_io_exporter.h"
#include "gpencil_io_svg.h"

#include "pugixml.hpp"

namespace blender {
namespace io {
namespace gpencil {

/* Constructor. */
GpencilSVGwriter::GpencilSVGwriter(void)
{
  std::cout << "Constructor\n";
}

bool GpencilSVGwriter::write(struct bContext *C,
                             const char *filepath,
                             const struct GpencilExportParams *params)
{
  Main *bmain = CTX_data_main(C);
  char svg_filename[FILE_MAX];
  BLI_strncpy(svg_filename, filepath, FILE_MAX);
  BLI_path_abs(svg_filename, BKE_main_blendfile_path(bmain));

  Object *ob = params->ob;

  //#ifdef WIN32
  //  UTF16_ENCODE(svg_filename);
  //#endif

  /* Create simple XML. */
  pugi::xml_document doc;

  // tag::code[]
  // add node with some name
  pugi::xml_node node = doc.append_child("node");

  // add description node with text child
  pugi::xml_node descr = node.append_child("object");
  descr.append_child(pugi::node_pcdata).set_value(ob->id.name + 2);

  // add param node before the description
  pugi::xml_node param = node.insert_child_before("param", descr);

  // add attributes to param node
  param.append_attribute("name") = "version";
  param.append_attribute("value") = 1.1;
  param.insert_attribute_after("type", param.attribute("name")) = "float";
  // end::code[]
  doc.save_file(svg_filename);

  return true;
}

}  // namespace gpencil
}  // namespace io
}  // namespace blender
