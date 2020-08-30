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
 *
 * The Original Code is Copyright (C) 2020 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup obj
 */

#pragma once

#include "BLI_utility_mixins.hh"

#include "DNA_curve_types.h"

namespace blender::io::obj {
class OBJNurbs : NonMovable, NonCopyable {
 private:
  const Depsgraph *depsgraph_;
  const OBJExportParams &export_params_;
  const Object *export_object_eval_;
  const Curve *export_curve_;
  float world_axes_transform_[4][4];

 public:
  OBJNurbs(Depsgraph *depsgraph, const OBJExportParams &export_params, Object *export_object);

  const char *get_curve_name() const;
  const ListBase *curve_nurbs() const;
  void calc_point_coords(const Nurb *nurb, int vert_index, float r_coords[3]) const;
  void get_curve_info(const Nurb *nurb, int &r_nurbs_degree, int &r_curv_num) const;

 private:
  void store_world_axes_transform();
};

}  // namespace blender::io::obj
