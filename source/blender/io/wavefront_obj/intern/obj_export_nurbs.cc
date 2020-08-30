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

#include "BLI_math.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "IO_wavefront_obj.h"
#include "obj_export_nurbs.hh"

namespace blender::io::obj {
/**
 * Store NURBS curves that will be exported in parameter form, not converted to meshes.
 */
OBJNurbs::OBJNurbs(Depsgraph *depsgraph,
                   const OBJExportParams &export_params,
                   Object *export_object)
    : depsgraph_(depsgraph), export_params_(export_params), export_object_eval_(export_object)
{
  export_object_eval_ = DEG_get_evaluated_object(depsgraph_, export_object);
  export_curve_ = static_cast<Curve *>(export_object_eval_->data);
  store_world_axes_transform();
}

/**
 * Store the product of export axes settings and an object's world transform matrix.
 */
void OBJNurbs::store_world_axes_transform()
{
  float axes_transform[3][3];
  unit_m3(axes_transform);
  /* -Y-forward and +Z-up are the default Blender axis settings. */
  mat3_from_axis_conversion(OBJ_AXIS_NEGATIVE_Y_FORWARD,
                            OBJ_AXIS_Z_UP,
                            export_params_.forward_axis,
                            export_params_.up_axis,
                            axes_transform);
  mul_m4_m3m4(world_axes_transform_, axes_transform, export_object_eval_->obmat);
  /* mul_m4_m3m4 does not copy last row of obmat, i.e. location data. */
  copy_v4_v4(world_axes_transform_[3], export_object_eval_->obmat[3]);
}

const char *OBJNurbs::get_curve_name() const
{
  return export_object_eval_->id.name + 2;
}

const ListBase *OBJNurbs::curve_nurbs() const
{
  return &export_curve_->nurb;
}

/**
 * Get coordinates of a vertex at given point index.
 */
void OBJNurbs::calc_point_coords(const Nurb *nurb, const int vert_index, float r_coords[3]) const
{
  BPoint *bpoint = nurb->bp;
  bpoint += vert_index;
  copy_v3_v3(r_coords, bpoint->vec);
  mul_m4_v3(world_axes_transform_, r_coords);
  mul_v3_fl(r_coords, export_params_.scaling_factor);
}

/**
 * Get nurbs' degree and number of "curv" points of a nurb.
 */
void OBJNurbs::get_curve_info(const Nurb *nurb, int &r_nurbs_degree, int &r_curv_num) const
{
  r_nurbs_degree = nurb->orderu - 1;
  /* "curv_num" is the number of control points in a nurbs.
   * If it is cyclic, degree also adds up. */
  r_curv_num = nurb->pntsv * nurb->pntsu;
  if (nurb->flagu & CU_NURB_CYCLIC) {
    r_curv_num += r_nurbs_degree;
  }
}

}  // namespace blender::io::obj
