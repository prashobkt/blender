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
 * \ingroup editor/io
 */

/* needed for directory lookup */
#ifndef WIN32
#  include <dirent.h>
#else
#  include "BLI_winstuff.h"
#endif

#include <errno.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_gpencil_types.h"
#include "DNA_space_types.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_main.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.h"

#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "io_gpencil.h"

#include "gpencil_io_exporter.h"

static int wm_gpencil_export_invoke(bContext *C, wmOperator *op, const wmEvent *event)
{
  UNUSED_VARS(event);

  RNA_boolean_set(op->ptr, "init_scene_frame_range", true);

  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    Main *bmain = CTX_data_main(C);
    char filepath[FILE_MAX];

    if (BKE_main_blendfile_path(bmain)[0] == '\0') {
      BLI_strncpy(filepath, "untitled", sizeof(filepath));
    }
    else {
      BLI_strncpy(filepath, BKE_main_blendfile_path(bmain), sizeof(filepath));
    }

    BLI_path_extension_replace(filepath, sizeof(filepath), ".svg");
    RNA_string_set(op->ptr, "filepath", filepath);
  }

  WM_event_add_fileselect(C, op);

  return OPERATOR_RUNNING_MODAL;
}

static ARegion *get_invoke_region(bContext *C)
{
  bScreen *screen = CTX_wm_screen(C);
  if (screen == NULL) {
    return NULL;
  }
  ScrArea *area = BKE_screen_find_big_area(screen, SPACE_VIEW3D, 0);
  if (area == NULL) {
    return NULL;
  }
  ARegion *region = BKE_area_find_region_type(area, RGN_TYPE_WINDOW);

  return region;
}

static bool is_keyframe_empty(bGPdata *gpd, int framenum)
{
  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    if (gpl->flag & GP_LAYER_HIDE) {
      continue;
    }
    LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
      if (gpf->framenum == framenum) {
        return false;
      }
    }
  }
  return true;
}

static int wm_gpencil_export_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
  Object *ob = CTX_data_active_object(C);
  Object *ob_eval_ = (Object *)DEG_get_evaluated_id(depsgraph, &ob->id);
  bGPdata *gpd_eval = (bGPdata *)ob_eval_->data;

  if (!RNA_struct_property_is_set(op->ptr, "filepath")) {
    BKE_report(op->reports, RPT_ERROR, "No filename given");
    return OPERATOR_CANCELLED;
  }

  /*For some reason the region cannot be retrieved from the context.
   * If a better solution is found in the future, remove this function. */
  ARegion *region = get_invoke_region(C);
  if (region == NULL) {
    BKE_report(op->reports, RPT_ERROR, "Unable to find valid 3D View area");
    return OPERATOR_CANCELLED;
  }

  char filename[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filename);

  const bool only_active_frame = RNA_boolean_get(op->ptr, "only_active_frame");
  const bool use_fill = RNA_boolean_get(op->ptr, "use_fill");
  const bool use_norm_thickness = RNA_boolean_get(op->ptr, "use_normalized_thickness");
  const bool use_selected_objects = RNA_boolean_get(op->ptr, "use_selected_objects");

  /* Set flags. */
  int flag = 0;
  SET_FLAG_FROM_TEST(flag, use_fill, GP_EXPORT_FILL);
  SET_FLAG_FROM_TEST(flag, use_norm_thickness, GP_EXPORT_NORM_THICKNESS);
  SET_FLAG_FROM_TEST(flag, use_selected_objects, GP_EXPORT_SELECTED_OBJECTS);

  struct GpencilExportParams params = {
      .C = C,
      .region = region,
      .obact = ob,
      .filename = filename,
      .mode = GP_EXPORT_TO_SVG,
      .frame_start = RNA_int_get(op->ptr, "start"),
      .frame_end = RNA_int_get(op->ptr, "end"),
      .flag = flag,
  };
  /* Take some defaults from the scene, if not specified explicitly. */
  Scene *scene = CTX_data_scene(C);
  if (params.frame_start == INT_MIN) {
    params.frame_start = SFRA;
  }
  if (params.frame_end == INT_MIN) {
    params.frame_end = EFRA;
  }

  int oldframe = (int)DEG_get_ctime(depsgraph);
  bool done = false;

  if (only_active_frame) {
    done = gpencil_io_export(&params);
  }
  else {
    for (int i = params.frame_start; i < params.frame_end + 1; i++) {
      if (is_keyframe_empty(gpd_eval, i)) {
        continue;
      }

      CFRA = i;
      BKE_scene_graph_update_for_newframe(depsgraph, bmain);
      sprintf(params.frame, "%04d", i);

      done |= gpencil_io_export(&params);
    }
  }

  /* Return frame state and DB to original state */
  if (!only_active_frame) {
    CFRA = oldframe;
    BKE_scene_graph_update_for_newframe(depsgraph, bmain);
  }
  if (done) {
    BKE_report(op->reports, RPT_INFO, "SVG export file created");
  }
  else {
    BKE_report(op->reports, RPT_WARNING, "Unable to export SVG");
  }

  return OPERATOR_FINISHED;
}

