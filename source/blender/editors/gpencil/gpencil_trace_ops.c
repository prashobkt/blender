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
 * The Original Code is Copyright (C) 2008, Blender Foundation
 * This is a new part of Blender
 * Operator for converting Grease Pencil data to geometry
 */

/** \file
 * \ingroup edgpencil
 */

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"

#include "BLT_translation.h"

#include "DNA_gpencil_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "BKE_context.h"
#include "BKE_duplilist.h"
#include "BKE_gpencil.h"
#include "BKE_image.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_object.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "ED_gpencil.h"

#include "gpencil_trace.h"
#include "potracelib.h"

/* Trace Image to Grease Pencil. */
static bool gp_trace_image_poll(bContext *C)
{
  SpaceLink *sl = CTX_wm_space_data(C);
  if ((sl != NULL) && (sl->spacetype == SPACE_IMAGE)) {
    SpaceImage *sima = CTX_wm_space_image(C);
    return (sima->image != NULL);
  }

  return false;
}

static int gp_trace_image_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  View3D *v3d = CTX_wm_view3d(C);
  SpaceImage *sima = CTX_wm_space_image(C);
  Object *ob_gpencil = NULL;

  potrace_bitmap_t *bm = NULL;
  potrace_param_t *param = NULL;
  potrace_state_t *st = NULL;

  char target[64];
  RNA_string_get(op->ptr, "target", target);
  const int frame_target = RNA_int_get(op->ptr, "frame_target");
  const float threshold = RNA_float_get(op->ptr, "threshold");
  const float scale = RNA_float_get(op->ptr, "scale");
  const float sample = RNA_float_get(op->ptr, "sample");
  const int resolution = RNA_int_get(op->ptr, "resolution");
  const int thickness = RNA_int_get(op->ptr, "thickness");
  const int turnpolicy = RNA_enum_get(op->ptr, "turnpolicy");

  ImBuf *ibuf;
  void *lock;
  ibuf = BKE_image_acquire_ibuf(sima->image, NULL, &lock);

  /* Create an empty BW bitmap. */
  bm = ED_gpencil_trace_bm_new(ibuf->x, ibuf->y);
  if (!bm) {
    return OPERATOR_CANCELLED;
  }

  /* Set tracing parameters, starting from defaults */
  param = potrace_param_default();
  if (!param) {
    return OPERATOR_CANCELLED;
  }
  param->turdsize = 0;
  param->turnpolicy = turnpolicy;

  /* Create a new grease pencil object in origin. */
  bool newob = false;
  if (STREQ(target, "*NEW")) {
    ushort local_view_bits = (v3d && v3d->localvd) ? v3d->local_view_uuid : 0;
    float loc[3] = {0.0f, 0.0f, 0.0f};
    ob_gpencil = ED_gpencil_add_object(C, loc, local_view_bits);
    newob = true;
  }
  else {
    ob_gpencil = BLI_findstring(&bmain->objects, target, offsetof(ID, name) + 2);
  }

  if ((ob_gpencil == NULL) || (ob_gpencil->type != OB_GPENCIL)) {
    BKE_report(op->reports, RPT_ERROR, "Target grease pencil object not valid");
    return OPERATOR_CANCELLED;
  }
  bGPdata *gpd = (bGPdata *)ob_gpencil->data;

  /* If the object has materials means it was created in a previous run. */
  if (ob_gpencil->totcol == 0) {
    const float default_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int r_idx;
    Material *mat_gp = BKE_gpencil_object_material_new(bmain, ob_gpencil, "Material", &r_idx);
    MaterialGPencilStyle *gp_style = mat_gp->gp_style;

    linearrgb_to_srgb_v4(gp_style->stroke_rgba, default_color);
    gp_style->flag |= GP_MATERIAL_STROKE_SHOW;
    gp_style->flag &= ~GP_MATERIAL_FILL_SHOW;
  }

  /* Create Layer and frame. */
  bGPDlayer *gpl = NULL;
  if (!newob) {
    gpl = BKE_gpencil_layer_active_get(gpd);
  }

  if (gpl == NULL) {
    gpl = BKE_gpencil_layer_addnew(gpd, DATA_("Trace"), true);
  }
  bGPDframe *gpf = BKE_gpencil_layer_frame_get(gpl, frame_target, GP_GETFRAME_ADD_NEW);

  /* Load BW bitmap with image. */
  ED_gpencil_trace_image_to_bm(ibuf, bm, threshold);

  /* Trace the bitmap. */
  st = potrace_trace(param, bm);
  if (!st || st->status != POTRACE_STATUS_OK) {
    ED_gpencil_trace_bm_free(bm);
    if (st) {
      potrace_state_free(st);
    }
    potrace_param_free(param);
    return OPERATOR_CANCELLED;
  }
  /* Free BW bitmap. */
  ED_gpencil_trace_bm_free(bm);

  /* Convert the trace to strokes. */
  int offset[2];
  offset[0] = ibuf->x / 2;
  offset[1] = ibuf->y / 2;
  ED_gpencil_trace_data_to_gp(st, ob_gpencil, gpf, offset, scale, sample, resolution, thickness);

  /* Free memory. */
  potrace_state_free(st);
  potrace_param_free(param);

  /* Release ibuf. */
  if (ibuf) {
    BKE_image_release_ibuf(sima->image, ibuf, lock);
  }

  /* notifiers */
  if (newob) {
    DEG_relations_tag_update(bmain);
  }
  DEG_id_tag_update(&scene->id, ID_RECALC_SELECT);
  DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_OBJECT | NA_ADDED, NULL);
  WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);

  return OPERATOR_FINISHED;
}

