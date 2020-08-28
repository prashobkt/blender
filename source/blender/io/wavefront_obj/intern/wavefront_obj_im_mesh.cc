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

#include "DNA_scene_types.h" /* For eVGroupSelect. */

#include "BKE_customdata.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_object.h"
#include "BKE_object_deform.h"

#include "BLI_map.hh"
#include "BLI_set.hh"
#include "BLI_vector_set.hh"

#include "bmesh.h"
#include "bmesh_operator_api.h"
#include "bmesh_tools.h"

#include "DNA_customdata_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"

#include "mesh_utils.hh"
#include "wavefront_obj_im_mesh.hh"

namespace blender::io::obj {

MeshFromGeometry::~MeshFromGeometry()
{
  if (mesh_object_ || blender_mesh_) {
    /* Move the object to own it. */
    mesh_object_.reset();
    blender_mesh_.reset();
    BLI_assert(0);
  }
}

void MeshFromGeometry::create_mesh(Main *bmain,
                                   const Map<std::string, std::unique_ptr<MTLMaterial>> &materials)
{
  std::string ob_name{mesh_geometry_.get_geometry_name()};
  if (ob_name.empty()) {
    ob_name = "Untitled";
  }
  Vector<FaceElement> new_faces;
  Set<std::pair<int, int>> fgon_edges;
  const auto [removed_faces, removed_loops]{tessellate_polygons(new_faces, fgon_edges)};

  const int64_t tot_verts_object{mesh_geometry_.tot_verts()};
  /* Total explicitly imported edges, not the ones belonging the polygons to be created. */
  const int64_t tot_edges{mesh_geometry_.tot_edges()};
  const int64_t tot_face_elems{mesh_geometry_.tot_face_elems() - removed_faces + new_faces.size()};
  const int64_t tot_loops{mesh_geometry_.tot_loops() - removed_loops + 3 * new_faces.size()};

  blender_mesh_.reset(
      BKE_mesh_new_nomain(tot_verts_object, tot_edges, 0, tot_loops, tot_face_elems));
  mesh_object_.reset(BKE_object_add_only_object(bmain, OB_MESH, ob_name.c_str()));
  mesh_object_->data = BKE_object_obdata_add_from_type(bmain, OB_MESH, ob_name.c_str());

  create_vertices();
  new_faces.extend(mesh_geometry_.face_elements());
  create_polys_loops(new_faces);
  create_edges();
  create_uv_verts();
  create_materials(bmain, materials);

  bool verbose_validate = false;
#ifdef DEBUG
  verbose_validate = true;
#endif
  BKE_mesh_validate(blender_mesh_.get(), verbose_validate, false);
#if 0
  /* TODO ankitm Check if it should be executed or not. */
  add_custom_normals();
#endif
  /* Un-tessellate unnecesarily triangulated n-gons. */
  dissolve_edges(fgon_edges);

  BKE_mesh_nomain_to_mesh(blender_mesh_.release(),
                          static_cast<Mesh *>(mesh_object_->data),
                          mesh_object_.get(),
                          &CD_MASK_EVERYTHING,
                          true);
}

/**
 * Tessellate potentially invalid polygons and fill the
 */
std::pair<int64_t, int64_t> MeshFromGeometry::tessellate_polygons(
    Vector<FaceElement> &r_new_faces, Set<std::pair<int, int>> &fgon_edges)
{
  int64_t removed_faces = 0;
  int64_t removed_loops = 0;
  for (const FaceElement &curr_face : mesh_geometry_.face_elements()) {
    if (curr_face.shaded_smooth && true) {  // should be valid/invalid
      return {removed_faces, removed_loops};
    }
    Vector<int> face_vert_indices;
    Vector<int> face_uv_indices;
    Vector<int> face_normal_indices;
    face_vert_indices.reserve(curr_face.face_corners.size());
    face_uv_indices.reserve(curr_face.face_corners.size());
    face_normal_indices.reserve(curr_face.face_corners.size());
    for (const FaceCorner &corner : curr_face.face_corners) {
      face_vert_indices.append(corner.vert_index);
      face_normal_indices.append(corner.vertex_normal_index);
      face_uv_indices.append(corner.uv_vert_index);
      removed_loops++;
    }

    Vector<Vector<int>> new_polygon_indices = ngon_tessellate(global_vertices_.vertices,
                                                              face_vert_indices);
    for (Span<int> triangle : new_polygon_indices) {
      r_new_faces.append({curr_face.vertex_group,
                          curr_face.shaded_smooth,
                          {{face_vert_indices[triangle[0]],
                            face_uv_indices[triangle[0]],
                            face_normal_indices[triangle[0]]},
                           {face_vert_indices[triangle[1]],
                            face_uv_indices[triangle[1]],
                            face_normal_indices[triangle[1]]},
                           {face_vert_indices[triangle[2]],
                            face_uv_indices[triangle[2]],
                            face_normal_indices[triangle[2]]}}});
    }
    if (new_polygon_indices.size() > 1) {
      Set<std::pair<int, int>> edge_users;
      for (Span<int> triangle : new_polygon_indices) {
        int prev_vidx = face_vert_indices[triangle.last()];
        for (const int ngidx : triangle) {
          int vidx = face_vert_indices[ngidx];
          if (vidx == prev_vidx) {
            continue;
          }
          std::pair<int, int> edge_key = {min_ii(prev_vidx, vidx), max_ii(prev_vidx, vidx)};
          prev_vidx = vidx;
          if (edge_users.contains(edge_key)) {
            fgon_edges.add(edge_key);
          }
          else {
            edge_users.add(edge_key);
          }
        }
      }
    }
    removed_faces++;
  }

  return {removed_faces, removed_loops};
}

void MeshFromGeometry::dissolve_edges(const Set<std::pair<int, int>> &fgon_edges)
{
  if (fgon_edges.is_empty()) {
    return;
  }
  struct BMeshCreateParams bm_create_params = {true};
  /* If calc_face_normal is false, it triggers BLI_assert(BM_face_is_normal_valid(f)). */
  struct BMeshFromMeshParams bm_convert_params = {true, 0, 0, 0};

  BMesh *bmesh = BKE_mesh_to_bmesh_ex(blender_mesh_.get(), &bm_create_params, &bm_convert_params);

  Vector<Array<BMVert *, 2>> edges;
  edges.reserve(fgon_edges.size());
  BM_mesh_elem_table_ensure(bmesh, BM_VERT);
  for (const std::pair<int, int> &edge : fgon_edges) {
    edges.append({BM_vert_at_index(bmesh, edge.first), BM_vert_at_index(bmesh, edge.second)});
  }

  BMO_op_callf(bmesh,
               BMO_FLAG_DEFAULTS,
               "dissolve_edges edges=%eb use_verts=%b use_face_split=%b",
               edges.data(),
               false,
               false);
  unique_mesh_ptr to_free = std::move(blender_mesh_);
  blender_mesh_.reset(BKE_mesh_from_bmesh_for_eval_nomain(bmesh, NULL, to_free.get()));
  to_free.reset();
  BM_mesh_free(bmesh);
}

void MeshFromGeometry::create_vertices()
{
  const int64_t tot_verts_object{mesh_geometry_.tot_verts()};
  for (int i = 0; i < tot_verts_object; ++i) {
    if (mesh_geometry_.vertex_index(i) < global_vertices_.vertices.size()) {
      copy_v3_v3(blender_mesh_->mvert[i].co,
                 global_vertices_.vertices[mesh_geometry_.vertex_index(i)]);
      if (i > mesh_geometry_.tot_normals()) {
        /* Silence debug warning in mesh validate. */
        normal_float_to_short_v3(blender_mesh_->mvert[i].no, (float[3]){1.0f, 1.0f, 1.0f});
      }
    }
    else {
      std::cerr << "Vertex index:" << mesh_geometry_.vertex_index(i)
                << " larger than total vertices:" << global_vertices_.vertices.size() << " ."
                << std::endl;
    }
  }
}

/**
 * Create polygons for the Mesh, set smooth shading flag, deform group name, assigned material
 * also.
 *
 * It must recieve all polygons to be added to the mesh. Remove holes from polygons before
 * calling this.
 */
void MeshFromGeometry::create_polys_loops(Span<FaceElement> all_faces)
{
  /* Will not be used if vertex groups are not imported. */
  blender_mesh_->dvert = nullptr;
  float weight = 0.0f;
  if (mesh_geometry_.tot_verts() && mesh_geometry_.use_vertex_groups()) {
    blender_mesh_->dvert = static_cast<MDeformVert *>(CustomData_add_layer(
        &blender_mesh_->vdata, CD_MDEFORMVERT, CD_CALLOC, nullptr, mesh_geometry_.tot_verts()));
    weight = 1.0f / mesh_geometry_.tot_verts();
  }
  else {
    UNUSED_VARS(weight);
  }

  /* Do not remove elements from the VectorSet since order of insertion is required.
   * StringRef is fine since per-face deform group name outlives the VectorSet. */
  VectorSet<StringRef> group_names;
  const int64_t tot_face_elems{blender_mesh_->totpoly};
  int tot_loop_idx = 0;

  for (int poly_idx = 0; poly_idx < tot_face_elems; ++poly_idx) {
    const FaceElement &curr_face = all_faces[poly_idx];
    if (curr_face.face_corners.size() < 3) {
      /* Don't add single vertex face, or edges. */
      std::cerr << "Face with less than 3 vertices found, skipping." << std::endl;
      continue;
    }

    MPoly &mpoly = blender_mesh_->mpoly[poly_idx];
    mpoly.totloop = curr_face.face_corners.size();
    mpoly.loopstart = tot_loop_idx;
    if (curr_face.shaded_smooth) {
      mpoly.flag |= ME_SMOOTH;
    }

    for (const FaceCorner &curr_corner : curr_face.face_corners) {
      MLoop &mloop = blender_mesh_->mloop[tot_loop_idx];
      tot_loop_idx++;
      mloop.v = curr_corner.vert_index;
      /* Set normals to silence mesh validate zero normals warnings. */
      if (curr_corner.vertex_normal_index >= 0 &&
          curr_corner.vertex_normal_index < global_vertices_.vertex_normals.size()) {
        normal_float_to_short_v3(blender_mesh_->mvert[mloop.v].no,
                                 global_vertices_.vertex_normals[curr_corner.vertex_normal_index]);
      }

      if (blender_mesh_->dvert) {
        /* Iterating over mloop results in finding the same vertex multiple times.
         * Another way is to allocate memory for dvert while creating vertices and fill them here.
         */
        MDeformVert &def_vert = blender_mesh_->dvert[mloop.v];
        if (!def_vert.dw) {
          def_vert.dw = static_cast<MDeformWeight *>(
              MEM_callocN(sizeof(MDeformWeight), "OBJ Import Deform Weight"));
        }
        /* Every vertex in a face is assigned the same deform group. */
        int64_t pos_name{group_names.index_of_try(curr_face.vertex_group)};
        if (pos_name == -1) {
          group_names.add_new(curr_face.vertex_group);
          pos_name = group_names.size() - 1;
        }
        BLI_assert(pos_name >= 0);
        /* Deform group number (def_nr) must behave like an index into the names' list. */
        *(def_vert.dw) = {static_cast<unsigned int>(pos_name), weight};
      }
    }
  }

  if (!blender_mesh_->dvert) {
    return;
  }
  /* Add deform group(s) to the object's defbase. */
  for (StringRef name : group_names) {
    /* Adding groups in this order assumes that def_nr is an index into the names' list. */
    BKE_object_defgroup_add_name(mesh_object_.get(), name.data());
  }
}

/**
 * Add explicitly imported OBJ edges to the mesh.
 */
void MeshFromGeometry::create_edges()
{
  const int64_t tot_edges{mesh_geometry_.tot_edges()};
  for (int i = 0; i < tot_edges; ++i) {
    const MEdge &src_edge = mesh_geometry_.edges()[i];
    MEdge &dst_edge = blender_mesh_->medge[i];
    BLI_assert(src_edge.v1 < mesh_geometry_.tot_verts() &&
               src_edge.v2 < mesh_geometry_.tot_verts());
    dst_edge.v1 = src_edge.v1;
    dst_edge.v2 = src_edge.v2;
    dst_edge.flag = ME_LOOSEEDGE;
  }

  /* Set argument `update` to true so that existing, explicitly imported edges can be merged
   * with the new ones created from polygons. */
  BKE_mesh_calc_edges(blender_mesh_.get(), true, false);
  BKE_mesh_calc_edges_loose(blender_mesh_.get());
}

/**
 * Add UV layer and vertices to the Mesh.
 */
void MeshFromGeometry::create_uv_verts()
{
  if (global_vertices_.uv_vertices.size() <= 0) {
    return;
  }
  MLoopUV *mluv_dst = static_cast<MLoopUV *>(CustomData_add_layer(
      &blender_mesh_->ldata, CD_MLOOPUV, CD_DEFAULT, nullptr, mesh_geometry_.tot_loops()));
  int tot_loop_idx = 0;

  for (const FaceElement &curr_face : mesh_geometry_.face_elements()) {
    for (const FaceCorner &curr_corner : curr_face.face_corners) {
      if (curr_corner.uv_vert_index >= 0 &&
          curr_corner.uv_vert_index < global_vertices_.uv_vertices.size()) {
        const float2 &mluv_src = global_vertices_.uv_vertices[curr_corner.uv_vert_index];
        copy_v2_v2(mluv_dst[tot_loop_idx].uv, mluv_src);
        tot_loop_idx++;
      }
    }
  }
}

/**
 * Add materials and the nodetree to the Mesh Object.
 */
void MeshFromGeometry::create_materials(
    Main *bmain, const Map<std::string, std::unique_ptr<MTLMaterial>> &materials)
{
  for (StringRef material_name : mesh_geometry_.material_names()) {
    if (!materials.contains_as(material_name)) {
      std::cerr << "Material named '" << material_name << "' not found in material library."
                << std::endl;
      continue;
    }
    const MTLMaterial &curr_mat = *materials.lookup_as(material_name).get();
    BKE_object_material_slot_add(bmain, mesh_object_.get());
    Material *mat = BKE_material_add(bmain, material_name.data());
    BKE_object_material_assign(
        bmain, mesh_object_.get(), mat, mesh_object_.get()->totcol, BKE_MAT_ASSIGN_USERPREF);

    ShaderNodetreeWrap mat_wrap{bmain, curr_mat};
    mat->use_nodes = true;
    mat->nodetree = mat_wrap.get_nodetree();
  }
}

/**
 * Needs more clarity about what is expected in the viewport if the function works.
 */
void MeshFromGeometry::add_custom_normals()
{
  const int64_t tot_loop_normals{mesh_geometry_.tot_normals()};
  float(*loop_normals)[3] = static_cast<float(*)[3]>(
      MEM_malloc_arrayN(tot_loop_normals, sizeof(float[3]), __func__));

  for (int index = 0; index < tot_loop_normals; index++) {
    copy_v3_v3(loop_normals[index],
               global_vertices_.vertex_normals[mesh_geometry_.vertex_normal_index(index)]);
  }

  blender_mesh_->flag |= ME_AUTOSMOOTH;
  BKE_mesh_set_custom_normals(blender_mesh_.get(), loop_normals);
  for (int i = 0; i < tot_loop_normals; i++) {
    print_v3("", loop_normals[i]);
  }
  MEM_freeN(loop_normals);
}
}  // namespace blender::io::obj
