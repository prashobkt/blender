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

#include "obj_import_mtl.hh"

namespace blender::io::obj {
class MaterialWrap {
 private:
  const OBJMesh &obj_mesh_data_;
  Vector<MTLMaterial> &r_mtl_materials_;
  /**
   * One of the object's materials, to be exported.
   */
  const Material *export_mtl_;
  /**
   * First Principled-BSDF node encountered in the object's node tree.
   */
  bNode *bsdf_node_;

 public:
  MaterialWrap(const OBJMesh &obj_mesh_data, Vector<MTLMaterial> &r_mtl_materials);
  void fill_materials();

 private:
  void init_bsdf_node(StringRefNull object_name);
  void store_bsdf_properties(MTLMaterial &r_mtl_mat) const;
  void store_image_textures(MTLMaterial &r_mtl_mat) const;
};
}  // namespace blender::io::obj
