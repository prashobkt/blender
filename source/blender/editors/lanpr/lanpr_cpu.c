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
 * \ingroup editor/lanpr
 */

/* Test commit for investigating git problems. */

#include "ED_lanpr.h"

#include "BLI_listbase.h"
#include "BLI_linklist.h"
#include "BLI_math_matrix.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"
#include "BLI_alloca.h"

#include "BKE_object.h"
#include "DNA_mesh_types.h"
#include "DNA_camera_types.h"
#include "DNA_modifier_types.h"
#include "DNA_text_types.h"
#include "DNA_lanpr_types.h"
#include "DNA_scene_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_meshdata_types.h"
#include "BKE_customdata.h"
#include "DEG_depsgraph_query.h"
#include "BKE_camera.h"
#include "BKE_gpencil.h"
#include "BKE_collection.h"
#include "BKE_report.h"
#include "BKE_screen.h"
#include "BKE_scene.h"
#include "BKE_text.h"
#include "BKE_context.h"
#include "MEM_guardedalloc.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "BLI_math.h"
#include "BLI_callbacks.h"
#include "BLI_string_utils.h"

#include "bmesh.h"
#include "bmesh_class.h"
#include "bmesh_tools.h"

#include "WM_types.h"
#include "WM_api.h"

#include "BKE_text.h"

extern LANPR_SharedResource lanpr_share;
extern const char *RE_engine_id_BLENDER_LANPR;
struct Object;

/* Own functions */

static LANPR_BoundingArea *lanpr_get_first_possible_bounding_area(LANPR_RenderBuffer *rb,
                                                                  LANPR_RenderLine *rl);

static void lanpr_link_line_with_bounding_area(LANPR_RenderBuffer *rb,
                                               LANPR_BoundingArea *RootBoundingArea,
                                               LANPR_RenderLine *rl);

static LANPR_BoundingArea *lanpr_get_next_bounding_area(LANPR_BoundingArea *This,
                                                        LANPR_RenderLine *rl,
                                                        real x,
                                                        real y,
                                                        real k,
                                                        int PositiveX,
                                                        int PositiveY,
                                                        real *NextX,
                                                        real *NextY);
static int lanpr_triangle_line_imagespace_intersection_v2(SpinLock *spl,
                                                          LANPR_RenderTriangle *rt,
                                                          LANPR_RenderLine *rl,
                                                          Object *cam,
                                                          double vp[4][4],
                                                          double *CameraDir,
                                                          double *From,
                                                          double *To);
static int lanpr_get_line_bounding_areas(LANPR_RenderBuffer *rb,
                                         LANPR_RenderLine *rl,
                                         int *rowBegin,
                                         int *rowEnd,
                                         int *colBegin,
                                         int *colEnd);

/* Layer operations */

static void lanpr_line_layer_unique_name(ListBase *list, LANPR_LineLayer *ll, const char *defname)
{
  BLI_uniquename(list, ll, defname, '.', offsetof(LANPR_LineLayer, name), sizeof(ll->name));
}

int ED_lanpr_max_occlusion_in_line_layers(SceneLANPR *lanpr)
{
  LANPR_LineLayer *lli;
  int max_occ = -1, max;
  for (lli = lanpr->line_layers.first; lli; lli = lli->next) {
    if (lli->use_multiple_levels) {
      max = MAX2(lli->qi_begin, lli->qi_end);
    }
    else {
      max = lli->qi_begin;
    }
    max_occ = MAX2(max, max_occ);
  }
  return max_occ;
}
LANPR_LineLayer *ED_lanpr_new_line_layer(SceneLANPR *lanpr)
{
  LANPR_LineLayer *ll = MEM_callocN(sizeof(LANPR_LineLayer), "Line Layer");

  lanpr_line_layer_unique_name(&lanpr->line_layers, ll, "Layer");

  int max_occ = ED_lanpr_max_occlusion_in_line_layers(lanpr);

  ll->qi_begin = ll->qi_end = max_occ + 1;
  ll->use_same_style = 1;
  ll->thickness = 1.0f;
  copy_v3_fl(ll->color, 0.8);
  ll->color[3] = 1.0f;
  ll->contour.enabled = 1;
  ll->crease.enabled = 1;
  ll->material_separate.enabled = 1;
  ll->edge_mark.enabled = 1;
  ll->intersection.enabled = 1;

  ll->normal_thickness_start = 0.2f;
  ll->normal_thickness_end = 1.5f;
  ll->normal_ramp_begin = 0.0f;
  ll->normal_ramp_end = 1.0f;

  ll->normal_mode = LANPR_NORMAL_DIRECTIONAL;

  lanpr->active_layer = ll;
  BLI_addtail(&lanpr->line_layers, ll);

  return ll;
}
LANPR_LineLayerComponent *ED_lanpr_new_line_component(SceneLANPR *lanpr)
{
  if (!lanpr->active_layer) {
    return 0;
  }
  LANPR_LineLayer *ll = lanpr->active_layer;

  LANPR_LineLayerComponent *llc = MEM_callocN(sizeof(LANPR_LineLayerComponent), "Line Component");
  BLI_addtail(&ll->components, llc);

  return llc;
}
static int lanpr_add_line_layer_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;

  ED_lanpr_new_line_layer(lanpr);

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_delete_line_layer_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;

  LANPR_LineLayer *ll = lanpr->active_layer;

  if (!ll) {
    return OPERATOR_FINISHED;
  }

  if (ll->prev) {
    lanpr->active_layer = ll->prev;
  }
  else if (ll->next) {
    lanpr->active_layer = ll->next;
  }
  else {
    lanpr->active_layer = 0;
  }

  BLI_remlink(&scene->lanpr.line_layers, ll);

  /*  if (ll->batch) GPU_batch_discard(ll->batch); */

  MEM_freeN(ll);

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_move_line_layer_exec(struct bContext *C, struct wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;

  LANPR_LineLayer *ll = lanpr->active_layer;

  if (!ll) {
    return OPERATOR_FINISHED;
  }

  int dir = RNA_enum_get(op->ptr, "direction");

  if (dir == 1 && ll->prev) {
    BLI_remlink(&lanpr->line_layers, ll);
    BLI_insertlinkbefore(&lanpr->line_layers, ll->prev, ll);
  }
  else if (dir == -1 && ll->next) {
    BLI_remlink(&lanpr->line_layers, ll);
    BLI_insertlinkafter(&lanpr->line_layers, ll->next, ll);
  }

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_add_line_component_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;

  ED_lanpr_new_line_component(lanpr);

  return OPERATOR_FINISHED;
}
static int lanpr_delete_line_component_exec(struct bContext *C, struct wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;
  LANPR_LineLayer *ll = lanpr->active_layer;
  LANPR_LineLayerComponent *llc;
  int i = 0;

  if (!ll) {
    return OPERATOR_FINISHED;
  }

  int index = RNA_int_get(op->ptr, "index");

  for (llc = ll->components.first; llc; llc = llc->next) {
    if (index == i) {
      break;
    }
    i++;
  }

  if (llc) {
    BLI_remlink(&ll->components, llc);
    MEM_freeN(llc);
  }

  return OPERATOR_FINISHED;
}

static int ED_lanpr_rebuild_all_commands_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;

  ED_lanpr_rebuild_all_command(lanpr);

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_enable_all_line_types_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;
  LANPR_LineLayer *ll;

  if (!(ll = lanpr->active_layer)) {
    return OPERATOR_FINISHED;
  }

  ll->contour.enabled = 1;
  ll->crease.enabled = 1;
  ll->edge_mark.enabled = 1;
  ll->material_separate.enabled = 1;
  ll->intersection.enabled = 1;

  copy_v3_v3(ll->contour.color, ll->color);
  copy_v3_v3(ll->crease.color, ll->color);
  copy_v3_v3(ll->edge_mark.color, ll->color);
  copy_v3_v3(ll->material_separate.color, ll->color);
  copy_v3_v3(ll->intersection.color, ll->color);

  ll->contour.thickness = 1;
  ll->crease.thickness = 1;
  ll->material_separate.thickness = 1;
  ll->edge_mark.thickness = 1;
  ll->intersection.thickness = 1;

  return OPERATOR_FINISHED;
}
static int lanpr_auto_create_line_layer_exec(struct bContext *C, struct wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;

  LANPR_LineLayer *ll;

  ll = ED_lanpr_new_line_layer(lanpr);
  ll->thickness = 1.7;

  lanpr_enable_all_line_types_exec(C, op);

  ll = ED_lanpr_new_line_layer(lanpr);
  ll->thickness = 0.9;
  copy_v3_fl(ll->color, 0.6);

  lanpr_enable_all_line_types_exec(C, op);

  ll = ED_lanpr_new_line_layer(lanpr);
  ll->thickness = 0.7;
  copy_v3_fl(ll->color, 0.5);

  lanpr_enable_all_line_types_exec(C, op);

  ED_lanpr_rebuild_all_command(lanpr);

  return OPERATOR_FINISHED;
}

void SCENE_OT_lanpr_add_line_layer(struct wmOperatorType *ot)
{

  ot->name = "Add Line Layer";
  ot->description = "Add a new line layer";
  ot->idname = "SCENE_OT_lanpr_add_line_layer";

  ot->exec = lanpr_add_line_layer_exec;
}
void SCENE_OT_lanpr_delete_line_layer(struct wmOperatorType *ot)
{

  ot->name = "Delete Line Layer";
  ot->description = "Delete selected line layer";
  ot->idname = "SCENE_OT_lanpr_delete_line_layer";

  ot->exec = lanpr_delete_line_layer_exec;
}
void SCENE_OT_lanpr_rebuild_all_commands(struct wmOperatorType *ot)
{

  ot->name = "Refresh Drawing Commands";
  ot->description = "Refresh LANPR line layer drawing commands";
  ot->idname = "SCENE_OT_lanpr_rebuild_all_commands";

  ot->exec = ED_lanpr_rebuild_all_commands_exec;
}
void SCENE_OT_lanpr_auto_create_line_layer(struct wmOperatorType *ot)
{

  ot->name = "Auto Create Line Layer";
  ot->description = "Automatically create defalt line layer config";
  ot->idname = "SCENE_OT_lanpr_auto_create_line_layer";

  ot->exec = lanpr_auto_create_line_layer_exec;
}
void SCENE_OT_lanpr_move_line_layer(struct wmOperatorType *ot)
{
  static const EnumPropertyItem line_layer_move[] = {
      {1, "UP", 0, "Up", ""}, {-1, "DOWN", 0, "Down", ""}, {0, NULL, 0, NULL, NULL}};

  ot->name = "Move Line Layer";
  ot->description = "Move LANPR line layer up and down";
  ot->idname = "SCENE_OT_lanpr_move_line_layer";

  /*  this need property to assign up/down direction */

  ot->exec = lanpr_move_line_layer_exec;

  RNA_def_enum(ot->srna,
               "direction",
               line_layer_move,
               0,
               "Direction",
               "Direction to move the active line layer towards");
}
void SCENE_OT_lanpr_enable_all_line_types(struct wmOperatorType *ot)
{
  ot->name = "Enable All Line Types";
  ot->description = "Enable All Line Types In This Line Layer";
  ot->idname = "SCENE_OT_lanpr_enable_all_line_types";

  ot->exec = lanpr_enable_all_line_types_exec;
}
void SCENE_OT_lanpr_add_line_component(struct wmOperatorType *ot)
{

  ot->name = "Add Line Component";
  ot->description = "Add a new line Component";
  ot->idname = "SCENE_OT_lanpr_add_line_component";

  ot->exec = lanpr_add_line_component_exec;
}
void SCENE_OT_lanpr_delete_line_component(struct wmOperatorType *ot)
{

  ot->name = "Delete Line Component";
  ot->description = "Delete selected line component";
  ot->idname = "SCENE_OT_lanpr_delete_line_component";

  ot->exec = lanpr_delete_line_component_exec;

  RNA_def_int(ot->srna, "index", 0, 0, 10000, "index", "index of this line component", 0, 10000);
}

/* Geometry */

int use_smooth_contour_modifier_contour = 0; /*  debug purpose */

