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

#include "pugixml.hpp"

struct Main;
struct GpencilExportParams;
struct ARegion;

namespace blender {
namespace io {
namespace gpencil {

class GpencilExporter {

 public:
  virtual bool write(void) = 0;
  void set_out_filename(struct bContext *C, char *filename);

  /* Geometry functions. */
  bool gpencil_3d_point_to_screen_space(struct ARegion *region,
                                        const float diff_mat[4][4],
                                        const float co[3],
                                        int r_co[2]);

  std::string rgb_to_hex(float color[3]);

 protected:
  GpencilExportParams params;
  char out_filename[FILE_MAX];
};

}  // namespace gpencil
}  // namespace io
}  // namespace blender
