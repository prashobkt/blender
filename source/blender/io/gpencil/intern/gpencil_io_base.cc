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
#include <algorithm>
#include <cctype>

#include <iostream>
#include <string>

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_main.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_object_types.h"

#ifdef WIN32
#  include "utfconv.h"
#endif

#include "UI_view2d.h"

#include "ED_view3d.h"

#include "gpencil_io_base.h"
#include "gpencil_io_exporter.h"

#include "pugixml.hpp"

namespace blender {
namespace io {
namespace gpencil {

/**
 * Set output file input_text full path.
 * \param C: Context.
 * \param filename: Path of the file provided by save dialog.
 */
void GpencilExporter::set_out_filename(struct bContext *C, char *filename)
{
  Main *bmain = CTX_data_main(C);
  BLI_strncpy(out_filename, filename, FILE_MAX);
  BLI_path_abs(out_filename, BKE_main_blendfile_path(bmain));

  //#ifdef WIN32
  //  UTF16_ENCODE(svg_filename);
  //#endif
}

/* Convert to screen space.
 * TODO: Cleanup using a more generic BKE function. */
bool GpencilExporter::gpencil_3d_point_to_screen_space(struct ARegion *region,
                                                       const float diff_mat[4][4],
                                                       const float co[3],
                                                       float r_co[2])
{
  float parent_co[3];
  mul_v3_m4v3(parent_co, diff_mat, co);
  float screen_co[2];
  //  eV3DProjTest test = (eV3DProjTest)(V3D_PROJ_RET_CLIP_BB | V3D_PROJ_RET_CLIP_WIN);
  eV3DProjTest test = (eV3DProjTest)(V3D_PROJ_RET_OK);
  if (ED_view3d_project_float_global(region, parent_co, screen_co, test) == V3D_PROJ_RET_OK) {
    if (!ELEM(V2D_IS_CLIPPED, screen_co[0], screen_co[1])) {
      copy_v2_v2(r_co, screen_co);
      return true;
    }
  }
  r_co[0] = V2D_IS_CLIPPED;
  r_co[1] = V2D_IS_CLIPPED;
  return false;
}

/**
 * Convert a color to Hex value (#FFFFFF)
 * \param color: Original RGB color
 * \return String with the conversion
 */
std::string GpencilExporter::rgb_to_hex(float color[3])
{
  int r = color[0] * 255.0f;
  int g = color[1] * 255.0f;
  int b = color[2] * 255.0f;
  char hex_string[20];
  sprintf(hex_string, "#%02X%02X%02X", r, g, b);

  std::string hexstr = hex_string;

  return hexstr;
}

/**
 * Convert a full string to lowercase
 * \param input_text: Input input_text
 * \return Lower case string
 */
std::string GpencilExporter::to_lower_string(char *input_text)
{
  ::std::string text = input_text;
  /* First remove any point of the string. */
  size_t found = text.find_first_of(".");
  while (found != std::string::npos) {
    text[found] = '_';
    found = text.find_first_of(".", found + 1);
  }

  std::transform(
      text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });

  return text;
}
}  // namespace gpencil
}  // namespace io
}  // namespace blender
