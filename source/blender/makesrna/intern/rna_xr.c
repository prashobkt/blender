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

static void rna_XrSessionState_world_location_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_world_location_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  zero_v3(r_values);
#  endif
}

static void rna_XrSessionState_world_location_set(PointerRNA *ptr, float* values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_world_location_set(xr, values);
#  else
  UNUSED_VARS(ptr);
  UNUSED_VARS(values);
#  endif
}

static void rna_XrSessionState_world_rotation_get(PointerRNA *ptr, float *r_values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_world_rotation_get(xr, r_values);
#  else
  UNUSED_VARS(ptr);
  unit_qt(r_values);
#  endif
}

static void rna_XrSessionState_world_rotation_set(PointerRNA *ptr, float *values)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_world_rotation_set(xr, values);
#else
  UNUSED_VARS(ptr);
  UNUSED_VARS(values);
#  endif
}

static float rna_XrSessionState_world_scale_get(PointerRNA *ptr)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  return WM_xr_session_state_world_scale_get(xr);
#  else
  return 1.f;
#  endif
}

static void rna_XrSessionState_world_scale_set(PointerRNA *ptr, float value)
{
#  ifdef WITH_XR_OPENXR
  const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr);
  WM_xr_session_state_world_scale_set(xr, value);
#  else
  UNUSED_VARS(ptr);
  UNUSED_VARS(value);
#  endif
}


#  ifdef WITH_XR_OPENXR
#    define rna_XrSessionState(side, attribute, type, clearFn) \
      static void rna_XrSessionState_##side##_controller_##attribute##_get(PointerRNA * ptr, \
                                                                     type *r_values) \
      { \
        const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr); \
        WM_xr_session_state_##side##_controller_##attribute##_get(xr, r_values); \
      }
#  else
#    define rna_XrSessionState(name, type, clearFn) \
      { \
        static void rna_XrSessionState_##side##_controller_##attribute##_get(PointerRNA *ptr, \
                                                                             type *r_values) \
        { \
          UNUSED_VARS(ptr); \
          clearFn(r_values); \
        }
#  endif


/* rna_XrSessionState_left_controller_location_get */
rna_XrSessionState(left, location, float, zero_v3)

/* rna_XrSessionState_left_controller_rotation_get */
rna_XrSessionState(left, rotation, float, unit_qt)

/* rna_XrSessionState_right_controller_location_get */
rna_XrSessionState(right, location, float, zero_v3)

/* rna_XrSessionState_right_controller_rotation_get */
rna_XrSessionState(right, rotation, float, unit_qt)


#  ifdef WITH_XR_OPENXR
#  define rna_XrSessionControllerValue(side, attribute, type)\
  static type rna_XrSessionState_##side##_controller_##attribute##_get(PointerRNA *ptr) \
      { \
        const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr); \
        return WM_xr_session_state_##side##_##attribute##_get(xr); \
      }
#    else
#    define rna_XrSessionControllerButton(side, attribute, type) \
  static type rna_XrSessionState_##side##_controller_##attribute##_get(PointerRNA *ptr) \
      { \
        const wmXrData *xr = rna_XrSessionState_wm_xr_data_get(ptr); \
        return (type)0; \
      }
#endif

/* rna_XrSessionState_left_controller_trigger_value_get */
rna_XrSessionControllerValue(left, trigger_value, float)
/* rna_XrSessionState_Right_controller_trigger_value_get */
rna_XrSessionControllerValue(right, trigger_value, float)
/* rna_XrSessionState_left_controller_trigger_touch_get */
rna_XrSessionControllerValue(left, trigger_touch, bool)
/* rna_XrSessionState_Right_controller_trigger_touch_get */
rna_XrSessionControllerValue(right, trigger_touch, bool)

/* rna_XrSessionState_left_controller_grip_value_get */
rna_XrSessionControllerValue(left, grip_value, float)
/* rna_XrSessionState_Right_controller_grip_value_get */
rna_XrSessionControllerValue(right, grip_value, float)

