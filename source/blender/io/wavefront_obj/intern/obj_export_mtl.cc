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

#include "BKE_image.h"
#include "BKE_node.h"

#include "BLI_float3.hh"
#include "BLI_map.hh"
#include "BLI_path_util.h"

#include "DNA_material_types.h"
#include "DNA_node_types.h"

#include "NOD_node_tree_ref.hh"

#include "obj_export_mesh.hh"
#include "obj_export_mtl.hh"
#include "obj_import_mtl.hh"

namespace blender::io::obj {

/**
 * Copy a float property of the given type from the bNode to given buffer.
 */
static void copy_property_from_node(MutableSpan<float> r_property,
                                    eNodeSocketDatatype property_type,
                                    const bNode *curr_node,
                                    const char *identifier)
{
  if (!curr_node) {
    return;
  }
  bNodeSocket *socket{nodeFindSocket(curr_node, SOCK_IN, identifier)};
  BLI_assert(socket && socket->type == property_type);
  if (!socket) {
    return;
  }
  switch (property_type) {
    case SOCK_FLOAT: {
      BLI_assert(r_property.size() == 1);
      bNodeSocketValueFloat *socket_def_value = static_cast<bNodeSocketValueFloat *>(
          socket->default_value);
      r_property[0] = socket_def_value->value;
      break;
    }
    case SOCK_RGBA: {
      BLI_assert(r_property.size() == 3);
      bNodeSocketValueRGBA *socket_def_value = static_cast<bNodeSocketValueRGBA *>(
          socket->default_value);
      copy_v3_v3(r_property.data(), socket_def_value->value);
      break;
    }
    case SOCK_VECTOR: {
      BLI_assert(r_property.size() == 3);
      bNodeSocketValueVector *socket_def_value = static_cast<bNodeSocketValueVector *>(
          socket->default_value);
      copy_v3_v3(r_property.data(), socket_def_value->value);
      break;
    }
    default: {
      BLI_assert(0);
      break;
    }
  }
}

/**
 * Collect all the source sockets linked to the destination socket in a destination node.
 */
static void linked_sockets_to_dest_id(Vector<const nodes::OutputSocketRef *> &r_linked_sockets,
                                      const bNode *dest_node,
                                      nodes::NodeTreeRef &node_tree,
                                      StringRefNull dest_socket_id)
{
  if (!dest_node) {
    return;
  }
  Span<const nodes::NodeRef *> object_dest_nodes = node_tree.nodes_by_type(dest_node->idname);
  Span<const nodes::InputSocketRef *> dest_inputs = object_dest_nodes.first()->inputs();
  const nodes::InputSocketRef *dest_socket = nullptr;
  for (const nodes::InputSocketRef *curr_socket : dest_inputs) {
    if (STREQ(curr_socket->bsocket()->identifier, dest_socket_id.data())) {
      dest_socket = curr_socket;
      break;
    }
  }
  if (dest_socket) {
    Span<const nodes::OutputSocketRef *> linked_sockets = dest_socket->directly_linked_sockets();
    r_linked_sockets.resize(linked_sockets.size());
    r_linked_sockets = linked_sockets;
  }
  else {
    r_linked_sockets.clear();
  }
}

/**
 * From a list of sockets, get the parent node which is of the given node type.
 */
static const bNode *get_node_of_type(Span<const nodes::OutputSocketRef *> sockets_list,
                                     int sh_node_type)
{
  for (const nodes::SocketRef *sock : sockets_list) {
    const bNode *curr_node = sock->bnode();
    if (curr_node->typeinfo->type == sh_node_type) {
      return curr_node;
    }
  }
  return nullptr;
}

/**
 * From a texture image shader node, get the image's filepath.
 * Returned filepath is stripped of inital "//". If packed image is found,
 * Only the leaf file name is given.
 */
static const char *get_image_filepath(const bNode *tex_node)
{
  if (tex_node) {
    Image *tex_image = reinterpret_cast<Image *>(tex_node->id);
    if (!tex_image || !BKE_image_has_filepath(tex_image)) {
      return nullptr;
    }
    const char *path = tex_image->filepath;
    if (BKE_image_has_packedfile(tex_image)) {
      /* Put image in the same directory as the MTL file. */
      path = BLI_path_slash_rfind(path) + 1;
      fprintf(stderr,
              "Packed image found:'%s'. Unpack and place the image in the same "
              "directory as the MTL file.\n",
              path);
    }
    if (path[0] == '/' && path[1] == '/') {
      path += 2;
    }
    return path;
  }
  return nullptr;
}

/**
 * Find the Principled-BSDF from the object's node tree & initialise class member.
 */
void MaterialWrap::init_bsdf_node(StringRefNull object_name)
{
  if (!export_mtl_->use_nodes) {
    fprintf(stderr,
            "No Principled-BSDF node found in the shader node tree of: '%s'.\n",
            object_name.data());
    bsdf_node_ = nullptr;
    return;
  }
  ListBase *nodes = &export_mtl_->nodetree->nodes;
  LISTBASE_FOREACH (bNode *, curr_node, nodes) {
    if (curr_node->typeinfo->type == SH_NODE_BSDF_PRINCIPLED) {
      bsdf_node_ = curr_node;
      return;
    }
  }
  fprintf(stderr,
          "No Principled-BSDF node found in the shader node tree of: '%s'.\n",
          object_name.data());
  bsdf_node_ = nullptr;
}

/**
 * Store properties found either in p-BSDF node or `Material` of the object.
 */
void MaterialWrap::store_bsdf_properties(MTLMaterial &r_mtl_mat) const
{
  /* Empirical, and copied from original python exporter. */
  float spec_exponent = (1.0f - export_mtl_->roughness) * 30;
  spec_exponent *= spec_exponent;
  float specular = export_mtl_->spec;
  copy_property_from_node({&specular, 1}, SOCK_FLOAT, bsdf_node_, "Specular");
  float metallic = export_mtl_->metallic;
  copy_property_from_node({&metallic, 1}, SOCK_FLOAT, bsdf_node_, "Metallic");
  float refraction_index = 1.0f;
  copy_property_from_node({&refraction_index, 1}, SOCK_FLOAT, bsdf_node_, "IOR");
  float dissolved = export_mtl_->a;
  copy_property_from_node({&dissolved, 1}, SOCK_FLOAT, bsdf_node_, "Alpha");
  bool transparent = dissolved != 1.0f;

  float3 diffuse_col = {export_mtl_->r, export_mtl_->g, export_mtl_->b};
  copy_property_from_node({diffuse_col, 3}, SOCK_RGBA, bsdf_node_, "Base Color");
  float3 emission_col = {0.0f, 0.0f, 0.0f};
  copy_property_from_node({emission_col, 3}, SOCK_RGBA, bsdf_node_, "Emission");

  /* See https://wikipedia.org/wiki/Wavefront_.obj_file for all possible values of illum. */
  /* Highlight on. */
  int illum = 2;
  if (specular > 0.0f) {
    /* Color on and Ambient on. */
    illum = 1;
  }
  else if (metallic > 0.0f) {
    /* Metallic ~= Reflection. */
    if (transparent) {
      /* Transparency: Refraction on, Reflection: ~~Fresnel off and Ray trace~~ on. */
      illum = 6;
    }
    else {
      /* Reflection on and Ray trace on. */
      illum = 3;
    }
  }
  else if (transparent) {
    /* Transparency: Glass on, Reflection: Ray trace off */
    illum = 9;
  }
  r_mtl_mat.Ns = spec_exponent;
  r_mtl_mat.Ka = {metallic, metallic, metallic};
  r_mtl_mat.Kd = diffuse_col;
  r_mtl_mat.Ks = {specular, specular, specular};
  r_mtl_mat.Ke = emission_col;
  r_mtl_mat.Ni = refraction_index;
  r_mtl_mat.d = dissolved;
  r_mtl_mat.illum = illum;
}

/**
 * Store image texture options and filepaths.
 */
void MaterialWrap::store_image_textures(MTLMaterial &r_mtl_mat) const
{
  /* Need to create a NodeTreeRef for a faster way to find linked sockets, as opposed to
   * looping over all the links in a node tree to match two sockets of our interest. */
  nodes::NodeTreeRef node_tree(export_mtl_->nodetree);

  /* Normal Map Texture has two extra tasks of:
   * - finding a Normal Map node before finding a texture node.
   * - finding "Strength" property of the node for `-bm` option.
   */

  for (Map<const std::string, tex_map_XX>::MutableItem texture_map :
       r_mtl_mat.texture_maps.items()) {
    Vector<const nodes::OutputSocketRef *> linked_sockets;
    const bNode *normal_map_node{nullptr};

    if (texture_map.key == "map_Bump") {
      /* Find sockets linked to destination "Normal" socket in p-bsdf node. */
      linked_sockets_to_dest_id(linked_sockets, bsdf_node_, node_tree, "Normal");
      /* Among the linked sockets, find Normal Map shader node. */
      normal_map_node = get_node_of_type(linked_sockets, SH_NODE_NORMAL_MAP);

      /* Find sockets linked to "Color" socket in normal map node. */
      linked_sockets_to_dest_id(linked_sockets, normal_map_node, node_tree, "Color");
    }
    else {
      /* Find sockets linked to the destination socket of interest, in p-bsdf node. */
      linked_sockets_to_dest_id(
          linked_sockets, bsdf_node_, node_tree, texture_map.value.dest_socket_id);
    }

    /* Among the linked sockets, find Image Texture shader node. */
    const bNode *tex_node{get_node_of_type(linked_sockets, SH_NODE_TEX_IMAGE)};
    if (!tex_node) {
      continue;
    }

    /* Find "Mapping" node if connected to texture node. */
    linked_sockets_to_dest_id(linked_sockets, tex_node, node_tree, "Vector");
    const bNode *mapping = get_node_of_type(linked_sockets, SH_NODE_MAPPING);

    /* Texture transform options. Only translation (origin offset, "-o") and scale
     * ("-o") are supported. */
    float3 map_translation = {0.0f, 0.0f, 0.0f};
    float3 map_scale = {1.0f, 1.0f, 1.0f};
    float normal_map_strength = -1.0f;
    if (normal_map_node) {
      copy_property_from_node({&normal_map_strength, 1}, SOCK_FLOAT, normal_map_node, "Strength");
    }
    copy_property_from_node({map_translation, 3}, SOCK_VECTOR, mapping, "Location");
    copy_property_from_node({map_scale, 3}, SOCK_VECTOR, mapping, "Scale");
    const char *tex_image_filepath = get_image_filepath(tex_node);
    if (!tex_image_filepath) {
      continue;
    }

    texture_map.value.scale = map_scale;
    texture_map.value.translation = map_translation;
    texture_map.value.image_path = tex_image_filepath;
    r_mtl_mat.map_Bump_strength = normal_map_strength;
  }
}

/**
 * Fill the given buffer with MTL material containers.
 */
void MaterialWrap::fill_materials()
{
  for (short i = 0; i < obj_mesh_data_.tot_col(); i++) {
    export_mtl_ = obj_mesh_data_.get_object_material(i + 1);
    if (!export_mtl_) {
      continue;
    }
    r_mtl_materials_[i].name = obj_mesh_data_.get_object_material_name(i + 1);
    init_bsdf_node(obj_mesh_data_.get_object_name());
    store_bsdf_properties(r_mtl_materials_[i]);
    store_image_textures(r_mtl_materials_[i]);
  }
}

/**
 * Append an object's materials to the .mtl file.
 */
MaterialWrap::MaterialWrap(const OBJMesh &obj_mesh_data, Vector<MTLMaterial> &r_mtl_materials)
    : obj_mesh_data_(obj_mesh_data), r_mtl_materials_(r_mtl_materials)
{
  r_mtl_materials.resize(obj_mesh_data.tot_col());
}
}  // namespace blender::io::obj
