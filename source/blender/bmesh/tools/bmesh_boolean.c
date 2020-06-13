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

#include "MEM_guardedalloc.h"

#include "BLI_boolean.h"
#include "BLI_math.h"

#include "bmesh.h"
#include "bmesh_boolean.h"

/* Make a a triangle mesh for input to the Boolean function.
 * We take only take triangles from faces f for which test_fn(f, user_data) returns 'side'.
 * Caller must call free_trimesh_input when done with the returned value. */
static Boolean_trimesh_input *trimesh_input_from_bm(BMesh *bm,
                                                    struct BMLoop *(*looptris)[3],
                                                    const int looptris_tot,
                                                    int (*(test_fn))(BMFace *f, void *user_data),
                                                    void *user_data,
                                                    int side)
{
  int i, in_v_index, in_t_index, bmv_index, looptri_index, tot_in_vert, tot_in_tri;
  Boolean_trimesh_input *trimesh;
  int *bmv_to_v, *v_to_bmv, *t_to_looptri;
  BMFace *f;

  BM_mesh_elem_index_ensure(bm, BM_VERT | BM_FACE);
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_FACE);
  trimesh = MEM_mallocN(sizeof(*trimesh), __func__);
  bmv_to_v = MEM_malloc_arrayN(bm->totvert, sizeof(int), __func__);
  v_to_bmv = MEM_malloc_arrayN(bm->totvert, sizeof(int), __func__);
  t_to_looptri = MEM_malloc_arrayN(looptris_tot, sizeof(int), __func__);
  for (i = 0; i < bm->totvert; i++) {
    bmv_to_v[i] = -1;
    v_to_bmv[i] = -1;
  }
  in_v_index = 0;
  tot_in_tri = 0;
  for (looptri_index = 0; looptri_index < looptris_tot; looptri_index++) {
    f = looptris[looptri_index][0]->f;
    if (test_fn(f, user_data) == side) {
      t_to_looptri[tot_in_tri++] = looptri_index;
      for (i = 0; i < 3; i++) {
        bmv_index = BM_elem_index_get(looptris[looptri_index][i]->v);
        if (bmv_to_v[bmv_index] == -1) {
          v_to_bmv[in_v_index] = bmv_index;
          bmv_to_v[bmv_index] = in_v_index;
          in_v_index++;
        }
      }
    }
  }
  tot_in_vert = in_v_index;
  trimesh->vert_len = tot_in_vert;
  trimesh->tri_len = tot_in_tri;
  trimesh->vert_coord = MEM_malloc_arrayN(tot_in_vert, sizeof(trimesh->vert_coord[0]), __func__);
  trimesh->tri = MEM_malloc_arrayN(tot_in_tri, sizeof(trimesh->tri[0]), __func__);
  for (in_v_index = 0; in_v_index < tot_in_vert; in_v_index++) {
    bmv_index = v_to_bmv[in_v_index];
    BMVert *bmv = BM_vert_at_index(bm, bmv_index);
    copy_v3_v3(trimesh->vert_coord[in_v_index], bmv->co);
  }
  for (in_t_index = 0; in_t_index < tot_in_tri; in_t_index++) {
    looptri_index = t_to_looptri[in_t_index];
    for (i = 0; i < 3; i++) {
      bmv_index = BM_elem_index_get(looptris[looptri_index][i]->v);
      BLI_assert(bmv_to_v[bmv_index] != -1);
      trimesh->tri[in_t_index][i] = bmv_to_v[bmv_index];
    }
  }
  MEM_freeN(bmv_to_v);
  MEM_freeN(v_to_bmv);
  MEM_freeN(t_to_looptri);

  return trimesh;
}

static void free_trimesh_input(Boolean_trimesh_input *in)
{
  MEM_freeN(in->vert_coord);
  MEM_freeN(in->tri);
  MEM_freeN(in);
}

static void apply_trimesh_output_to_bmesh(BMesh *bm, Boolean_trimesh_output *out)
{
  /* For now, for testing, just create new BMesh elements for returned subdivided mesh and kill old
   * mesh. */
  int v, t;
  BMIter iter;
  BMVert *bmv, *bmv_next;

  BM_ITER_MESH_MUTABLE (bmv, bmv_next, &iter, bm, BM_VERTS_OF_MESH) {
    BM_vert_kill(bm, bmv);
  }

  if (out->vert_len > 0 && out->tri_len > 0) {
    BMVert **new_bmv = MEM_malloc_arrayN(out->vert_len, sizeof(BMVert *), __func__);
    for (v = 0; v < out->vert_len; v++) {
      new_bmv[v] = BM_vert_create(bm, out->vert_coord[v], NULL, BM_CREATE_NOP);
    }
    for (t = 0; t < out->tri_len; t++) {
      BMVert *v0 = new_bmv[out->tri[t][0]];
      BMVert *v1 = new_bmv[out->tri[t][1]];
      BMVert *v2 = new_bmv[out->tri[t][2]];
      BM_face_create_quad_tri(bm, v0, v1, v2, NULL, NULL, BM_CREATE_NOP);
    }
  }
}

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
  Boolean_trimesh_input *in_a = trimesh_input_from_bm(
      bm, looptris, looptris_tot, test_fn, user_data, 0);
  Boolean_trimesh_input *in_b = NULL;
  if (!use_self) {
    in_b = trimesh_input_from_bm(bm, looptris, looptris_tot, test_fn, user_data, 1);
    /* The old boolean code does DIFFERENCE as b - a, which is weird, so adapt here
     * so the rest of the code is sensible.
     */
    SWAP(Boolean_trimesh_input *, in_a, in_b);
  }
  Boolean_trimesh_output *out = BLI_boolean_trimesh(in_a, in_b, boolean_mode);
  BLI_assert(out != NULL);
  bool intersections_found = out->tri_len != in_a->tri_len + (in_b ? in_b->tri_len : 0) ||
                             out->vert_len != in_a->vert_len + (in_b ? in_b->vert_len : 0);
  apply_trimesh_output_to_bmesh(bm, out);
  free_trimesh_input(in_a);
  if (in_b) {
    free_trimesh_input(in_b);
  }
  BLI_boolean_trimesh_free(out);
  return intersections_found;
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
                           const bool UNUSED(use_separate_all))
{
  Boolean_trimesh_input *in_a = trimesh_input_from_bm(
      bm, looptris, looptris_tot, test_fn, user_data, 0);
  Boolean_trimesh_input *in_b = NULL;
  if (!use_self) {
    in_b = trimesh_input_from_bm(bm, looptris, looptris_tot, test_fn, user_data, 1);
  }
  Boolean_trimesh_output *out = BLI_boolean_trimesh(in_a, in_b, BOOLEAN_NONE);
  BLI_assert(out != NULL);
  bool intersections_found = out->tri_len != in_a->tri_len + (in_b ? in_b->tri_len : 0) ||
                             out->vert_len != in_a->vert_len + (in_b ? in_b->vert_len : 0);
  apply_trimesh_output_to_bmesh(bm, out);
  free_trimesh_input(in_a);
  if (in_b) {
    free_trimesh_input(in_b);
  }
  BLI_boolean_trimesh_free(out);
  return intersections_found;
}