static void lanpr_cut_render_line(LANPR_RenderBuffer *rb,
                                  LANPR_RenderLine *rl,
                                  real Begin,
                                  real End)
{
  LANPR_RenderLineSegment *rls = rl->segments.first, *irls;
  LANPR_RenderLineSegment *begin_segment = 0, *end_segment = 0;
  LANPR_RenderLineSegment *ns = 0, *ns2 = 0;
  int untouched = 0;

  if (TNS_DOUBLE_CLOSE_ENOUGH(Begin, End)) {
    return;
  }

  if (Begin != Begin) {
    Begin = 0;
  }
  if (End != End) {
    End = 0;
  }

  if (Begin > End) {
    real t = Begin;
    Begin = End;
    End = t;
  }

  for (rls = rl->segments.first; rls; rls = rls->next) {
    if (TNS_DOUBLE_CLOSE_ENOUGH(rls->at, Begin)) {
      begin_segment = rls;
      ns = begin_segment;
      break;
    }
    if (!rls->next) {
      break;
    }
    irls = rls->next;
    if (irls->at > Begin + 1e-09 && Begin > rls->at) {
      begin_segment = irls;
      ns = mem_static_aquire_thread(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
      break;
    }
  }
  if (!begin_segment && TNS_DOUBLE_CLOSE_ENOUGH(1, End)) {
    untouched = 1;
  }
  for (rls = begin_segment; rls; rls = rls->next) {
    if (TNS_DOUBLE_CLOSE_ENOUGH(rls->at, End)) {
      end_segment = rls;
      ns2 = end_segment;
      break;
    }
    /*  irls = rls->next; */
    /*  added this to prevent rls->at == 1.0 (we don't need an end point for this) */
    if (!rls->next && TNS_DOUBLE_CLOSE_ENOUGH(1, End)) {
      end_segment = rls;
      ns2 = end_segment;
      untouched = 1;
      break;
    }
    else if (rls->at > End) {
      end_segment = rls;
      ns2 = mem_static_aquire_thread(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
      break;
    }
  }

  if (!ns) {
    ns = mem_static_aquire_thread(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
  }
  if (!ns2) {
    if (untouched) {
      ns2 = ns;
      end_segment = ns2;
    }
    else
      ns2 = mem_static_aquire_thread(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
  }

  if (begin_segment) {
    if (begin_segment != ns) {
      ns->occlusion = begin_segment->prev ? (irls = begin_segment->prev)->occlusion : 0;
      BLI_insertlinkbefore(&rl->segments, (void *)begin_segment, (void *)ns);
    }
  }
  else {
    ns->occlusion = (irls = rl->segments.last)->occlusion;
    BLI_addtail(&rl->segments, ns);
  }
  if (end_segment) {
    if (end_segment != ns2) {
      ns2->occlusion = end_segment->prev ? (irls = end_segment->prev)->occlusion : 0;
      BLI_insertlinkbefore(&rl->segments, (void *)end_segment, (void *)ns2);
    }
  }
  else {
    ns2->occlusion = (irls = rl->segments.last)->occlusion;
    BLI_addtail(&rl->segments, ns2);
  }

  ns->at = Begin;
  if (!untouched) {
    ns2->at = End;
  }
  else {
    ns2 = ns2->next;
  }

  for (rls = ns; rls && rls != ns2; rls = rls->next) {
    rls->occlusion++;
  }

  char min_occ = 127;
  for (rls = rl->segments.first; rls; rls = rls->next) {
    min_occ = MIN2(min_occ, rls->occlusion);
  }
  rl->min_occ = min_occ;
}
static int lanpr_make_next_occlusion_task_info(LANPR_RenderBuffer *rb, LANPR_RenderTaskInfo *rti)
{
  LinkData *data;
  int i;
  int res = 0;

  BLI_spin_lock(&rb->cs_management);

  if (rb->contour_managed) {
    data = rb->contour_managed;
    rti->contour = (void *)data;
    rti->contour_pointers.first = data;
    for (i = 0; i < TNS_THREAD_LINE_COUNT && data; i++) {
      data = data->next;
    }
    rb->contour_managed = data;
    rti->contour_pointers.last = data ? data->prev : rb->contours.last;
    res = 1;
  }
  else {
    BLI_listbase_clear(&rti->contour_pointers);
    rti->contour = 0;
  }

  if (rb->intersection_managed) {
    data = rb->intersection_managed;
    rti->intersection = (void *)data;
    rti->intersection_pointers.first = data;
    for (i = 0; i < TNS_THREAD_LINE_COUNT && data; i++) {
      data = data->next;
    }
    rb->intersection_managed = data;
    rti->intersection_pointers.last = data ? data->prev : rb->intersection_lines.last;
    res = 1;
  }
  else {
    BLI_listbase_clear(&rti->intersection_pointers);
    rti->intersection = 0;
  }

  if (rb->crease_managed) {
    data = rb->crease_managed;
    rti->crease = (void *)data;
    rti->crease_pointers.first = data;
    for (i = 0; i < TNS_THREAD_LINE_COUNT && data; i++) {
      data = data->next;
    }
    rb->crease_managed = data;
    rti->crease_pointers.last = data ? data->prev : rb->crease_lines.last;
    res = 1;
  }
  else {
    BLI_listbase_clear(&rti->crease_pointers);
    rti->crease = 0;
  }

  if (rb->material_managed) {
    data = rb->material_managed;
    rti->material = (void *)data;
    rti->material_pointers.first = data;
    for (i = 0; i < TNS_THREAD_LINE_COUNT && data; i++) {
      data = data->next;
    }
    rb->material_managed = data;
    rti->material_pointers.last = data ? data->prev : rb->material_lines.last;
    res = 1;
  }
  else {
    BLI_listbase_clear(&rti->material_pointers);
    rti->material = 0;
  }

  if (rb->edge_mark_managed) {
    data = rb->edge_mark_managed;
    rti->edge_mark = (void *)data;
    rti->edge_mark_pointers.first = data;
    for (i = 0; i < TNS_THREAD_LINE_COUNT && data; i++) {
      data = data->next;
    }
    rb->edge_mark_managed = data;
    rti->edge_mark_pointers.last = data ? data->prev : rb->edge_marks.last;
    res = 1;
  }
  else {
    BLI_listbase_clear(&rti->edge_mark_pointers);
    rti->edge_mark = 0;
  }

  BLI_spin_unlock(&rb->cs_management);

  return res;
}
static void lanpr_calculate_single_line_occlusion(LANPR_RenderBuffer *rb,
                                                  LANPR_RenderLine *rl,
                                                  int thread_id)
{
  real x = rl->l->fbcoord[0], y = rl->l->fbcoord[1];
  LANPR_BoundingArea *ba = lanpr_get_first_possible_bounding_area(rb, rl);
  LANPR_BoundingArea *nba = ba;
  LANPR_RenderTriangleThread *rt;
  LinkData *lip;
  Object *c = rb->scene->camera;
  real l, r;
  real k = (rl->r->fbcoord[1] - rl->l->fbcoord[1]) /
           (rl->r->fbcoord[0] - rl->l->fbcoord[0] + 1e-30);
  int PositiveX = (rl->r->fbcoord[0] - rl->l->fbcoord[0]) > 0 ?
                      1 :
                      (rl->r->fbcoord[0] == rl->l->fbcoord[0] ? 0 : -1);
  int PositiveY = (rl->r->fbcoord[1] - rl->l->fbcoord[1]) > 0 ?
                      1 :
                      (rl->r->fbcoord[1] == rl->l->fbcoord[1] ? 0 : -1);

  /*  printf("PX %d %lf   PY %d %lf\n", PositiveX, rl->r->fbcoord[0] - */
  /*  rl->l->fbcoord[0], PositiveY, rl->r->fbcoord[1] - */
  /*  rl->l->fbcoord[1]); */

  while (nba) {

    for (lip = nba->linked_triangles.first; lip; lip = lip->next) {
      rt = lip->data;
      if (rt->testing[thread_id] == rl || rl->l->intersecting_with == (void *)rt ||
          rl->r->intersecting_with == (void *)rt) {
        continue;
      }
      rt->testing[thread_id] = rl;
      if (lanpr_triangle_line_imagespace_intersection_v2(&rb->cs_management,
                                                         (void *)rt,
                                                         rl,
                                                         c,
                                                         rb->view_projection,
                                                         rb->view_vector,
                                                         &l,
                                                         &r)) {
        lanpr_cut_render_line(rb, rl, l, r);
        if (rl->min_occ > rb->max_occlusion_level) {
          return; /* No need to caluclate any longer. */
        }
      }
    }

    nba = lanpr_get_next_bounding_area(nba, rl, x, y, k, PositiveX, PositiveY, &x, &y);
  }
}
static void lanpr_THREAD_calculate_line_occlusion(TaskPool *__restrict UNUSED(pool),
                                                  LANPR_RenderTaskInfo *rti,
                                                  int UNUSED(threadid))
{
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;
  LinkData *lip;

  while (lanpr_make_next_occlusion_task_info(rb, rti)) {

    for (lip = (void *)rti->contour; lip && lip->prev != rti->contour_pointers.last;
         lip = lip->next) {
      lanpr_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    for (lip = (void *)rti->crease; lip && lip->prev != rti->crease_pointers.last;
         lip = lip->next) {
      lanpr_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    for (lip = (void *)rti->intersection; lip && lip->prev != rti->intersection_pointers.last;
         lip = lip->next) {
      lanpr_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    for (lip = (void *)rti->material; lip && lip->prev != rti->material_pointers.last;
         lip = lip->next) {
      lanpr_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    for (lip = (void *)rti->edge_mark; lip && lip->prev != rti->edge_mark_pointers.last;
         lip = lip->next) {
      lanpr_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }
  }
}
static void lanpr_THREAD_calculate_line_occlusion_begin(LANPR_RenderBuffer *rb)
{
  int thread_count = rb->thread_count;
  LANPR_RenderTaskInfo *rti = MEM_callocN(sizeof(LANPR_RenderTaskInfo) * thread_count,
                                          "Task Pool");
  TaskScheduler *scheduler = BLI_task_scheduler_get();
  int i;

  rb->contour_managed = rb->contours.first;
  rb->crease_managed = rb->crease_lines.first;
  rb->intersection_managed = rb->intersection_lines.first;
  rb->material_managed = rb->material_lines.first;
  rb->edge_mark_managed = rb->edge_marks.first;

  TaskPool *tp = BLI_task_pool_create(scheduler, 0);

  for (i = 0; i < thread_count; i++) {
    rti[i].thread_id = i;
    BLI_task_pool_push(tp,
                       (TaskRunFunction)lanpr_THREAD_calculate_line_occlusion,
                       &rti[i],
                       0,
                       TASK_PRIORITY_HIGH);
  }
  BLI_task_pool_work_and_wait(tp);
  BLI_task_pool_free(tp);

  MEM_freeN(rti);
}

int ED_lanpr_point_inside_triangled(tnsVector2d v, tnsVector2d v0, tnsVector2d v1, tnsVector2d v2)
{
  double cl, c;

  cl = (v0[0] - v[0]) * (v1[1] - v[1]) - (v0[1] - v[1]) * (v1[0] - v[0]);
  c = cl;

  cl = (v1[0] - v[0]) * (v2[1] - v[1]) - (v1[1] - v[1]) * (v2[0] - v[0]);
  if (c * cl <= 0) {
    return 0;
  }
  else
    c = cl;

  cl = (v2[0] - v[0]) * (v0[1] - v[1]) - (v2[1] - v[1]) * (v0[0] - v[0]);
  if (c * cl <= 0) {
    return 0;
  }
  else
    c = cl;

  cl = (v0[0] - v[0]) * (v1[1] - v[1]) - (v0[1] - v[1]) * (v1[0] - v[0]);
  if (c * cl <= 0) {
    return 0;
  }

  return 1;
}
static int lanpr_point_on_lined(tnsVector2d v, tnsVector2d v0, tnsVector2d v1)
{
  real c1, c2;

  c1 = tMatGetLinearRatio(v0[0], v1[0], v[0]);
  c2 = tMatGetLinearRatio(v0[1], v1[1], v[1]);

  if (TNS_DOUBLE_CLOSE_ENOUGH(c1, c2) && c1 >= 0 && c1 <= 1) {
    return 1;
  }

  return 0;
}
static int lanpr_point_triangle_relation(tnsVector2d v,
                                         tnsVector2d v0,
                                         tnsVector2d v1,
                                         tnsVector2d v2)
{
  double cl, c;
  real r;
  if (lanpr_point_on_lined(v, v0, v1) || lanpr_point_on_lined(v, v1, v2) ||
      lanpr_point_on_lined(v, v2, v0)) {
    return 1;
  }

  cl = (v0[0] - v[0]) * (v1[1] - v[1]) - (v0[1] - v[1]) * (v1[0] - v[0]);
  c = cl;

  cl = (v1[0] - v[0]) * (v2[1] - v[1]) - (v1[1] - v[1]) * (v2[0] - v[0]);
  if ((r = c * cl) < 0) {
    return 0;
  }
  /*  else if(r == 0) return 1; // removed, point could still be on the extention line of some edge
   */
  else
    c = cl;

  cl = (v2[0] - v[0]) * (v0[1] - v[1]) - (v2[1] - v[1]) * (v0[0] - v[0]);
  if ((r = c * cl) < 0) {
    return 0;
  }
  /*  else if(r == 0) return 1; */
  else
    c = cl;

  cl = (v0[0] - v[0]) * (v1[1] - v[1]) - (v0[1] - v[1]) * (v1[0] - v[0]);
  if ((r = c * cl) < 0) {
    return 0;
  }
  else if (r == 0) {
    return 1;
  }

  return 2;
}
static int lanpr_point_inside_triangle3de(tnsVector3d v,
                                          tnsVector3d v0,
                                          tnsVector3d v1,
                                          tnsVector3d v2)
{
  tnsVector3d l, r;
  tnsVector3d N1, N2;
  real d;

  sub_v3_v3v3_db(l, v1, v0);
  sub_v3_v3v3_db(r, v, v1);
  /*  tmat_normalize_self_3d(l); */
  /*  tmat_normalize_self_3d(r); */
  cross_v3_v3v3_db(N1, l, r);

  sub_v3_v3v3_db(l, v2, v1);
  sub_v3_v3v3_db(r, v, v2);
  /*  tmat_normalize_self_3d(l); */
  /*  tmat_normalize_self_3d(r); */
  cross_v3_v3v3_db(N2, l, r);

  if ((d = dot_v3v3_db(N1, N2)) < 0) {
    return 0;
  }
  /*  if (d<DBL_EPSILON) return -1; */

  sub_v3_v3v3_db(l, v0, v2);
  sub_v3_v3v3_db(r, v, v0);
  /*  tmat_normalize_self_3d(l); */
  /*  tmat_normalize_self_3d(r); */
  cross_v3_v3v3_db(N1, l, r);

  if ((d = dot_v3v3_db(N1, N2)) < 0) {
    return 0;
  }
  /*  if (d<DBL_EPSILON) return -1; */

  sub_v3_v3v3_db(l, v1, v0);
  sub_v3_v3v3_db(r, v, v1);
  /*  tmat_normalize_self_3d(l); */
  /*  tmat_normalize_self_3d(r); */
  cross_v3_v3v3_db(N2, l, r);

  if ((d = dot_v3v3_db(N1, N2)) < 0) {
    return 0;
  }
  /*  if (d<DBL_EPSILON) return -1; */

  return 1;
}

static LANPR_RenderElementLinkNode *lanpr_new_cull_triangle_space64(LANPR_RenderBuffer *rb)
{
  LANPR_RenderElementLinkNode *reln;

  LANPR_RenderTriangle *RenderTriangles = mem_static_aquire(
      &rb->render_data_pool,
      64 * rb->triangle_size); /*  CreateNewBuffer(LANPR_RenderTriangle, 64); */

  reln = list_append_pointer_static_sized(&rb->triangle_buffer_pointers,
                                          &rb->render_data_pool,
                                          RenderTriangles,
                                          sizeof(LANPR_RenderElementLinkNode));
  reln->element_count = 64;
  reln->additional = 1;

  return reln;
}
static LANPR_RenderElementLinkNode *lanpr_new_cull_point_space64(LANPR_RenderBuffer *rb)
{
  LANPR_RenderElementLinkNode *reln;

  LANPR_RenderVert *Rendervertices = mem_static_aquire(
      &rb->render_data_pool,
      sizeof(LANPR_RenderVert) * 64); /*  CreateNewBuffer(LANPR_RenderVert, 64); */

  reln = list_append_pointer_static_sized(&rb->vertex_buffer_pointers,
                                          &rb->render_data_pool,
                                          Rendervertices,
                                          sizeof(LANPR_RenderElementLinkNode));
  reln->element_count = 64;
  reln->additional = 1;

  return reln;
}
static void lanpr_assign_render_line_with_triangle(LANPR_RenderTriangle *rt)
{
  if (!rt->rl[0]->tl) {
    rt->rl[0]->tl = rt;
  }
  else if (!rt->rl[0]->tr) {
    rt->rl[0]->tr = rt;
  }

  if (!rt->rl[1]->tl) {
    rt->rl[1]->tl = rt;
  }
  else if (!rt->rl[1]->tr) {
    rt->rl[1]->tr = rt;
  }

  if (!rt->rl[2]->tl) {
    rt->rl[2]->tl = rt;
  }
  else if (!rt->rl[2]->tr) {
    rt->rl[2]->tr = rt;
  }
}
static void lanpr_post_triangle(LANPR_RenderTriangle *rt, LANPR_RenderTriangle *orig)
{
  if (rt->v[0]) {
    add_v3_v3_db(rt->gc, rt->v[0]->fbcoord);
  }
  if (rt->v[1]) {
    add_v3_v3_db(rt->gc, rt->v[1]->fbcoord);
  }
  if (rt->v[2]) {
    add_v3_v3_db(rt->gc, rt->v[2]->fbcoord);
  }
  mul_v3db_db(rt->gc, 1.0f / 3.0f);

  copy_v3_v3_db(rt->gn, orig->gn);
}

#define RT_AT(head, rb, offset) ((BYTE *)head + offset * rb->triangle_size)

static void lanpr_cull_triangles(LANPR_RenderBuffer *rb)
{
  LANPR_RenderLine *rl;
  LANPR_RenderTriangle *rt, *rt1, *rt2;
  LANPR_RenderVert *rv;
  LANPR_RenderElementLinkNode *reln, *veln, *teln;
  LANPR_RenderLineSegment *rls;
  double **vp = rb->view_projection;
  int i;
  real a;
  int v_count = 0, t_count = 0;
  Object *o;

  real cam_pos[3];
  Object *cam = ((Object *)rb->scene->camera);
  cam_pos[0] = cam->obmat[3][0];
  cam_pos[1] = cam->obmat[3][1];
  cam_pos[2] = cam->obmat[3][2];

  real view_dir[3], clip_advance[3];
  copy_v3_v3_db(view_dir, rb->view_vector);
  copy_v3_v3_db(clip_advance, rb->view_vector);
  mul_v3db_db(clip_advance, -((Camera *)cam->data)->clip_start);
  add_v3_v3_db(cam_pos, clip_advance);

  veln = lanpr_new_cull_point_space64(rb);
  teln = lanpr_new_cull_triangle_space64(rb);
  rv = &((LANPR_RenderVert *)veln->pointer)[v_count];
  rt1 = (void *)(((BYTE *)teln->pointer) + rb->triangle_size * t_count);

  for (reln = rb->triangle_buffer_pointers.first; reln; reln = reln->next) {
    if (reln->additional) {
      continue;
    }
    o = reln->object_ref;
    for (i = 0; i < reln->element_count; i++) {
      int In1 = 0, In2 = 0, In3 = 0;
      rt = (void *)(((BYTE *)reln->pointer) + rb->triangle_size * i);
      if (rt->v[0]->fbcoord[3] < ((Camera *)cam->data)->clip_start) {
        In1 = 1;
      }
      if (rt->v[1]->fbcoord[3] < ((Camera *)cam->data)->clip_start) {
        In2 = 1;
      }
      if (rt->v[2]->fbcoord[3] < ((Camera *)cam->data)->clip_start) {
        In3 = 1;
      }

      if (v_count > 60) {
        veln->element_count = v_count;
        veln = lanpr_new_cull_point_space64(rb);
        v_count = 0;
      }

      if (t_count > 60) {
        teln->element_count = t_count;
        teln = lanpr_new_cull_triangle_space64(rb);
        t_count = 0;
      }

      /*  if ((!rt->rl[0]->next && !rt->rl[0]->prev) || */
      /*     (!rt->rl[1]->next && !rt->rl[1]->prev) || */
      /*     (!rt->rl[2]->next && !rt->rl[2]->prev)) { */
      /* 	printf("'"); // means this triangle is lonely???? */
      /* } */

      rv = &((LANPR_RenderVert *)veln->pointer)[v_count];
      rt1 = (void *)(((BYTE *)teln->pointer) + rb->triangle_size * t_count);
      rt2 = (void *)(((BYTE *)teln->pointer) + rb->triangle_size * (t_count + 1));

      real vv1[3], vv2[3], dot1, dot2;

      switch (In1 + In2 + In3) {
        case 0:
          continue;
        case 3:
          rt->cull_status = LANPR_CULL_DISCARD;
          BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
          rt->rl[0]->next = rt->rl[0]->prev = 0;
          BLI_remlink(&rb->all_render_lines, (void *)rt->rl[1]);
          rt->rl[1]->next = rt->rl[1]->prev = 0;
          BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
          rt->rl[2]->next = rt->rl[2]->prev = 0;
          continue;
        case 2:
          rt->cull_status = LANPR_CULL_USED;
          if (!In1) {
            sub_v3_v3v3_db(vv1, rt->v[0]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[2]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[0].gloc, rt->v[0]->gloc, rt->v[2]->gloc, a);
            mul_v4_m4v3_db(rv[0].fbcoord, vp, rv[0].gloc);

            sub_v3_v3v3_db(vv1, rt->v[0]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[1]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[1].gloc, rt->v[0]->gloc, rt->v[1]->gloc, a);
            mul_v4_m4v3_db(rv[1].fbcoord, vp, rv[1].gloc);

            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
            rt->rl[0]->next = rt->rl[0]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[1]);
            rt->rl[1]->next = rt->rl[1]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
            rt->rl[2]->next = rt->rl[2]->prev = 0;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = rt->v[0];
            rl->tl = rt->rl[0]->tl == rt ? rt1 : rt->rl[0]->tl;
            rl->tr = rt->rl[0]->tr == rt ? rt1 : rt->rl[0]->tr;
            rt1->rl[0] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[0];
            rl->r = &rv[0];
            rl->tl = rt->rl[2]->tl == rt ? rt1 : rt->rl[2]->tl;
            rl->tr = rt->rl[2]->tr == rt ? rt1 : rt->rl[2]->tr;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rt1->v[0] = rt->v[0];
            rt1->v[1] = &rv[1];
            rt1->v[2] = &rv[0];

            lanpr_post_triangle(rt1, rt);

            v_count += 2;
            t_count += 1;
            continue;
          }
          else if (!In3) {
            sub_v3_v3v3_db(vv1, rt->v[2]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[0]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[0].gloc, rt->v[2]->gloc, rt->v[0]->gloc, a);
            mul_v4_m4v3_db(rv[0].fbcoord, vp, rv[0].gloc);

            sub_v3_v3v3_db(vv1, rt->v[2]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[1]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[1].gloc, rt->v[2]->gloc, rt->v[1]->gloc, a);
            mul_v4_m4v3_db(rv[1].fbcoord, vp, rv[1].gloc);

            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
            rt->rl[0]->next = rt->rl[0]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[1]);
            rt->rl[1]->next = rt->rl[1]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
            rt->rl[2]->next = rt->rl[2]->prev = 0;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = &rv[1];
            rl->tl = rt1;
            rt1->rl[0] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = rt->v[2];
            rl->tl = rt->rl[1]->tl == rt ? rt1 : rt->rl[1]->tl;
            rl->tr = rt->rl[1]->tr == rt ? rt1 : rt->rl[1]->tr;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[2];
            rl->r = &rv[0];
            rl->tl = rt->rl[2]->tl == rt ? rt1 : rt->rl[2]->tl;
            rl->tr = rt->rl[2]->tr == rt ? rt1 : rt->rl[2]->tr;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rt1->v[0] = &rv[1];
            rt1->v[1] = rt->v[2];
            rt1->v[2] = &rv[0];

            lanpr_post_triangle(rt1, rt);

            v_count += 2;
            t_count += 1;
            continue;
          }
          else if (!In2) {
            sub_v3_v3v3_db(vv1, rt->v[1]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[2]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[0].gloc, rt->v[1]->gloc, rt->v[2]->gloc, a);
            mul_v4_m4v3_db(rv[0].fbcoord, vp, rv[0].gloc);

            sub_v3_v3v3_db(vv1, rt->v[1]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[0]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[1].gloc, rt->v[1]->gloc, rt->v[0]->gloc, a);
            mul_v4_m4v3_db(rv[1].fbcoord, vp, rv[1].gloc);

            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
            rt->rl[0]->next = rt->rl[0]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[1]);
            rt->rl[1]->next = rt->rl[1]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
            rt->rl[2]->next = rt->rl[2]->prev = 0;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = rt->v[1];
            rl->tl = rt->rl[0]->tl == rt ? rt1 : rt->rl[0]->tl;
            rl->tr = rt->rl[0]->tr == rt ? rt1 : rt->rl[0]->tr;
            rt1->rl[0] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[1];
            rl->r = &rv[1];
            rl->tl = rt->rl[1]->tl == rt ? rt1 : rt->rl[1]->tl;
            rl->tr = rt->rl[1]->tr == rt ? rt1 : rt->rl[1]->tr;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rt1->v[0] = rt->v[1];
            rt1->v[1] = &rv[1];
            rt1->v[2] = &rv[0];

            lanpr_post_triangle(rt1, rt);

            v_count += 2;
            t_count += 1;
            continue;
          }
          break;
        case 1:
          rt->cull_status = LANPR_CULL_USED;
          if (In1) {
            sub_v3_v3v3_db(vv1, rt->v[1]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[0]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot2 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[0].gloc, rt->v[0]->gloc, rt->v[1]->gloc, a);
            mul_v4_m4v3_db(rv[0].fbcoord, vp, rv[0].gloc);

            sub_v3_v3v3_db(vv1, rt->v[2]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[0]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot2 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[1].gloc, rt->v[0]->gloc, rt->v[2]->gloc, a);
            mul_v4_m4v3_db(rv[1].fbcoord, vp, rv[1].gloc);

            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
            rt->rl[0]->next = rt->rl[0]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
            rt->rl[2]->next = rt->rl[2]->prev = 0;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = rt->v[1];
            rl->tl = rt1;
            rl->tr = rt->rl[0]->tr == rt ? rt->rl[0]->tl : rt->rl[0]->tr;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[1];
            rl->r = &rv[1];
            rl->tl = rt1;
            rl->tr = rt2;
            rt1->rl[0] = rl;
            rt2->rl[0] = rl;
            rl->object_ref = o;

            rt1->v[0] = rt->v[1];
            rt1->v[1] = &rv[1];
            rt1->v[2] = &rv[0];

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[2];
            rl->r = &rv[1];
            rl->tl = rt2;
            rl->tr = rt->rl[2]->tr == rt ? rt->rl[2]->tl : rt->rl[2]->tr;
            rt2->rl[2] = rl;
            rt2->rl[1] = rt->rl[1];
            rl->object_ref = o;

            rt2->v[0] = &rv[1];
            rt2->v[1] = rt->v[1];
            rt2->v[2] = rt->v[2];

            lanpr_post_triangle(rt1, rt);
            lanpr_post_triangle(rt2, rt);

            v_count += 2;
            t_count += 2;
            continue;
          }
          else if (In2) {

            sub_v3_v3v3_db(vv1, rt->v[1]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[2]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[0].gloc, rt->v[1]->gloc, rt->v[2]->gloc, a);
            mul_v4_m4v3_db(rv[0].fbcoord, vp, rv[0].gloc);

            sub_v3_v3v3_db(vv1, rt->v[1]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[0]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[1].gloc, rt->v[1]->gloc, rt->v[0]->gloc, a);
            mul_v4_m4v3_db(rv[1].fbcoord, vp, rv[1].gloc);

            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
            rt->rl[0]->next = rt->rl[0]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[1]);
            rt->rl[1]->next = rt->rl[1]->prev = 0;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = rt->v[2];
            rl->tl = rt1;
            rl->tr = rt->rl[1]->tl == rt ? rt->rl[1]->tr : rt->rl[1]->tl;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[2];
            rl->r = &rv[1];
            rl->tl = rt1;
            rl->tr = rt2;
            rt1->rl[0] = rl;
            rt2->rl[0] = rl;
            rl->object_ref = o;

            rt1->v[0] = rt->v[2];
            rt1->v[1] = &rv[1];
            rt1->v[2] = &rv[0];

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[0];
            rl->r = &rv[1];
            rl->tl = rt2;
            rl->tr = rt->rl[0]->tr == rt ? rt->rl[0]->tl : rt->rl[0]->tr;
            rt2->rl[2] = rl;
            rt2->rl[1] = rt->rl[2];
            rl->object_ref = o;

            rt2->v[0] = &rv[1];
            rt2->v[1] = rt->v[2];
            rt2->v[2] = rt->v[0];

            lanpr_post_triangle(rt1, rt);
            lanpr_post_triangle(rt2, rt);

            v_count += 2;
            t_count += 2;
            continue;
          }
          else if (In3) {

            sub_v3_v3v3_db(vv1, rt->v[2]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[0]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[0].gloc, rt->v[2]->gloc, rt->v[0]->gloc, a);
            mul_v4_m4v3_db(rv[0].fbcoord, vp, rv[0].gloc);

            sub_v3_v3v3_db(vv1, rt->v[2]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[1]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            interp_v3_v3v3_db(rv[1].gloc, rt->v[2]->gloc, rt->v[1]->gloc, a);
            mul_v4_m4v3_db(rv[1].fbcoord, vp, rv[1].gloc);

            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[1]);
            rt->rl[1]->next = rt->rl[1]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
            rt->rl[2]->next = rt->rl[2]->prev = 0;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = rt->v[0];
            rl->tl = rt1;
            rl->tr = rt->rl[2]->tl == rt ? rt->rl[2]->tr : rt->rl[2]->tl;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[0];
            rl->r = &rv[1];
            rl->tl = rt1;
            rl->tr = rt2;
            rt1->rl[0] = rl;
            rt2->rl[0] = rl;
            rl->object_ref = o;

            rt1->v[0] = rt->v[0];
            rt1->v[1] = &rv[1];
            rt1->v[2] = &rv[0];

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[1];
            rl->r = &rv[1];
            rl->tl = rt2;
            rl->tr = rt->rl[1]->tr == rt ? rt->rl[1]->tl : rt->rl[1]->tr;
            rt2->rl[2] = rl;
            rt2->rl[1] = rt->rl[0];
            rl->object_ref = o;

            rt2->v[0] = &rv[1];
            rt2->v[1] = rt->v[0];
            rt2->v[2] = rt->v[1];

            lanpr_post_triangle(rt1, rt);
            lanpr_post_triangle(rt2, rt);

            v_count += 2;
            t_count += 2;
            continue;
          }
          break;
      }
    }
    teln->element_count = t_count;
    veln->element_count = v_count;
  }
}
static void lanpr_perspective_division(LANPR_RenderBuffer *rb)
{
  LANPR_RenderVert *rv;
  LANPR_RenderElementLinkNode *reln;
  Camera *cam = rb->scene->camera->data;
  int i;

  if (cam->type != CAM_PERSP) {
    return;
  }

  for (reln = rb->vertex_buffer_pointers.first; reln; reln = reln->next) {
    rv = reln->pointer;
    for (i = 0; i < reln->element_count; i++) {
      /*  if (rv->fbcoord[2] < -DBL_EPSILON) continue; */
      mul_v3db_db(rv[i].fbcoord, 1 / rv[i].fbcoord[3]);
      /*  rv[i].fbcoord[2] = cam->clipsta * cam->clipend / (cam->clipend - */
      /*  fabs(rv[i].fbcoord[2]) * (cam->clipend - cam->clipsta)); */

      rv[i].fbcoord[0] -= cam->shiftx * 2;
      rv[i].fbcoord[1] -= cam->shifty * 2;
    }
  }
}

static void lanpr_transform_render_vert(BMVert *v,
                                        int index,
                                        LANPR_RenderVert *RvBuf,
                                        double (*MvMat)[4],
                                        double (*MvPMat)[4],
                                        Camera *UNUSED(camera))
{
  double co[4];
  LANPR_RenderVert *rv = &RvBuf[index];
  copy_v3db_v3fl(co, v->co);
  mul_v3_m4v3_db(rv->gloc, MvMat, co);
  mul_v4_m4v3_db(rv->fbcoord, MvPMat, co);
}

static void lanpr_make_render_geometry_buffers_object(
    Object *o, double (*MvMat)[4], double (*MvPMat)[4], LANPR_RenderBuffer *rb, int override_usage)
{
  BMesh *bm;
  BMVert *v;
  BMFace *f;
  BMEdge *e;
  BMLoop *loop;
  LANPR_RenderLine *rl;
  LANPR_RenderTriangle *rt;
  double new_mvp[4][4], new_mv[4][4], Normal[4][4];
  LANPR_RenderElementLinkNode *reln;
  Object *cam_object = rb->scene->camera;
  Camera *c = cam_object->data;
  LANPR_RenderVert *orv;
  LANPR_RenderLine *orl;
  LANPR_RenderTriangle *ort;
  LanprEdge *fe;
  int CanFindFreestyle = 0;
  int i;

  int usage = override_usage ? override_usage : o->lanpr.usage;

  if (usage == OBJECT_FEATURE_LINE_EXCLUDE) {
    return;
  }

  if (o->type == OB_MESH) {

    mul_m4db_m4db_m4fl_uniq(new_mvp, MvPMat, o->obmat);
    mul_m4db_m4db_m4fl_uniq(new_mv, MvMat, o->obmat);

    invert_m4_m4(o->imat, o->obmat);
    transpose_m4(o->imat);
    copy_m4d_m4(Normal, o->imat);

    const BMAllocTemplate allocsize = BMALLOC_TEMPLATE_FROM_ME(((Mesh *)(o->data)));
    bm = BM_mesh_create(&allocsize,
                        &((struct BMeshCreateParams){
                            .use_toolflags = true,
                        }));
    BM_mesh_bm_from_me(bm,
                       o->data,
                       &((struct BMeshFromMeshParams){
                           .calc_face_normal = true,
                       }));
    BM_mesh_elem_hflag_disable_all(bm, BM_FACE | BM_EDGE, BM_ELEM_TAG, false);
    BM_mesh_triangulate(
        bm, MOD_TRIANGULATE_QUAD_BEAUTY, MOD_TRIANGULATE_NGON_BEAUTY, 4, false, NULL, NULL, NULL);
    BM_mesh_normals_update(bm);
    BM_mesh_elem_table_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);
    BM_mesh_elem_index_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);

    if (CustomData_has_layer(&bm->edata, CD_LANPR_EDGE)) {
      CanFindFreestyle = 1;
    }

    orv = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderVert) * bm->totvert);
    ort = mem_static_aquire(&rb->render_data_pool, bm->totface * rb->triangle_size);
    orl = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine) * bm->totedge);

    reln = list_append_pointer_static_sized(&rb->vertex_buffer_pointers,
                                            &rb->render_data_pool,
                                            orv,
                                            sizeof(LANPR_RenderElementLinkNode));
    reln->element_count = bm->totvert;
    reln->object_ref = o;

    reln = list_append_pointer_static_sized(&rb->line_buffer_pointers,
                                            &rb->render_data_pool,
                                            orl,
                                            sizeof(LANPR_RenderElementLinkNode));
    reln->element_count = bm->totedge;
    reln->object_ref = o;

    reln = list_append_pointer_static_sized(&rb->triangle_buffer_pointers,
                                            &rb->render_data_pool,
                                            ort,
                                            sizeof(LANPR_RenderElementLinkNode));
    reln->element_count = bm->totface;
    reln->object_ref = o;

    for (i = 0; i < bm->totvert; i++) {
      v = BM_vert_at_index(bm, i);
      lanpr_transform_render_vert(v, i, orv, new_mv, new_mvp, c);
    }

    rl = orl;
    for (i = 0; i < bm->totedge; i++) {
      e = BM_edge_at_index(bm, i);
      if (CanFindFreestyle) {
        fe = CustomData_bmesh_get(&bm->edata, e->head.data, CD_LANPR_EDGE);
        if (fe->flag & LANPR_EDGE_MARK) {
          rl->flags |= LANPR_EDGE_FLAG_EDGE_MARK;
        }
      }
      if (use_smooth_contour_modifier_contour) {
        rl->edge_idx = i;
        if (BM_elem_flag_test(e->v1, BM_ELEM_SELECT) && BM_elem_flag_test(e->v2, BM_ELEM_SELECT)) {
          rl->flags |= LANPR_EDGE_FLAG_CONTOUR;
        }
      }

      rl->l = &orv[BM_elem_index_get(e->v1)];
      rl->r = &orv[BM_elem_index_get(e->v2)];

      rl->object_ref = o;

      LANPR_RenderLineSegment *rls = mem_static_aquire(&rb->render_data_pool,
                                                       sizeof(LANPR_RenderLineSegment));
      BLI_addtail(&rl->segments, rls);
      if (usage == OBJECT_FEATURE_LINE_INHERENT) {
        BLI_addtail(&rb->all_render_lines, rl);
      }
      rl++;
    }

    rt = ort;
    for (i = 0; i < bm->totface; i++) {
      f = BM_face_at_index(bm, i);

      loop = f->l_first;
      rt->v[0] = &orv[BM_elem_index_get(loop->v)];
      rt->rl[0] = &orl[BM_elem_index_get(loop->e)];
      loop = loop->next;
      rt->v[1] = &orv[BM_elem_index_get(loop->v)];
      rt->rl[1] = &orl[BM_elem_index_get(loop->e)];
      loop = loop->next;
      rt->v[2] = &orv[BM_elem_index_get(loop->v)];
      rt->rl[2] = &orl[BM_elem_index_get(loop->e)];

      rt->material_id = f->mat_nr;

      add_v3_v3_db(rt->gc, rt->v[0]->fbcoord);
      add_v3_v3_db(rt->gc, rt->v[1]->fbcoord);
      add_v3_v3_db(rt->gc, rt->v[2]->fbcoord);
      mul_v3db_db(rt->gc, 1.0f / 3.0f);

      double gn[3];
      copy_v3db_v3fl(gn, f->no);
      mul_v3_mat3_m4v3_db(rt->gn, Normal, gn);
      normalize_v3_d(rt->gn);
      lanpr_assign_render_line_with_triangle(rt);
      /*  m = tnsGetIndexedMaterial(rb->scene, f->material_id); */
      /*  if(m) m->Previewv_count += (f->triangle_count*3); */

      if (BM_elem_flag_test(f, BM_ELEM_SELECT)) {
        rt->material_id = 1;
      }

      rt = (LANPR_RenderTriangle *)(((unsigned char *)rt) + rb->triangle_size);
    }

    BM_mesh_free(bm);
  }
}

