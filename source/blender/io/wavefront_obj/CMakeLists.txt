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
  .
  ../../blenkernel
  ../../blenlib
  ../../bmesh
  ../../bmesh/intern
  ../../depsgraph
  ../../editors/include
  ../../makesdna
  ../../makesrna
  ../../nodes
  ../../windowmanager
  ../../../../intern/guardedalloc
)

set(INC_SYS

)

set(SRC
  intern/utils.cc
  intern/wavefront_obj.cc
  intern/obj_exporter.cc
  intern/obj_export_mesh.cc
  intern/obj_export_mtl.cc
  intern/obj_export_nurbs.cc
  intern/obj_export_file_writer.cc
  intern/obj_import_file_reader.cc
  intern/obj_importer.cc
  intern/obj_import_mesh.cc
  intern/obj_import_mtl.cc
  intern/obj_import_nurbs.cc
  intern/obj_import_objects.cc

  IO_wavefront_obj.h
  intern/utils.hh
  intern/obj_exporter.hh
  intern/obj_export_mesh.hh
  intern/obj_export_mtl.hh
  intern/obj_export_nurbs.hh
  intern/obj_export_file_writer.hh
  intern/obj_import_file_reader.hh
  intern/obj_importer.hh
  intern/obj_import_mesh.hh
  intern/obj_import_mtl.hh
  intern/obj_import_nurbs.hh
  intern/obj_import_objects.hh
)

set(LIB
  bf_blenkernel
)

blender_add_lib(bf_wavefront_obj "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
