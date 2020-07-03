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

#ifndef __BLI_BOOLEAN_HH__
#define __BLI_BOOLEAN_HH__

/** \file
 * \ingroup bli
 */

#include "BLI_array.hh"
#include "BLI_math_mpq.hh"
#include "BLI_mesh_intersect.hh"
#include "BLI_mpq3.hh"

namespace blender::meshintersect {

/* Enum values after BOOLEAN_NONE need to match BMESH_ISECT_BOOLEAN_... values in
 * editmesh_intersect.c. */
enum bool_optype {
  BOOLEAN_NONE = -1,
  /* Aligned with BooleanModifierOp. */
  BOOLEAN_ISECT = 0,
  BOOLEAN_UNION = 1,
  BOOLEAN_DIFFERENCE = 2,
};

struct PolyMeshOrig {
  Array<int> vert_orig;
  Array<Array<int>> face_orig;
  Array<Array<std::pair<int, int>>> edge_orig;
};

struct PolyMesh {
  Array<mpq3> vert;
  Array<Array<int>> face;
  /* triangulation can have zero length: then boolean will do it.  */
  Array<Array<IndexedTriangle>> triangulation;
  /* orig can be dummy for boolean input, but has useful information for its output. */
  PolyMeshOrig orig;
};

PolyMesh boolean(PolyMesh &pm, bool_optype op, int nshapes, std::function<int(int)> shape_fn);

TriMesh boolean_trimesh(const TriMesh &tm,
                        bool_optype op,
                        int nshapes,
                        std::function<int(int)> shape_fn);

void write_obj_polymesh(const Array<mpq3> &vert,
                        const Array<Array<int>> &face,
                        const std::string &objname);

}  // namespace blender::meshintersect

#endif /* __BLI_BOOLEAN_HH__ */