static int lanpr_object_has_feature_line_modifier(Object *o)
{
  ModifierData *md;
  for (md = o->modifiers.first; md; md = md->next) {
    if (md->type == eModifierType_FeatureLine) {
      return 1;
    }
  }
  return 0;
}

int ED_lanpr_object_collection_usage_check(Collection *c, Object *o)
{
  CollectionChild *cc;
  int object_is_used = (lanpr_object_has_feature_line_modifier(o) &&
                        o->lanpr.usage == OBJECT_FEATURE_LINE_INHERENT);

  if (object_is_used && c->lanpr.force && c->lanpr.usage != COLLECTION_FEATURE_LINE_INCLUDE) {
    if (BKE_collection_has_object_recursive(c, o)) {
      if (c->lanpr.usage == COLLECTION_FEATURE_LINE_EXCLUDE) {
        return OBJECT_FEATURE_LINE_EXCLUDE;
      }
      else if (c->lanpr.usage == COLLECTION_FEATURE_LINE_OCCLUSION_ONLY) {
        return OBJECT_FEATURE_LINE_OCCLUSION_ONLY;
      }
    }
  }

  if (!c->children.first) {
    if (BKE_collection_has_object(c, o)) {
      if (o->lanpr.usage == OBJECT_FEATURE_LINE_INHERENT) {
        if (c->lanpr.usage == COLLECTION_FEATURE_LINE_OCCLUSION_ONLY) {
          return OBJECT_FEATURE_LINE_OCCLUSION_ONLY;
        }
        else if (c->lanpr.usage == COLLECTION_FEATURE_LINE_EXCLUDE) {
          return OBJECT_FEATURE_LINE_EXCLUDE;
        }
        else {
          return OBJECT_FEATURE_LINE_INHERENT;
        }
      }
      else {
        return o->lanpr.usage;
      }
    }
    else {
      return OBJECT_FEATURE_LINE_INHERENT;
    }
  }

  for (cc = c->children.first; cc; cc = cc->next) {
    int result = ED_lanpr_object_collection_usage_check(cc->collection, o);
    if (result > OBJECT_FEATURE_LINE_INHERENT) {
      return result;
    }
  }

  return OBJECT_FEATURE_LINE_INHERENT;
}