/* rna_XrSessionState_left_controller_primary_click_get */
rna_XrSessionControllerValue(left, primary_click, bool)
/* rna_XrSessionState_left_controller_primary_touch_get */
rna_XrSessionControllerValue(left, primary_touch, bool)
/* rna_XrSessionState_left_controller_secondary_click_get */
rna_XrSessionControllerValue(left, secondary_click, bool)
/* rna_XrSessionState_left_controller_secondary_touch_get */
rna_XrSessionControllerValue(left, secondary_touch, bool)

/* rna_XrSessionState_right_controller_primary_click_get */
rna_XrSessionControllerValue(right, primary_click, bool)
/* rna_XrSessionState_right_controller_primary_touch_get */
rna_XrSessionControllerValue(right, primary_touch, bool)
/* rna_XrSessionState_right_controller_secondary_click_get */
rna_XrSessionControllerValue(right, secondary_click, bool)
/* rna_XrSessionState_right_controller_secondary_touch_get */
rna_XrSessionControllerValue(right, secondary_touch, bool)

/* rna_XrSessionState_left_controller_thumbstick_x_get */
rna_XrSessionControllerValue(left, thumbstick_x, float)
/* rna_XrSessionState_right_controller_thumbstick_x_get */
rna_XrSessionControllerValue(right, thumbstick_x, float)

/* rna_XrSessionState_left_controller_thumbstick_y_get */
rna_XrSessionControllerValue(left, thumbstick_y, float)
/* rna_XrSessionState_right_controller_thumbstick_y_get */
rna_XrSessionControllerValue(right, thumbstick_y, float)

/* rna_XrSessionState_left_controller_thumbstick_click_get */
rna_XrSessionControllerValue(left, thumbstick_click, bool)
/* rna_XrSessionState_right_controller_thumbstick_click_get */
rna_XrSessionControllerValue(right, thumbstick_click, bool)
/* rna_XrSessionState_left_controller_thumbstick_touch_get */
rna_XrSessionControllerValue(left, thumbstick_touch, bool)
/* rna_XrSessionState_right_controller_thumbstick_touch_get */
rna_XrSessionControllerValue(right, thumbstick_touch, bool)

#else /* RNA_RUNTIME */

static void rna_def_xr_session_settings(BlenderRNA *brna)
{
  StructRNA *srna;
  PropertyRNA *prop;

  static const EnumPropertyItem base_pose_types[] = {
      {XR_BASE_POSE_SCENE_CAMERA,
       "SCENE_CAMERA",
       0,
       "Scene Camera",
       "Follow the active scene camera to define the VR view's base pose"},
      {XR_BASE_POSE_OBJECT,
       "OBJECT",
       0,
       "Object",
       "Follow the transformation of an object to define the VR view's base pose"},
      {XR_BASE_POSE_CUSTOM,
       "CUSTOM",
       0,
       "Custom",
       "Follow a custom transformation to define the VR view's base pose"},
      {0, NULL, 0, NULL, NULL},
  };

  srna = RNA_def_struct(brna, "XrSessionSettings", NULL);
  RNA_def_struct_ui_text(srna, "XR Session Settings", "");

  prop = RNA_def_property(srna, "shading", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_NEVER_NULL);
  RNA_def_property_ui_text(prop, "Shading Settings", "");
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);

  prop = RNA_def_property(srna, "base_pose_type", PROP_ENUM, PROP_NONE);
  RNA_def_property_clear_flag(prop, PROP_ANIMATABLE);
  RNA_def_property_enum_items(prop, base_pose_types);
  RNA_def_property_ui_text(
      prop,
      "Base Pose Type",
      "Define where the location and rotation for the VR view come from, to which "
      "translation and rotation deltas from the VR headset will be applied to");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_object", PROP_POINTER, PROP_NONE);
  RNA_def_property_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop,
                           "Base Pose Object",
                           "Object to take the location and rotation to which translation and "
                           "rotation deltas from the VR headset will be applied to");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_ui_text(prop,
                           "Base Pose Location",
                           "Coordinates to apply translation deltas from the VR headset to");
  RNA_def_property_ui_range(prop, -FLT_MAX, FLT_MAX, 1, RNA_TRANSLATION_PREC_DEFAULT);
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "base_pose_angle", PROP_FLOAT, PROP_AXISANGLE);
  RNA_def_property_ui_text(
      prop,
      "Base Pose Angle",
      "Rotation angle around the Z-Axis to apply the rotation deltas from the VR headset to");
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
  RNA_def_property_ui_text(prop, "Clip Start", "VR viewport near clipping distance");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "clip_end", PROP_FLOAT, PROP_DISTANCE);
  RNA_def_property_range(prop, 1e-6f, FLT_MAX);
  RNA_def_property_ui_range(prop, 0.001f, FLT_MAX, 10, 3);
  RNA_def_property_ui_text(prop, "Clip End", "VR viewport far clipping distance");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);

  prop = RNA_def_property(srna, "use_positional_tracking", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_sdna(prop, NULL, "flag", XR_SESSION_USE_POSITION_TRACKING);
  RNA_def_property_ui_text(
      prop,
      "Positional Tracking",
      "Allow VR headsets to affect the location in virtual space, in addition to the rotation");
  RNA_def_property_update(prop, NC_WM | ND_XR_DATA_CHANGED, NULL);
}

