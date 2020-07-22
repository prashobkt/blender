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
	src
)

set(INC_SYS
	${EIGEN3_INCLUDE_DIRS}
	../../source/blender/blenlib
	../../source/blender/makesdna # BLI_math_geom requires DNA
)

set(SDFGEN_SRC
#	src/sdfgen/array1.h
#	src/sdfgen/array2.h
#	src/sdfgen/array3.h
#	src/sdfgen/hashgrid.h
#	src/sdfgen/hashtable.h
#	src/sdfgen/makelevelset3.cpp
#	src/sdfgen/makelevelset3.h
#	src/sdfgen/util.h
#	src/sdfgen/vec.h
)

set(SRC
	src/admmpd_bvh.h
	src/admmpd_bvh.cpp
	src/admmpd_bvh_traverse.h
	src/admmpd_bvh_traverse.cpp
	src/admmpd_collision.h
	src/admmpd_collision.cpp
	src/admmpd_energy.h
	src/admmpd_energy.cpp
	src/admmpd_geom.h
	src/admmpd_geom.cpp
	src/admmpd_linsolve.h
	src/admmpd_linsolve.cpp
	src/admmpd_sdf.h
	src/admmpd_sdf.cpp
	src/admmpd_solver.h
	src/admmpd_solver.cpp
	src/admmpd_mesh.h
	src/admmpd_mesh.cpp
	src/admmpd_types.h
	src/svd/ImplicitQRSVD.h
	src/svd/Tools.h
	${SDFGEN_SRC}
)

set(LIB
)

blender_add_lib(extern_admmpd "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