static void lanpr_make_render_geometry_buffers(Depsgraph *depsgraph,
                                               Scene *s,
                                               Object *c /*camera*/,
                                               LANPR_RenderBuffer *rb)
{
  double proj[4][4], view[4][4], result[4][4];
  float inv[4][4];
  Camera *cam = c->data;

  float sensor = BKE_camera_sensor_size(cam->sensor_fit, cam->sensor_x, cam->sensor_y);
  real fov = focallength_to_fov(cam->lens, sensor);

  memset(rb->material_pointers, 0, sizeof(void *) * 2048);

  real asp = ((real)rb->w / (real)rb->h);

  if (cam->type == CAM_PERSP) {
    tmat_make_perspective_matrix_44d(proj, fov, asp, cam->clip_start, cam->clip_end);
  }
  else if (cam->type == CAM_ORTHO) {
    real w = cam->ortho_scale / 2;
    tmat_make_ortho_matrix_44d(proj, -w, w, -w / asp, w / asp, cam->clip_start, cam->clip_end);
  }

  unit_m4_db(view);

  /*  tObjApplyself_transformMatrix(c, 0); */
  /*  tObjApplyGlobalTransformMatrixReverted(c); */
  invert_m4_m4(inv, c->obmat);
  mul_m4db_m4db_m4fl_uniq(result, proj, inv);
  copy_m4_m4_db(proj, result);
  copy_m4_m4_db(rb->view_projection, proj);

  BLI_listbase_clear(&rb->triangle_buffer_pointers);
  BLI_listbase_clear(&rb->vertex_buffer_pointers);

  DEG_OBJECT_ITER_BEGIN (depsgraph,
                         o,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY | DEG_ITER_OBJECT_FLAG_VISIBLE |
                             DEG_ITER_OBJECT_FLAG_DUPLI | DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    int usage = ED_lanpr_object_collection_usage_check(s->master_collection, o);

    lanpr_make_render_geometry_buffers_object(o, view, proj, rb, usage);
  }
  DEG_OBJECT_ITER_END;

  /*  for (collection = s->master_collection.first; collection; collection = collection->ID.next) {
   */
  /* 	for (co = collection->gobject.first; co; co = co->next) { */
  /* 		//tObjApplyGlobalTransformMatrixRecursive(o); */
  /* 		lanpr_make_render_geometry_buffers_object(o, view, proj, rb); */
  /* 	} */
  /* } */
}

#define INTERSECT_SORT_MIN_TO_MAX_3(ia, ib, ic, lst) \
  { \
    lst[0] = TNS_MIN3_INDEX(ia, ib, ic); \
    lst[1] = (((ia <= ib && ib <= ic) || (ic <= ib && ib <= ia)) ? \
                  1 : \
                  (((ic <= ia && ia <= ib) || (ib < ia && ia <= ic)) ? 0 : 2)); \
    lst[2] = TNS_MAX3_INDEX(ia, ib, ic); \
  }

/*  ia ib ic are ordered */
#define INTERSECT_JUST_GREATER(is, order, num, index) \
  { \
    index = (num < is[order[0]] ? \
                 order[0] : \
                 (num < is[order[1]] ? order[1] : (num < is[order[2]] ? order[2] : order[2]))); \
  }

/*  ia ib ic are ordered */
#define INTERSECT_JUST_SMALLER(is, order, num, index) \
  { \
    index = (num > is[order[2]] ? \
                 order[2] : \
                 (num > is[order[1]] ? order[1] : (num > is[order[0]] ? order[0] : order[0]))); \
  }

static LANPR_RenderLine *lanpr_another_edge(LANPR_RenderTriangle *rt, LANPR_RenderVert *rv)
{
  if (rt->v[0] == rv) {
    return rt->rl[1];
  }
  else if (rt->v[1] == rv) {
    return rt->rl[2];
  }
  else if (rt->v[2] == rv) {
    return rt->rl[0];
  }
  return 0;
}
static int lanpr_share_edge_direct(LANPR_RenderTriangle *rt, LANPR_RenderLine *rl)
{
  if (rt->rl[0] == rl || rt->rl[1] == rl || rt->rl[2] == rl) {
    return 1;
  }
  return 0;
}

static int lanpr_triangle_line_imagespace_intersection_v2(SpinLock *UNUSED(spl),
                                                          LANPR_RenderTriangle *rt,
                                                          LANPR_RenderLine *rl,
                                                          Object *cam,
                                                          double vp[4][4],
                                                          double *CameraDir,
                                                          double *From,
                                                          double *To)
{
  double is[3] = {0};
  int order[3];
  int LCross = -1, RCross = -1;
  int a, b, c;
  int StL = 0, StR = 0;

  tnsVector3d Lv;
  tnsVector3d Rv;
  tnsVector4d vd4;
  real Cv[3];
  real DotL, DotR, DotLA, DotRA;
  real DotF;
  tnsVector4d gloc, Trans;
  real Cut = -1;

  double *LFBC = rl->l->fbcoord, *RFBC = rl->r->fbcoord, *FBC0 = rt->v[0]->fbcoord,
         *FBC1 = rt->v[1]->fbcoord, *FBC2 = rt->v[2]->fbcoord;

  /*  printf("(%f %f)(%f %f)(%f %f)   (%f %f)(%f %f)\n", FBC0[0], FBC0[1], FBC1[0], FBC1[1], */
  /*  FBC2[0], FBC2[1], LFBC[0], LFBC[1], RFBC[0], RFBC[1]); */

  /*  bound box. */
  /*  if (MIN3(FBC0[2], FBC1[2], FBC2[2]) > MAX2(LFBC[2], RFBC[2])) */
  /* 	return 0; */
  if ((MAX3(FBC0[0], FBC1[0], FBC2[0]) < MIN2(LFBC[0], RFBC[0])) ||
      (MIN3(FBC0[0], FBC1[0], FBC2[0]) > MAX2(LFBC[0], RFBC[0])) ||
      (MAX3(FBC0[1], FBC1[1], FBC2[1]) < MIN2(LFBC[1], RFBC[1])) ||
      (MIN3(FBC0[1], FBC1[1], FBC2[1]) > MAX2(LFBC[1], RFBC[1]))) {
    return 0;
  }

  if (lanpr_share_edge_direct(rt, rl)) {
    return 0;
  }

  a = lanpr_LineIntersectTest2d(LFBC, RFBC, FBC0, FBC1, &is[0]);
  b = lanpr_LineIntersectTest2d(LFBC, RFBC, FBC1, FBC2, &is[1]);
  c = lanpr_LineIntersectTest2d(LFBC, RFBC, FBC2, FBC0, &is[2]);

  /*  printf("abc: %d %d %d\n", a,b,c); */

  INTERSECT_SORT_MIN_TO_MAX_3(is[0], is[1], is[2], order);

  sub_v3_v3v3_db(Lv, rl->l->gloc, rt->v[0]->gloc);
  sub_v3_v3v3_db(Rv, rl->r->gloc, rt->v[0]->gloc);

  copy_v3_v3_db(Cv, CameraDir);

  copy_v4db_v4fl(vd4, cam->obmat[3]);
  if (((Camera *)cam->data)->type == CAM_PERSP) {
    sub_v3_v3v3_db(Cv, vd4, rt->v[0]->gloc);
  }

  DotL = dot_v3v3_db(Lv, rt->gn);
  DotR = dot_v3v3_db(Rv, rt->gn);
  DotF = dot_v3v3_db(Cv, rt->gn);

  if (!DotF) {
    return 0;
  }

  if (!a && !b && !c) {
    if (!(StL = lanpr_point_triangle_relation(LFBC, FBC0, FBC1, FBC2)) &&
        !(StR = lanpr_point_triangle_relation(RFBC, FBC0, FBC1, FBC2))) {
      return 0; /*  not occluding */
    }
  }

  StL = lanpr_point_triangle_relation(LFBC, FBC0, FBC1, FBC2);
  StR = lanpr_point_triangle_relation(RFBC, FBC0, FBC1, FBC2);

  /*  for (rv = rt->intersecting_verts.first; rv; rv = rv->next) { */
  /* 	if (rv->intersecting_with == rt && rv->intersecting_line == rl) { */
  /* 		Cut = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], */
  /*  rv->fbcoord[0]); 		break; */
  /* 	} */
  /* } */

  DotLA = fabs(DotL);
  if (DotLA < DBL_EPSILON) {
    DotLA = 0;
    DotL = 0;
  }
  DotRA = fabs(DotR);
  if (DotRA < DBL_EPSILON) {
    DotRA = 0;
    DotR = 0;
  }
  if (DotL - DotR == 0) {
    Cut = 100000;
  }
  else if (DotL * DotR <= 0) {
    Cut = DotLA / fabs(DotL - DotR);
  }
  else {
    Cut = fabs(DotR + DotL) / fabs(DotL - DotR);
    Cut = DotRA > DotLA ? 1 - Cut : Cut;
  }

  if (((Camera *)cam->data)->type == CAM_PERSP) {
    interp_v3_v3v3_db(gloc, rl->l->gloc, rl->r->gloc, Cut);
    mul_v4_m4v3_db(Trans, vp, gloc);
    mul_v3db_db(Trans, (1 / Trans[3]) /**HeightMultiply/2*/);
    Camera *camera = cam->data;
    Trans[0] -= camera->shiftx * 2;
    Trans[1] -= camera->shifty * 2;
  }
  else {
    interp_v3_v3v3_db(Trans, rl->l->fbcoord, rl->r->fbcoord, Cut);
    /*  mul_v4_m4v3_db(Trans, vp, gloc); */
  }

  /*  Trans[2] = tmat_dist_3dv(gloc, cam->Base.gloc); */
  /*  Trans[2] = cam->clipsta*cam->clipend / (cam->clipend - fabs(Trans[2]) * (cam->clipend - */
  /*  cam->clipsta)); */

  /*  prevent vertical problem ? */
  if (rl->l->fbcoord[0] != rl->r->fbcoord[0]) {
    Cut = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], Trans[0]);
  }
  else {
    Cut = tMatGetLinearRatio(rl->l->fbcoord[1], rl->r->fbcoord[1], Trans[1]);
  }

  /*
  In = ED_lanpr_point_inside_triangled(
      Trans, rt->v[0]->fbcoord, rt->v[1]->fbcoord, rt->v[2]->fbcoord);
  */
  if (StL == 2) {
    if (StR == 2) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (StR == 1) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (StR == 0) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 0, RCross);
    }
  }
  else if (StL == 1) {
    if (StR == 2) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (StR == 1) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (StR == 0) {
      INTERSECT_JUST_GREATER(is, order, DBL_TRIANGLE_LIM, RCross);
      if (TNS_ABC(RCross) && is[RCross] > (DBL_TRIANGLE_LIM)) {
        INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      }
      else {
        INTERSECT_JUST_SMALLER(is, order, -DBL_TRIANGLE_LIM, LCross);
        INTERSECT_JUST_GREATER(is, order, -DBL_TRIANGLE_LIM, RCross);
      }
    }
  }
  else if (StL == 0) {
    if (StR == 2) {
      INTERSECT_JUST_SMALLER(is, order, 1 - DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (StR == 1) {
      INTERSECT_JUST_SMALLER(is, order, 1 - DBL_TRIANGLE_LIM, LCross);
      if (TNS_ABC(LCross) && is[LCross] < (1 - DBL_TRIANGLE_LIM)) {
        INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
      }
      else {
        INTERSECT_JUST_SMALLER(is, order, 1 + DBL_TRIANGLE_LIM, LCross);
        INTERSECT_JUST_GREATER(is, order, 1 + DBL_TRIANGLE_LIM, RCross);
      }
    }
    else if (StR == 0) {
      INTERSECT_JUST_GREATER(is, order, 0, LCross);
      if (TNS_ABC(LCross) && is[LCross] > 0) {
        INTERSECT_JUST_GREATER(is, order, is[LCross], RCross);
      }
      else {
        INTERSECT_JUST_GREATER(is, order, is[LCross], LCross);
        INTERSECT_JUST_GREATER(is, order, is[LCross], RCross);
      }
    }
  }

  real LF = DotL * DotF, RF = DotR * DotF;
  /*  int CrossCount = a + b + c; */
  /*  if (CrossCount == 2) { */
  /* 	INTERSECT_JUST_GREATER(is, order, 0, LCross); */
  /* 	if (!TNS_ABC(LCross)) INTERSECT_JUST_GREATER(is, order, is[LCross], LCross); */
  /* 	INTERSECT_JUST_GREATER(is, order, is[LCross], RCross); */
  /* }else if(CrossCount == 1 || StL+StR==1) { */
  /* 	if (StL) { */
  /* 		INTERSECT_JUST_GREATER(is, order, DBL_TRIANGLE_LIM, RCross); */
  /* 		INTERSECT_JUST_SMALLER(is, order, is[RCross], LCross); */
  /* 	}else if(StR) { */
  /* 		INTERSECT_JUST_SMALLER(is, order, 1 - DBL_TRIANGLE_LIM, LCross); */
  /* 		INTERSECT_JUST_GREATER(is, order, is[LCross], RCross); */
  /* 	} */
  /* }else if(CrossCount == 0) { */
  /* 	INTERSECT_JUST_SMALLER(is, order, 0, LCross); */
  /* 	INTERSECT_JUST_GREATER(is, order, 1, RCross); */
  /* } */

  if (LF <= 0 && RF <= 0 && (DotL || DotR)) {

    *From = MAX2(0, is[LCross]);
    *To = MIN2(1, is[RCross]);
    if (*From >= *To) {
      return 0;
    }
    /*  printf("1 From %f to %f\n",*From, *To); */
    return 1;
  }
  else if (LF >= 0 && RF <= 0 && (DotL || DotR)) {
    *From = MAX2(Cut, is[LCross]);
    *To = MIN2(1, is[RCross]);
    if (*From >= *To) {
      return 0;
    }
    /*  printf("2 From %f to %f\n",*From, *To); */
    return 1;
  }
  else if (LF <= 0 && RF >= 0 && (DotL || DotR)) {
    *From = MAX2(0, is[LCross]);
    *To = MIN2(Cut, is[RCross]);
    if (*From >= *To) {
      return 0;
    }
    /*  printf("3 From %f to %f\n",*From, *To); */
    return 1;
  }
  else
    return 0;
  return 1;
}

static LANPR_RenderLine *lanpr_triangle_share_edge(LANPR_RenderTriangle *l,
                                                   LANPR_RenderTriangle *r)
{
  if (l->rl[0] == r->rl[0]) {
    return r->rl[0];
  }
  if (l->rl[0] == r->rl[1]) {
    return r->rl[1];
  }
  if (l->rl[0] == r->rl[2]) {
    return r->rl[2];
  }
  if (l->rl[1] == r->rl[0]) {
    return r->rl[0];
  }
  if (l->rl[1] == r->rl[1]) {
    return r->rl[1];
  }
  if (l->rl[1] == r->rl[2]) {
    return r->rl[2];
  }
  if (l->rl[2] == r->rl[0]) {
    return r->rl[0];
  }
  if (l->rl[2] == r->rl[1]) {
    return r->rl[1];
  }
  if (l->rl[2] == r->rl[2]) {
    return r->rl[2];
  }
  return 0;
}
static LANPR_RenderVert *lanpr_triangle_share_point(LANPR_RenderTriangle *l,
                                                    LANPR_RenderTriangle *r)
{
  if (l->v[0] == r->v[0]) {
    return r->v[0];
  }
  if (l->v[0] == r->v[1]) {
    return r->v[1];
  }
  if (l->v[0] == r->v[2]) {
    return r->v[2];
  }
  if (l->v[1] == r->v[0]) {
    return r->v[0];
  }
  if (l->v[1] == r->v[1]) {
    return r->v[1];
  }
  if (l->v[1] == r->v[2]) {
    return r->v[2];
  }
  if (l->v[2] == r->v[0]) {
    return r->v[0];
  }
  if (l->v[2] == r->v[1]) {
    return r->v[1];
  }
  if (l->v[2] == r->v[2]) {
    return r->v[2];
  }
  return 0;
}

static LANPR_RenderVert *lanpr_triangle_line_intersection_test(LANPR_RenderBuffer *rb,
                                                               LANPR_RenderLine *rl,
                                                               LANPR_RenderTriangle *rt,
                                                               LANPR_RenderTriangle *testing,
                                                               LANPR_RenderVert *Last)
{
  tnsVector3d Lv;
  tnsVector3d Rv;
  real DotL, DotR;
  LANPR_RenderVert *Result, *rv;
  tnsVector3d gloc;
  LANPR_RenderVert *l = rl->l, *r = rl->r;
  int result;

  for (rv = testing->intersecting_verts.first; rv; rv = rv->next) {
    if (rv->intersecting_with == rt && rv->intersecting_line == rl) {
      return rv;
    }
  }

  sub_v3_v3v3_db(Lv, l->gloc, testing->v[0]->gloc);
  sub_v3_v3v3_db(Rv, r->gloc, testing->v[0]->gloc);

  DotL = dot_v3v3_db(Lv, testing->gn);
  DotR = dot_v3v3_db(Rv, testing->gn);

  if (DotL * DotR > 0 || (!DotL && !DotR)) {
    return 0;
  }

  DotL = fabs(DotL);
  DotR = fabs(DotR);

  interp_v3_v3v3_db(gloc, l->gloc, r->gloc, DotL / (DotL + DotR));

  if (Last && TNS_DOUBLE_CLOSE_ENOUGH(Last->gloc[0], gloc[0]) &&
      TNS_DOUBLE_CLOSE_ENOUGH(Last->gloc[1], gloc[1]) &&
      TNS_DOUBLE_CLOSE_ENOUGH(Last->gloc[2], gloc[2])) {

    Last->intersecting_line2 = rl;
    return 0;
  }

  if (!(result = lanpr_point_inside_triangle3de(
            gloc, testing->v[0]->gloc, testing->v[1]->gloc, testing->v[2]->gloc))) {
    return 0;
  }
  /*else if(result < 0) {
     return 0;
     }*/

  Result = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderVert));

  if (DotL > 0 || DotR < 0) {
    Result->positive = 1;
  }
  else {
    Result->positive = 0;
  }

  /*  Result->IntersectingOnFace = testing; */
  Result->edge_used = 1;
  /*  Result->IntersectL = l; */
  Result->v = (void *)r; /*  Caution! */
                         /*  Result->intersecting_with = rt; */
  copy_v3_v3_db(Result->gloc, gloc);

  BLI_addtail(&testing->intersecting_verts, Result);

  return Result;
}
static LANPR_RenderLine *lanpr_triangle_generate_intersection_line_only(
    LANPR_RenderBuffer *rb, LANPR_RenderTriangle *rt, LANPR_RenderTriangle *testing)
{
  LANPR_RenderVert *l = 0, *r = 0;
  LANPR_RenderVert **Next = &l;
  LANPR_RenderLine *Result;
  LANPR_RenderVert *E0T = 0;
  LANPR_RenderVert *E1T = 0;
  LANPR_RenderVert *E2T = 0;
  LANPR_RenderVert *TE0 = 0;
  LANPR_RenderVert *TE1 = 0;
  LANPR_RenderVert *TE2 = 0;
  tnsVector3d cl; /*  rb->scene->ActiveCamera->gloc; */
  real ZMax = ((Camera *)rb->scene->camera->data)->clip_end;
  real ZMin = ((Camera *)rb->scene->camera->data)->clip_start;
  LANPR_RenderVert *Share = lanpr_triangle_share_point(testing, rt);
  Camera *cam = rb->scene->camera->data;

  copy_v3db_v3fl(cl, rb->scene->camera->obmat[3]);

  if (Share) {
    LANPR_RenderVert *NewShare;
    LANPR_RenderLine *rl = lanpr_another_edge(rt, Share);

    l = NewShare = mem_static_aquire(&rb->render_data_pool, (sizeof(LANPR_RenderVert)));

    NewShare->positive = 1;
    NewShare->edge_used = 1;
    /*  NewShare->IntersectL = l; */
    NewShare->v = (void *)r; /*  Caution! */
    /*  Result->intersecting_with = rt; */
    copy_v3_v3_db(NewShare->gloc, Share->gloc);

    r = lanpr_triangle_line_intersection_test(rb, rl, rt, testing, 0);

    if (!r) {
      rl = lanpr_another_edge(testing, Share);
      r = lanpr_triangle_line_intersection_test(rb, rl, testing, rt, 0);
      if (!r) {
        return 0;
      }
      BLI_addtail(&testing->intersecting_verts, NewShare);
    }
    else {
      BLI_addtail(&rt->intersecting_verts, NewShare);
    }
  }
  else {
    if (!rt->rl[0] || !rt->rl[1] || !rt->rl[2]) {
      return 0; /*  shouldn't need this, there must be problems in culling. */
    }
    E0T = lanpr_triangle_line_intersection_test(rb, rt->rl[0], rt, testing, 0);
    if (E0T && (!(*Next))) {
      (*Next) = E0T;
      (*Next)->intersecting_line = rt->rl[0];
      Next = &r;
    }
    E1T = lanpr_triangle_line_intersection_test(rb, rt->rl[1], rt, testing, l);
    if (E1T && (!(*Next))) {
      (*Next) = E1T;
      (*Next)->intersecting_line = rt->rl[1];
      Next = &r;
    }
    if (!(*Next)) {
      E2T = lanpr_triangle_line_intersection_test(rb, rt->rl[2], rt, testing, l);
    }
    if (E2T && (!(*Next))) {
      (*Next) = E2T;
      (*Next)->intersecting_line = rt->rl[2];
      Next = &r;
    }

    if (!(*Next)) {
      TE0 = lanpr_triangle_line_intersection_test(rb, testing->rl[0], testing, rt, l);
    }
    if (TE0 && (!(*Next))) {
      (*Next) = TE0;
      (*Next)->intersecting_line = testing->rl[0];
      Next = &r;
    }
    if (!(*Next)) {
      TE1 = lanpr_triangle_line_intersection_test(rb, testing->rl[1], testing, rt, l);
    }
    if (TE1 && (!(*Next))) {
      (*Next) = TE1;
      (*Next)->intersecting_line = testing->rl[1];
      Next = &r;
    }
    if (!(*Next)) {
      TE2 = lanpr_triangle_line_intersection_test(rb, testing->rl[2], testing, rt, l);
    }
    if (TE2 && (!(*Next))) {
      (*Next) = TE2;
      (*Next)->intersecting_line = testing->rl[2];
      Next = &r;
    }

    if (!(*Next)) {
      return 0;
    }
  }
  mul_v4_m4v3_db(l->fbcoord, rb->view_projection, l->gloc);
  mul_v4_m4v3_db(r->fbcoord, rb->view_projection, r->gloc);
  mul_v3db_db(l->fbcoord, (1 / l->fbcoord[3]) /**HeightMultiply/2*/);
  mul_v3db_db(r->fbcoord, (1 / r->fbcoord[3]) /**HeightMultiply/2*/);

  l->fbcoord[0] -= cam->shiftx * 2;
  l->fbcoord[1] -= cam->shifty * 2;
  r->fbcoord[0] -= cam->shiftx * 2;
  r->fbcoord[1] -= cam->shifty * 2;

  l->fbcoord[2] = ZMin * ZMax / (ZMax - fabs(l->fbcoord[2]) * (ZMax - ZMin));
  r->fbcoord[2] = ZMin * ZMax / (ZMax - fabs(r->fbcoord[2]) * (ZMax - ZMin));

  l->intersecting_with = rt;
  r->intersecting_with = testing;

  /* ((1 / rl->l->fbcoord[3])*rb->FrameBuffer->H / 2) */

  Result = mem_static_aquire(&rb->render_data_pool, sizeof(LANPR_RenderLine));
  Result->l = l;
  Result->r = r;
  Result->tl = rt;
  Result->tr = testing;
  LANPR_RenderLineSegment *rls = mem_static_aquire(&rb->render_data_pool,
                                                   sizeof(LANPR_RenderLineSegment));
  BLI_addtail(&Result->segments, rls);
  BLI_addtail(&rb->all_render_lines, Result);
  Result->flags |= LANPR_EDGE_FLAG_INTERSECTION;
  list_append_pointer_static(&rb->intersection_lines, &rb->render_data_pool, Result);
  int r1, r2, c1, c2, row, col;
  if (lanpr_get_line_bounding_areas(rb, Result, &r1, &r2, &c1, &c2)) {
    for (row = r1; row != r2 + 1; row++) {
      for (col = c1; col != c2 + 1; col++) {
        lanpr_link_line_with_bounding_area(rb, &rb->initial_bounding_areas[row * 4 + col], Result);
      }
    }
  }

  rb->intersection_count++;

  return Result;
}
static void lanpr_triangle_calculate_intersections_in_bounding_area(LANPR_RenderBuffer *rb,
                                                                    LANPR_RenderTriangle *rt,
                                                                    LANPR_BoundingArea *ba)
{
  LANPR_RenderTriangle *TestingTriangle;
  LinkData *lip, *next_lip;

  real *FBC0 = rt->v[0]->fbcoord, *FBC1 = rt->v[1]->fbcoord, *FBC2 = rt->v[2]->fbcoord;

  if (ba->child) {
    lanpr_triangle_calculate_intersections_in_bounding_area(rb, rt, &ba->child[0]);
    lanpr_triangle_calculate_intersections_in_bounding_area(rb, rt, &ba->child[1]);
    lanpr_triangle_calculate_intersections_in_bounding_area(rb, rt, &ba->child[2]);
    lanpr_triangle_calculate_intersections_in_bounding_area(rb, rt, &ba->child[3]);
    return;
  }

  for (lip = ba->linked_triangles.first; lip; lip = next_lip) {
    next_lip = lip->next;
    TestingTriangle = lip->data;
    if (TestingTriangle == rt || TestingTriangle->testing == rt ||
        lanpr_triangle_share_edge(rt, TestingTriangle)) {

      continue;
    }

    TestingTriangle->testing = rt;
    real *RFBC0 = TestingTriangle->v[0]->fbcoord, *RFBC1 = TestingTriangle->v[1]->fbcoord,
         *RFBC2 = TestingTriangle->v[2]->fbcoord;

    if ((MIN3(FBC0[2], FBC1[2], FBC2[2]) > MAX3(RFBC0[2], RFBC1[2], RFBC2[2])) ||
        (MAX3(FBC0[2], FBC1[2], FBC2[2]) < MIN3(RFBC0[2], RFBC1[2], RFBC2[2])) ||
        (MIN3(FBC0[0], FBC1[0], FBC2[0]) > MAX3(RFBC0[0], RFBC1[0], RFBC2[0])) ||
        (MAX3(FBC0[0], FBC1[0], FBC2[0]) < MIN3(RFBC0[0], RFBC1[0], RFBC2[0])) ||
        (MIN3(FBC0[1], FBC1[1], FBC2[1]) > MAX3(RFBC0[1], RFBC1[1], RFBC2[1])) ||
        (MAX3(FBC0[1], FBC1[1], FBC2[1]) < MIN3(RFBC0[1], RFBC1[1], RFBC2[1]))) {
      continue;
    }

    lanpr_triangle_generate_intersection_line_only(rb, rt, TestingTriangle);
  }
}