static void ui_gpencil_export_settings(uiLayout *layout, PointerRNA *imfptr)
{

  uiLayout *box, *row, *col, *sub;

  uiLayoutSetPropSep(layout, true);
  uiLayoutSetPropDecorate(layout, false);

  box = uiLayoutBox(layout);

  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Scene Options"), ICON_SCENE_DATA);

  row = uiLayoutRow(box, false);
  uiItemR(row, imfptr, "use_selected_objects", 0, NULL, ICON_NONE);

  row = uiLayoutRow(box, false);
  uiItemR(row, imfptr, "only_active_frame", 0, NULL, ICON_NONE);

  col = uiLayoutColumn(box, false);

  sub = uiLayoutColumn(col, true);
  uiLayoutSetActive(sub, !RNA_boolean_get(imfptr, "only_active_frame"));
  uiItemR(sub, imfptr, "start", 0, IFACE_("Frame Start"), ICON_NONE);
  uiItemR(sub, imfptr, "end", 0, IFACE_("End"), ICON_NONE);

  box = uiLayoutBox(layout);
  row = uiLayoutRow(box, false);
  uiItemL(row, IFACE_("Export Options"), ICON_SCENE_DATA);

  col = uiLayoutColumn(box, false);

  sub = uiLayoutColumn(col, true);
  uiItemR(sub, imfptr, "use_fill", 0, NULL, ICON_NONE);
  uiItemR(sub, imfptr, "use_normalized_thickness", 0, NULL, ICON_NONE);
}

static void wm_gpencil_export_draw(bContext *C, wmOperator *op)
{

  PointerRNA ptr;

  RNA_pointer_create(NULL, op->type->srna, op->properties, &ptr);

  /* Conveniently set start and end frame to match the scene's frame range. */
  Scene *scene = CTX_data_scene(C);

  if (scene != NULL && RNA_boolean_get(&ptr, "init_scene_frame_range")) {
    RNA_int_set(&ptr, "start", SFRA);
    RNA_int_set(&ptr, "end", EFRA);

    RNA_boolean_set(&ptr, "init_scene_frame_range", false);
  }

  ui_gpencil_export_settings(op->layout, &ptr);
}

static bool wm_gpencil_export_check(bContext *UNUSED(C), wmOperator *op)
{

  char filepath[FILE_MAX];
  RNA_string_get(op->ptr, "filepath", filepath);

  if (!BLI_path_extension_check(filepath, ".svg")) {
    BLI_path_extension_ensure(filepath, FILE_MAX, ".svg");
    RNA_string_set(op->ptr, "filepath", filepath);
    return true;
  }

  return false;
}

static bool wm_gpencil_export_poll(bContext *C)
{
  if (CTX_wm_window(C) == NULL) {
    return false;
  }
  Object *ob = CTX_data_active_object(C);
  if ((ob == NULL) || (ob->type != OB_GPENCIL)) {
    return false;
  }

  bGPdata *gpd = (bGPdata *)ob->data;
  bGPDlayer *gpl = BKE_gpencil_layer_active_get(gpd);

  if (gpl == NULL) {
    return false;
  }

  return true;
}

void WM_OT_gpencil_export(wmOperatorType *ot)
{
  ot->name = "Export Grease Pencil";
  ot->description = "Export current grease pencil";
  ot->idname = "WM_OT_gpencil_export";

  ot->invoke = wm_gpencil_export_invoke;
  ot->exec = wm_gpencil_export_exec;
  ot->poll = wm_gpencil_export_poll;
  ot->ui = wm_gpencil_export_draw;
  ot->check = wm_gpencil_export_check;

  WM_operator_properties_filesel(ot,
                                 FILE_TYPE_OBJECT_IO,
                                 FILE_BLENDER,
                                 FILE_SAVE,
                                 WM_FILESEL_FILEPATH | WM_FILESEL_SHOW_PROPS,
                                 FILE_DEFAULTDISPLAY,
                                 FILE_SORT_ALPHA);

  RNA_def_int(ot->srna,
              "start",
              INT_MIN,
              INT_MIN,
              INT_MAX,
              "Start Frame",
              "Start frame of the export, use the default value to "
              "take the start frame of the current scene",
              INT_MIN,
              INT_MAX);

  RNA_def_int(ot->srna,
              "end",
              INT_MIN,
              INT_MIN,
              INT_MAX,
              "End Frame",
              "End frame of the export, use the default value to "
              "take the end frame of the current scene",
              INT_MIN,
              INT_MAX);

  RNA_def_boolean(ot->srna, "only_active_frame", true, "Active Frame", "Export only active frame");
  RNA_def_boolean(ot->srna, "use_fill", true, "Fill", "Export filled areas");
  RNA_def_boolean(ot->srna,
                  "use_normalized_thickness",
                  false,
                  "Normalize",
                  "Export strokes with constant thickness along the stroke");
  RNA_def_boolean(ot->srna,
                  "use_selected_objects",
                  true,
                  "All Selected Objects",
                  "Export all selected objects, unselect for export active object only");

  /* This dummy prop is used to check whether we need to init the start and
   * end frame values to that of the scene's, otherwise they are reset at
   * every change, draw update. */
  RNA_def_boolean(ot->srna, "init_scene_frame_range", false, "", "");
}
