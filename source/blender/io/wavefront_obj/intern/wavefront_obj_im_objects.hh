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

#include "BKE_lib_id.h"

#include "BLI_array.hh"
#include "BLI_float2.hh"
#include "BLI_float3.hh"
#include "BLI_vector.hh"

#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"

#include "wavefront_obj_ex_file_writer.hh"

namespace blender::io::obj {

/**
 * List of all vertex and UV vertex coordinates in an OBJ file accessible to any
 * Geometry instance at any time.
 */
struct GlobalVertices {
  Vector<float3> vertices{};
  Vector<float2> uv_vertices{};
  Vector<float3> vertex_normals;
};

/**
 * Keeps track of the vertices that belong to other Geometries.
 * Needed only for MLoop.v and MEdge.v1 which needs vertex indices ranging from (0 to total
 * vertices in the mesh) as opposed to the other OBJ indices ranging from (0 to total vertices
 * in the global list).
 */
struct VertexIndexOffset {
 private:
  int offset_ = 0;

 public:
  void set_index_offset(const int64_t total_vertices)
  {
    offset_ = total_vertices;
  }
  int64_t get_index_offset() const
  {
    return offset_;
  }
};

/**
 * A face's corner in an OBJ file. In Blender, it translates to a mloop vertex.
 */
struct FaceCorner {
  /* These indices range from zero to total vertices in the OBJ file. */
  int vert_index;
  /* -1 is to indicate abscense of UV vertices. Only < 0 condition should be checked since
   * it can be less than -1 too. */
  int uv_vert_index = -1;
  int vertex_normal_index;
};

struct FaceElement {
  std::string vertex_group{};
  bool shaded_smooth = false;
  Vector<FaceCorner> face_corners;
  /* Not read from the OBJ file. Set to true for potentially invalid polygons. */
  bool invalid = false;
};

/**
 * Contains data for one single NURBS curve in the OBJ file.
 */
struct NurbsElement {
  /**
   * For curves, groups may be used to specify multiple splines in the same curve object.
   * It may also serve as the name of the curve if not specified explicitly.
   */
  std::string group_{};
  int degree = 0;
  /**
   * Indices into the global list of vertex coordinates. Must be non-negative.
   */
  Vector<int> curv_indices{};
  /* Values in the parm u/v line in a curve definition. */
  Vector<float> parm{};
};

enum eGeometryType {
  GEOM_MESH = OB_MESH,
  GEOM_CURVE = OB_CURVE,
};

class Geometry {
 private:
  eGeometryType geom_type_ = GEOM_MESH;
  std::string geometry_name_{};
  Vector<std::string> material_names_{};
  /**
   * Indices in the vector range from zero to total vertices in a geomery.
   * Values range from zero to total coordinates in the global list.
   */
  Vector<int> vertex_indices_;
  Vector<int> vertex_normal_indices_;
  /** Edges written in the file in addition to (or even without polygon) elements. */
  Vector<MEdge> edges_{};
  Vector<FaceElement> face_elements_{};
  bool use_vertex_groups_ = false;
  NurbsElement nurbs_element_;
  int tot_loops_ = 0;

 public:
  Geometry(eGeometryType type, StringRef ob_name)
      : geom_type_(type), geometry_name_(std::string(ob_name)){};

  eGeometryType get_geom_type() const;
  void set_geom_type(const eGeometryType new_type);
  StringRef get_geometry_name() const;
  void set_geometry_name(StringRef new_name);

  int64_t vertex_index(const int64_t index) const;
  int64_t tot_verts() const;
  Span<FaceElement> face_elements() const;
  const FaceElement &ith_face_element(const int64_t index) const;
  int64_t tot_face_elems() const;
  bool use_vertex_groups() const;
  Span<MEdge> edges() const;
  int64_t tot_edges() const;
  int tot_loops() const;
  int64_t vertex_normal_index(const int64_t index) const;
  int64_t tot_normals() const;

  Span<std::string> material_names() const;

  const NurbsElement &nurbs_elem() const;
  const std::string &group() const;

  /* Parser class edits all the parameters of the Geometry class. */
  friend class OBJParser;
};

struct UniqueObjectDeleter {
  void operator()(Object *object)
  {
    BKE_id_free(NULL, object);
  }
};

using unique_object_ptr = std::unique_ptr<Object, UniqueObjectDeleter>;

class OBJImportCollection {
 private:
  Main *bmain_;
  Scene *scene_;
  /**
   * The collection that holds all the imported objects.
   */
  Collection *obj_import_collection_;

 public:
  OBJImportCollection(Main *bmain, Scene *scene);

  void add_object_to_collection(unique_object_ptr b_object);
};
}  // namespace blender::io::obj