static void lanpr_compute_view_Vector(LANPR_RenderBuffer *rb)
{
  float direction[3] = {0, 0, 1};
  float trans[3];
  float inv[4][4];

  invert_m4_m4(inv, rb->scene->camera->obmat);
  mul_v3_mat3_m4v3(trans, inv, direction);
  copy_v3db_v3fl(rb->view_vector, trans);
}

static void lanpr_compute_scene_contours(LANPR_RenderBuffer *rb, float threshold)
{
  real *view_vector = rb->view_vector;
  real Dot1 = 0, Dot2 = 0;
  real Result;
  tnsVector3d cam_location;
  int Add = 0;
  Object *cam_obj = rb->scene->camera;
  Camera *c = cam_obj->data;
  LANPR_RenderLine *rl;
  int contour_count = 0;
  int crease_count = 0;
  int MaterialCount = 0;

  if (c->type == CAM_ORTHO) {
    lanpr_compute_view_Vector(rb);
  }

  for (rl = rb->all_render_lines.first; rl; rl = rl->next) {
    /*  if(rl->testing) */
    /*  if (!lanpr_line_crosses_frame(rl->l->fbcoord, rl->r->fbcoord)) */
    /* 	continue; */

    Add = 0;
    Dot1 = 0;
    Dot2 = 0;

    if (c->type == CAM_PERSP) {
      copy_v3db_v3fl(cam_location, cam_obj->obmat[3]);
      sub_v3_v3v3_db(view_vector, rl->l->gloc, cam_location);
    }

    if (use_smooth_contour_modifier_contour) {
      if (rl->flags & LANPR_EDGE_FLAG_CONTOUR) {
        Add = 1;
      }
    }
    else {
      if (rl->tl) {
        Dot1 = dot_v3v3_db(view_vector, rl->tl->gn);
      }
      else {
        Add = 1;
      }
      if (rl->tr) {
        Dot2 = dot_v3v3_db(view_vector, rl->tr->gn);
      }
      else {
        Add = 1;
      }
    }

    if (!Add) {
      if ((Result = Dot1 * Dot2) <= 0 && (Dot1 + Dot2)) {
        Add = 1;
      }
      else if (dot_v3v3_db(rl->tl->gn, rl->tr->gn) < threshold) {
        Add = 2;
      }
      else if (rl->tl && rl->tr && rl->tl->material_id != rl->tr->material_id) {
        Add = 3;
      }
    }

    if (Add == 1) {
      rl->flags |= LANPR_EDGE_FLAG_CONTOUR;
      list_append_pointer_static(&rb->contours, &rb->render_data_pool, rl);
      contour_count++;
    }
    else if (Add == 2) {
      rl->flags |= LANPR_EDGE_FLAG_CREASE;
      list_append_pointer_static(&rb->crease_lines, &rb->render_data_pool, rl);
      crease_count++;
    }
    else if (Add == 3) {
      rl->flags |= LANPR_EDGE_FLAG_MATERIAL;
      list_append_pointer_static(&rb->material_lines, &rb->render_data_pool, rl);
      MaterialCount++;
    }
    if (rl->flags & LANPR_EDGE_FLAG_EDGE_MARK) {
      /*  no need to mark again */
      Add = 4;
      list_append_pointer_static(&rb->edge_marks, &rb->render_data_pool, rl);
      /*  continue; */
    }
    if (Add) {
      int r1, r2, c1, c2, row, col;
      if (lanpr_get_line_bounding_areas(rb, rl, &r1, &r2, &c1, &c2)) {
        for (row = r1; row != r2 + 1; row++) {
          for (col = c1; col != c2 + 1; col++) {
            lanpr_link_line_with_bounding_area(rb, &rb->initial_bounding_areas[row * 4 + col], rl);
          }
        }
      }
    }

    /*  line count reserved for feature such as progress feedback */
  }
}

/* Buffer operations */

void ED_lanpr_destroy_render_data(LANPR_RenderBuffer *rb)
{
  if (!rb) {
    return;
  }

  rb->contour_count = 0;
  rb->contour_managed = 0;
  rb->intersection_count = 0;
  rb->intersection_managed = 0;
  rb->material_line_count = 0;
  rb->material_managed = 0;
  rb->crease_count = 0;
  rb->crease_managed = 0;
  rb->edge_mark_count = 0;
  rb->edge_mark_managed = 0;

  BLI_listbase_clear(&rb->contours);
  BLI_listbase_clear(&rb->intersection_lines);
  BLI_listbase_clear(&rb->crease_lines);
  BLI_listbase_clear(&rb->material_lines);
  BLI_listbase_clear(&rb->edge_marks);
  BLI_listbase_clear(&rb->all_render_lines);
  BLI_listbase_clear(&rb->chains);

  BLI_listbase_clear(&rb->vertex_buffer_pointers);
  BLI_listbase_clear(&rb->line_buffer_pointers);
  BLI_listbase_clear(&rb->triangle_buffer_pointers);

  BLI_spin_end(&rb->cs_data);
  BLI_spin_end(&rb->cs_info);
  BLI_spin_end(&rb->cs_management);
  BLI_spin_end(&rb->render_data_pool.cs_mem);

  mem_static_destroy(&rb->render_data_pool);
}
LANPR_RenderBuffer *ED_lanpr_create_render_buffer(void)
{
  if (lanpr_share.render_buffer_shared) {
    ED_lanpr_destroy_render_data(lanpr_share.render_buffer_shared);
    return lanpr_share.render_buffer_shared;
  }

  LANPR_RenderBuffer *rb = MEM_callocN(sizeof(LANPR_RenderBuffer), "LANPR render buffer");

  lanpr_share.render_buffer_shared = rb;

  rb->cached_for_frame = -1;

  BLI_spin_init(&rb->cs_data);
  BLI_spin_init(&rb->cs_info);
  BLI_spin_init(&rb->cs_management);
  BLI_spin_init(&rb->render_data_pool.cs_mem);

  return rb;
}

static int lanpr_max_occlusion_in_collections(Collection *c)
{
  CollectionChild *cc;
  int max_occ = 0;
  int max;
  if (c->lanpr.use_multiple_levels) {
    max = MAX2(c->lanpr.level_start, c->lanpr.level_end);
  }
  else {
    max = c->lanpr.level_start;
  }
  max_occ = MAX2(max, max_occ);

  for (cc = c->children.first; cc; cc = cc->next) {
    max = lanpr_max_occlusion_in_collections(cc->collection);
    max_occ = MAX2(max, max_occ);
  }

  return max_occ;
}
static int lanpr_max_occlusion_in_targets(Depsgraph *depsgraph)
{
  int max_occ = 0;
  int max;
  Scene *s = DEG_get_evaluated_scene(depsgraph);

  /* Objects */
  DEG_OBJECT_ITER_BEGIN (depsgraph,
                         o,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY | DEG_ITER_OBJECT_FLAG_VISIBLE |
                             DEG_ITER_OBJECT_FLAG_DUPLI | DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    ModifierData *md;
    for (md = o->modifiers.first; md; md = md->next) {
      if (md->type == eModifierType_FeatureLine) {
        FeatureLineModifierData *flmd = (FeatureLineModifierData *)md;
        if (flmd->target) {
          if (flmd->use_multiple_levels) {
            max = MAX2(flmd->level_start, flmd->level_end);
          }
          else {
            max = flmd->level_start;
          }
          max_occ = MAX2(max, max_occ);
        }
      }
    }
  }
  DEG_OBJECT_ITER_END;

  /* Collections */
  max = lanpr_max_occlusion_in_collections(s->master_collection);

  max_occ = MAX2(max, max_occ);

  return max_occ;
}
static int lanpr_get_max_occlusion_level(Depsgraph *dg)
{
  Scene *s = DEG_get_evaluated_scene(dg);
  SceneLANPR *lanpr = &s->lanpr;
  if (!strcmp(s->r.engine, RE_engine_id_BLENDER_LANPR)) {
    /* Use the line layers in scene LANPR settings */
    return ED_lanpr_max_occlusion_in_line_layers(lanpr);
  }
  else {
    /* Other engines, use GPencil configurations */
    return lanpr_max_occlusion_in_targets(dg);
  }
}

static int lanpr_get_render_triangle_size(LANPR_RenderBuffer *rb)
{
  if (rb->thread_count == 0) {
    rb->thread_count = BKE_render_num_threads(&rb->scene->r);
  }
  return sizeof(LANPR_RenderTriangle) + (sizeof(LANPR_RenderLine *) * rb->thread_count);
}

