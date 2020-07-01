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
 * \ingroup bmesh
 *
 * Main functions for boolean on a BMesh (used by the tool and modifier)
 */

#include "BLI_array.hh"
#include "BLI_boolean.hh"
#include "BLI_math.h"
#include "BLI_math_mpq.hh"
#include "BLI_mesh_intersect.hh"

#include "bmesh.h"
#include "bmesh_boolean.h"

namespace blender {
namespace meshintersect {

static PolyMesh polymesh_from_bm(BMesh *bm,
                                 struct BMLoop *(*looptris)[3],
                                 const int looptris_tot)
{
  BM_mesh_elem_index_ensure(bm, BM_VERT | BM_FACE);
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_FACE);
  PolyMesh pm;
  pm.vert = Array<mpq3>(bm->totvert);
  for (int v = 0; v < bm->totvert; ++v) {
    BMVert *bmv = BM_vert_at_index(bm, v);
    pm.vert[v] = mpq3(mpq_class(bmv->co[0]),
                      mpq_class(bmv->co[1]),
                      mpq_class(bmv->co[2]));
  }
  pm.face = Array<Array<int>>(bm->totface);
  pm.triangulation = Array<Array<IndexedTriangle>>(bm->totface);
  for (int f = 0; f < bm->totface; ++f) {
    BMFace *bmf = BM_face_at_index(bm, f);
    int flen = bmf->len;
    pm.face[f] = Array<int>(flen);
    Array<int> &face = pm.face[f];
    BMLoop *l = bmf->l_first;
    for (int i = 0; i < flen; ++i) {
      face[i] = BM_elem_index_get(l->v);
      l = l->next;
    }
    /* A correct triangulation of a polygon with flen sides has flen-2 tris. */
    pm.triangulation[f] = Array<IndexedTriangle>(flen - 2);
  }
  Array<int> triangulation_next_index(bm->totface, 0);
  for (int i = 0; i < looptris_tot; ++i) {
    BMFace *bmf = looptris[i][0]->f;
    int f = BM_elem_index_get(bmf);
    BLI_assert(triangulation_next_index[f] < bmf->len - 2);
    if (triangulation_next_index[f] >= bmf->len - 2) {
      continue;
    }
    int v0 = BM_elem_index_get(looptris[i][0]->v);
    int v1 = BM_elem_index_get(looptris[i][1]->v);
    int v2 = BM_elem_index_get(looptris[i][2]->v);
    pm.triangulation[f][triangulation_next_index[f]++] = IndexedTriangle(v0, v1, v2, f);
  }
  return pm;
}

static void apply_polymesh_output_to_bmesh(BMesh *bm, const PolyMesh &pm_out)
{
  /* For now, just for testing, just kill the whole old mesh and create the new one.
   * No attempt yet to use proper examples for the new elements so that they inherit the
   * proper attributes.
   * No attempt yet to leave the correct geometric elements selected.
   */

  /* The BM_ITER_... macros need attention to work in C++. For now, copy the old BMVerts. */
  int totvert_orig = bm->totvert;
  Array<BMVert *> orig_bmv(totvert_orig);
  for (int v = 0; v < bm->totvert; ++v) {
    orig_bmv[v] = BM_vert_at_index(bm, v);
  }
  for (int v = 0; v < totvert_orig; ++v) {
    BM_vert_kill(bm, orig_bmv[v]);
  }

  if (pm_out.vert.size() > 0 && pm_out.face.size() > 0) {
    Array<BMVert *> new_bmv(pm_out.vert.size());
    for (int v = 0; v < static_cast<int>(pm_out.vert.size()); ++v) {
      float co[3];
      const mpq3 & mpq3_co = pm_out.vert[v];
      for (int i = 0; i < 3; ++i) {
        co[i] = static_cast<float>(mpq3_co[i].get_d());
      }
      new_bmv[v] = BM_vert_create(bm, co, NULL, BM_CREATE_NOP);
    }
    int maxflen = 0;
    int new_totface = static_cast<int>(pm_out.face.size());
    for (int f = 0; f < new_totface; ++f) {
      maxflen = max_ii(maxflen, static_cast<int>(pm_out.face[f].size()));
    }
    Array<BMVert *> face_bmverts(maxflen);
    for (int f = 0; f < new_totface; ++f) {
      const Array<int> &face = pm_out.face[f];
      int flen = static_cast<int>(face.size());
      for (int i = 0; i < flen; ++i) {
        BMVert *bmv = new_bmv[face[i]];
        face_bmverts[i] = bmv;
      }
      BM_face_create_ngon_verts(bm, face_bmverts.begin(), flen, NULL, BM_CREATE_NOP, true, true);
    }
  }
}

static int bmesh_boolean(BMesh *bm,
                         struct BMLoop *(*looptris)[3],
                         const int looptris_tot,
                         int (*test_fn)(BMFace *f, void *user_data),
                         void *user_data,
                         const bool use_self,
                         const bool UNUSED(use_separate_all),
                         const int boolean_mode)
{
  PolyMesh pm_in = polymesh_from_bm(bm, looptris, looptris_tot);
  std::function<int(int)> shape_fn;
  int nshapes;
  if (use_self) {
    /* Unary boolean operation. Want every face where test_fn doesn't return -1. */
    nshapes = 1;
    shape_fn = [bm, test_fn, user_data](int f) {
      BMFace *bmf = BM_face_at_index(bm, f);
      if (test_fn(bmf, user_data) != -1) {
        return 0;
      }
      else {
        return -1;
      }
    };
  }
  else {
    /* Binary boolean operation.
     * Because our boolean function's difference does shape 0 - shape 1,
     * and Blender's convention is to do the opposite, reverse the shape
     * assigment in this test.
     */
    nshapes = 2;
    shape_fn = [bm, test_fn, user_data](int f) {
      BMFace *bmf = BM_face_at_index(bm, f);
      int test_val = test_fn(bmf, user_data);
      if (test_val == 0) {
        return 1;
      }
      else if (test_val == 1) {
        return 0;
      }
      else {
        return -1;
      }
    };
  }
  bool_optype op = static_cast<bool_optype>(boolean_mode);
  PolyMesh pm_out = boolean(pm_in, op, nshapes, shape_fn);
  apply_polymesh_output_to_bmesh(bm, pm_out);
  return pm_in.vert.size() != pm_out.vert.size() || pm_in.face.size() != pm_out.face.size();
}

}  // namespace meshintersect
}  // namespace blender

