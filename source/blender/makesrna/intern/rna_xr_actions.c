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