static void rna_def_xr_define_sized_property(StructRNA *srna,
                                       char *name,
                                       char *description,
                                       char *function_name,
                                       PropertySubType sub_type,
                                       PropertyType type,
                                       int array_size)
{
  PropertyRNA *prop = RNA_def_property(srna, name, type, sub_type);
  RNA_def_property_array(prop, array_size);
  RNA_def_property_float_funcs(prop, function_name, NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, name, description);
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

  func = RNA_def_function(srna, "reset_to_base_pose", "rna_XrSessionState_reset_to_base_pose");
  RNA_def_function_ui_description(func, "Force resetting of position and rotation deltas");
  RNA_def_function_flag(func, FUNC_NO_SELF);
  parm = RNA_def_pointer(func, "context", "Context", "", "");
  RNA_def_parameter_flags(parm, PROP_NEVER_NULL, PARM_REQUIRED);

  prop = RNA_def_property(srna, "viewer_pose_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_viewer_pose_location_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Viewer Pose Location",
      "Last known location of the viewer pose (center between the eyes) in world space");

  prop = RNA_def_property(srna, "viewer_pose_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_viewer_pose_rotation_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(
      prop,
      "Viewer Pose Rotation",
      "Last known rotation of the viewer pose (center between the eyes) in world space");

  prop = RNA_def_property(srna, "world_location", PROP_FLOAT, PROP_TRANSLATION);
  RNA_def_property_array(prop, 3);
  RNA_def_property_float_funcs(prop,
                               "rna_XrSessionState_world_location_get",
                               "rna_XrSessionState_world_location_set",
                               NULL);
  RNA_def_property_ui_text(
      prop,
      "World Location",
      "Last known location of the world in world space");

  prop = RNA_def_property(srna, "world_rotation", PROP_FLOAT, PROP_QUATERNION);
  RNA_def_property_array(prop, 4);
  RNA_def_property_float_funcs(prop,
                               "rna_XrSessionState_world_rotation_get",
                               "rna_XrSessionState_world_rotation_set",
                               NULL);
  RNA_def_property_ui_text(
      prop,
      "World Rotation",
      "Last known rotation of the world in world space");

  prop = RNA_def_property(srna, "world_scale", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(
      prop, "rna_XrSessionState_world_scale_get", "rna_XrSessionState_world_scale_set", NULL);
  RNA_def_property_ui_text(prop, "World Scale", "Get World Scale Value");

  rna_def_xr_define_sized_property(srna,
                                   "left_controller_location",
                                   "Last known location of the left controller in world space",
                                   "rna_XrSessionState_left_controller_location_get",
                                   PROP_TRANSLATION,
                                   PROP_FLOAT,
                                   3);

  rna_def_xr_define_sized_property(srna,
                                   "left_controller_rotation",
                                   "Last known rotation of the left controller in world space",
                                   "rna_XrSessionState_left_controller_rotation_get",
                                   PROP_QUATERNION,
                                   PROP_FLOAT,
                                   4);

  rna_def_xr_define_sized_property(srna,
                                   "right_controller_location",
                                   "Last known location of the right controller in world space",
                                   "rna_XrSessionState_right_controller_location_get",
                                   PROP_TRANSLATION,
                                   PROP_FLOAT,
                                   3);

  rna_def_xr_define_sized_property(srna,
                                   "right_controller_rotation",
                                   "Last known rotation of the right controller in world space",
                                   "rna_XrSessionState_right_controller_rotation_get",
                                   PROP_QUATERNION,
                                   PROP_FLOAT,
                                   4);

  prop = RNA_def_property(srna, "left_trigger_value", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_left_controller_trigger_value_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Trigger", "Get Left Trigger Value");

  prop = RNA_def_property(srna, "left_trigger_touch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_left_controller_trigger_touch_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Trigger Touch", "Get Left Trigger Touch");

  prop = RNA_def_property(srna, "right_trigger_value", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_right_controller_trigger_value_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Trigger", "Get Right Trigger Value");

  prop = RNA_def_property(srna, "right_trigger_touch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_right_controller_trigger_touch_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Trigger Touch", "Get Right Trigger Touch");

  prop = RNA_def_property(srna, "left_grip_value", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_left_controller_grip_value_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Grip", "Get Left Grip Value");

  prop = RNA_def_property(srna, "right_grip_value", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_right_controller_grip_value_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Grip", "Get Right Grip Value");

  prop = RNA_def_property(srna, "left_primary_click", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_left_controller_primary_click_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Primary Click", "Get Left Primary Click");

  prop = RNA_def_property(srna, "left_primary_touch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_left_controller_primary_touch_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Primary Touch", "Get Left Primary Touch");

  prop = RNA_def_property(srna, "left_secondary_click", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_left_controller_secondary_click_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Secondary Click", "Get Left Secondary Click");

  prop = RNA_def_property(srna, "left_secondary_touch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_left_controller_secondary_touch_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Secondary Touch", "Get Left Secondary Touch");

  prop = RNA_def_property(srna, "right_primary_click", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_right_controller_primary_click_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Primary Click", "Get Right Primary Click");

  prop = RNA_def_property(srna, "right_primary_touch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_right_controller_primary_touch_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Primary Touch", "Get Right Primary Touch");

  prop = RNA_def_property(srna, "right_secondary_click", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_right_controller_secondary_click_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Secondary Click", "Get Right Secondary Click");

  prop = RNA_def_property(srna, "right_secondary_touch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_right_controller_secondary_touch_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Secondary Touch", "Get Right Secondary Touch");

  prop = RNA_def_property(srna, "left_thumbstick_x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_left_controller_thumbstick_x_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Thumbstick X", "Get Left Thumbstick X Value");

  prop = RNA_def_property(srna, "left_thumbstick_y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_left_controller_thumbstick_y_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Thumbstick Y", "Get Left Thumbstick Y Value");

  prop = RNA_def_property(srna, "left_thumbstick_click", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_left_controller_thumbstick_click_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Thumbstick Click", "Get Left Thumbstick Click Value");

  prop = RNA_def_property(srna, "left_thumbstick_touch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_left_controller_thumbstick_touch_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Left Thumbstick Touch", "Get Left Thumbstick Touch Value");

  prop = RNA_def_property(srna, "right_thumbstick_x", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_right_controller_thumbstick_x_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Thumbstick X", "Get Right Thumbstick X Value");

  prop = RNA_def_property(srna, "right_thumbstick_y", PROP_FLOAT, PROP_NONE);
  RNA_def_property_float_funcs(prop, "rna_XrSessionState_right_controller_thumbstick_y_get", NULL, NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Thumbstick Y", "Get Right Thumbstick Y Value");

  prop = RNA_def_property(srna, "right_thumbstick_click", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_right_controller_thumbstick_click_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Thumbstick Click", "Get Right Thumbstick Click Value");

  prop = RNA_def_property(srna, "right_thumbstick_touch", PROP_BOOLEAN, PROP_NONE);
  RNA_def_property_boolean_funcs(prop, "rna_XrSessionState_right_controller_thumbstick_touch_get", NULL);
  RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  RNA_def_property_ui_text(prop, "Right Thumbstick Touch", "Get Right Thumbstick Touch Value");
}

void RNA_def_xr(BlenderRNA *brna)
{
  RNA_define_animate_sdna(false);

  rna_def_xr_session_settings(brna);
  rna_def_xr_session_state(brna);

  RNA_define_animate_sdna(true);
}

#endif /* RNA_RUNTIME */
