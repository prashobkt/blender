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

#include "BLI_float3.hh"
#include "BLI_span.hh"
#include "BLI_vector.hh"

struct Object;
struct OBJImportParams;

namespace blender::io::obj {
Vector<Vector<int>> ngon_tessellate(Span<float3> vertex_coords, Span<int> face_vertex_indices);

void transform_object(Object *object, const OBJImportParams &import_params);
}  // namespace blender::io::obj
