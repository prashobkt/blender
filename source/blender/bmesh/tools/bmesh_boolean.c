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

/* Caller must call free_trimesh_input when done with the returned value. */
static Boolean_trimesh_input *trimesh_input_from_bm(BMesh *bm,
                                                    struct BMLoop *(*looptris)[3],
                                                    const int looptris_tot,
                                                    int (*(test_fn))(BMFace *f, void *user_data),
                                                    void *user_data)
{
  int v, t, i;

  BM_mesh_elem_index_ensure(bm, BM_VERT | BM_FACE);
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_FACE);
  Boolean_trimesh_input *trimesh = MEM_mallocN(sizeof(*trimesh), __func__);
  trimesh->vert_len = bm->totvert;
  trimesh->tri_len = looptris_tot;
  trimesh->vert_coord = MEM_malloc_arrayN(bm->totvert, sizeof(trimesh->vert_coord[0]), __func__);
  trimesh->tri = MEM_malloc_arrayN(looptris_tot, sizeof(trimesh->tri[0]), __func__);
  for (v = 0; v < bm->totvert; v++) {
    BMVert *bmv = BM_vert_at_index(bm, v);
    copy_v3_v3(trimesh->vert_coord[v], bmv->co);
  }
  for (t = 0; t < looptris_tot; t++) {
    BMFace *f = looptris[t][0]->f;
    if (test_fn(f, user_data) == -1) {
      continue;
    }
    for (i = 0; i < 3; ++i) {
      BMLoop *l = looptris[t][i];
      trimesh->tri[t][i] = BM_elem_index_get(l->v);
    }
  }
  return trimesh;
}

static void free_trimesh_input(Boolean_trimesh_input *in)
{
  MEM_freeN(in->vert_coord);
  MEM_freeN(in->tri);
  MEM_freeN(in);
}

bool BM_mesh_boolean(BMesh *bm,
                     struct BMLoop *(*looptris)[3],
                     const int looptris_tot,
                     int (*test_fn)(BMFace *f, void *user_data),
                     void *user_data,
                     const bool use_self,
                     const int boolean_mode)
{
  BLI_assert(use_self);
  Boolean_trimesh_input *in = trimesh_input_from_bm(
      bm, looptris, looptris_tot, test_fn, user_data);
  Boolean_trimesh_output *out = BLI_boolean_trimesh(in, boolean_mode);
  BLI_assert(out != NULL);
  free_trimesh_input(in);
  BLI_boolean_trimesh_free(out);
  return false;
}

bool BM_mesh_boolean_knife(BMesh *bm,
                           struct BMLoop *(*looptris)[3],
                           const int looptris_tot,
                           int (*test_fn)(BMFace *f, void *user_data),
                           void *user_data,
                           const bool UNUSED(use_self),
                           const bool UNUSED(use_separate_all))
{
  int v, t;

  Boolean_trimesh_input *in = trimesh_input_from_bm(
      bm, looptris, looptris_tot, test_fn, user_data);
  Boolean_trimesh_output *out = BLI_boolean_trimesh(in, BOOLEAN_NONE);

  /* For now, for testing, just create new BMesh elements for returned subdivided mesh. */
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

  free_trimesh_input(in);
  BLI_boolean_trimesh_free(out);
  return false;
}
