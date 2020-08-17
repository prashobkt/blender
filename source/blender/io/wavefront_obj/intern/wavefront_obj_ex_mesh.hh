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

#include "BLI_array.hh"
#include "BLI_utility_mixins.hh"
#include "BLI_vector.hh"

#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "IO_wavefront_obj.h"

namespace blender::io::obj {
class OBJMesh : NonMovable, NonCopyable {
 private:
  Depsgraph *depsgraph_;
  const OBJExportParams &export_params_;

  Object *export_object_eval_;
  Mesh *export_mesh_eval_;
  /**
   * For curves which are converted to mesh, and triangulated meshes, a new mesh is allocated
   * which needs to be freed later.
   */
  bool mesh_eval_needs_free_ = false;
  /**
   * Final transform of an object obtained from export settings (up_axis, forward_axis) and world
   * transform matrix.
   */
  float world_and_axes_transform_[4][4] = {};

  /**
   * Total UV vertices in a mesh's texture map.
   */
  uint tot_uv_vertices_ = 0;
  /**
   * Total smooth groups in an object.
   */
  uint tot_smooth_groups_ = 0;
  /**
   * Smooth group of all the polygons. 0 if the polygon is not shaded smooth.
   */
  int *poly_smooth_groups_ = nullptr;

 public:
  OBJMesh(Depsgraph *depsgraph, const OBJExportParams &export_params, Object *export_object);
  ~OBJMesh();

  uint tot_vertices() const;
  uint tot_polygons() const;
  uint tot_uv_vertices() const;
  uint tot_edges() const;
  uint tot_normals() const;
  short tot_col() const;
  uint tot_smooth_groups() const;
  int ith_smooth_group(int poly_index) const;

  void ensure_mesh_normals() const;
  void ensure_mesh_edges() const;
  void calc_smooth_groups();
  const Material *get_object_material(const short mat_nr) const;
  const MPoly &get_ith_poly(const uint i) const;

  const char *get_object_name() const;
  const char *get_object_data_name() const;
  const char *get_object_material_name(const short mat_nr) const;

  void calc_vertex_coords(const uint vert_index, float r_coords[3]) const;
  void calc_poly_vertex_indices(const uint poly_index, Vector<uint> &r_poly_vertex_indices) const;
  void store_uv_coords_and_indices(Vector<std::array<float, 2>> &r_uv_coords,
                                   Vector<Vector<uint>> &r_uv_indices);
  void calc_poly_normal(const uint poly_index, float r_poly_normal[3]) const;
  void calc_vertex_normal(const uint vert_index, float r_vertex_normal[3]) const;
  void calc_poly_normal_indices(const uint poly_index, Vector<uint> &r_normal_indices) const;
  const char *get_poly_deform_group_name(const MPoly &mpoly, short &r_last_vertex_group) const;
  Array<int, 2> calc_edge_vert_indices(const uint edge_index) const;

 private:
  void triangulate_mesh_eval();
  void store_world_axes_transform();
};
}  // namespace blender::io::obj
