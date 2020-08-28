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

#include <fstream>

#include "IO_wavefront_obj.h"
#include "wavefront_obj_im_mtl.hh"
#include "wavefront_obj_im_objects.hh"

namespace blender::io::obj {

class OBJParser {
 private:
  const OBJImportParams &import_params_;
  std::ifstream obj_file_;
  Vector<std::string> mtl_libraries_{};

 public:
  OBJParser(const OBJImportParams &import_params);

  void parse_and_store(Vector<std::unique_ptr<Geometry>> &r_all_geometries,
                       GlobalVertices &r_global_vertices);
  Span<std::string> mtl_libraries() const;
  void print_obj_data(Span<std::unique_ptr<Geometry>> all_geometries,
                      const GlobalVertices &global_vertices);
};

/**
 * All texture map options with number of arguments they accept.
 */
class TextureMapOptions {
 private:
  Map<const std::string, int> tex_map_options;

 public:
  TextureMapOptions()
  {
    tex_map_options.add_new("-blendu", 1);
    tex_map_options.add_new("-blendv", 1);
    tex_map_options.add_new("-boost", 1);
    tex_map_options.add_new("-mm", 2);
    tex_map_options.add_new("-o", 3);
    tex_map_options.add_new("-s", 3);
    tex_map_options.add_new("-t", 3);
    tex_map_options.add_new("-texres", 1);
    tex_map_options.add_new("-clamp", 1);
    tex_map_options.add_new("-bm", 1);
    tex_map_options.add_new("-imfchan", 1);
  }

  /**
   * All valid option strings.
   */
  Map<const std::string, int>::KeyIterator all_options() const
  {
    return tex_map_options.keys();
  }

  int number_of_args(StringRef option) const
  {
    return tex_map_options.lookup_as(std::string(option));
  }
};

class MTLParser {
 private:
  char mtl_file_path_[FILE_MAX];
  /**
   * Directory in which the MTL file is found.
   */
  char mtl_dir_path_[FILE_MAX];
  std::ifstream mtl_file_;

 public:
  MTLParser(StringRef mtl_library_, StringRefNull obj_filepath);

  void parse_and_store(Map<std::string, std::unique_ptr<MTLMaterial>> &r_mtl_materials);
};
}  // namespace blender::io::obj
