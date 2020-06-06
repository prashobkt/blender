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

#include <iostream>

#include "gmpxx.h"

#include "BLI_array.hh"
#include "BLI_assert.h"
#include "BLI_mesh_intersect.hh"
#include "BLI_mpq3.hh"

#include "BLI_boolean.h"

namespace BLI {
namespace MeshIntersect {

static TriMesh self_boolean(const TriMesh &tm_in, int bool_optype)
{
  constexpr int dbg_level = 0;
  if (dbg_level > 0) {
    std::cout << "\nSELF_BOOLEAN op=" << bool_optype << "\n";
  }
  TriMesh tm_si = trimesh_self_intersect(tm_in);
  return tm_si;
}

} /* namespace MeshIntersect */
} /* namespace BLI */

extern "C" Boolean_trimesh_output *BLI_boolean_trimesh(const Boolean_trimesh_input *input,
                                                       int bool_optype)
{
  constexpr int dbg_level = 1;
  BLI::MeshIntersect::TriMesh tm_in;
  tm_in.vert = BLI::Array<BLI::mpq3>(input->vert_len);
  for (int v = 0; v < input->vert_len; ++v) {
    tm_in.vert[v] = BLI::mpq3(
        input->vert_coord[v][0], input->vert_coord[v][1], input->vert_coord[v][2]);
  }
  tm_in.tri = BLI::Array<BLI::MeshIntersect::IndexedTriangle>(input->tri_len);
  for (int t = 0; t < input->tri_len; ++t) {
    tm_in.tri[t] = BLI::MeshIntersect::IndexedTriangle(
        input->tri[t][0], input->tri[t][1], input->tri[t][2], t);
  }
  BLI::MeshIntersect::TriMesh tm_out = self_boolean(tm_in, bool_optype);
  if (dbg_level > 0) {
    BLI::MeshIntersect::write_html_trimesh(
        tm_out.vert, tm_out.tri, "mesh_boolean_test.html", "after self_boolean");
    BLI::MeshIntersect::write_obj_trimesh(tm_out.vert, tm_out.tri, "test_tettet");
  }
  int nv = tm_out.vert.size();
  int nt = tm_out.tri.size();
  Boolean_trimesh_output *output = static_cast<Boolean_trimesh_output *>(
      MEM_mallocN(sizeof(*output), __func__));
  output->vert_len = nv;
  output->tri_len = nt;
  output->vert_coord = static_cast<decltype(output->vert_coord)>(
      MEM_malloc_arrayN(nv, sizeof(output->vert_coord[0]), __func__));
  output->tri = static_cast<decltype(output->tri)>(
      MEM_malloc_arrayN(nt, sizeof(output->tri[0]), __func__));
  for (int v = 0; v < nv; ++v) {
    output->vert_coord[v][0] = static_cast<float>(tm_out.vert[v][0].get_d());
    output->vert_coord[v][1] = static_cast<float>(tm_out.vert[v][1].get_d());
    output->vert_coord[v][2] = static_cast<float>(tm_out.vert[v][2].get_d());
  }
  for (int t = 0; t < nt; ++t) {
    output->tri[t][0] = tm_out.tri[t].v0();
    output->tri[t][1] = tm_out.tri[t].v1();
    output->tri[t][2] = tm_out.tri[t].v2();
  }
  return output;
}

extern "C" void BLI_boolean_trimesh_free(Boolean_trimesh_output *output)
{
  MEM_freeN(output->vert_coord);
  MEM_freeN(output->tri);
  MEM_freeN(output);
}
