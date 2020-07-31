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
 */

/** \file
 * \ingroup RNA
 */

#include "DNA_view3d_types.h"
#include "DNA_xr_types.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"

#include "WM_types.h"

#include "rna_internal.h"

#ifdef RNA_RUNTIME

#  include "BLI_math.h"

#  include "WM_api.h"

static bool rna_XrSessionState_is_running(bContext *C)
{
#  ifdef WITH_XR_OPENXR
  const wmWindowManager *wm = CTX_wm_manager(C);
  return WM_xr_session_exists(&wm->xr);
#  else
  UNUSED_VARS(C);
  return false;
#  endif
}

static void rna_XrSessionState_reset_to_base_pose(bContext *C)
{
#  ifdef WITH_XR_OPENXR
  wmWindowManager *wm = CTX_wm_manager(C);
  WM_xr_session_base_pose_reset(&wm->xr);
#  else
  UNUSED_VARS(C);
#  endif
}

#  ifdef WITH_XR_OPENXR
static wmXrData *rna_XrSessionState_wm_xr_data_get(PointerRNA *ptr)
{
  /* Callers could also get XrSessionState pointer through ptr->data, but prefer if we just
   * consistently pass wmXrData pointers to the WM_xr_xxx() API. */

  BLI_assert(ptr->type == &RNA_XrSessionState);

  wmWindowManager *wm = (wmWindowManager *)ptr->owner_id;
  BLI_assert(wm && (GS(wm->id.name) == ID_WM));

  return &wm->xr;
}
#  endif

static void rna_XrSessionState_viewer_pose_location_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_viewer_pose_location_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void rna_XrSessionState_viewer_pose_rotation_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_viewer_pose_rotation_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

#else /* RNA_RUNTIME */

static void rna_def_xr_action_set(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;
  PropertyRNA *parm;

  srna = RNA_def_struct(brna, "XrActionSet", NULL);
  RNA_def_struct_ui_text(srna, "XrActionSet", "Xr Action Set");

  //Static function to create an action set (should be part of the session struct)
  //TODO: merge with session struct and rna_xr.

  func = RNA_def_function(srna, "create_set", "rna_XrAction_create_set");
  RNA_def_function_ui_description(func, "Create an action set.");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_string(func, "action_set_name", NULL, 0, "", "Name of the action set.");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "action_set", "ActionSet", "", "Created GHOST_OpenXr Action Set.");
  RNA_def_function_return(func, parm);

  //Function to create an action.
  func = RNA_def_function(srna, "create_action", "rna_XrAction_create_action");
  RNA_def_function_ui_description(func, "Create an action.");
  parm = RNA_def_string(func, "action_name", NULL, 0, "", "Name of the action.");
  RNA_def_parameter_flags(parm, 0, PARM_REQUIRED);
  parm = RNA_def_pointer(func, "action", "Action", "", "Created GHOST_OpenXr Action.");
  RNA_def_function_return(func, parm);
}

static void rna_def_xr_action(BlenderRNA *brna) {
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "XrAction", NULL);
  RNA_def_struct_ui_text(srna, "XrAction", "Xr Action");

  func = RNA_def_function(srna, "create_set", "rna_XrAction_create_set");
  RNA_def_function_ui_description(func, "Create an action set.");

  //TODO: How do we map bindings to 'operators'?
  //TODO: Probably need a way of mapping callbacks and operators.
}


void RNA_def_xr_actions(BlenderRNA *brna)
{
  rna_def_xr_action_set(brna);
}

#endif /* RNA_RUNTIME */
