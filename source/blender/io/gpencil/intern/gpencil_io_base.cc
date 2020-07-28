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
#include "BKE_gpencil_geom.h"
#include "BKE_main.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_gpencil_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

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
void GpencilExporter::set_out_filename(char *filename)
{
  BLI_strncpy(out_filename, filename, FILE_MAX);
  BLI_path_abs(out_filename, BKE_main_blendfile_path(bmain));

  //#ifdef WIN32
  //  UTF16_ENCODE(svg_filename);
  //#endif
}

/* Convert to screen space.
 * TODO: Cleanup using a more generic BKE function. */
bool GpencilExporter::gpencil_3d_point_to_screen_space(const float co[3], float r_co[2])
{
  float parent_co[3];
  mul_v3_m4v3(parent_co, diff_mat, co);
  float screen_co[2];
  eV3DProjTest test = (eV3DProjTest)(V3D_PROJ_RET_OK);
  if (ED_view3d_project_float_global(params.region, parent_co, screen_co, test) ==
      V3D_PROJ_RET_OK) {
    if (!ELEM(V2D_IS_CLIPPED, screen_co[0], screen_co[1])) {
      copy_v2_v2(r_co, screen_co);
      /* Invert X axis. */
      if (invert_axis[0]) {
        r_co[0] = params.region->winx - r_co[0];
      }
      /* Invert Y axis. */
      if (invert_axis[1]) {
        r_co[1] = params.region->winy - r_co[1];
      }

      return true;
    }
  }
  r_co[0] = V2D_IS_CLIPPED;
  r_co[1] = V2D_IS_CLIPPED;

  /* Invert X axis. */
  if (invert_axis[0]) {
    r_co[0] = params.region->winx - r_co[0];
  }
  /* Invert Y axis. */
  if (invert_axis[1]) {
    r_co[1] = params.region->winy - r_co[1];
  }

  return false;
}

/**
 * Get average pressure
 * \param gps: Pointer to stroke
 * \retun value
 */
float GpencilExporter::stroke_average_pressure_get(struct bGPDstroke *gps)
{
  bGPDspoint *pt = NULL;

  if (gps->totpoints == 1) {
    pt = &gps->points[0];
    return pt->pressure;
  }

  float tot = 0.0f;
  for (int i = 0; i < gps->totpoints; i++) {
    pt = &gps->points[i];
    tot += pt->pressure;
  }

  return tot / (float)gps->totpoints;
}

/**
 * Check if the thickness of the stroke is constant
 * \param gps: Pointer to stroke
 * \retun true if all points thickness are equal.
 */
bool GpencilExporter::is_stroke_thickness_constant(struct bGPDstroke *gps)
{
  if (gps->totpoints == 1) {
    return true;
  }

  bGPDspoint *pt = &gps->points[0];
  float prv_pressure = pt->pressure;

  for (int i = 0; i < gps->totpoints; i++) {
    pt = &gps->points[i];
    if (pt->pressure != prv_pressure) {
      return false;
    }
  }

  return true;
}

float GpencilExporter::stroke_point_radius_get(struct bGPDstroke *gps)
{
  const bGPDlayer *gpl = gpl_current_get();
  RegionView3D *rv3d = (RegionView3D *)params.region->regiondata;
  bGPDspoint *pt = NULL;
  float v1[2], screen_co[2], screen_ex[2];

  pt = &gps->points[0];
  gpencil_3d_point_to_screen_space(&pt->x, screen_co);

  /* Radius. */
  bGPDstroke *gps_perimeter = BKE_gpencil_stroke_perimeter_from_view(
      rv3d, gpd, gpl, gps, 3, diff_mat);

  pt = &gps_perimeter->points[0];
  gpencil_3d_point_to_screen_space(&pt->x, screen_ex);

  sub_v2_v2v2(v1, screen_co, screen_ex);
  float radius = len_v2(v1);
  BKE_gpencil_free_stroke(gps_perimeter);

  return radius;
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
