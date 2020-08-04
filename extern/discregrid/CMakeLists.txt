# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# The Original Code is Copyright (C) 2006, Blender Foundation
# All rights reserved.
# ***** END GPL LICENSE BLOCK *****

set(INC
	discregrid/include
	discregrid/include/Discregrid
	extern
)

set(INC_SYS
	${EIGEN3_INCLUDE_DIRS}
)

set(SRC
	discregrid/include/Discregrid/discrete_grid.hpp
	discregrid/include/Discregrid/cubic_lagrange_discrete_grid.hpp
	discregrid/include/Discregrid/acceleration/bounding_sphere_hierarchy.hpp
	discregrid/include/Discregrid/acceleration/bounding_sphere.hpp
	discregrid/include/Discregrid/acceleration/kd_tree.hpp
	discregrid/include/Discregrid/acceleration/kd_tree.inl
	discregrid/include/Discregrid/mesh/triangle_mesh.hpp
	discregrid/include/Discregrid/mesh/entity_containers.hpp
	discregrid/include/Discregrid/mesh/entity_iterators.hpp
	discregrid/include/Discregrid/mesh/halfedge.hpp
	discregrid/include/Discregrid/geometry/mesh_distance.hpp
	discregrid/src/geometry/point_triangle_distance.hpp
	discregrid/include/Discregrid/utility/serialize.hpp
	discregrid/include/Discregrid/utility/lru_cache.hpp
	discregrid/src/utility/timing.hpp
	discregrid/src/utility/spinlock.hpp
	discregrid/src/discrete_grid.cpp
	discregrid/src/cubic_lagrange_discrete_grid.cpp
	discregrid/src/acceleration/bounding_sphere_hierarchy.cpp
	discregrid/src/mesh/entity_containers.cpp
	discregrid/src/mesh/entity_iterators.cpp
	discregrid/src/mesh/triangle_mesh.cpp
	discregrid/src/geometry/mesh_distance.cpp
	discregrid/src/geometry/point_triangle_distance.cpp
	discregrid/src/utility/timing.cpp
)

set(LIB
)

blender_add_lib(extern_discregrid "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
