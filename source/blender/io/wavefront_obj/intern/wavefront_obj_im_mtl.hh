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

#include "MEM_guardedalloc.h"

#include "DNA_node_types.h"

#include "BLI_float3.hh"
#include "BLI_map.hh"
#include "BLI_string_ref.hh"

namespace blender::io::obj {
/**
 * Used for storing parameters for all kinds of texture maps from MTL file.
 */
struct tex_map_XX {
  tex_map_XX(StringRef to_socket_id) : dest_socket_id(to_socket_id){};

  const std::string dest_socket_id{};
  float3 translation = {0.0f, 0.0f, 0.0f};
  float3 scale = {1.0f, 1.0f, 1.0f};
  std::string image_path{};
  std::string mtl_dir_path;
};

/**
 * Store material data parsed from MTL file.
 */
struct MTLMaterial {
  MTLMaterial()
  {
    texture_maps.add("map_Kd", tex_map_XX("Base Color"));
    texture_maps.add("map_Ks", tex_map_XX("Specular"));
    texture_maps.add("map_Ns", tex_map_XX("Roughness"));
    texture_maps.add("map_d", tex_map_XX("Alpha"));
    texture_maps.add("map_refl", tex_map_XX("Metallic"));
    texture_maps.add("map_Ke", tex_map_XX("Emission"));
    texture_maps.add("map_Bump", tex_map_XX("Normal"));
  }

  /**
   * Return a reference to the texture map corresponding to the given ID
   * Caller must ensure that the lookup key given exists in the Map.
   */
  tex_map_XX &tex_map_of_type(StringRef map_string)
  {
    {
      BLI_assert(texture_maps.contains_as(map_string));
      return texture_maps.lookup_as(map_string);
    }
  }

  std::string name{};
  float Ns{1.0f};
  float3 Ka{0.0f};
  float3 Kd{0.8f, 0.8f, 0.8f};
  float3 Ks{1.0f};
  float3 Ke{0.0f};
  float Ni{1.0f};
  float d{1.0f};
  int illum{0};
  Map<const std::string, tex_map_XX> texture_maps;
  /** Only used for Normal Map node: map_Bump. */
  float map_Bump_strength = 0.0f;
};

struct UniqueNodeDeleter {
  void operator()(bNode *node)
  {
    MEM_freeN(node);
  }
};

struct UniqueNodetreeDeleter {
  void operator()(bNodeTree *node)
  {
    MEM_freeN(node);
  }
};

using unique_node_ptr = std::unique_ptr<bNode, UniqueNodeDeleter>;
using unique_nodetree_ptr = std::unique_ptr<bNodeTree, UniqueNodetreeDeleter>;

class ShaderNodetreeWrap {
 private:
  unique_nodetree_ptr nodetree_;
  unique_node_ptr bsdf_;
  unique_node_ptr shader_output_;
  const MTLMaterial *mtl_mat_;

 public:
  ShaderNodetreeWrap(Main *bmain, const MTLMaterial &mtl_mat);
  ~ShaderNodetreeWrap();

  bNodeTree *get_nodetree();

 private:
  bNode *add_node_to_tree(const int node_type);
  void link_sockets(unique_node_ptr from_node,
                    StringRef from_node_id,
                    bNode *to_node,
                    StringRef to_node_id);
  void set_bsdf_socket_values();
  void add_image_textures(Main *bmain);
};
}  // namespace blender::io::obj