void GPENCIL_OT_trace_image(wmOperatorType *ot)
{
  static const EnumPropertyItem turnpolicy_type[] = {
      {POTRACE_TURNPOLICY_BLACK,
       "BLACK",
       0,
       "Black",
       "prefers to connect black (foreground) components"},
      {POTRACE_TURNPOLICY_WHITE,
       "WHITE",
       0,
       "White",
       "Prefers to connect white (background) components"},
      {POTRACE_TURNPOLICY_LEFT, "LEFT", 0, "Left", "Always take a left turn"},
      {POTRACE_TURNPOLICY_RIGHT, "RIGHT", 0, "Right", "Always take a right turn"},
      {POTRACE_TURNPOLICY_MINORITY,
       "MINORITY",
       0,
       "Minority",
       "Prefers to connect the color (black or white) that occurs least frequently in a local "
       "neighborhood of the current position"},
      {POTRACE_TURNPOLICY_MAJORITY,
       "MAJORITY",
       0,
       "Majority",
       "Prefers to connect the color (black or white) that occurs most frequently in a local "
       "neighborhood of the current position"},
      {POTRACE_TURNPOLICY_RANDOM, "RANDOM", 0, "Random", "Choose pseudo-randomly"},
      {0, NULL, 0, NULL, NULL},
  };

  /* identifiers */
  ot->name = "Trace Image to Grease Pencil";
  ot->idname = "GPENCIL_OT_trace_image";
  ot->description = "Extract Black and White image to Grease Pencil strokes";

  /* callbacks */
  ot->exec = gp_trace_image_exec;
  ot->poll = gp_trace_image_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_string(ot->srna,
                 "target",
                 "*NEW",
                 64,
                 "Target Object",
                 "Target grease pencil object name. Leave empty for new object");
  RNA_def_int(ot->srna, "frame_target", 1, 1, 100000, "Frame Target", "", 1, 100000);
  RNA_def_int(ot->srna, "thickness", 10, 1, 1000, "Thickness", "", 1, 1000);
  RNA_def_int(
      ot->srna, "resolution", 5, 1, 20, "Resolution", "Resolution of the generated curves", 1, 20);

  RNA_def_float(ot->srna,
                "scale",
                1.0f,
                0.001f,
                100.0f,
                "Scale",
                "Scale of the final stroke",
                0.001f,
                100.0f);
  RNA_def_float(ot->srna,
                "sample",
                0.05f,
                0.001f,
                100.0f,
                "Sample",
                "Distance to sample points",
                0.001f,
                100.0f);
  RNA_def_float_factor(ot->srna,
                       "threshold",
                       0.5f,
                       0.0f,
                       1.0f,
                       "Color Threshold",
                       "Determine what is considered white and what black",
                       0.0f,
                       1.0f);
  RNA_def_enum(ot->srna,
               "turnpolicy",
               turnpolicy_type,
               POTRACE_TURNPOLICY_MINORITY,
               "Turn Policy",
               "Determines how to resolve ambiguities during decomposition of bitmaps into paths");
}