int lanpr_count_this_line(LANPR_RenderLine *rl, LANPR_LineLayer *ll)
{
  LANPR_LineLayerComponent *llc = ll->components.first;
  int AndResult = 1, OrResult = 0;
  if (!llc) {
    return 1;
  }
  for (; llc; llc = llc->next) {
    if (llc->component_mode == LANPR_COMPONENT_MODE_ALL) {
      OrResult = 1;
    }
    else if (llc->component_mode == LANPR_COMPONENT_MODE_OBJECT && llc->object_select) {
      if (rl->object_ref && rl->object_ref->id.orig_id == &llc->object_select->id) {
        OrResult = 1;
      }
      else {
        AndResult = 0;
      }
    }
    else if (llc->component_mode == LANPR_COMPONENT_MODE_MATERIAL && llc->material_select) {
      if ((rl->tl && rl->tl->material_id == llc->material_select->index) ||
          (rl->tr && rl->tr->material_id == llc->material_select->index) ||
          (!(rl->flags & LANPR_EDGE_FLAG_INTERSECTION))) {
        OrResult = 1;
      }
      else {
        AndResult = 0;
      }
    }
    else if (llc->component_mode == LANPR_COMPONENT_MODE_COLLECTION && llc->collection_select) {
      if (BKE_collection_has_object(llc->collection_select,
                                    (Object *)rl->object_ref->id.orig_id)) {
        OrResult = 1;
      }
      else {
        AndResult = 0;
      }
    }
  }
  if (ll->logic_mode == LANPR_COMPONENT_LOGIG_OR) {
    return OrResult;
  }
  else {
    return AndResult;
  }
}
long lanpr_count_leveled_edge_segment_count(ListBase *LineList, LANPR_LineLayer *ll)
{
  LinkData *lip;
  LANPR_RenderLine *rl;
  LANPR_RenderLineSegment *rls;
  long Count = 0;
  for (lip = LineList->first; lip; lip = lip->next) {
    rl = lip->data;
    if (!lanpr_count_this_line(rl, ll)) {
      continue;
    }

    for (rls = rl->segments.first; rls; rls = rls->next) {

      if (!ll->use_multiple_levels) {
        if (rls->occlusion == ll->qi_begin) {
          Count++;
        }
      }
      else {
        if (rls->occlusion >= ll->qi_begin && rls->occlusion <= ll->qi_end) {
          Count++;
        }
      }
    }
  }
  return Count;
}
long lanpr_count_intersection_segment_count(LANPR_RenderBuffer *rb)
{
  LANPR_RenderLine *rl;
  long Count = 0;
  for (rl = rb->intersection_lines.first; rl; rl = rl->next) {
    Count++;
  }
  return Count;
}
void *lanpr_make_leveled_edge_vertex_array(LANPR_RenderBuffer *UNUSED(rb),
                                           ListBase *LineList,
                                           float *vertexArray,
                                           float *NormalArray,
                                           float **NextNormal,
                                           LANPR_LineLayer *ll,
                                           float componet_id)
{
  LinkData *lip;
  LANPR_RenderLine *rl;
  LANPR_RenderLineSegment *rls, *irls;
  float *v = vertexArray;
  float *N = NormalArray;
  for (lip = LineList->first; lip; lip = lip->next) {
    rl = lip->data;
    if (!lanpr_count_this_line(rl, ll)) {
      continue;
    }

    for (rls = rl->segments.first; rls; rls = rls->next) {
      int use = 0;
      if (!ll->use_multiple_levels) {
        if (rls->occlusion == ll->qi_begin) {
          use = 1;
        }
      }
      else {
        if (rls->occlusion >= ll->qi_begin && rls->occlusion <= ll->qi_end) {
          use = 1;
        }
      }

      if (!use)
        continue;

      if (rl->tl) {
        N[0] += rl->tl->gn[0];
        N[1] += rl->tl->gn[1];
        N[2] += rl->tl->gn[2];
      }
      if (rl->tr) {
        N[0] += rl->tr->gn[0];
        N[1] += rl->tr->gn[1];
        N[2] += rl->tr->gn[2];
      }
      if (rl->tl || rl->tr) {
        normalize_v3(N);
        copy_v3_v3(&N[3], N);
      }
      N += 6;

      CLAMP(rls->at, 0, 1);
      if ((irls = rls->next) != NULL) {
        CLAMP(irls->at, 0, 1);
      }

      *v = interpf(rl->r->fbcoord[0], rl->l->fbcoord[0], rls->at);
      v++;
      *v = interpf(rl->r->fbcoord[1], rl->l->fbcoord[1], rls->at);
      v++;
      *v = componet_id;
      v++;
      *v = interpf(rl->r->fbcoord[0], rl->l->fbcoord[0], irls ? irls->at : 1);
      v++;
      *v = interpf(rl->r->fbcoord[1], rl->l->fbcoord[1], irls ? irls->at : 1);
      v++;
      *v = componet_id;
      v++;
    }
  }
  *NextNormal = N;
  return v;
}

void lanpr_chain_generate_draw_command(LANPR_RenderBuffer *rb);

#define TNS_BOUND_AREA_CROSSES(b1, b2) \
  ((b1)[0] < (b2)[1] && (b1)[1] > (b2)[0] && (b1)[3] < (b2)[2] && (b1)[2] > (b2)[3])

static void lanpr_make_initial_bounding_areas(LANPR_RenderBuffer *rb)
{
  int sp_w = 4; /*  20; */
  int sp_h = 4; /*  rb->H / (rb->W / sp_w); */
  int row, col;
  LANPR_BoundingArea *ba;
  real span_w = (real)1 / sp_w * 2.0;
  real span_h = (real)1 / sp_h * 2.0;

  rb->tile_count_x = sp_w;
  rb->tile_count_y = sp_h;
  rb->width_per_tile = span_w;
  rb->height_per_tile = span_h;

  rb->bounding_area_count = sp_w * sp_h;
  rb->initial_bounding_areas = mem_static_aquire(
      &rb->render_data_pool, sizeof(LANPR_BoundingArea) * rb->bounding_area_count);

  for (row = 0; row < sp_h; row++) {
    for (col = 0; col < sp_w; col++) {
      ba = &rb->initial_bounding_areas[row * 4 + col];

      ba->l = span_w * col - 1.0;
      ba->r = (col == sp_w - 1) ? 1.0 : (span_w * (col + 1) - 1.0);
      ba->u = 1.0 - span_h * row;
      ba->b = (row == sp_h - 1) ? -1.0 : (1.0 - span_h * (row + 1));

      ba->cx = (ba->l + ba->r) / 2;
      ba->cy = (ba->u + ba->b) / 2;

      if (row) {
        list_append_pointer_static(
            &ba->up, &rb->render_data_pool, &rb->initial_bounding_areas[(row - 1) * 4 + col]);
      }
      if (col) {
        list_append_pointer_static(
            &ba->lp, &rb->render_data_pool, &rb->initial_bounding_areas[row * 4 + col - 1]);
      }
      if (row != sp_h - 1) {
        list_append_pointer_static(
            &ba->bp, &rb->render_data_pool, &rb->initial_bounding_areas[(row + 1) * 4 + col]);
      }
      if (col != sp_w - 1) {
        list_append_pointer_static(
            &ba->rp, &rb->render_data_pool, &rb->initial_bounding_areas[row * 4 + col + 1]);
      }
    }
  }
}
static void lanpr_connect_new_bounding_areas(LANPR_RenderBuffer *rb, LANPR_BoundingArea *Root)
{
  LANPR_BoundingArea *ba = Root->child, *tba;
  LinkData *lip, *lip2, *next_lip;
  LANPR_StaticMemPool *mph = &rb->render_data_pool;

  list_append_pointer_static_pool(mph, &ba[1].rp, &ba[0]);
  list_append_pointer_static_pool(mph, &ba[0].lp, &ba[1]);
  list_append_pointer_static_pool(mph, &ba[1].bp, &ba[2]);
  list_append_pointer_static_pool(mph, &ba[2].up, &ba[1]);
  list_append_pointer_static_pool(mph, &ba[2].rp, &ba[3]);
  list_append_pointer_static_pool(mph, &ba[3].lp, &ba[2]);
  list_append_pointer_static_pool(mph, &ba[3].up, &ba[0]);
  list_append_pointer_static_pool(mph, &ba[0].bp, &ba[3]);

  for (lip = Root->lp.first; lip; lip = lip->next) {
    tba = lip->data;
    if (ba[1].u > tba->b && ba[1].b < tba->u) {
      list_append_pointer_static_pool(mph, &ba[1].lp, tba);
      list_append_pointer_static_pool(mph, &tba->rp, &ba[1]);
    }
    if (ba[2].u > tba->b && ba[2].b < tba->u) {
      list_append_pointer_static_pool(mph, &ba[2].lp, tba);
      list_append_pointer_static_pool(mph, &tba->rp, &ba[2]);
    }
  }
  for (lip = Root->rp.first; lip; lip = lip->next) {
    tba = lip->data;
    if (ba[0].u > tba->b && ba[0].b < tba->u) {
      list_append_pointer_static_pool(mph, &ba[0].rp, tba);
      list_append_pointer_static_pool(mph, &tba->lp, &ba[0]);
    }
    if (ba[3].u > tba->b && ba[3].b < tba->u) {
      list_append_pointer_static_pool(mph, &ba[3].rp, tba);
      list_append_pointer_static_pool(mph, &tba->lp, &ba[3]);
    }
  }
  for (lip = Root->up.first; lip; lip = lip->next) {
    tba = lip->data;
    if (ba[0].r > tba->l && ba[0].l < tba->r) {
      list_append_pointer_static_pool(mph, &ba[0].up, tba);
      list_append_pointer_static_pool(mph, &tba->bp, &ba[0]);
    }
    if (ba[1].r > tba->l && ba[1].l < tba->r) {
      list_append_pointer_static_pool(mph, &ba[1].up, tba);
      list_append_pointer_static_pool(mph, &tba->bp, &ba[1]);
    }
  }
  for (lip = Root->bp.first; lip; lip = lip->next) {
    tba = lip->data;
    if (ba[2].r > tba->l && ba[2].l < tba->r) {
      list_append_pointer_static_pool(mph, &ba[2].bp, tba);
      list_append_pointer_static_pool(mph, &tba->up, &ba[2]);
    }
    if (ba[3].r > tba->l && ba[3].l < tba->r) {
      list_append_pointer_static_pool(mph, &ba[3].bp, tba);
      list_append_pointer_static_pool(mph, &tba->up, &ba[3]);
    }
  }
  for (lip = Root->lp.first; lip; lip = lip->next) {
    for (lip2 = ((LANPR_BoundingArea *)lip->data)->rp.first; lip2; lip2 = next_lip) {
      next_lip = lip2->next;
      tba = lip2->data;
      if (tba == Root) {
        list_remove_pointer_item_no_free(&((LANPR_BoundingArea *)lip->data)->rp, lip2);
        if (ba[1].u > tba->b && ba[1].b < tba->u) {
          list_append_pointer_static_pool(mph, &tba->rp, &ba[1]);
        }
        if (ba[2].u > tba->b && ba[2].b < tba->u) {
          list_append_pointer_static_pool(mph, &tba->rp, &ba[2]);
        }
      }
    }
  }
  for (lip = Root->rp.first; lip; lip = lip->next) {
    for (lip2 = ((LANPR_BoundingArea *)lip->data)->lp.first; lip2; lip2 = next_lip) {
      next_lip = lip2->next;
      tba = lip2->data;
      if (tba == Root) {
        list_remove_pointer_item_no_free(&((LANPR_BoundingArea *)lip->data)->lp, lip2);
        if (ba[0].u > tba->b && ba[0].b < tba->u) {
          list_append_pointer_static_pool(mph, &tba->lp, &ba[0]);
        }
        if (ba[3].u > tba->b && ba[3].b < tba->u) {
          list_append_pointer_static_pool(mph, &tba->lp, &ba[3]);
        }
      }
    }
  }
  for (lip = Root->up.first; lip; lip = lip->next) {
    for (lip2 = ((LANPR_BoundingArea *)lip->data)->bp.first; lip2; lip2 = next_lip) {
      next_lip = lip2->next;
      tba = lip2->data;
      if (tba == Root) {
        list_remove_pointer_item_no_free(&((LANPR_BoundingArea *)lip->data)->bp, lip2);
        if (ba[0].r > tba->l && ba[0].l < tba->r) {
          list_append_pointer_static_pool(mph, &tba->up, &ba[0]);
        }
        if (ba[1].r > tba->l && ba[1].l < tba->r) {
          list_append_pointer_static_pool(mph, &tba->up, &ba[1]);
        }
      }
    }
  }
  for (lip = Root->bp.first; lip; lip = lip->next) {
    for (lip2 = ((LANPR_BoundingArea *)lip->data)->up.first; lip2; lip2 = next_lip) {
      next_lip = lip2->next;
      tba = lip2->data;
      if (tba == Root) {
        list_remove_pointer_item_no_free(&((LANPR_BoundingArea *)lip->data)->up, lip2);
        if (ba[2].r > tba->l && ba[2].l < tba->r) {
          list_append_pointer_static_pool(mph, &tba->bp, &ba[2]);
        }
        if (ba[3].r > tba->l && ba[3].l < tba->r) {
          list_append_pointer_static_pool(mph, &tba->bp, &ba[3]);
        }
      }
    }
  }
  while (list_pop_pointer_no_free(&Root->lp))
    ;
  while (list_pop_pointer_no_free(&Root->rp))
    ;
  while (list_pop_pointer_no_free(&Root->up))
    ;
  while (list_pop_pointer_no_free(&Root->bp))
    ;
}
static void lanpr_link_triangle_with_bounding_area(LANPR_RenderBuffer *rb,
                                                   LANPR_BoundingArea *RootBoundingArea,
                                                   LANPR_RenderTriangle *rt,
                                                   real *LRUB,
                                                   int Recursive);
