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

#include "DNA_gpencil_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_object.h"
#include "BKE_report.h"
#include "BKE_scene.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "ED_gpencil.h"

/* Check frame_end is always > start frame! */
static void gp_bake_set_frame_end(struct Main *UNUSED(main),
                                  struct Scene *UNUSED(scene),
                                  struct PointerRNA *ptr)
{
  int frame_start = RNA_int_get(ptr, "frame_start");
  int frame_end = RNA_int_get(ptr, "frame_end");

  if (frame_end <= frame_start) {
    RNA_int_set(ptr, "frame_end", frame_start + 1);
  }
}

/* Extract mesh animation to Grease Pencil. */
static bool gp_bake_mesh_animation_poll(bContext *C)
{
  if (CTX_data_mode_enum(C) != CTX_MODE_OBJECT) {
    return false;
  }

  /* Only if the current view is 3D View. */
  ScrArea *sa = CTX_wm_area(C);
  return (sa && sa->spacetype);
}

static int gp_bake_mesh_animation_exec(bContext *C, wmOperator *op)
{
  Main *bmain = CTX_data_main(C);
  Depsgraph *depsgraph = CTX_data_ensure_evaluated_depsgraph(C);
  Scene *scene = CTX_data_scene(C);
  View3D *v3d = CTX_wm_view3d(C);
  Object *ob = CTX_data_active_object(C);

  /* Cannot check this in poll because the active object changes. */
  if ((ob == NULL) || (ob->type != OB_MESH)) {
    BKE_report(op->reports, RPT_ERROR, "No Mesh object selected");
    return OPERATOR_CANCELLED;
  }

  /* Set cursor to indicate working. */
  WM_cursor_wait(1);

  Object *ob_eval = (Object *)DEG_get_evaluated_object(depsgraph, ob);

  /* Grab all relevant settings. */
  const int step = RNA_int_get(op->ptr, "step");

  const int frame_start = (scene->r.sfra > RNA_int_get(op->ptr, "frame_start")) ?
                              scene->r.sfra :
                              RNA_int_get(op->ptr, "frame_start");

  const int frame_end = (scene->r.efra < RNA_int_get(op->ptr, "frame_end")) ?
                            scene->r.efra :
                            RNA_int_get(op->ptr, "frame_end");

  const float angle = RNA_float_get(op->ptr, "angle");
  const int thickness = RNA_int_get(op->ptr, "thickness");
  const bool use_seams = RNA_boolean_get(op->ptr, "seams");
  const bool use_faces = RNA_boolean_get(op->ptr, "faces");
  const float offset = RNA_float_get(op->ptr, "offset");

  /* Create a new grease pencil object in origin. */
  ushort local_view_bits = (v3d && v3d->localvd) ? v3d->local_view_uuid : 0;
  float loc[3] = {0.0f, 0.0f, 0.0f};
  Object *ob_gpencil = ED_gpencil_add_object(C, loc, local_view_bits);

  /* Loop all frame range. */
  int oldframe = (int)DEG_get_ctime(depsgraph);
  int key = -1;
  for (int i = frame_start; i < frame_end + 1; i++) {
    key++;
    /* Jump if not step limit but include last frame always. */
    if ((key % step != 0) && (i != frame_end)) {
      continue;
    }

    /* Move scene to new frame. */
    CFRA = i;
    BKE_scene_graph_update_for_newframe(depsgraph, bmain);

    /* Generate strokes. */
    BKE_gpencil_convert_mesh(bmain,
                             depsgraph,
                             scene,
                             ob_gpencil,
                             ob,
                             angle,
                             thickness,
                             offset,
                             ob_eval->obmat,
                             use_seams,
                             use_faces);
  }

  /* Return scene frame state and DB to original state. */
  CFRA = oldframe;
  BKE_scene_graph_update_for_newframe(depsgraph, bmain);

  /* Remove unused materials. */
  int actcol = ob_gpencil->actcol;
  for (int slot = 1; slot <= ob_gpencil->totcol; slot++) {
    while (slot <= ob_gpencil->totcol && !BKE_object_material_slot_used(ob_gpencil->data, slot)) {
      ob_gpencil->actcol = slot;
      BKE_object_material_slot_remove(CTX_data_main(C), ob_gpencil);

      if (actcol >= slot) {
        actcol--;
      }
    }
  }
  ob_gpencil->actcol = actcol;

  /* notifiers */
  DEG_id_tag_update(&scene->id, ID_RECALC_SELECT);
  WM_event_add_notifier(C, NC_OBJECT | NA_ADDED, NULL);
  WM_event_add_notifier(C, NC_SCENE | ND_OB_ACTIVE, scene);

  /* Reset cursor. */
  WM_cursor_wait(0);

  /* done */
  return OPERATOR_FINISHED;
}

void GPENCIL_OT_bake_mesh_animation(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Bake Mesh Animation to Grease Pencil";
  ot->idname = "GPENCIL_OT_bake_mesh_animation";
  ot->description = "Bake Mesh Animation to Grease Pencil strokes";

  /* callbacks */
  ot->exec = gp_bake_mesh_animation_exec;
  ot->poll = gp_bake_mesh_animation_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  ot->prop = RNA_def_int(
      ot->srna, "frame_start", 1, 1, 100000, "Start Frame", "The start frame", 1, 100000);

  prop = RNA_def_int(
      ot->srna, "frame_end", 250, 1, 100000, "End Frame", "The end frame of animation", 1, 100000);
  RNA_def_property_update_runtime(prop, gp_bake_set_frame_end);

  prop = RNA_def_int(ot->srna, "step", 1, 1, 100, "Step", "Step between generated frames", 1, 100);

  prop = RNA_def_float_rotation(ot->srna,
                                "angle",
                                0,
                                NULL,
                                DEG2RADF(0.0f),
                                DEG2RADF(180.0f),
                                "Threshold Angle",
                                "Threshold to determine ends of the strokes",
                                DEG2RADF(0.0f),
                                DEG2RADF(180.0f));
  RNA_def_property_float_default(prop, DEG2RADF(70.0f));

  RNA_def_int(ot->srna, "thickness", 1, 1, 100, "Thickness", "", 1, 100);
  RNA_def_boolean(ot->srna, "seams", 0, "Only Seam Edges", "Convert only seam edges");
  RNA_def_boolean(ot->srna, "faces", 1, "Export Faces", "Export faces as filled strokes");
  RNA_def_float_distance(
      ot->srna, "offset", 0.001f, 0.0, 100.0, "Offset", "Offset strokes from fill", 0.0, 100.00);
}
