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

static Mesh mesh_from_bm(BMesh *bm,
                         struct BMLoop *(*looptris)[3],
                         const int looptris_tot,
                         Mesh *r_triangulated,
                         MArena *arena)
{
  BLI_assert(r_triangulated != nullptr);
  BM_mesh_elem_index_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);
  const int estimate_num_outv = (3 * bm->totvert) / 2;
  const int estimate_num_outf = (3 * bm->totface) / 2;
  arena->reserve(estimate_num_outv, estimate_num_outf);
  Array<Vertp> vert(bm->totvert);
  for (int v = 0; v < bm->totvert; ++v) {
    BMVert *bmv = BM_vert_at_index(bm, v);
    vert[v] = arena->add_or_find_vert(mpq3(bmv->co[0], bmv->co[1], bmv->co[2]), v);
  }
  Array<Facep> face(bm->totface);
  constexpr uint estimated_max_facelen = 100;
  Vector<Vertp, estimated_max_facelen> face_vert;
  Vector<int, estimated_max_facelen> face_edge_orig;
  for (int f = 0; f < bm->totface; ++f) {
    BMFace *bmf = BM_face_at_index(bm, f);
    int flen = bmf->len;
    face_vert.clear();
    face_edge_orig.clear();
    BMLoop *l = bmf->l_first;
    for (int i = 0; i < flen; ++i) {
      Vertp v = vert[BM_elem_index_get(l->v)];
      face_vert.append(v);
      int e_index = BM_elem_index_get(l->e);
      face_edge_orig.append(e_index);
      l = l->next;
    }
    face[f] = arena->add_face(face_vert, f, face_edge_orig);
  }
  /* Now do the triangulation mesh.
   * The loop_tris have accurate v and f members for the triangles,
   * but their next and e pointers are not correct for the loops
   * that start added-diagonal edges.
   */
  Array<Facep> tri_face(looptris_tot);
  face_vert.resize(3);
  face_edge_orig.resize(3);
  for (int i = 0; i < looptris_tot; ++i) {
    BMFace *bmf = looptris[i][0]->f;
    int f = BM_elem_index_get(bmf);
    for (int j = 0; j < 3; ++j) {
      BMLoop *l = looptris[i][j];
      int v_index = BM_elem_index_get(l->v);
      int e_index;
      if (l->next->v == looptris[i][(j + 1) % 3]->v) {
        e_index = BM_elem_index_get(l->e);
      }
      else {
        e_index = NO_INDEX;
      }
      face_vert[j] = vert[v_index];
      face_edge_orig[j] = e_index;
    }
    tri_face[i] = arena->add_face(face_vert, f, face_edge_orig);
  }
  r_triangulated->set_faces(tri_face);
  return Mesh(face);
}

static bool apply_mesh_output_to_bmesh(BMesh *bm, Mesh &m_out)
{
  /* Change BMesh bm to have the mesh match m_out. Return true if there were any changes at all.
   *
   * For now, just for testing, just kill the whole old mesh and create the new one.
   * No attempt yet to use proper examples for the new elements so that they inherit the
   * proper attributes.
   * No attempt yet to leave the correct geometric elements selected.
   */

  m_out.populate_vert();

  /* This is not quite the right test for "no changes" but will do for now. */
  if (m_out.vert_size() == bm->totvert && m_out.face_size() == bm->totface) {
    return false;
  }

  /* The BM_ITER_... macros need attention to work in C++. For now, copy the old BMVerts. */
  int totvert_orig = bm->totvert;
  Array<BMVert *> orig_bmv(totvert_orig);
  for (int v = 0; v < bm->totvert; ++v) {
    orig_bmv[v] = BM_vert_at_index(bm, v);
  }
  for (int v = 0; v < totvert_orig; ++v) {
    BM_vert_kill(bm, orig_bmv[v]);
  }

  if (m_out.vert_size() > 0 && m_out.face_size() > 0) {
    Array<BMVert *> new_bmv(m_out.vert_size());
    for (uint v : m_out.vert_index_range()) {
      Vertp vertp = m_out.vert(v);
      float co[3];
      const double3 &d_co = vertp->co;
      for (int i = 0; i < 3; ++i) {
        co[i] = static_cast<float>(d_co[i]);
      }
      new_bmv[v] = BM_vert_create(bm, co, NULL, BM_CREATE_NOP);
    }
    int maxflen = 0;
    for (Facep f : m_out.faces()) {
      maxflen = max_ii(maxflen, static_cast<int>(f->size()));
    }
    Array<BMVert *> face_bmverts(maxflen);
    for (Facep f : m_out.faces()) {
      const Face &face = *f;
      int flen = static_cast<int>(face.size());
      for (int i = 0; i < flen; ++i) {
        Vertp v = face[i];
        uint v_index = m_out.lookup_vert(v);
        BLI_assert(v_index < new_bmv.size());
        face_bmverts[i] = new_bmv[v_index];
      }
      BM_face_create_ngon_verts(bm, face_bmverts.begin(), flen, NULL, BM_CREATE_NOP, true, true);
    }
  }
  return true;
}

static bool bmesh_boolean(BMesh *bm,
                          struct BMLoop *(*looptris)[3],
                          const int looptris_tot,
                          int (*test_fn)(BMFace *f, void *user_data),
                          void *user_data,
                          const bool use_self,
                          const bool UNUSED(use_separate_all),
                          const int boolean_mode)
{
  MArena arena;
  Mesh m_triangulated;
  Mesh m_in = mesh_from_bm(bm, looptris, looptris_tot, &m_triangulated, &arena);
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
      return -1;
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
      if (test_val == 1) {
        return 0;
      }
      return -1;
    };
  }
  bool_optype op = static_cast<bool_optype>(boolean_mode);
  Mesh m_out = boolean_mesh(m_in, op, nshapes, shape_fn, &m_triangulated, &arena);
  bool any_change = apply_mesh_output_to_bmesh(bm, m_out);
  return any_change;
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
  return blender::meshintersect::bmesh_boolean(
      bm, looptris, looptris_tot, test_fn, user_data, use_self, false, boolean_mode);
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
  return blender::meshintersect::bmesh_boolean(bm,
                                               looptris,
                                               looptris_tot,
                                               test_fn,
                                               user_data,
                                               use_self,
                                               use_separate_all,
                                               blender::meshintersect::BOOLEAN_NONE);
}

} /* extern "C" */
