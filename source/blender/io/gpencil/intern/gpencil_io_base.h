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
#include <map>
#include <string>
#include <vector>

#include "BLI_path_util.h"

#include "DNA_defs.h"

#include "gpencil_io_exporter.h"

struct Main;
struct ARegion;

struct bGPDlayer;
struct bGPDstroke;

namespace blender {
namespace io {
namespace gpencil {

class GpencilExporter {

 public:
  virtual bool write(std::string actual_frame) = 0;
  void set_out_filename(struct bContext *C, char *filename);

  /* Geometry functions. */
  bool gpencil_3d_point_to_screen_space(struct ARegion *region,
                                        const float diff_mat[4][4],
                                        const float co[3],
                                        const bool invert,
                                        float r_co[2]);

  bool is_stroke_thickness_constant(struct bGPDstroke *gps);
  float stroke_average_pressure_get(struct bGPDstroke *gps);
  float stroke_point_radius_get(const struct bGPDlayer *gpl,
                                struct bGPDstroke *gps,
                                float diff_mat[4][4]);

  std::string rgb_to_hex(float color[3]);
  std::string to_lower_string(char *input_text);

 protected:
  GpencilExportParams params;
  char out_filename[FILE_MAX];
  /* Data for easy access. */
  struct bGPdata *gpd;
};

}  // namespace gpencil
}  // namespace io
}  // namespace blender
