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
 * The Original Code is Copyright (C) 2019 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup editors
 */


#include <stdlib.h>

#include "BLI_utildefines.h"

#include "WM_api.h"

#include "ED_lanpr.h"

void ED_operatortypes_lanpr(void)
{
  /* lanpr:  */
  WM_operatortype_append(SCENE_OT_lanpr_calculate_feature_lines);
  WM_operatortype_append(SCENE_OT_lanpr_add_line_layer);
  WM_operatortype_append(SCENE_OT_lanpr_delete_line_layer);
  WM_operatortype_append(SCENE_OT_lanpr_rebuild_all_commands);
  WM_operatortype_append(SCENE_OT_lanpr_auto_create_line_layer);
  WM_operatortype_append(SCENE_OT_lanpr_move_line_layer);
  WM_operatortype_append(SCENE_OT_lanpr_add_line_component);
  WM_operatortype_append(SCENE_OT_lanpr_delete_line_component);
  WM_operatortype_append(SCENE_OT_lanpr_enable_all_line_types);
  WM_operatortype_append(SCENE_OT_lanpr_update_gp_strokes);
  WM_operatortype_append(SCENE_OT_lanpr_bake_gp_strokes);

  WM_operatortype_append(OBJECT_OT_lanpr_update_gp_target);
  /* Not working */
  /* WM_operatortype_append(OBJECT_OT_lanpr_update_gp_source); */
}