static void lanpr_split_bounding_area(LANPR_RenderBuffer *rb, LANPR_BoundingArea *Root)
{
  LANPR_BoundingArea *ba = mem_static_aquire(&rb->render_data_pool,
                                             sizeof(LANPR_BoundingArea) * 4);
  LANPR_RenderTriangle *rt;
  LANPR_RenderLine *rl;

  ba[0].l = Root->cx;
  ba[0].r = Root->r;
  ba[0].u = Root->u;
  ba[0].b = Root->cy;
  ba[0].cx = (ba[0].l + ba[0].r) / 2;
  ba[0].cy = (ba[0].u + ba[0].b) / 2;

  ba[1].l = Root->l;
  ba[1].r = Root->cx;
  ba[1].u = Root->u;
  ba[1].b = Root->cy;
  ba[1].cx = (ba[1].l + ba[1].r) / 2;
  ba[1].cy = (ba[1].u + ba[1].b) / 2;

  ba[2].l = Root->l;
  ba[2].r = Root->cx;
  ba[2].u = Root->cy;
  ba[2].b = Root->b;
  ba[2].cx = (ba[2].l + ba[2].r) / 2;
  ba[2].cy = (ba[2].u + ba[2].b) / 2;

  ba[3].l = Root->cx;
  ba[3].r = Root->r;
  ba[3].u = Root->cy;
  ba[3].b = Root->b;
  ba[3].cx = (ba[3].l + ba[3].r) / 2;
  ba[3].cy = (ba[3].u + ba[3].b) / 2;

  Root->child = ba;

  lanpr_connect_new_bounding_areas(rb, Root);

  while ((rt = list_pop_pointer_no_free(&Root->linked_triangles)) != NULL) {
    LANPR_BoundingArea *cba = Root->child;
    real b[4];
    b[0] = MIN3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
    b[1] = MAX3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
    b[2] = MAX3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
    b[3] = MIN3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
    if (TNS_BOUND_AREA_CROSSES(b, &cba[0].l)) {
      lanpr_link_triangle_with_bounding_area(rb, &cba[0], rt, b, 0);
    }
    if (TNS_BOUND_AREA_CROSSES(b, &cba[1].l)) {
      lanpr_link_triangle_with_bounding_area(rb, &cba[1], rt, b, 0);
    }
    if (TNS_BOUND_AREA_CROSSES(b, &cba[2].l)) {
      lanpr_link_triangle_with_bounding_area(rb, &cba[2], rt, b, 0);
    }
    if (TNS_BOUND_AREA_CROSSES(b, &cba[3].l)) {
      lanpr_link_triangle_with_bounding_area(rb, &cba[3], rt, b, 0);
    }
  }

  while ((rl = list_pop_pointer_no_free(&Root->linked_lines)) != NULL) {
    lanpr_link_line_with_bounding_area(rb, Root, rl);
  }

  rb->bounding_area_count += 3;
}
static int lanpr_line_crosses_bounding_area(LANPR_RenderBuffer *UNUSED(fb),
                                            tnsVector2d l,
                                            tnsVector2d r,
                                            LANPR_BoundingArea *ba)
{
  real vx, vy;
  tnsVector4d converted;
  real c1, c;

  if (((converted[0] = (real)ba->l) > MAX2(l[0], r[0])) ||
      ((converted[1] = (real)ba->r) < MIN2(l[0], r[0])) ||
      ((converted[2] = (real)ba->b) > MAX2(l[1], r[1])) ||
      ((converted[3] = (real)ba->u) < MIN2(l[1], r[1]))) {
    return 0;
  }

  vx = l[0] - r[0];
  vy = l[1] - r[1];

  c1 = vx * (converted[2] - l[1]) - vy * (converted[0] - l[0]);
  c = c1;

  c1 = vx * (converted[2] - l[1]) - vy * (converted[1] - l[0]);
  if (c1 * c <= 0) {
    return 1;
  }
  else {
    c = c1;
  }

  c1 = vx * (converted[3] - l[1]) - vy * (converted[0] - l[0]);
  if (c1 * c <= 0) {
    return 1;
  }
  else {
    c = c1;
  }

  c1 = vx * (converted[3] - l[1]) - vy * (converted[1] - l[0]);
  if (c1 * c <= 0) {
    return 1;
  }
  else {
    c = c1;
  }

  return 0;
}
static int lanpr_triangle_covers_bounding_area(LANPR_RenderBuffer *fb,
                                               LANPR_RenderTriangle *rt,
                                               LANPR_BoundingArea *ba)
{
  tnsVector2d p1, p2, p3, p4;
  real *FBC1 = rt->v[0]->fbcoord, *FBC2 = rt->v[1]->fbcoord, *FBC3 = rt->v[2]->fbcoord;

  p3[0] = p1[0] = (real)ba->l;
  p2[1] = p1[1] = (real)ba->b;
  p2[0] = p4[0] = (real)ba->r;
  p3[1] = p4[1] = (real)ba->u;

  if ((FBC1[0] >= p1[0] && FBC1[0] <= p2[0] && FBC1[1] >= p1[1] && FBC1[1] <= p3[1]) ||
      (FBC2[0] >= p1[0] && FBC2[0] <= p2[0] && FBC2[1] >= p1[1] && FBC2[1] <= p3[1]) ||
      (FBC3[0] >= p1[0] && FBC3[0] <= p2[0] && FBC3[1] >= p1[1] && FBC3[1] <= p3[1])) {
    return 1;
  }

  if (ED_lanpr_point_inside_triangled(p1, FBC1, FBC2, FBC3) ||
      ED_lanpr_point_inside_triangled(p2, FBC1, FBC2, FBC3) ||
      ED_lanpr_point_inside_triangled(p3, FBC1, FBC2, FBC3) ||
      ED_lanpr_point_inside_triangled(p4, FBC1, FBC2, FBC3)) {
    return 1;
  }

  if ((lanpr_line_crosses_bounding_area(fb, FBC1, FBC2, ba)) ||
      (lanpr_line_crosses_bounding_area(fb, FBC2, FBC3, ba)) ||
      (lanpr_line_crosses_bounding_area(fb, FBC3, FBC1, ba))) {
    return 1;
  }

  return 0;
}
static void lanpr_link_triangle_with_bounding_area(LANPR_RenderBuffer *rb,
                                                   LANPR_BoundingArea *RootBoundingArea,
                                                   LANPR_RenderTriangle *rt,
                                                   real *LRUB,
                                                   int Recursive)
{
  if (!lanpr_triangle_covers_bounding_area(rb, rt, RootBoundingArea)) {
    return;
  }
  if (!RootBoundingArea->child) {
    list_append_pointer_static_pool(
        &rb->render_data_pool, &RootBoundingArea->linked_triangles, rt);
    RootBoundingArea->triangle_count++;
    if (RootBoundingArea->triangle_count > 200 && Recursive) {
      lanpr_split_bounding_area(rb, RootBoundingArea);
    }
    if (Recursive && rb->enable_intersections) {
      lanpr_triangle_calculate_intersections_in_bounding_area(rb, rt, RootBoundingArea);
    }
  }
  else {
    LANPR_BoundingArea *ba = RootBoundingArea->child;
    real *B1 = LRUB;
    real b[4];
    if (!LRUB) {
      b[0] = MIN3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
      b[1] = MAX3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
      b[2] = MAX3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
      b[3] = MIN3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
      B1 = b;
    }
    if (TNS_BOUND_AREA_CROSSES(B1, &ba[0].l)) {
      lanpr_link_triangle_with_bounding_area(rb, &ba[0], rt, B1, Recursive);
    }
    if (TNS_BOUND_AREA_CROSSES(B1, &ba[1].l)) {
      lanpr_link_triangle_with_bounding_area(rb, &ba[1], rt, B1, Recursive);
    }
    if (TNS_BOUND_AREA_CROSSES(B1, &ba[2].l)) {
      lanpr_link_triangle_with_bounding_area(rb, &ba[2], rt, B1, Recursive);
    }
    if (TNS_BOUND_AREA_CROSSES(B1, &ba[3].l)) {
      lanpr_link_triangle_with_bounding_area(rb, &ba[3], rt, B1, Recursive);
    }
  }
}
static void lanpr_link_line_with_bounding_area(LANPR_RenderBuffer *rb,
                                               LANPR_BoundingArea *RootBoundingArea,
                                               LANPR_RenderLine *rl)
{
  if (!RootBoundingArea->child) {
    list_append_pointer_static_pool(&rb->render_data_pool, &RootBoundingArea->linked_lines, rl);
  }
  else {
    if (lanpr_line_crosses_bounding_area(
            rb, rl->l->fbcoord, rl->r->fbcoord, &RootBoundingArea->child[0])) {
      lanpr_link_line_with_bounding_area(rb, &RootBoundingArea->child[0], rl);
    }
    else if (lanpr_line_crosses_bounding_area(
                 rb, rl->l->fbcoord, rl->r->fbcoord, &RootBoundingArea->child[1])) {
      lanpr_link_line_with_bounding_area(rb, &RootBoundingArea->child[1], rl);
    }
    else if (lanpr_line_crosses_bounding_area(
                 rb, rl->l->fbcoord, rl->r->fbcoord, &RootBoundingArea->child[2])) {
      lanpr_link_line_with_bounding_area(rb, &RootBoundingArea->child[2], rl);
    }
    else if (lanpr_line_crosses_bounding_area(
                 rb, rl->l->fbcoord, rl->r->fbcoord, &RootBoundingArea->child[3])) {
      lanpr_link_line_with_bounding_area(rb, &RootBoundingArea->child[3], rl);
    }
  }
}
static int lanpr_get_triangle_bounding_areas(LANPR_RenderBuffer *rb,
                                             LANPR_RenderTriangle *rt,
                                             int *rowBegin,
                                             int *rowEnd,
                                             int *colBegin,
                                             int *colEnd)
{
  real sp_w = rb->width_per_tile, sp_h = rb->height_per_tile;
  real b[4];

  if (!rt->v[0] || !rt->v[1] || !rt->v[2]) {
    return 0;
  }

  b[0] = MIN3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
  b[1] = MAX3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
  b[2] = MIN3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
  b[3] = MAX3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);

  if (b[0] > 1 || b[1] < -1 || b[2] > 1 || b[3] < -1) {
    return 0;
  }

  (*colBegin) = (int)((b[0] + 1.0) / sp_w);
  (*colEnd) = (int)((b[1] + 1.0) / sp_w);
  (*rowEnd) = rb->tile_count_y - (int)((b[2] + 1.0) / sp_h) - 1;
  (*rowBegin) = rb->tile_count_y - (int)((b[3] + 1.0) / sp_h) - 1;

  if ((*colEnd) >= rb->tile_count_x) {
    (*colEnd) = rb->tile_count_x - 1;
  }
  if ((*rowEnd) >= rb->tile_count_y) {
    (*rowEnd) = rb->tile_count_y - 1;
  }
  if ((*colBegin) < 0) {
    (*colBegin) = 0;
  }
  if ((*rowBegin) < 0) {
    (*rowBegin) = 0;
  }

  return 1;
}
static int lanpr_get_line_bounding_areas(LANPR_RenderBuffer *rb,
                                         LANPR_RenderLine *rl,
                                         int *rowBegin,
                                         int *rowEnd,
                                         int *colBegin,
                                         int *colEnd)
{
  real sp_w = rb->width_per_tile, sp_h = rb->height_per_tile;
  real b[4];

  if (!rl->l || !rl->r) {
    return 0;
  }

  if (rl->l->fbcoord[0] != rl->l->fbcoord[0] || rl->r->fbcoord[0] != rl->r->fbcoord[0]) {
    return 0;
  }

  b[0] = MIN2(rl->l->fbcoord[0], rl->r->fbcoord[0]);
  b[1] = MAX2(rl->l->fbcoord[0], rl->r->fbcoord[0]);
  b[2] = MIN2(rl->l->fbcoord[1], rl->r->fbcoord[1]);
  b[3] = MAX2(rl->l->fbcoord[1], rl->r->fbcoord[1]);

  if (b[0] > 1 || b[1] < -1 || b[2] > 1 || b[3] < -1) {
    return 0;
  }

  (*colBegin) = (int)((b[0] + 1.0) / sp_w);
  (*colEnd) = (int)((b[1] + 1.0) / sp_w);
  (*rowEnd) = rb->tile_count_y - (int)((b[2] + 1.0) / sp_h) - 1;
  (*rowBegin) = rb->tile_count_y - (int)((b[3] + 1.0) / sp_h) - 1;

  if ((*colEnd) >= rb->tile_count_x) {
    (*colEnd) = rb->tile_count_x - 1;
  }
  if ((*rowEnd) >= rb->tile_count_y) {
    (*rowEnd) = rb->tile_count_y - 1;
  }
  if ((*colBegin) < 0) {
    (*colBegin) = 0;
  }
  if ((*rowBegin) < 0) {
    (*rowBegin) = 0;
  }

  return 1;
}
LANPR_BoundingArea *ED_lanpr_get_point_bounding_area(LANPR_RenderBuffer *rb, real x, real y)
{
  real sp_w = rb->width_per_tile, sp_h = rb->height_per_tile;
  int col, row;

  if (x > 1 || x < -1 || y > 1 || y < -1) {
    return 0;
  }

  col = (int)((x + 1.0) / sp_w);
  row = rb->tile_count_y - (int)((y + 1.0) / sp_h) - 1;

  if (col >= rb->tile_count_x) {
    col = rb->tile_count_x - 1;
  }
  if (row >= rb->tile_count_y) {
    row = rb->tile_count_y - 1;
  }
  if (col < 0) {
    col = 0;
  }
  if (row < 0) {
    row = 0;
  }

  return &rb->initial_bounding_areas[row * 4 + col];
}
static LANPR_BoundingArea *lanpr_get_point_bounding_area_recursive(LANPR_BoundingArea *ba,
                                                                   real x,
                                                                   real y)
{
  if (!ba->child) {
    return ba;
  }
  else {
#define IN_BOUND(i, x, y) \
  ba->child[i].l <= x && ba->child[i].r >= x && ba->child[i].b <= y && ba->child[i].u >= y

    if (IN_BOUND(0, x, y)) {
      return lanpr_get_point_bounding_area_recursive(&ba->child[0], x, y);
    }
    else if (IN_BOUND(1, x, y)) {
      return lanpr_get_point_bounding_area_recursive(&ba->child[1], x, y);
    }
    else if (IN_BOUND(2, x, y)) {
      return lanpr_get_point_bounding_area_recursive(&ba->child[2], x, y);
    }
    else if (IN_BOUND(3, x, y)) {
      return lanpr_get_point_bounding_area_recursive(&ba->child[3], x, y);
    }
  }
  return NULL;
}
LANPR_BoundingArea *ED_lanpr_get_point_bounding_area_deep(LANPR_RenderBuffer *rb, real x, real y)
{
  LANPR_BoundingArea *ba;
  if ((ba = ED_lanpr_get_point_bounding_area(rb, x, y)) != NULL) {
    return lanpr_get_point_bounding_area_recursive(ba, x, y);
  }
  return NULL;
}

static void lanpr_add_triangles(LANPR_RenderBuffer *rb)
{
  LANPR_RenderElementLinkNode *reln;
  LANPR_RenderTriangle *rt;
  int i, lim;
  int x1, x2, y1, y2;
  int r, co;

  for (reln = rb->triangle_buffer_pointers.first; reln; reln = reln->next) {
    rt = reln->pointer;
    lim = reln->element_count;
    for (i = 0; i < lim; i++) {
      if (rt->cull_status) {
        rt = (void *)(((BYTE *)rt) + rb->triangle_size);
        continue;
      }
      if (lanpr_get_triangle_bounding_areas(rb, rt, &y1, &y2, &x1, &x2)) {
        for (co = x1; co <= x2; co++) {
          for (r = y1; r <= y2; r++) {
            lanpr_link_triangle_with_bounding_area(
                rb, &rb->initial_bounding_areas[r * 4 + co], rt, 0, 1);
          }
        }
      }
      else {
        ; /*  throw away. */
      }
      rt = (void *)(((BYTE *)rt) + rb->triangle_size);
      /*  if (tnsglobal_TriangleIntersectionCount >= 2000) { */
      /*  tnsset_PlusRenderIntersectionCount(rb, tnsglobal_TriangleIntersectionCount); */
      /*  tnsglobal_TriangleIntersectionCount = 0; */
      /* } */
    }
  }
  /*  tnsset_PlusRenderIntersectionCount(rb, tnsglobal_TriangleIntersectionCount); */
}
static LANPR_BoundingArea *lanpr_get_next_bounding_area(LANPR_BoundingArea *This,
                                                        LANPR_RenderLine *rl,
                                                        real x,
                                                        real y,
                                                        real k,
                                                        int PositiveX,
                                                        int PositiveY,
                                                        real *NextX,
                                                        real *NextY)
{
  real rx, ry, ux, uy, lx, ly, bx, by;
  real r1, r2;
  LANPR_BoundingArea *ba;
  LinkData *lip;
  if (PositiveX > 0) {
    rx = This->r;
    ry = y + k * (rx - x);
    if (PositiveY > 0) {
      uy = This->u;
      ux = x + (uy - y) / k;
      r1 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], rx);
      r2 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], ux);
      if (MIN2(r1, r2) > 1) {
        return 0;
      }
      if (r1 <= r2) {
        for (lip = This->rp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->u >= ry && ba->b < ry) {
            *NextX = rx;
            *NextY = ry;
            return ba;
          }
        }
      }
      else {
        for (lip = This->up.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->r >= ux && ba->l < ux) {
            *NextX = ux;
            *NextY = uy;
            return ba;
          }
        }
      }
    }
    else if (PositiveY < 0) {
      by = This->b;
      bx = x + (by - y) / k;
      r1 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], rx);
      r2 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], bx);
      if (MIN2(r1, r2) > 1) {
        return 0;
      }
      if (r1 <= r2) {
        for (lip = This->rp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->u >= ry && ba->b < ry) {
            *NextX = rx;
            *NextY = ry;
            return ba;
          }
        }
      }
      else {
        for (lip = This->bp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->r >= bx && ba->l < bx) {
            *NextX = bx;
            *NextY = by;
            return ba;
          }
        }
      }
    }
    else { /*  Y diffence == 0 */
      r1 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], This->r);
      if (r1 > 1) {
        return 0;
      }
      for (lip = This->rp.first; lip; lip = lip->next) {
        ba = lip->data;
        if (ba->u >= y && ba->b < y) {
          *NextX = This->r;
          *NextY = y;
          return ba;
        }
      }
    }
  }
  else if (PositiveX < 0) { /*  X diffence < 0 */
    lx = This->l;
    ly = y + k * (lx - x);
    if (PositiveY > 0) {
      uy = This->u;
      ux = x + (uy - y) / k;
      r1 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], lx);
      r2 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], ux);
      if (MIN2(r1, r2) > 1) {
        return 0;
      }
      if (r1 <= r2) {
        for (lip = This->lp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->u >= ly && ba->b < ly) {
            *NextX = lx;
            *NextY = ly;
            return ba;
          }
        }
      }
      else {
        for (lip = This->up.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->r >= ux && ba->l < ux) {
            *NextX = ux;
            *NextY = uy;
            return ba;
          }
        }
      }
    }
    else if (PositiveY < 0) {
      by = This->b;
      bx = x + (by - y) / k;
      r1 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], lx);
      r2 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], bx);
      if (MIN2(r1, r2) > 1) {
        return 0;
      }
      if (r1 <= r2) {
        for (lip = This->lp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->u >= ly && ba->b < ly) {
            *NextX = lx;
            *NextY = ly;
            return ba;
          }
        }
      }
      else {
        for (lip = This->bp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->r >= bx && ba->l < bx) {
            *NextX = bx;
            *NextY = by;
            return ba;
          }
        }
      }
    }
    else { /*  Y diffence == 0 */
      r1 = tMatGetLinearRatio(rl->l->fbcoord[0], rl->r->fbcoord[0], This->l);
      if (r1 > 1) {
        return 0;
      }
      for (lip = This->lp.first; lip; lip = lip->next) {
        ba = lip->data;
        if (ba->u >= y && ba->b < y) {
          *NextX = This->l;
          *NextY = y;
          return ba;
        }
      }
    }
  }
  else { /*  X difference == 0; */
    if (PositiveY > 0) {
      r1 = tMatGetLinearRatio(rl->l->fbcoord[1], rl->r->fbcoord[1], This->u);
      if (r1 > 1) {
        return 0;
      }
      for (lip = This->up.first; lip; lip = lip->next) {
        ba = lip->data;
        if (ba->r > x && ba->l <= x) {
          *NextX = x;
          *NextY = This->u;
          return ba;
        }
      }
    }
    else if (PositiveY < 0) {
      r1 = tMatGetLinearRatio(rl->l->fbcoord[1], rl->r->fbcoord[1], This->b);
      if (r1 > 1) {
        return 0;
      }
      for (lip = This->bp.first; lip; lip = lip->next) {
        ba = lip->data;
        if (ba->r > x && ba->l <= x) {
          *NextX = x;
          *NextY = This->b;
          return ba;
        }
      }
    }
    else
      return 0; /*  segment has no length */
  }
  return 0;
}

static LANPR_BoundingArea *lanpr_get_bounding_area(LANPR_RenderBuffer *rb, real x, real y)
{
  LANPR_BoundingArea *iba;
  real sp_w = rb->width_per_tile, sp_h = rb->height_per_tile;
  int c = (int)((x + 1.0) / sp_w);
  int r = rb->tile_count_y - (int)((y + 1.0) / sp_h) - 1;
  if (r < 0) {
    r = 0;
  }
  if (c < 0) {
    c = 0;
  }
  if (r >= rb->tile_count_y) {
    r = rb->tile_count_y - 1;
  }
  if (c >= rb->tile_count_x) {
    c = rb->tile_count_x - 1;
  }

  iba = &rb->initial_bounding_areas[r * 4 + c];
  while (iba->child) {
    if (x > iba->cx) {
      if (y > iba->cy) {
        iba = &iba->child[0];
      }
      else {
        iba = &iba->child[3];
      }
    }
    else {
      if (y > iba->cy) {
        iba = &iba->child[1];
      }
      else {
        iba = &iba->child[2];
      }
    }
  }
  return iba;
}
static LANPR_BoundingArea *lanpr_get_first_possible_bounding_area(LANPR_RenderBuffer *rb,
                                                                  LANPR_RenderLine *rl)
{
  LANPR_BoundingArea *iba;
  real data[2] = {rl->l->fbcoord[0], rl->l->fbcoord[1]};
  tnsVector2d LU = {-1, 1}, RU = {1, 1}, LB = {-1, -1}, RB = {1, -1};
  real r = 1, sr = 1;

  if (data[0] > -1 && data[0] < 1 && data[1] > -1 && data[1] < 1) {
    return lanpr_get_bounding_area(rb, data[0], data[1]);
  }
  else {
    if ((lanpr_LineIntersectTest2d(rl->l->fbcoord, rl->r->fbcoord, LU, RU, &sr) && sr < r &&
         sr > 0) ||
        (lanpr_LineIntersectTest2d(rl->l->fbcoord, rl->r->fbcoord, LB, RB, &sr) && sr < r &&
         sr > 0) ||
        (lanpr_LineIntersectTest2d(rl->l->fbcoord, rl->r->fbcoord, LB, LU, &sr) && sr < r &&
         sr > 0) ||
        (lanpr_LineIntersectTest2d(rl->l->fbcoord, rl->r->fbcoord, RB, RU, &sr) && sr < r &&
         sr > 0)) {
      r = sr;
    }
    interp_v2_v2v2_db(data, rl->l->fbcoord, rl->r->fbcoord, r);

    return lanpr_get_bounding_area(rb, data[0], data[1]);
  }

  return iba;
}

/* Calculations */

