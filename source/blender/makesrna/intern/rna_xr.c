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

#ifndef WITH_OPENXR
BLI_STATIC_ASSERT(false, "Tried to compile rna_xr.c even though WITH_OPENXR is not defined.");
#endif

#ifdef RNA_RUNTIME

#  include "WM_api.h"

static bool rna_XrSessionState_is_running(bContext *C)
{
  const wmWindowManager *wm = CTX_wm_manager(C);
  return WM_xr_is_session_running(&wm->xr);
}

static wmXrData *rna_XrSessionState_wm_xr_data_get(PointerRNA *ptr)
{
  /* Callers could also get bXrSessionState pointer through ptr->data, but prefer if we just
   * consistently pass wmXrData pointers to the WM_xr_xxx() API. */

  BLI_assert(ptr->type == &RNA_XrSessionState);

  wmWindowManager *wm = (wmWindowManager *)ptr->owner_id;
  BLI_assert(wm && (GS(wm->id.name) == ID_WM));

  return &wm->xr;
}

static void rna_XrSessionState_viewer_location_get(PointerRNA *ptr, float *values)
{
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_viewer_location_get(xr, values);
}

static void rna_XrSessionState_viewer_rotation_get(PointerRNA *ptr, float *values)
{
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_viewer_rotation_get(xr, values);
}

#else /* RNA_RUNTIME */

static void rna_def_xr_session_settings(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  srna = RNA_def_struct(brna, "XrSessionSettings", NULL);
  RNA_def_struct_sdna(srna, "bXrSessionSettings");
  RNA_def_struct_ui_text(srna, "XR-Session Settings", "");

  prop = RNA_def_property(srna, "shading_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_enum_items(prop, rna_enum_shading_type_items);
  RNA_def_property_ui_text(prop, "Shading Type", "Method to display/shade objects in the VR View");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_floor", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_GRIDFLOOR);
  RNA_def_property_ui_text(prop, "Display Grid Floor", "Show the ground plane grid");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "show_annotation", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "draw_flags", V3D_OFSDRAW_SHOW_ANNOTATION);
  RNA_def_property_ui_text(prop, "Show Annotation", "Show annotations for this view");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "clip_start", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_range(prop, 1e-6f, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  RNA_def_property_ui_text(prop, "Clip Start", "VR View near clipping distance");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "clip_end", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_range(prop, 1e-6f, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  RNA_def_property_ui_text(prop, "Clip End", "VR View far clipping distance");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "use_positional_tracking", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", XR_SESSION_USE_POSITION_TRACKING);
  RNA_def_property_ui_text(prop,
                           "Positional Tracking",
                           "Limit view movements to rotation only (three degrees of freedom)");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);
}

static void rna_def_xr_session_state(BlenderRNA *brna)
{
  StructRNA *srna;
  FunctionRNA *func;
  PropertyRNA *parm, *prop;

  srna = RNA_def_struct(brna, "XrSessionState", NULL);
  RNA_def_struct_clear_flag(srna, STRUCT_UNDO);
  RNA_def_struct_ui_text(srna, "Session State", "Runtime state information about the VR session");

  func = RNA_def_function(srna, "is_running", "rna_XrSessionState_is_running");
  RNA_def_function_ui_description(func, "Query if the VR session is currently running");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);
  parm = RNA_def_boolean(func, "result", 0, "Result", "");
  RNA_def_function_return(func, parm);

  prop = RNA_def_property(srna, "viewer_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_viewer_location_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Viewer Location",
      "Last known location of the viewer (centroid of the eyes) in world space");

  prop = RNA_def_property(srna, "viewer_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_viewer_rotation_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Viewer Rotation",
      "Last known Rotation of the viewer (centroid of the eyes) in world space");
}

void RNA_def_xr(BlenderRNA *brna)
{
  RNA_define_animate_sdna(false);

  rna_def_xr_session_settings(brna);
  rna_def_xr_session_state(brna);

  RNA_define_animate_sdna(true);
}

#endif /* RNA_RUNTIME */
