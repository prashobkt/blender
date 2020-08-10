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
 * The Original Code is Copyright (C) 2009 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup spuserpref
 */

#include <BLI_string.h>
#include <string.h>

#include "DNA_screen_types.h"

#include "BLI_listbase.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_report.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_types.h"

#include "UI_interface.h"

#include "../interface/interface_intern.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_userpref.h"
#include "userpref_intern.h"

#include "CLG_log.h"
#include "MEM_guardedalloc.h"

/* -------------------------------------------------------------------- */
/** \name Reset Default Theme Operator
 * \{ */

static int preferences_reset_default_theme_exec(bContext *C, wmOperator *UNUSED(op))
{
  Main *bmain = CTX_data_main(C);
  UI_theme_init_default();
  UI_style_init_default();
  WM_reinit_gizmomap_all(bmain);
  WM_event_add_notifier(C, NC_WINDOW, NULL);
  U.runtime.is_dirty = true;
  return OPERATOR_FINISHED;
}

static void PREFERENCES_OT_reset_default_theme(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Reset to Default Theme";
  ot->idname = "PREFERENCES_OT_reset_default_theme";
  ot->description = "Reset to the default theme colors";

  /* callbacks */
  ot->exec = preferences_reset_default_theme_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Add Auto-Execution Path Operator
 * \{ */

static int preferences_autoexec_add_exec(bContext *UNUSED(C), wmOperator *UNUSED(op))
{
  bPathCompare *path_cmp = MEM_callocN(sizeof(bPathCompare), "bPathCompare");
  BLI_addtail(&U.autoexec_paths, path_cmp);
  U.runtime.is_dirty = true;
  return OPERATOR_FINISHED;
}

static void PREFERENCES_OT_autoexec_path_add(wmOperatorType *ot)
{
  ot->name = "Add Autoexec Path";
  ot->idname = "PREFERENCES_OT_autoexec_path_add";
  ot->description = "Add path to exclude from auto-execution";

  ot->exec = preferences_autoexec_add_exec;

  ot->flag = OPTYPE_INTERNAL;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Remove Auto-Execution Path Operator
 * \{ */

static int preferences_autoexec_remove_exec(bContext *UNUSED(C), wmOperator *op)
{
  const int index = RNA_int_get(op->ptr, "index");
  bPathCompare *path_cmp = BLI_findlink(&U.autoexec_paths, index);
  if (path_cmp) {
    BLI_freelinkN(&U.autoexec_paths, path_cmp);
    U.runtime.is_dirty = true;
  }
  return OPERATOR_FINISHED;
}

static void PREFERENCES_OT_autoexec_path_remove(wmOperatorType *ot)
{
  ot->name = "Remove Autoexec Path";
  ot->idname = "PREFERENCES_OT_autoexec_path_remove";
  ot->description = "Remove path to exclude from auto-execution";

  ot->exec = preferences_autoexec_remove_exec;

  ot->flag = OPTYPE_INTERNAL;

  RNA_def_int(ot->srna, "index", 0, 0, INT_MAX, "Index", "", 0, 1000);
}

/** override preferences with globals */
void USERPREF_save_global_log_settings()
{
  CLG_type_filter_get(U.log_type_filter, 256);
  U.log_severity = CLG_severity_level_get();
  U.log_use_basename = CLG_output_use_basename_get();
  U.log_use_timestamp = CLG_output_use_timestamp_get();
  U.log_use_stdout = CLG_use_stdout_get();
  U.log_always_show_warnings = CLG_always_show_warnings_get();
  BLI_strncpy(U.log_output_file_path, CLG_file_output_path_get(), 256);
  U.debug_flags = G.debug;
  U.debug_value = G.debug_value;
  U.verbose = G.log.level;
  U.runtime.is_dirty = true;
}

/** override globals with preferences
 * :param UserDef_RuntimeCommandLineArgs mask
 */
void USERPREF_restore_global_log_settings(bool use_command_line_mask)
{
  int mask = use_command_line_mask ? U.runtime.use_settings_from_command_line : 0;
  if (!(mask & ARGS_LOG_TYPE)) {
    CLG_type_filter_set(U.log_type_filter);
  }
  if (!(mask & ARGS_LOG_SEVERITY)) {
    CLG_severity_level_set(U.log_severity);
  }
  if (!(mask & ARGS_LOG_SHOW_BASENAME)) {
    CLG_output_use_basename_set(U.log_use_basename);
  }
  if (!(mask & ARGS_LOG_SHOW_TIMESTAMP)) {
    CLG_output_use_timestamp_set(U.log_use_timestamp);
  }
  if (!(mask & ARGS_LOG_FILE)) {
    CLG_use_stdout_set(U.log_use_stdout);
  }
  /*
    if (!(mask & ARGS_LOG_DISABLE_ALWAYS_SHOW_WARNINGS)) {
      CLG_always_show_warnings_set(U.log_always_show_warnings);
    }
  */
  if (!(mask & ARGS_VERBOSE)) {
    CLG_file_output_path_set(U.log_output_file_path);
  }
  if (!(mask & ARGS_DEBUG)) {
    /* TODO (grzelins) we need proper setter, not only enabler */
    G_debug_enable(U.debug_flags);
    G.debug = U.debug_flags;
  }
  if (!(mask & ARGS_DEBUG_VALUE)) {
    G.debug_value = U.debug_value;
  }
  if (!(mask & ARGS_VERBOSE)) {
    G_verbose_set(U.verbose);
  }
}

void USERPREF_restore_factory_log_settings()
{
  CLG_type_filter_set(CLG_DEFAULT_LOG_TYPE_FILTER);
  CLG_severity_level_set(CLG_DEFAULT_SEVERITY);
  CLG_output_use_basename_set(CLG_DEFAULT_USE_BASENAME);
  CLG_output_use_timestamp_set(CLG_DEFAULT_USE_TIMESTAMP);
  CLG_use_stdout_set(CLG_DEFAULT_USE_STDOUT);
  CLG_always_show_warnings_set(CLG_DEFAULT_ALWAYS_SHOW_WARNINGS);
  CLG_file_output_path_set(CLG_DEFAULT_OUTPUT_PATH);
  G.debug = 0;
  G.debug_value = 0;
  G.log.level = 0;
  USERPREF_save_global_log_settings();
}

static int preferences_log_save_exec(bContext *UNUSED(C), wmOperator *UNUSED(op))
{
  USERPREF_save_global_log_settings();
  return OPERATOR_FINISHED;
}

static void PREFERENCES_OT_log_preferences_save(wmOperatorType *ot)
{
  ot->name = "Save Log Preferences";
  ot->idname = "PREFERENCES_OT_log_preferences_save";
  ot->description = "Save log and debug related preferences";

  ot->exec = preferences_log_save_exec;

  ot->flag = OPTYPE_REGISTER;
}

static int preferences_log_restore_exec(bContext *UNUSED(C), wmOperator *UNUSED(op))
{
  USERPREF_restore_factory_log_settings();
  return OPERATOR_FINISHED;
}

static void PREFERENCES_OT_log_preferences_reset_default(wmOperatorType *ot)
{
  ot->name = "Save Log Preferences";
  ot->idname = "PREFERENCES_OT_log_preferences_reset_default";
  ot->description = "Reset log and debug related preferences to factory settings";

  ot->exec = preferences_log_restore_exec;

  ot->flag = OPTYPE_REGISTER;
}
/** \} */

void ED_operatortypes_userpref(void)
{
  WM_operatortype_append(PREFERENCES_OT_reset_default_theme);
  WM_operatortype_append(PREFERENCES_OT_autoexec_path_add);
  WM_operatortype_append(PREFERENCES_OT_autoexec_path_remove);
  WM_operatortype_append(PREFERENCES_OT_log_preferences_save);
  WM_operatortype_append(PREFERENCES_OT_log_preferences_reset_default);
}