extern "C" {
/*
 * Perform the boolean operation specified by boolean_mode on the mesh bm.
 * The inputs to the boolean operation are either one submesh (if use_self is true),
 * or two submeshes. The submeshes are specified by providing a test_fn which takes
 * a face and the supplied user_data and says with 'side' of the boolean operation
 * that face is for: 0 for the first side (side A), 1 for the second side (side B),
 * and -1 if the face is to be ignored completely in the boolean operation.
 *
 * If use_self is true, all operations do the same: the submesh is self-intersected
 * and all pieces inside that result are removed.
 * Otherwise, the operations can be one of BMESH_ISECT_BOOLEAN_ISECT, BMESH_ISECT_BOOLEAN_UNION,
 * or BMESH_ISECT_BOOLEAN_DIFFERENCE.
 *
 * (The actual library function called to do the boolean is internally capable of handling
 * n-ary operands, so maybe in the future we can expose that functionality to users.)
 */
bool BM_mesh_boolean(BMesh *bm,
                     struct BMLoop *(*looptris)[3],
                     const int looptris_tot,
                     int (*test_fn)(BMFace *f, void *user_data),
                     void *user_data,
                     const bool use_self,
                     const int boolean_mode)
{
  return blender::meshintersect::bmesh_boolean(bm, looptris, looptris_tot, test_fn, user_data, use_self, false, boolean_mode);
}

/*
 * Perform a Knife Intersection operation on the mesh bm.
 * There are either one or two operands, the same as described above for BM_mesh_boolean().
 * If use_separate_all is true, each edge that is created from the intersection should
 * be used to separate all its incident faces. TODO: implement that.
 * TODO: need to ensure that "selected/non-selected" flag of original faces gets propagated
 * to the intersection result faces.
 */
bool BM_mesh_boolean_knife(BMesh *bm,
                           struct BMLoop *(*looptris)[3],
                           const int looptris_tot,
                           int (*test_fn)(BMFace *f, void *user_data),
                           void *user_data,
                           const bool use_self,
                           const bool use_separate_all)
{
  return blender::meshintersect::bmesh_boolean(bm, looptris, looptris_tot, test_fn, user_data, use_self, use_separate_all, blender::meshintersect::BOOLEAN_NONE);
}

} /* extern "C" */