int ED_lanpr_compute_feature_lines_internal(Depsgraph *depsgraph, int intersectons_only)
{
  LANPR_RenderBuffer *rb;
  Scene *s = DEG_get_evaluated_scene(depsgraph);
  SceneLANPR *lanpr = &s->lanpr;
  int is_lanpr_engine = !strcmp(s->r.engine, RE_engine_id_BLENDER_LANPR);

  if (!is_lanpr_engine && (lanpr->flag & LANPR_ENABLED) == 0) {
    return OPERATOR_CANCELLED;
  }

  rb = ED_lanpr_create_render_buffer();

  lanpr_share.render_buffer_shared = rb;

  rb->scene = s;
  rb->w = s->r.xsch;
  rb->h = s->r.ysch;
  rb->enable_intersections = lanpr->enable_intersections;

  rb->triangle_size = lanpr_get_render_triangle_size(rb);

  rb->max_occlusion_level = lanpr_get_max_occlusion_level(depsgraph);

  ED_lanpr_update_render_progress("LANPR: Loading geometries.");

  lanpr_make_render_geometry_buffers(depsgraph, rb->scene, rb->scene->camera, rb);

  lanpr_compute_view_Vector(rb);
  lanpr_cull_triangles(rb);

  lanpr_perspective_division(rb);

  lanpr_make_initial_bounding_areas(rb);

  if (!intersectons_only) {
    lanpr_compute_scene_contours(rb, lanpr->crease_threshold);
  }

  ED_lanpr_update_render_progress("LANPR: Computing intersections.");

  lanpr_add_triangles(rb);

  ED_lanpr_update_render_progress("LANPR: Computing line occlusion.");

  if (!intersectons_only) {
    lanpr_THREAD_calculate_line_occlusion_begin(rb);
  }

  ED_lanpr_update_render_progress("LANPR: Chaining.");

  /* When not using LANPR engine, chaining is forced in order to generate data for GPencil. */
  if ((lanpr->enable_chaining || !is_lanpr_engine) && (!intersectons_only)) {
    float t_image = rb->scene->lanpr.chaining_image_threshold;
    float t_geom = rb->scene->lanpr.chaining_geometry_threshold;

    ED_lanpr_NO_THREAD_chain_feature_lines(rb);

    if (is_lanpr_engine) {
      /* Enough with it. We can provide an option after we have LANPR internal smoothing */
      return OPERATOR_CANCELLED;
    }

    /* Below are simply for better GPencil experience. */

    ED_lanpr_split_chains_for_fixed_occlusion(rb);

    if (t_image < FLT_EPSILON && t_geom < FLT_EPSILON) {
      t_geom = 0.0f;
      t_image = 0.01f;
    }

    ED_lanpr_connect_chains(rb, 1);
    ED_lanpr_connect_chains(rb, 0);

    /* This configuration ensures there won't be accidental lost of short segments */
    ED_lanpr_discard_short_chains(rb, MIN3(t_image, t_geom, 0.01f) - FLT_EPSILON);
  }

  rb->cached_for_frame = rb->scene->r.cfra;

  return OPERATOR_FINISHED;
}
static bool lanpr_camera_exists(struct bContext *c)
{
  Scene *s = CTX_data_scene(c);
  return s->camera ? true : false;
}
static int lanpr_compute_feature_lines_exec(struct bContext *C, struct wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  SceneLANPR *lanpr = &scene->lanpr;
  int result;
  int is_lanpr_engine = !strcmp(scene->r.engine, RE_engine_id_BLENDER_LANPR);

  if (!is_lanpr_engine && (lanpr->flag & LANPR_ENABLED) == 0) {
    return OPERATOR_CANCELLED;
  }

  if (!scene->camera) {
    BKE_report(op->reports, RPT_ERROR, "There is no active camera in this scene!");
    printf("LANPR Warning: There is no active camera in this scene!\n");
    return OPERATOR_FINISHED;
  }

  int intersections_only = (is_lanpr_engine && lanpr->master_mode != LANPR_MASTER_MODE_SOFTWARE);

  result = ED_lanpr_compute_feature_lines_internal(CTX_data_depsgraph_pointer(C),
                                                   intersections_only);

  ED_lanpr_rebuild_all_command(lanpr);

  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);

  return result;
}
static void lanpr_compute_feature_lines_cancel(struct bContext *UNUSED(C),
                                               struct wmOperator *UNUSED(op))
{
  return;
}

void SCENE_OT_lanpr_calculate_feature_lines(struct wmOperatorType *ot)
{

  /* identifiers */
  ot->name = "Calculate Feature Lines";
  ot->description = "LANPR calculates feature line in current scene";
  ot->idname = "SCENE_OT_lanpr_calculate";

  ot->poll = lanpr_camera_exists;
  ot->cancel = lanpr_compute_feature_lines_cancel;
  ot->exec = lanpr_compute_feature_lines_exec;
}

static bool lanpr_render_buffer_found(struct bContext *UNUSED(C))
{
  if (lanpr_share.render_buffer_shared) {
    return true;
  }
  return false;
}

/* Access */
bool ED_lanpr_dpix_shader_error()
{
  return lanpr_share.dpix_shader_error;
}
bool ED_lanpr_disable_edge_splits(Scene *s)
{
  return ((s->lanpr.flag & LANPR_ENABLED) && s->lanpr.disable_edge_splits);
}

/* GPencil bindings */

static void lanpr_generate_gpencil_from_chain(Depsgraph *depsgraph,
                                              Object *ob,
                                              bGPDlayer *UNUSED(gpl),
                                              bGPDframe *gpf,
                                              int qi_begin,
                                              int qi_end,
                                              int material_nr,
                                              Collection *col,
                                              int types)
{
  Scene *scene = DEG_get_evaluated_scene(depsgraph);
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;

  if (rb == NULL) {
    printf("NULL LANPR rb!\n");
    return;
  }
  if (scene->lanpr.master_mode != LANPR_MASTER_MODE_SOFTWARE) {
    return;
  }

  int color_idx = 0;
  short thickness = 100;

  float mat[4][4];

  unit_m4(mat);

  LANPR_RenderLineChain *rlc;
  LANPR_RenderLineChainItem *rlci;
  for (rlc = rb->chains.first; rlc; rlc = rlc->next) {

    if (rlc->picked) {
      continue;
    }
    if (ob && !rlc->object_ref) {
      continue; /* intersection lines are all in the first collection running into here */
    }
    if (!(rlc->type & types)) {
      continue;
    }
    if (rlc->level > qi_end || rlc->level < qi_begin) {
      continue;
    }
    if (ob && &ob->id != rlc->object_ref->id.orig_id) {
      continue;
    }
    if (col && rlc->object_ref) {
      if (!BKE_collection_has_object_recursive(col, (Object *)rlc->object_ref->id.orig_id)) {
        continue;
      }
    }

    rlc->picked = 1;

    int array_idx = 0;
    int count = ED_lanpr_count_chain(rlc);
    bGPDstroke *gps = BKE_gpencil_add_stroke(gpf, color_idx, count, thickness);

    float *stroke_data = BLI_array_alloca(stroke_data, count * GP_PRIM_DATABUF_SIZE);

    for (rlci = rlc->chain.first; rlci; rlci = rlci->next) {
      float opatity = 1.0f; /* rlci->occlusion ? 0.0f : 1.0f; */
      stroke_data[array_idx] = rlci->gpos[0];
      stroke_data[array_idx + 1] = rlci->gpos[1];
      stroke_data[array_idx + 2] = rlci->gpos[2];
      stroke_data[array_idx + 3] = 1;       /*  thickness */
      stroke_data[array_idx + 4] = opatity; /*  hardness? */
      array_idx += 5;
    }

    BKE_gpencil_stroke_add_points(gps, stroke_data, count, mat);
    gps->mat_nr = material_nr;
  }
}
static void lanpr_clear_gp_lanpr_flags(Depsgraph *dg, int frame)
{
  DEG_OBJECT_ITER_BEGIN (dg,
                         o,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY | DEG_ITER_OBJECT_FLAG_VISIBLE |
                             DEG_ITER_OBJECT_FLAG_DUPLI | DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    if (o->type == OB_GPENCIL) {
      bGPdata *gpd = ((Object *)o->id.orig_id)->data;
      bGPDlayer *gpl;
      for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
        bGPDframe *gpf = BKE_gpencil_layer_find_frame(gpl, frame);
        if (!gpf) {
          continue;
        }
        gpf->flag &= ~GP_FRAME_LANPR_CLEARED;
      }
    }
  }
  DEG_OBJECT_ITER_END;
}
static void lanpr_update_gp_strokes_recursive(
    Depsgraph *dg, struct Collection *col, int frame, Object *source_only, Object *target_only)
{
  Object *ob;
  Object *gpobj;
  ModifierData *md;
  bGPdata *gpd;
  bGPDlayer *gpl;
  bGPDframe *gpf;
  CollectionObject *co;
  CollectionChild *cc;

  for (co = col->gobject.first; co || source_only; co = co->next) {
    ob = source_only ? source_only : co->ob;
    for (md = ob->modifiers.first; md; md = md->next) {
      if (md->type == eModifierType_FeatureLine) {
        FeatureLineModifierData *flmd = (FeatureLineModifierData *)md;
        if (flmd->target && flmd->target->type == OB_GPENCIL) {
          gpobj = flmd->target;

          if (target_only && target_only != gpobj) {
            continue;
          }

          gpd = gpobj->data;
          gpl = BKE_gpencil_layer_get_index(gpd, flmd->layer, 1);
          if (!gpl) {
            gpl = BKE_gpencil_layer_addnew(gpd, "lanpr_layer", true);
          }
          gpf = BKE_gpencil_layer_getframe(gpl, frame, GP_GETFRAME_ADD_NEW);

          if (gpf->strokes.first &&
              !lanpr_share.render_buffer_shared->scene->lanpr.gpencil_overwrite) {
            continue;
          }

          if (!(gpf->flag & GP_FRAME_LANPR_CLEARED)) {
            BKE_gpencil_free_strokes(gpf);
            gpf->flag |= GP_FRAME_LANPR_CLEARED;
          }

          lanpr_generate_gpencil_from_chain(dg,
                                            ob,
                                            gpl,
                                            gpf,
                                            flmd->level_start,
                                            flmd->use_multiple_levels ? flmd->level_end :
                                                                        flmd->level_start,
                                            flmd->material,
                                            NULL,
                                            flmd->types);
          DEG_id_tag_update(&gpd->id,
                            ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
        }
      }
    }
    if (source_only) {
      return;
    }
  }
  for (cc = col->children.first; cc; cc = cc->next) {
    lanpr_update_gp_strokes_recursive(dg, cc->collection, frame, source_only, target_only);
  }
}
static void lanpr_update_gp_strokes_collection(
    Depsgraph *dg, struct Collection *col, int frame, int this_only, Object *target_only)
{
  Object *gpobj;
  bGPdata *gpd;
  bGPDlayer *gpl;
  bGPDframe *gpf;
  CollectionChild *cc;

  /* depth first */
  if (!this_only) {
    for (cc = col->children.first; cc; cc = cc->next) {
      lanpr_update_gp_strokes_collection(dg, cc->collection, frame, this_only, target_only);
    }
  }

  if (col->lanpr.usage != COLLECTION_LANPR_INCLUDE || !col->lanpr.target) {
    return;
  }

  gpobj = col->lanpr.target;

  if (target_only && target_only != gpobj) {
    return;
  }

  gpd = gpobj->data;
  gpl = BKE_gpencil_layer_get_index(gpd, col->lanpr.layer, 1);
  if (!gpl) {
    gpl = BKE_gpencil_layer_addnew(gpd, "lanpr_layer", true);
  }
  gpf = BKE_gpencil_layer_getframe(gpl, frame, GP_GETFRAME_ADD_NEW);

  if (gpf->strokes.first && !lanpr_share.render_buffer_shared->scene->lanpr.gpencil_overwrite) {
    return;
  }

  if (!(gpf->flag & GP_FRAME_LANPR_CLEARED)) {
    BKE_gpencil_free_strokes(gpf);
    gpf->flag |= GP_FRAME_LANPR_CLEARED;
  }

  lanpr_generate_gpencil_from_chain(dg,
                                    NULL,
                                    gpl,
                                    gpf,
                                    col->lanpr.level_start,
                                    col->lanpr.use_multiple_levels ? col->lanpr.level_end :
                                                                     col->lanpr.level_start,
                                    col->lanpr.material,
                                    col,
                                    col->lanpr.types);
  DEG_id_tag_update(&gpd->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
}
static void lanpr_update_gp_strokes_actual(Scene *scene, Depsgraph *dg)
{
  int frame = scene->r.cfra;

  if (!lanpr_share.render_buffer_shared ||
      lanpr_share.render_buffer_shared->cached_for_frame != frame) {
    ED_lanpr_compute_feature_lines_internal(dg, 0);
  }

  ED_lanpr_chain_clear_picked_flag(lanpr_share.render_buffer_shared);

  lanpr_update_gp_strokes_recursive(dg, scene->master_collection, frame, NULL, NULL);

  lanpr_update_gp_strokes_collection(dg, scene->master_collection, frame, 0, NULL);

  lanpr_clear_gp_lanpr_flags(dg, frame);
}
static int lanpr_update_gp_strokes_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph_pointer(C);

  lanpr_update_gp_strokes_actual(scene, dg);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_bake_gp_strokes_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph_pointer(C);
  int frame;
  int frame_begin = scene->r.sfra;
  int frame_end = scene->r.efra;

  for (frame = frame_begin; frame <= frame_end; frame++) {
    // BKE_scene_frame_set(scene,frame);
    DEG_evaluate_on_framechange(CTX_data_main(C), dg, frame);

    ED_lanpr_compute_feature_lines_internal(dg, 0);

    ED_lanpr_chain_clear_picked_flag(lanpr_share.render_buffer_shared);

    lanpr_update_gp_strokes_recursive(dg, scene->master_collection, frame, NULL, NULL);

    lanpr_update_gp_strokes_collection(dg, scene->master_collection, frame, 0, NULL);
  }

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_update_gp_target_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph_pointer(C);
  Object *gpo = CTX_data_active_object(C);

  int frame = scene->r.cfra;

  if (!lanpr_share.render_buffer_shared ||
      lanpr_share.render_buffer_shared->cached_for_frame != frame) {
    ED_lanpr_compute_feature_lines_internal(dg, 0);
  }

  ED_lanpr_chain_clear_picked_flag(lanpr_share.render_buffer_shared);

  lanpr_update_gp_strokes_recursive(dg, scene->master_collection, frame, NULL, gpo);

  lanpr_update_gp_strokes_collection(dg, scene->master_collection, frame, 0, gpo);

  lanpr_clear_gp_lanpr_flags(dg, frame);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lanpr_update_gp_source_exec(struct bContext *C, struct wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph_pointer(C);
  Object *source_obj = CTX_data_active_object(C);

  int frame = scene->r.cfra;

  if (!lanpr_share.render_buffer_shared ||
      lanpr_share.render_buffer_shared->cached_for_frame != frame) {
    ED_lanpr_compute_feature_lines_internal(dg, 0);
  }

  ED_lanpr_chain_clear_picked_flag(lanpr_share.render_buffer_shared);

  lanpr_update_gp_strokes_recursive(dg, scene->master_collection, frame, source_obj, NULL);

  lanpr_update_gp_strokes_collection(dg, scene->master_collection, frame, 0, NULL);

  lanpr_clear_gp_lanpr_flags(dg, frame);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}

static bool lanpr_active_is_gpencil_object(bContext *C)
{
  Object *o = CTX_data_active_object(C);
  return o->type == OB_GPENCIL;
}
static bool lanpr_active_is_source_object(bContext *C)
{
  Object *o = CTX_data_active_object(C);
  if (o->type != OB_MESH) {
    return false;
  }
  else {
    ModifierData *md;
    for (md = o->modifiers.first; md; md = md->next) {
      if (md->type == eModifierType_FeatureLine) {
        return true;
      }
    }
  }
  return false;
}

void SCENE_OT_lanpr_update_gp_strokes(struct wmOperatorType *ot)
{
  ot->name = "Update LANPR Strokes";
  ot->description = "Update strokes for LANPR grease pencil targets";
  ot->idname = "SCENE_OT_lanpr_update_gp_strokes";

  ot->exec = lanpr_update_gp_strokes_exec;
}
void SCENE_OT_lanpr_bake_gp_strokes(struct wmOperatorType *ot)
{
  ot->name = "Bake LANPR Strokes";
  ot->description = "Bake strokes for LANPR grease pencil targets in all frames";
  ot->idname = "SCENE_OT_lanpr_bake_gp_strokes";

  ot->exec = lanpr_bake_gp_strokes_exec;
}
void OBJECT_OT_lanpr_update_gp_target(struct wmOperatorType *ot)
{
  ot->name = "Update Strokes";
  ot->description = "Update LANPR strokes for selected GPencil object";
  ot->idname = "OBJECT_OT_lanpr_update_gp_target";

  ot->poll = lanpr_active_is_gpencil_object;
  ot->exec = lanpr_update_gp_target_exec;
}
/* Not working due to lack of GP flags for the object */
void OBJECT_OT_lanpr_update_gp_source(struct wmOperatorType *ot)
{
  ot->name = "Update Strokes";
  ot->description = "Update LANPR strokes for selected Mesh object.";
  ot->idname = "OBJECT_OT_lanpr_update_gp_source";

  ot->poll = lanpr_active_is_source_object;
  ot->exec = lanpr_update_gp_source_exec;
}

/* Post-frame updater */

void ED_lanpr_post_frame_update_external(Scene *s, Depsgraph *dg)
{
  if ((s->lanpr.flag & LANPR_ENABLED) == 0 || !s->lanpr.auto_update) {
    return;
  }
  if (strcmp(s->r.engine, RE_engine_id_BLENDER_LANPR)) {
    /* Not LANPR engine, do GPencil updates. */
    /* LANPR engine will automatically update when drawing the viewport. */
    if (!lanpr_share.render_buffer_shared ||
        lanpr_share.render_buffer_shared->cached_for_frame != s->r.cfra) {
      ED_lanpr_compute_feature_lines_internal(dg, 0);
      lanpr_update_gp_strokes_actual(s, dg);
    }
  }
}
