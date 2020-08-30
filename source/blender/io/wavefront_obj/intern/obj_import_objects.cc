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

#include "BKE_collection.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_build.h"

#include "DNA_collection_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "obj_import_objects.hh"

namespace blender::io::obj {

eGeometryType Geometry::get_geom_type() const
{
  return geom_type_;
}

/**
 * Use very rarely. Only when it is guaranteed that the
 * type originally set is wrong.
 */
void Geometry::set_geom_type(const eGeometryType new_type)
{
  geom_type_ = new_type;
}

StringRef Geometry::get_geometry_name() const
{
  return geometry_name_;
}

void Geometry::set_geometry_name(StringRef new_name)
{
  geometry_name_ = std::string(new_name);
}

/**
 * Returns an index that ranges from zero to total coordinates in the
 * global list of vertices.
 */
int64_t Geometry::vertex_index(const int64_t index) const
{
  return vertex_indices_[index];
}

int64_t Geometry::tot_verts() const
{
  return vertex_indices_.size();
}

Span<FaceElement> Geometry::face_elements() const
{
  return face_elements_;
}

const FaceElement &Geometry::ith_face_element(const int64_t index) const
{
  return face_elements_[index];
}

int64_t Geometry::tot_face_elems() const
{
  return face_elements_.size();
}

bool Geometry::use_vertex_groups() const
{
  return use_vertex_groups_;
}

Span<MEdge> Geometry::edges() const
{
  return edges_;
}

int64_t Geometry::tot_edges() const
{
  return edges_.size();
}

int Geometry::tot_loops() const
{
  return tot_loops_;
}

int64_t Geometry::vertex_normal_index(const int64_t index) const
{
  return vertex_normal_indices_[index];
}

int64_t Geometry::tot_normals() const
{
  return vertex_normal_indices_.size();
}

Span<std::string> Geometry::material_names() const
{
  return material_names_;
}

const NurbsElement &Geometry::nurbs_elem() const
{
  return nurbs_element_;
}

const std::string &Geometry::group() const
{
  return nurbs_element_.group_;
}

/**
 * Create a collection to store all imported objects.
 */
OBJImportCollection::OBJImportCollection(Main *bmain, Scene *scene) : bmain_(bmain), scene_(scene)
{
  obj_import_collection_ = BKE_collection_add(
      bmain_, scene_->master_collection, "OBJ import collection");
}

/**
 * Add the given Mesh/Curve object to the OBJ import collection.
 */
void OBJImportCollection::add_object_to_collection(unique_object_ptr b_object)
{
  BKE_collection_object_add(bmain_, obj_import_collection_, b_object.release());
  id_fake_user_set(&obj_import_collection_->id);
  DEG_id_tag_update(&obj_import_collection_->id, ID_RECALC_COPY_ON_WRITE);
  DEG_relations_tag_update(bmain_);
}

}  // namespace blender::io::obj
