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
# The Original Code is Copyright (C) 2020, Blender Foundation
# All rights reserved.
# ***** END GPL LICENSE BLOCK *****

set(INC
  src
  src/include
)

set(INC_SYS

)

set(SRC
  src/backend_dxf.c
  src/backend_eps.c
  src/backend_geojson.c
  src/backend_pdf.c
  src/backend_pgm.c
  src/backend_svg.c
  src/backend_xfig.c
  src/bbox.c
  src/bitmap_io.c
  src/curve.c
  src/decompose.c
  src/flate.c
  #src/getopt.c
  #src/getopt1.c
  src/greymap.c
  src/lzw.c
  #src/mkbitmap.c
  src/potracelib.c
  src/progress_bar.c
  src/render.c
  src/trace.c
  src/trans.c
)
set(LIB
)

if(WIN32)
  add_definitions(/D_USE_MATH_DEFINES)
endif()

# normally given by configure.sh
add_definitions(/DPOTRACE="POTrace")
add_definitions(/DVERSION="Blender")
add_definitions(/DHAVE_INTTYPES_H)

blender_add_lib(extern_potrace "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")

#demo for testing the library, can be removed 
#add_executable(potrace src/potracelib_demo.c)
#target_link_libraries(potrace extern_potrace)
