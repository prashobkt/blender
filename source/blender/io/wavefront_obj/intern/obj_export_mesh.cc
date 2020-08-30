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

#include "BKE_customdata.h"
#include "BKE_lib_id.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mapping.h"
#include "BKE_object.h"

#include "BLI_listbase.h"
#include "BLI_math.h"

#include "bmesh.h"
#include "bmesh_tools.h"

#include "DEG_depsgraph_query.h"

#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"

#include "obj_export_mesh.hh"

namespace blender::io::obj {
/**
 * Store evaluated object and mesh pointers depending on object type.
 * New meshes are created for supported curves converted to meshes, and triangulated
 * meshes.
 */
OBJMesh::OBJMesh(Depsgraph *depsgraph, const OBJExportParams &export_params, Object *export_object)
    : depsgraph_(depsgraph), export_params_(export_params), export_object_eval_(export_object)
{
  export_object_eval_ = DEG_get_evaluated_object(depsgraph_, export_object);
  export_mesh_eval_ = BKE_object_get_evaluated_mesh(export_object_eval_);
  mesh_eval_needs_free_ = false;

  if (!export_mesh_eval_) {
    /* Curves and nurbs surfaces need a new mesh when they're exported in the form of vertices and
     * edges.
     */
    export_mesh_eval_ = BKE_mesh_new_from_object(depsgraph_, export_object_eval_, true);
    /* Since a new mesh been allocated, it needs to be freed in the destructor. */
    mesh_eval_needs_free_ = true;
  }

  switch (export_object_eval_->type) {
    case OB_SURF:
      ATTR_FALLTHROUGH;
    case OB_MESH: {
      if (export_params_.export_triangulated_mesh) {
        triangulate_mesh_eval();
      }
      break;
    }
    default: {
      break;
    }
  }
  store_world_axes_transform();
}

/**
 * Free new meshes allocated for triangulated meshes, and curves converted to meshes.
 */
OBJMesh::~OBJMesh()
{
  if (mesh_eval_needs_free_) {
    BKE_id_free(NULL, export_mesh_eval_);
  }
  if (poly_smooth_groups_) {
    MEM_freeN(poly_smooth_groups_);
  }
}

/**
 * Triangulate and update OBJMesh evaluated mesh.
 * \note The new mesh created here needs to be freed.
 */
void OBJMesh::triangulate_mesh_eval()
{
  if (export_mesh_eval_->totpoly <= 0) {
    mesh_eval_needs_free_ = false;
    return;
  }
  struct BMeshCreateParams bm_create_params = {false};
  /* If calc_face_normal is false, it triggers BLI_assert(BM_face_is_normal_valid(f)). */
  struct BMeshFromMeshParams bm_convert_params = {true, 0, 0, 0};
  /* Lower threshold where triangulation of a face starts, i.e. a quadrilateral will be
   * triangulated here. */
  const int triangulate_min_verts = 4;

  BMesh *bmesh = BKE_mesh_to_bmesh_ex(export_mesh_eval_, &bm_create_params, &bm_convert_params);
  BM_mesh_triangulate(bmesh,
                      MOD_TRIANGULATE_NGON_BEAUTY,
                      MOD_TRIANGULATE_QUAD_SHORTEDGE,
                      triangulate_min_verts,
                      false,
                      NULL,
                      NULL,
                      NULL);
  export_mesh_eval_ = BKE_mesh_from_bmesh_for_eval_nomain(bmesh, NULL, export_mesh_eval_);
  mesh_eval_needs_free_ = true;
  BM_mesh_free(bmesh);
}

/**
 * Store the product of export axes settings and an object's world transform matrix in
 * world_and_axes_transform[4][4].
 */
void OBJMesh::store_world_axes_transform()
{
  float axes_transform[3][3];
  unit_m3(axes_transform);
  /* -Y-forward and +Z-up are the default Blender axis settings. */
  mat3_from_axis_conversion(OBJ_AXIS_NEGATIVE_Y_FORWARD,
                            OBJ_AXIS_Z_UP,
                            export_params_.forward_axis,
                            export_params_.up_axis,
                            axes_transform);
  mul_m4_m3m4(world_and_axes_transform_, axes_transform, export_object_eval_->obmat);
  /* mul_m4_m3m4 does not copy last row of obmat, i.e. location data. */
  copy_v4_v4(world_and_axes_transform_[3], export_object_eval_->obmat[3]);
}

uint OBJMesh::tot_vertices() const
{
  return export_mesh_eval_->totvert;
}

uint OBJMesh::tot_polygons() const
{
  return export_mesh_eval_->totpoly;
}

uint OBJMesh::tot_uv_vertices() const
{
  return tot_uv_vertices_;
}

uint OBJMesh::tot_edges() const
{
  return export_mesh_eval_->totedge;
}

uint OBJMesh::tot_normals() const
{
  /* Calculate smooth groups first. Or use total polygons if suitable. */
  BLI_assert(poly_smooth_groups_);
  return tot_smooth_groups_ > 0 ? export_mesh_eval_->totvert : export_mesh_eval_->totpoly;
}

/**
 * Total materials in the object to export.
 */
short OBJMesh::tot_col() const
{
  return export_mesh_eval_->totcol;
}

/**
 * Total smooth groups in the object to export.
 */
uint OBJMesh::tot_smooth_groups() const
{
  return tot_smooth_groups_;
}

/**
 * Return smooth group of the polygon at the given index.
 */
int OBJMesh::ith_smooth_group(int poly_index) const
{
  BLI_assert(poly_smooth_groups_);
  return poly_smooth_groups_[poly_index];
}

void OBJMesh::ensure_mesh_normals() const
{
  BKE_mesh_ensure_normals(export_mesh_eval_);
}

void OBJMesh::ensure_mesh_edges() const
{
  BKE_mesh_calc_edges(export_mesh_eval_, true, false);
  BKE_mesh_calc_edges_loose(export_mesh_eval_);
}

/**
 * Calculate smooth groups of a smooth shaded object.
 * \return A polygon aligned array of smooth group numbers or bitflags if export
 * settings specify so.
 */
void OBJMesh::calc_smooth_groups()
{
  int tot_smooth_groups = 0;
  const bool use_bitflags = export_params_.smooth_groups_bitflags;
  poly_smooth_groups_ = BKE_mesh_calc_smoothgroups(export_mesh_eval_->medge,
                                                   export_mesh_eval_->totedge,
                                                   export_mesh_eval_->mpoly,
                                                   export_mesh_eval_->totpoly,
                                                   export_mesh_eval_->mloop,
                                                   export_mesh_eval_->totloop,
                                                   &tot_smooth_groups,
                                                   use_bitflags);
  tot_smooth_groups_ = tot_smooth_groups;
}

/**
 * Return mat_nr-th material of the object.
 */
const Material *OBJMesh::get_object_material(const short mat_nr) const
{
  return BKE_object_material_get(export_object_eval_, mat_nr);
}

const MPoly &OBJMesh::get_ith_poly(const uint i) const
{
  return export_mesh_eval_->mpoly[i];
}

/**
 * Get object name as it appears in the outliner.
 */
const char *OBJMesh::get_object_name() const
{
  return export_object_eval_->id.name + 2;
}

/**
 * Get object's mesh name.
 */
const char *OBJMesh::get_object_data_name() const
{
  return export_mesh_eval_->id.name + 2;
}

/**
 * Get object's material (at the given index) name.
 */
const char *OBJMesh::get_object_material_name(short mat_nr) const
{
  const Material *mat = BKE_object_material_get(export_object_eval_, mat_nr);
  return mat->id.name + 2;
}

/**
 * Calculate coordinates of a vertex at the given index.
 */
void OBJMesh::calc_vertex_coords(const uint vert_index, float r_coords[3]) const
{
  copy_v3_v3(r_coords, export_mesh_eval_->mvert[vert_index].co);
  mul_m4_v3(world_and_axes_transform_, r_coords);
  mul_v3_fl(r_coords, export_params_.scaling_factor);
}

/**
 * Calculate vertex indices of all vertices of a polygon at the given index.
 */
void OBJMesh::calc_poly_vertex_indices(const uint poly_index,
                                       Vector<uint> &r_poly_vertex_indices) const
{
  const MPoly &mpoly = export_mesh_eval_->mpoly[poly_index];
  const MLoop *mloop = &export_mesh_eval_->mloop[mpoly.loopstart];
  r_poly_vertex_indices.resize(mpoly.totloop);
  for (uint loop_index = 0; loop_index < mpoly.totloop; loop_index++) {
    r_poly_vertex_indices[loop_index] = mloop[loop_index].v + 1;
  }
}

/**
 * Store UV vertex coordinates of an object as well as their indices.
 */
void OBJMesh::store_uv_coords_and_indices(Vector<std::array<float, 2>> &r_uv_coords,
                                          Vector<Vector<uint>> &r_uv_indices)
{
  const MPoly *mpoly = export_mesh_eval_->mpoly;
  const MLoop *mloop = export_mesh_eval_->mloop;
  const uint totpoly = export_mesh_eval_->totpoly;
  const uint totvert = export_mesh_eval_->totvert;
  const MLoopUV *mloopuv = static_cast<MLoopUV *>(
      CustomData_get_layer(&export_mesh_eval_->ldata, CD_MLOOPUV));
  if (!mloopuv) {
    tot_uv_vertices_ = 0;
    return;
  }
  const float limit[2] = {STD_UV_CONNECT_LIMIT, STD_UV_CONNECT_LIMIT};

  UvVertMap *uv_vert_map = BKE_mesh_uv_vert_map_create(
      mpoly, mloop, mloopuv, totpoly, totvert, limit, false, false);

  r_uv_indices.resize(totpoly);
  /* At least total vertices of a mesh will be present in its texture map. So
   * reserve minimum space early. */
  r_uv_coords.reserve(totvert);

  tot_uv_vertices_ = 0;
  for (uint vertex_index = 0; vertex_index < totvert; vertex_index++) {
    const UvMapVert *uv_vert = BKE_mesh_uv_vert_map_get_vert(uv_vert_map, vertex_index);
    for (; uv_vert; uv_vert = uv_vert->next) {
      if (uv_vert->separate) {
        tot_uv_vertices_ += 1;
      }
      if (tot_uv_vertices_ == 0) {
        return;
      }
      const uint vertices_in_poly = (uint)mpoly[uv_vert->poly_index].totloop;

      /* Fill up UV vertex's coordinates. */
      r_uv_coords.resize(tot_uv_vertices_);
      const int loopstart = mpoly[uv_vert->poly_index].loopstart;
      const float(&vert_uv_coords)[2] = mloopuv[loopstart + uv_vert->loop_of_poly_index].uv;
      r_uv_coords[tot_uv_vertices_ - 1][0] = vert_uv_coords[0];
      r_uv_coords[tot_uv_vertices_ - 1][1] = vert_uv_coords[1];

      r_uv_indices[uv_vert->poly_index].resize(vertices_in_poly);
      r_uv_indices[uv_vert->poly_index][uv_vert->loop_of_poly_index] = tot_uv_vertices_;
    }
  }
  BKE_mesh_uv_vert_map_free(uv_vert_map);
}

/**
 * Calculate face normal of a polygon at given index.
 */
void OBJMesh::calc_poly_normal(const uint poly_index, float r_poly_normal[3]) const
{
  const MPoly &poly_to_write = export_mesh_eval_->mpoly[poly_index];
  const MLoop &mloop = export_mesh_eval_->mloop[poly_to_write.loopstart];
  const MVert &mvert = *(export_mesh_eval_->mvert);
  BKE_mesh_calc_poly_normal(&poly_to_write, &mloop, &mvert, r_poly_normal);
  mul_mat3_m4_v3(world_and_axes_transform_, r_poly_normal);
}

/**
 * Calculate vertex normal of a vertex at the given index.
 *
 * Should be used when a mesh is shaded smooth.
 */
void OBJMesh::calc_vertex_normal(const uint vert_index, float r_vertex_normal[3]) const
{
  normal_short_to_float_v3(r_vertex_normal, export_mesh_eval_->mvert[vert_index].no);
  mul_mat3_m4_v3(world_and_axes_transform_, r_vertex_normal);
}

/**
 * Calculate face normal indices of all vertices in a polygon.
 */
void OBJMesh::calc_poly_normal_indices(const uint poly_index, Vector<uint> &r_normal_indices) const
{
  r_normal_indices.resize(export_mesh_eval_->mpoly[poly_index].totloop);
  if (tot_smooth_groups_ > 0) {
    const MPoly &mpoly = export_mesh_eval_->mpoly[poly_index];
    const MLoop *mloop = &export_mesh_eval_->mloop[mpoly.loopstart];
    for (int i = 0; i < r_normal_indices.size(); i++) {
      r_normal_indices[i] = mloop[i].v + 1;
    }
  }
  else {
    for (int i = 0; i < r_normal_indices.size(); i++) {
      r_normal_indices[i] = poly_index + 1;
    }
  }
}

/**
 * Find the name of the vertex group with the maximum number of vertices in a poly.
 *
 * If no vertex belongs to any group, returned name is "off".
 * If two or more groups have the same number of vertices (maximum), group name depends on the
 * implementation of std::max_element.
 * If the group corresponding to r_last_vertex_group shows up on current polygon, return nullptr so
 * that caller can skip that group.
 *
 * \param r_last_vertex_group stores the index of the vertex group found in last iteration,
 * indexing into Object->defbase.
 */
const char *OBJMesh::get_poly_deform_group_name(const MPoly &mpoly,
                                                short &r_last_vertex_group) const
{
  const MLoop *mloop = &export_mesh_eval_->mloop[mpoly.loopstart];
  /* Indices of the vector index into deform groups of an object; values are the number of vertex
   * members in one deform group. */
  Vector<int> deform_group_members{};
  const uint tot_deform_groups = BLI_listbase_count(&export_object_eval_->defbase);
  deform_group_members.resize(tot_deform_groups, 0);
  /* Whether at least one vertex in the polygon belongs to any group. */
  bool found_group = false;

  const MDeformVert *dvert_orig = static_cast<MDeformVert *>(
      CustomData_get_layer(&export_mesh_eval_->vdata, CD_MDEFORMVERT));
  if (!dvert_orig) {
    return nullptr;
  }

  const MDeformWeight *curr_weight = nullptr;
  const MDeformVert *dvert = nullptr;
  for (uint loop_index = 0; loop_index < mpoly.totloop; loop_index++) {
    dvert = &dvert_orig[(mloop + loop_index)->v];
    curr_weight = dvert->dw;
    if (curr_weight) {
      bDeformGroup *vertex_group = static_cast<bDeformGroup *>(
          BLI_findlink((&export_object_eval_->defbase), curr_weight->def_nr));
      if (vertex_group) {
        deform_group_members[curr_weight->def_nr] += 1;
        found_group = true;
      }
    }
  }

  if (!found_group) {
    if (r_last_vertex_group == -1) {
      /* No vertex group found in this face, just like in the last iteration. */
      return nullptr;
    }
    /* -1 indicates deform group having no vertices in it. */
    r_last_vertex_group = -1;
    return "off";
  }

  /* Index of the group with maximum vertices. */
  short max_idx = (short)(std::max_element(deform_group_members.begin(),
                                           deform_group_members.end()) -
                          deform_group_members.begin());
  if (max_idx == r_last_vertex_group) {
    /* No need to update the name, this is the same as the group name in the last iteration. */
    return nullptr;
  }

  r_last_vertex_group = max_idx;
  const bDeformGroup &vertex_group = *(
      static_cast<bDeformGroup *>(BLI_findlink(&export_object_eval_->defbase, max_idx)));

  return vertex_group.name;
}

/**
 * Only for curve converted to meshes and primitive circle: calculate vertex indices of one edge.
 */
Array<int, 2> OBJMesh::calc_edge_vert_indices(const uint edge_index) const
{
  const MEdge &edge = export_mesh_eval_->medge[edge_index];
  if (edge.flag & ME_LOOSEEDGE) {
    return {edge.v1 + 1, edge.v2 + 1};
  }
  return {};
}
}  // namespace blender::io::obj
