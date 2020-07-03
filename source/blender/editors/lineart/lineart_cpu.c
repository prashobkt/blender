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

#include "ED_lineart.h"

#include "BLI_alloca.h"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math_matrix.h"
#include "BLI_task.h"
#include "BLI_utildefines.h"

#include "BKE_camera.h"
#include "BKE_collection.h"
#include "BKE_context.h"
#include "BKE_customdata.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_object.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.h"
#include "BKE_text.h"
#include "DEG_depsgraph_query.h"
#include "DNA_camera_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_lineart_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_scene_types.h"
#include "DNA_text_types.h"
#include "MEM_guardedalloc.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "BLI_math.h"
#include "BLI_string_utils.h"

#include "bmesh.h"
#include "bmesh_class.h"
#include "bmesh_tools.h"

#include "WM_api.h"
#include "WM_types.h"

#include "BKE_text.h"

#include "lineart_intern.h"

LineartSharedResource lineart_share;

/* Own functions */

static LineartBoundingArea *lineart_get_first_possible_bounding_area(LineartRenderBuffer *rb,
                                                                     LineartRenderLine *rl);

static void lineart_link_line_with_bounding_area(LineartRenderBuffer *rb,
                                                 LineartBoundingArea *root_ba,
                                                 LineartRenderLine *rl);

static LineartBoundingArea *lineart_get_next_bounding_area(LineartBoundingArea *This,
                                                           LineartRenderLine *rl,
                                                           double x,
                                                           double y,
                                                           double k,
                                                           int positive_x,
                                                           int positive_y,
                                                           double *next_x,
                                                           double *next_y);
static int lineart_triangle_line_imagespace_intersection_v2(SpinLock *spl,
                                                            const LineartRenderTriangle *rt,
                                                            const LineartRenderLine *rl,
                                                            const double *override_camera_loc,
                                                            const char override_cam_is_persp,
                                                            const double vp[4][4],
                                                            const double *camera_dir,
                                                            const double cam_shift_x,
                                                            const double cam_shift_y,
                                                            double *from,
                                                            double *to);
static int lineart_get_line_bounding_areas(LineartRenderBuffer *rb,
                                           LineartRenderLine *rl,
                                           int *rowbegin,
                                           int *rowend,
                                           int *colbegin,
                                           int *colend);

/* Layer operations */

static void lineart_line_layer_unique_name(ListBase *list,
                                           LineartLineLayer *ll,
                                           const char *defname)
{
  BLI_uniquename(list, ll, defname, '.', offsetof(LineartLineLayer, name), sizeof(ll->name));
}

int ED_lineart_max_occlusion_in_line_layers(SceneLineart *lineart)
{
  LineartLineLayer *lli;
  int max_occ = -1, max;
  for (lli = lineart->line_layers.first; lli; lli = lli->next) {
    if (lli->flags & LRT_LINE_LAYER_USE_MULTIPLE_LEVELS) {
      max = MAX2(lli->level_start, lli->level_end);
    }
    else {
      max = lli->level_start;
    }
    max_occ = MAX2(max, max_occ);
  }
  return max_occ;
}
LineartLineLayer *ED_lineart_new_line_layer(SceneLineart *lineart)
{
  LineartLineLayer *ll = MEM_callocN(sizeof(LineartLineLayer), "Line Layer");

  lineart_line_layer_unique_name(&lineart->line_layers, ll, "Layer");

  int max_occ = ED_lineart_max_occlusion_in_line_layers(lineart);

  ll->level_start = ll->level_end = max_occ + 1;
  ll->flags |= LRT_LINE_LAYER_USE_SAME_STYLE;
  ll->thickness = 1.0f;
  copy_v3_fl(ll->color, 0.8);
  ll->color[3] = 1.0f;
  ll->contour.use = 1;
  ll->crease.use = 1;
  ll->material_separate.use = 1;
  ll->edge_mark.use = 1;
  ll->intersection.use = 1;

  ll->normal_thickness_start = 0.2f;
  ll->normal_thickness_end = 1.5f;
  ll->normal_ramp_begin = 0.0f;
  ll->normal_ramp_end = 1.0f;

  ll->normal_mode = LRT_NORMAL_DIRECTIONAL;

  lineart->active_layer = ll;
  BLI_addtail(&lineart->line_layers, ll);

  return ll;
}
static int lineart_add_line_layer_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  SceneLineart *lineart = &scene->lineart;

  ED_lineart_new_line_layer(lineart);

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);

  return OPERATOR_FINISHED;
}
static int lineart_delete_line_layer_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  SceneLineart *lineart = &scene->lineart;

  LineartLineLayer *ll = lineart->active_layer;

  if (ll == NULL) {
    return OPERATOR_FINISHED;
  }

  if (ll->prev) {
    lineart->active_layer = ll->prev;
  }
  else if (ll->next) {
    lineart->active_layer = ll->next;
  }
  else {
    lineart->active_layer = 0;
  }

  BLI_remlink(&scene->lineart.line_layers, ll);

  MEM_freeN(ll);

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);

  return OPERATOR_FINISHED;
}
static int lineart_move_line_layer_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  SceneLineart *lineart = &scene->lineart;

  LineartLineLayer *ll = lineart->active_layer;

  if (ll == NULL) {
    return OPERATOR_FINISHED;
  }

  int dir = RNA_enum_get(op->ptr, "direction");

  if (dir == 1 && ll->prev) {
    BLI_remlink(&lineart->line_layers, ll);
    BLI_insertlinkbefore(&lineart->line_layers, ll->prev, ll);
  }
  else if (dir == -1 && ll->next) {
    BLI_remlink(&lineart->line_layers, ll);
    BLI_insertlinkafter(&lineart->line_layers, ll->next, ll);
  }

  DEG_id_tag_update(&scene->id, ID_RECALC_COPY_ON_WRITE);

  return OPERATOR_FINISHED;
}

static int lineart_enable_all_line_types_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  SceneLineart *lineart = &scene->lineart;
  LineartLineLayer *ll;

  if (!(ll = lineart->active_layer)) {
    return OPERATOR_FINISHED;
  }

  ll->contour.use = 1;
  ll->crease.use = 1;
  ll->edge_mark.use = 1;
  ll->material_separate.use = 1;
  ll->intersection.use = 1;

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
static int lineart_auto_create_line_layer_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  SceneLineart *lineart = &scene->lineart;

  LineartLineLayer *ll;

  ll = ED_lineart_new_line_layer(lineart);
  ll->thickness = 1.7;

  lineart_enable_all_line_types_exec(C, op);

  ll = ED_lineart_new_line_layer(lineart);
  ll->thickness = 0.9;
  copy_v3_fl(ll->color, 0.6);

  lineart_enable_all_line_types_exec(C, op);

  ll = ED_lineart_new_line_layer(lineart);
  ll->thickness = 0.7;
  copy_v3_fl(ll->color, 0.5);

  lineart_enable_all_line_types_exec(C, op);

  return OPERATOR_FINISHED;
}

void SCENE_OT_lineart_add_line_layer(wmOperatorType *ot)
{

  ot->name = "Add Line Layer";
  ot->description = "Add a new line layer";
  ot->idname = "SCENE_OT_lineart_add_line_layer";

  ot->exec = lineart_add_line_layer_exec;
}
void SCENE_OT_lineart_delete_line_layer(wmOperatorType *ot)
{

  ot->name = "Delete Line Layer";
  ot->description = "Delete selected line layer";
  ot->idname = "SCENE_OT_lineart_delete_line_layer";

  ot->exec = lineart_delete_line_layer_exec;
}
void SCENE_OT_lineart_auto_create_line_layer(wmOperatorType *ot)
{

  ot->name = "Auto Create Line Layer";
  ot->description = "Automatically create defalt line layer config";
  ot->idname = "SCENE_OT_lineart_auto_create_line_layer";

  ot->exec = lineart_auto_create_line_layer_exec;
}
void SCENE_OT_lineart_move_line_layer(wmOperatorType *ot)
{
  static const EnumPropertyItem line_layer_move[] = {
      {1, "UP", 0, "Up", ""}, {-1, "DOWN", 0, "Down", ""}, {0, NULL, 0, NULL, NULL}};

  ot->name = "Move Line Layer";
  ot->description = "Move LRT line layer up and down";
  ot->idname = "SCENE_OT_lineart_move_line_layer";

  /*  this need property to assign up/down direction */

  ot->exec = lineart_move_line_layer_exec;

  RNA_def_enum(ot->srna,
               "direction",
               line_layer_move,
               0,
               "Direction",
               "Direction to move the active line layer towards");
}
void SCENE_OT_lineart_enable_all_line_types(wmOperatorType *ot)
{
  ot->name = "Enable All Line Types";
  ot->description = "Enable All Line Types In This Line Layer";
  ot->idname = "SCENE_OT_lineart_enable_all_line_types";

  ot->exec = lineart_enable_all_line_types_exec;
}

/* Geometry */

int use_smooth_contour_modifier_contour = 0; /*  debug purpose */

static void lineart_cut_render_line(LineartRenderBuffer *rb,
                                    LineartRenderLine *rl,
                                    double begin,
                                    double end)
{
  LineartRenderLineSegment *rls = rl->segments.first, *irls;
  LineartRenderLineSegment *begin_segment = 0, *end_segment = 0;
  LineartRenderLineSegment *ns = 0, *ns2 = 0;
  int untouched = 0;

  if (TNS_DOUBLE_CLOSE_ENOUGH(begin, end)) {
    return;
  }

  if (begin != begin) {
    begin = 0;
  }
  if (end != end) {
    end = 0;
  }

  if (begin > end) {
    double t = begin;
    begin = end;
    end = t;
  }

  for (rls = rl->segments.first; rls; rls = rls->next) {
    if (TNS_DOUBLE_CLOSE_ENOUGH(rls->at, begin)) {
      begin_segment = rls;
      ns = begin_segment;
      break;
    }
    if (rls->next == NULL) {
      break;
    }
    irls = rls->next;
    if (irls->at > begin + 1e-09 && begin > rls->at) {
      begin_segment = irls;
      ns = mem_static_aquire_thread(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
      break;
    }
  }
  if (!begin_segment && TNS_DOUBLE_CLOSE_ENOUGH(1, end)) {
    untouched = 1;
  }
  for (rls = begin_segment; rls; rls = rls->next) {
    if (TNS_DOUBLE_CLOSE_ENOUGH(rls->at, end)) {
      end_segment = rls;
      ns2 = end_segment;
      break;
    }
    /*  irls = rls->next; */
    /*  added this to prevent rls->at == 1.0 (we don't need an end point for this) */
    if (!rls->next && TNS_DOUBLE_CLOSE_ENOUGH(1, end)) {
      end_segment = rls;
      ns2 = end_segment;
      untouched = 1;
      break;
    }
    else if (rls->at > end) {
      end_segment = rls;
      ns2 = mem_static_aquire_thread(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
      break;
    }
  }

  if (ns == NULL) {
    ns = mem_static_aquire_thread(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
  }
  if (ns2 == NULL) {
    if (untouched) {
      ns2 = ns;
      end_segment = ns2;
    }
    else
      ns2 = mem_static_aquire_thread(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
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

  ns->at = begin;
  if (!untouched) {
    ns2->at = end;
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
static int lineart_make_next_occlusion_task_info(LineartRenderBuffer *rb,
                                                 LineartRenderTaskInfo *rti)
{
  LinkData *data;
  int i;
  int res = 0;

  BLI_spin_lock(&rb->lock_task);

  if (rb->contour_managed) {
    data = rb->contour_managed;
    rti->contour = (void *)data;
    rti->contour_pointers.first = data;
    for (i = 0; i < LRT_THREAD_LINE_COUNT && data; i++) {
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
    for (i = 0; i < LRT_THREAD_LINE_COUNT && data; i++) {
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
    for (i = 0; i < LRT_THREAD_LINE_COUNT && data; i++) {
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
    for (i = 0; i < LRT_THREAD_LINE_COUNT && data; i++) {
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
    for (i = 0; i < LRT_THREAD_LINE_COUNT && data; i++) {
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

  BLI_spin_unlock(&rb->lock_task);

  return res;
}
static void lineart_calculate_single_line_occlusion(LineartRenderBuffer *rb,
                                                    LineartRenderLine *rl,
                                                    int thread_id)
{
  double x = rl->l->fbcoord[0], y = rl->l->fbcoord[1];
  LineartBoundingArea *ba = lineart_get_first_possible_bounding_area(rb, rl);
  LineartBoundingArea *nba = ba;
  LineartRenderTriangleThread *rt;
  LinkData *lip;
  double l, r;
  double k = (rl->r->fbcoord[1] - rl->l->fbcoord[1]) /
             (rl->r->fbcoord[0] - rl->l->fbcoord[0] + 1e-30);
  int positive_x = (rl->r->fbcoord[0] - rl->l->fbcoord[0]) > 0 ?
                       1 :
                       (rl->r->fbcoord[0] == rl->l->fbcoord[0] ? 0 : -1);
  int positive_y = (rl->r->fbcoord[1] - rl->l->fbcoord[1]) > 0 ?
                       1 :
                       (rl->r->fbcoord[1] == rl->l->fbcoord[1] ? 0 : -1);

  while (nba) {

    for (lip = nba->linked_triangles.first; lip; lip = lip->next) {
      rt = lip->data;
      if (rt->testing[thread_id] == rl || rl->l->intersecting_with == (void *)rt ||
          rl->r->intersecting_with == (void *)rt) {
        continue;
      }
      rt->testing[thread_id] = rl;
      if (lineart_triangle_line_imagespace_intersection_v2(&rb->lock_task,
                                                           (void *)rt,
                                                           rl,
                                                           rb->camera_pos,
                                                           rb->cam_is_persp,
                                                           rb->view_projection,
                                                           rb->view_vector,
                                                           rb->shift_x,
                                                           rb->shift_y,
                                                           &l,
                                                           &r)) {
        lineart_cut_render_line(rb, rl, l, r);
        if (rl->min_occ > rb->max_occlusion_level) {
          return; /* No need to caluclate any longer. */
        }
      }
    }

    nba = lineart_get_next_bounding_area(nba, rl, x, y, k, positive_x, positive_y, &x, &y);
  }
}

static bool lineart_calculation_is_canceled(void)
{
  bool is_canceled;
  BLI_spin_lock(&lineart_share.lock_render_status);
  switch (lineart_share.flag_render_status) {
    case LRT_RENDER_INCOMPELTE:
      is_canceled = true;
      break;
    default:
      is_canceled = false;
  }
  BLI_spin_unlock(&lineart_share.lock_render_status);
  return is_canceled;
}
static void lineart_calculate_line_occlusion_worker(TaskPool *__restrict UNUSED(pool),
                                                    LineartRenderTaskInfo *rti)
{
  LineartRenderBuffer *rb = lineart_share.render_buffer_shared;
  LinkData *lip;

  while (lineart_make_next_occlusion_task_info(rb, rti)) {

    for (lip = (void *)rti->contour; lip && lip->prev != rti->contour_pointers.last;
         lip = lip->next) {
      lineart_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    /* Monitoring cancelation flag every once a while. */
    if (lineart_calculation_is_canceled())
      return;

    for (lip = (void *)rti->crease; lip && lip->prev != rti->crease_pointers.last;
         lip = lip->next) {
      lineart_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    if (lineart_calculation_is_canceled())
      return;

    for (lip = (void *)rti->intersection; lip && lip->prev != rti->intersection_pointers.last;
         lip = lip->next) {
      lineart_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    if (lineart_calculation_is_canceled())
      return;

    for (lip = (void *)rti->material; lip && lip->prev != rti->material_pointers.last;
         lip = lip->next) {
      lineart_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    if (lineart_calculation_is_canceled())
      return;

    for (lip = (void *)rti->edge_mark; lip && lip->prev != rti->edge_mark_pointers.last;
         lip = lip->next) {
      lineart_calculate_single_line_occlusion(rb, lip->data, rti->thread_id);
    }

    if (lineart_calculation_is_canceled())
      return;
  }
}
static void lineart_calculate_line_occlusion_begin(LineartRenderBuffer *rb)
{
  int thread_count = rb->thread_count;
  LineartRenderTaskInfo *rti = MEM_callocN(sizeof(LineartRenderTaskInfo) * thread_count,
                                           "Task Pool");
  int i;

  rb->contour_managed = rb->contours.first;
  rb->crease_managed = rb->crease_lines.first;
  rb->intersection_managed = rb->intersection_lines.first;
  rb->material_managed = rb->material_lines.first;
  rb->edge_mark_managed = rb->edge_marks.first;

  TaskPool *tp = BLI_task_pool_create(NULL, TASK_PRIORITY_HIGH);

  for (i = 0; i < thread_count; i++) {
    rti[i].thread_id = i;
    BLI_task_pool_push(
        tp, (TaskRunFunction)lineart_calculate_line_occlusion_worker, &rti[i], 0, NULL);
  }
  BLI_task_pool_work_and_wait(tp);
  BLI_task_pool_free(tp);

  MEM_freeN(rti);
}

int ED_lineart_point_inside_triangled(double v[2], double v0[2], double v1[2], double v2[2])
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
static int lineart_point_on_lined(double v[2], double v0[2], double v1[2])
{
  double c1, c2;

  c1 = tmat_get_linear_ratio(v0[0], v1[0], v[0]);
  c2 = tmat_get_linear_ratio(v0[1], v1[1], v[1]);

  if (TNS_DOUBLE_CLOSE_ENOUGH(c1, c2) && c1 >= 0 && c1 <= 1) {
    return 1;
  }

  return 0;
}
static int lineart_point_triangle_relation(double v[2], double v0[2], double v1[2], double v2[2])
{
  double cl, c;
  double r;
  if (lineart_point_on_lined(v, v0, v1) || lineart_point_on_lined(v, v1, v2) ||
      lineart_point_on_lined(v, v2, v0)) {
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
static int lineart_point_inside_triangle3de(double v[3], double v0[3], double v1[3], double v2[3])
{
  double l[3], r[3];
  double N1[3], N2[3];
  double d;

  sub_v3_v3v3_db(l, v1, v0);
  sub_v3_v3v3_db(r, v, v1);
  cross_v3_v3v3_db(N1, l, r);

  sub_v3_v3v3_db(l, v2, v1);
  sub_v3_v3v3_db(r, v, v2);
  cross_v3_v3v3_db(N2, l, r);

  if ((d = dot_v3v3_db(N1, N2)) < 0) {
    return 0;
  }

  sub_v3_v3v3_db(l, v0, v2);
  sub_v3_v3v3_db(r, v, v0);
  cross_v3_v3v3_db(N1, l, r);

  if ((d = dot_v3v3_db(N1, N2)) < 0) {
    return 0;
  }

  sub_v3_v3v3_db(l, v1, v0);
  sub_v3_v3v3_db(r, v, v1);
  cross_v3_v3v3_db(N2, l, r);

  if ((d = dot_v3v3_db(N1, N2)) < 0) {
    return 0;
  }

  return 1;
}

static LineartRenderElementLinkNode *lineart_new_cull_triangle_space64(LineartRenderBuffer *rb)
{
  LineartRenderElementLinkNode *reln;

  LineartRenderTriangle *render_triangles = mem_static_aquire(
      &rb->render_data_pool,
      64 * rb->triangle_size); /*  CreateNewBuffer(LineartRenderTriangle, 64); */

  reln = list_append_pointer_static_sized(&rb->triangle_buffer_pointers,
                                          &rb->render_data_pool,
                                          render_triangles,
                                          sizeof(LineartRenderElementLinkNode));
  reln->element_count = 64;
  reln->additional = 1;

  return reln;
}
static LineartRenderElementLinkNode *lineart_new_cull_point_space64(LineartRenderBuffer *rb)
{
  LineartRenderElementLinkNode *reln;

  LineartRenderVert *render_vertices = mem_static_aquire(&rb->render_data_pool,
                                                         sizeof(LineartRenderVert) * 64);

  reln = list_append_pointer_static_sized(&rb->vertex_buffer_pointers,
                                          &rb->render_data_pool,
                                          render_vertices,
                                          sizeof(LineartRenderElementLinkNode));
  reln->element_count = 64;
  reln->additional = 1;

  return reln;
}
static void lineart_assign_render_line_with_triangle(LineartRenderTriangle *rt)
{
  if (rt->rl[0]->tl == NULL) {
    rt->rl[0]->tl = rt;
  }
  else if (rt->rl[0]->tr == NULL) {
    rt->rl[0]->tr = rt;
  }

  if (rt->rl[1]->tl == NULL) {
    rt->rl[1]->tl = rt;
  }
  else if (rt->rl[1]->tr == NULL) {
    rt->rl[1]->tr = rt;
  }

  if (rt->rl[2]->tl == NULL) {
    rt->rl[2]->tl = rt;
  }
  else if (rt->rl[2]->tr == NULL) {
    rt->rl[2]->tr = rt;
  }
}
static void lineart_post_triangle(LineartRenderTriangle *rt, LineartRenderTriangle *orig)
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

#define RT_AT(head, rb, offset) ((unsigned char *)head + offset * rb->triangle_size)

/** This function cuts triangles that are (partially or fully) behind near clipping plane.
 * for triangles that crossing the near plane, it will generate new 1 or 2 triangles with
 * new topology that represents the trimmed triangle. (which then became a triangle or square)
 */
static void lineart_cull_triangles(LineartRenderBuffer *rb)
{
  LineartRenderLine *rl;
  LineartRenderTriangle *rt, *rt1, *rt2;
  LineartRenderVert *rv;
  LineartRenderElementLinkNode *reln, *veln, *teln;
  LineartRenderLineSegment *rls;
  double(*vp)[4] = rb->view_projection;
  int i;
  double a;
  int v_count = 0, t_count = 0;
  Object *o;

  double view_dir[3], clip_advance[3];
  copy_v3_v3_db(view_dir, rb->view_vector);
  copy_v3_v3_db(clip_advance, rb->view_vector);

  double cam_pos[3];
  double clip_start;
  copy_v3_v3_db(cam_pos, rb->camera_pos);
  clip_start = rb->near_clip;
  mul_v3db_db(clip_advance, -clip_start);
  add_v3_v3_db(cam_pos, clip_advance);

  veln = lineart_new_cull_point_space64(rb);
  teln = lineart_new_cull_triangle_space64(rb);
  rv = &((LineartRenderVert *)veln->pointer)[v_count];
  rt1 = (void *)(((unsigned char *)teln->pointer) + rb->triangle_size * t_count);

  for (reln = rb->triangle_buffer_pointers.first; reln; reln = reln->next) {
    if (reln->additional) {
      continue;
    }
    o = reln->object_ref;
    for (i = 0; i < reln->element_count; i++) {

      /* These three represents points that are in the clipping range or not*/
      int in0 = 0, in1 = 0, in2 = 0;

      /* Select the triangle in the array. */
      rt = (void *)(((unsigned char *)reln->pointer) + rb->triangle_size * i);

      /* Point inside near plane */
      if (rt->v[0]->fbcoord[2] < 0) {
        in0 = 1;
      }
      if (rt->v[1]->fbcoord[2] < 0) {
        in1 = 1;
      }
      if (rt->v[2]->fbcoord[2] < 0) {
        in2 = 1;
      }

      /* Additional memory space for storing generated points and triangles */
      if (v_count > 60) {
        veln->element_count = v_count;
        veln = lineart_new_cull_point_space64(rb);
        v_count = 0;
      }
      if (t_count > 60) {
        teln->element_count = t_count;
        teln = lineart_new_cull_triangle_space64(rb);
        t_count = 0;
      }

      rv = &((LineartRenderVert *)veln->pointer)[v_count];
      rt1 = (void *)(((unsigned char *)teln->pointer) + rb->triangle_size * t_count);
      rt2 = (void *)(((unsigned char *)teln->pointer) + rb->triangle_size * (t_count + 1));

      double vv1[3], vv2[3], dot1, dot2;

      switch (in0 + in1 + in2) {
        case 0: /* ignore this triangle. */
          continue;
        case 3:
          /** triangle completely behind near plane, throw it away
           * also remove render lines form being computed.
           */
          rt->cull_status = LRT_CULL_DISCARD;
          BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
          rt->rl[0]->next = rt->rl[0]->prev = 0;
          BLI_remlink(&rb->all_render_lines, (void *)rt->rl[1]);
          rt->rl[1]->next = rt->rl[1]->prev = 0;
          BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
          rt->rl[2]->next = rt->rl[2]->prev = 0;
          continue;
        case 2:
          /** Two points behind near plane, cut those and
           * generate 2 new points, 3 lines and 1 triangle */
          rt->cull_status = LRT_CULL_USED;

          /** (!in0) means "when point 0 is visible".
           * conditons for point 1, 2 are the same idea.
           * 1-----|-------0
           * |     |   ---
           * |     |---
           * |  ---|
           * 2--   |
           *     (near)---------->(far)
           * Will become:
           *       |N******0
           *       |*  ***
           *       |N**
           *       |
           *       |
           *     (near)---------->(far)
           */
          if (!in0) {

            /* cut point for line 2---|-----0 */
            sub_v3_v3v3_db(vv1, rt->v[0]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[2]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            /* assign it to a new point */
            interp_v3_v3v3_db(rv[0].gloc, rt->v[0]->gloc, rt->v[2]->gloc, a);
            mul_v4_m4v3_db(rv[0].fbcoord, vp, rv[0].gloc);

            /* cut point for line 1---|-----0 */
            sub_v3_v3v3_db(vv1, rt->v[0]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[1]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot1 / (dot1 + dot2);
            /* assign it to another new point */
            interp_v3_v3v3_db(rv[1].gloc, rt->v[0]->gloc, rt->v[1]->gloc, a);
            mul_v4_m4v3_db(rv[1].fbcoord, vp, rv[1].gloc);

            /* remove all original render lines */
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
            rt->rl[0]->next = rt->rl[0]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[1]);
            rt->rl[1]->next = rt->rl[1]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
            rt->rl[2]->next = rt->rl[2]->prev = 0;

            /* New line connecting two new points */
            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            /** note: inverting rl->l/r (left/right point) doesn't matter as long as
             * rt->rl and rt->v has the same sequence. and the winding direction
             * can be either CW or CCW but needs to be consistent throughout the calculation.
             */
            rl->l = &rv[1];
            rl->r = &rv[0];
            /* only one adjacent triangle, because the other side is the near plane */
            /* use tl or tr doesn't matter. */
            rl->tl = rt1;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            /* new line connecting original point 0 and a new point */
            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = rt->v[0];
            /* restore adjacent triangle data. */
            rl->tl = rt->rl[0]->tl == rt ? rt1 : rt->rl[0]->tl;
            rl->tr = rt->rl[0]->tr == rt ? rt1 : rt->rl[0]->tr;
            rt1->rl[0] = rl;
            rl->object_ref = o;

            /* new line connecting original point 0 and another new point */
            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[0];
            rl->r = &rv[0];
            /* restore adjacent triangle data. */
            rl->tl = rt->rl[2]->tl == rt ? rt1 : rt->rl[2]->tl;
            rl->tr = rt->rl[2]->tr == rt ? rt1 : rt->rl[2]->tr;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            /* re-assign triangle point array to two new points. */
            rt1->v[0] = rt->v[0];
            rt1->v[1] = &rv[1];
            rt1->v[2] = &rv[0];

            lineart_post_triangle(rt1, rt);

            v_count += 2;
            t_count += 1;
            continue;
          }
          else if (!in2) {
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

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = &rv[1];
            rl->tl = rt1;
            rt1->rl[0] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = rt->v[2];
            rl->tl = rt->rl[1]->tl == rt ? rt1 : rt->rl[1]->tl;
            rl->tr = rt->rl[1]->tr == rt ? rt1 : rt->rl[1]->tr;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[2];
            rl->r = &rv[0];
            rl->tl = rt->rl[2]->tl == rt ? rt1 : rt->rl[2]->tl;
            rl->tr = rt->rl[2]->tr == rt ? rt1 : rt->rl[2]->tr;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rt1->v[0] = &rv[0];   /*&rv[1];*/
            rt1->v[1] = &rv[1];   /*rt->v[2];*/
            rt1->v[2] = rt->v[2]; /*&rv[0];*/

            lineart_post_triangle(rt1, rt);

            v_count += 2;
            t_count += 1;
            continue;
          }
          else if (!in1) {
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

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = rt->v[1];
            rl->tl = rt->rl[1]->tl == rt ? rt1 : rt->rl[1]->tl;
            rl->tr = rt->rl[1]->tr == rt ? rt1 : rt->rl[1]->tr;
            rt1->rl[0] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[1];
            rl->r = &rv[1];
            rl->tl = rt->rl[0]->tl == rt ? rt1 : rt->rl[0]->tl;
            rl->tr = rt->rl[0]->tr == rt ? rt1 : rt->rl[0]->tr;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rt1->v[0] = &rv[0];   /*rt->v[1];*/
            rt1->v[1] = rt->v[1]; /*&rv[1];*/
            rt1->v[2] = &rv[1];   /*&rv[0];*/

            lineart_post_triangle(rt1, rt);

            v_count += 2;
            t_count += 1;
            continue;
          }
          break;
        case 1:
          /** Two points behind near plane, cut those and
           * generate 2 new points, 4 lines and 2 triangles */
          rt->cull_status = LRT_CULL_USED;

          /** (in0) means "when point 0 is invisible".
           * conditons for point 1, 2 are the same idea.
           * 0------|----------1
           *   --   |          |
           *     ---|          |
           *        |--        |
           *        |  ---     |
           *        |     ---  |
           *        |        --2
           *      (near)---------->(far)
           * Will become:
           *        |N*********1
           *        |*     *** |
           *        |*  ***    |
           *        |N**       |
           *        |  ***     |
           *        |     ***  |
           *        |        **2
           *      (near)---------->(far)
           */
          if (in0) {
            /* Cut point for line 0---|------1 */
            sub_v3_v3v3_db(vv1, rt->v[1]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[0]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot2 / (dot1 + dot2);
            /* Assign to a new point */
            interp_v3_v3v3_db(rv[0].gloc, rt->v[0]->gloc, rt->v[1]->gloc, a);
            mul_v4_m4v3_db(rv[0].fbcoord, vp, rv[0].gloc);

            /* Cut point for line 0---|------2 */
            sub_v3_v3v3_db(vv1, rt->v[2]->gloc, cam_pos);
            sub_v3_v3v3_db(vv2, cam_pos, rt->v[0]->gloc);
            dot1 = dot_v3v3_db(vv1, view_dir);
            dot2 = dot_v3v3_db(vv2, view_dir);
            a = dot2 / (dot1 + dot2);
            /* Assign to aother new point */
            interp_v3_v3v3_db(rv[1].gloc, rt->v[0]->gloc, rt->v[2]->gloc, a);
            mul_v4_m4v3_db(rv[1].fbcoord, vp, rv[1].gloc);

            /* Remove two cutted lines, the visible line is untouched. */
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[0]);
            rt->rl[0]->next = rt->rl[0]->prev = 0;
            BLI_remlink(&rb->all_render_lines, (void *)rt->rl[2]);
            rt->rl[2]->next = rt->rl[2]->prev = 0;

            /* New line connects two new points */
            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            /** New line connects new point 0 and old point 1,
             * this is a border line.
             */
            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = rt->v[1];
            rl->tl = rt1;
            rl->tr = rt->rl[0]->tr == rt ? rt->rl[0]->tl : rt->rl[0]->tr;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            /** New line connects new point 1 and old point 1,
             * this is a inner line separating newly generated triangles.
             */
            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[1];
            rl->r = &rv[1];
            rl->tl = rt1;
            rl->tr = rt2;
            rt1->rl[0] = rl;
            rt2->rl[0] = rl;
            rl->object_ref = o;

            /* We now have one triangle closed. */
            rt1->v[0] = rt->v[1];
            rt1->v[1] = &rv[1];
            rt1->v[2] = &rv[0];

            /** New line connects new point 1 and old point 2,
             * this is also a border line.
             */
            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = rt->v[2];
            rl->r = &rv[1];
            rl->tl = rt2;
            rl->tr = rt->rl[2]->tr == rt ? rt->rl[2]->tl : rt->rl[2]->tr;
            rt2->rl[2] = rl;
            rt2->rl[1] = rt->rl[1];
            rl->object_ref = o;

            /* Close the second triangle. */
            rt2->v[0] = &rv[1];
            rt2->v[1] = rt->v[1];
            rt2->v[2] = rt->v[2];

            lineart_post_triangle(rt1, rt);
            lineart_post_triangle(rt2, rt);

            v_count += 2;
            t_count += 2;
            continue;
          }
          else if (in1) {

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

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = rt->v[2];
            rl->tl = rt1;
            rl->tr = rt->rl[1]->tl == rt ? rt->rl[1]->tr : rt->rl[1]->tl;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
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

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
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

            lineart_post_triangle(rt1, rt);
            lineart_post_triangle(rt2, rt);

            v_count += 2;
            t_count += 2;
            continue;
          }
          else if (in2) {

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

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[1];
            rl->r = &rv[0];
            rl->tl = rt1;
            rt1->rl[1] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
            BLI_addtail(&rl->segments, rls);
            BLI_addtail(&rb->all_render_lines, rl);
            rl->l = &rv[0];
            rl->r = rt->v[0];
            rl->tl = rt1;
            rl->tr = rt->rl[2]->tl == rt ? rt->rl[2]->tr : rt->rl[2]->tl;
            rt1->rl[2] = rl;
            rl->object_ref = o;

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
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

            rl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
            rls = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLineSegment));
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

            lineart_post_triangle(rt1, rt);
            lineart_post_triangle(rt2, rt);

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
static void lineart_perspective_division(LineartRenderBuffer *rb)
{
  LineartRenderVert *rv;
  LineartRenderElementLinkNode *reln;
  int i;

  if (!rb->cam_is_persp) {
    return;
  }

  for (reln = rb->vertex_buffer_pointers.first; reln; reln = reln->next) {
    rv = reln->pointer;
    for (i = 0; i < reln->element_count; i++) {
      mul_v3db_db(rv[i].fbcoord, 1 / rv[i].fbcoord[3]);
      rv[i].fbcoord[0] -= rb->shift_x * 2;
      rv[i].fbcoord[1] -= rb->shift_y * 2;
    }
  }
}

static void lineart_transform_render_vert(
    BMVert *v, int index, LineartRenderVert *RvBuf, double (*mv_mat)[4], double (*mvp_mat)[4])
{
  double co[4];
  LineartRenderVert *rv = &RvBuf[index];
  copy_v3db_v3fl(co, v->co);
  mul_v3_m4v3_db(rv->gloc, mv_mat, co);
  mul_v4_m4v3_db(rv->fbcoord, mvp_mat, co);
}

static void lineart_make_render_geometry_buffers_object(Object *o,
                                                        double (*mv_mat)[4],
                                                        double (*mvp_mat)[4],
                                                        LineartRenderBuffer *rb,
                                                        int override_usage)
{
  BMesh *bm;
  BMVert *v;
  BMFace *f;
  BMEdge *e;
  BMLoop *loop;
  LineartRenderLine *rl;
  LineartRenderTriangle *rt;
  double new_mvp[4][4], new_mv[4][4], normal[4][4];
  float imat[4][4];
  LineartRenderElementLinkNode *reln;
  LineartRenderVert *orv;
  LineartRenderLine *orl;
  LineartRenderTriangle *ort;
  FreestyleEdge *fe;
  int CanFindFreestyle = 0;
  int i;

  int usage = override_usage ? override_usage : o->lineart.usage;

  if (usage == OBJECT_FEATURE_LINE_EXCLUDE) {
    return;
  }

  if (o->type == OB_MESH) {

    mul_m4db_m4db_m4fl_uniq(new_mvp, mvp_mat, o->obmat);
    mul_m4db_m4db_m4fl_uniq(new_mv, mv_mat, o->obmat);

    invert_m4_m4(imat, o->obmat);
    transpose_m4(imat);
    copy_m4d_m4(normal, imat);

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

    if (CustomData_has_layer(&bm->edata, CD_FREESTYLE_EDGE)) {
      CanFindFreestyle = 1;
    }

    orv = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderVert) * bm->totvert);
    ort = mem_static_aquire(&rb->render_data_pool, bm->totface * rb->triangle_size);
    orl = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine) * bm->totedge);

    reln = list_append_pointer_static_sized(&rb->vertex_buffer_pointers,
                                            &rb->render_data_pool,
                                            orv,
                                            sizeof(LineartRenderElementLinkNode));
    reln->element_count = bm->totvert;
    reln->object_ref = o;

    reln = list_append_pointer_static_sized(&rb->line_buffer_pointers,
                                            &rb->render_data_pool,
                                            orl,
                                            sizeof(LineartRenderElementLinkNode));
    reln->element_count = bm->totedge;
    reln->object_ref = o;

    reln = list_append_pointer_static_sized(&rb->triangle_buffer_pointers,
                                            &rb->render_data_pool,
                                            ort,
                                            sizeof(LineartRenderElementLinkNode));
    reln->element_count = bm->totface;
    reln->object_ref = o;

    for (i = 0; i < bm->totvert; i++) {
      v = BM_vert_at_index(bm, i);
      lineart_transform_render_vert(v, i, orv, new_mv, new_mvp);
    }

    rl = orl;
    for (i = 0; i < bm->totedge; i++) {
      e = BM_edge_at_index(bm, i);
      if (CanFindFreestyle) {
        fe = CustomData_bmesh_get(&bm->edata, e->head.data, CD_FREESTYLE_EDGE);
        if (fe->flag & FREESTYLE_EDGE_MARK) {
          rl->flags |= LRT_EDGE_FLAG_EDGE_MARK;
        }
      }
      if (use_smooth_contour_modifier_contour) {
        rl->edge_idx = i;
        if (BM_elem_flag_test(e->v1, BM_ELEM_SELECT) && BM_elem_flag_test(e->v2, BM_ELEM_SELECT)) {
          rl->flags |= LRT_EDGE_FLAG_CONTOUR;
        }
      }

      rl->l = &orv[BM_elem_index_get(e->v1)];
      rl->r = &orv[BM_elem_index_get(e->v2)];

      rl->object_ref = o;

      LineartRenderLineSegment *rls = mem_static_aquire(&rb->render_data_pool,
                                                        sizeof(LineartRenderLineSegment));
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
      mul_v3_mat3_m4v3_db(rt->gn, normal, gn);
      normalize_v3_d(rt->gn);
      lineart_assign_render_line_with_triangle(rt);

      rt = (LineartRenderTriangle *)(((unsigned char *)rt) + rb->triangle_size);
    }

    BM_mesh_free(bm);
  }
}

int ED_lineart_object_collection_usage_check(Collection *c, Object *o)
{
  CollectionChild *cc;
  int object_is_used = (o->lineart.usage == OBJECT_FEATURE_LINE_INCLUDE ||
                        o->lineart.usage == OBJECT_FEATURE_LINE_INHERENT);

  if (object_is_used && (c->lineart_usage != COLLECTION_LRT_INCLUDE)) {
    if (BKE_collection_has_object_recursive(c, (Object *)(o->id.orig_id))) {
      if (c->lineart_usage == COLLECTION_LRT_EXCLUDE) {
        return OBJECT_FEATURE_LINE_EXCLUDE;
      }
      else if (c->lineart_usage == COLLECTION_LRT_OCCLUSION_ONLY) {
        return OBJECT_FEATURE_LINE_OCCLUSION_ONLY;
      }
    }
  }

  if (c->children.first == NULL) {
    if (BKE_collection_has_object(c, o)) {
      if (o->lineart.usage == OBJECT_FEATURE_LINE_INHERENT) {
        if ((c->lineart_usage == COLLECTION_LRT_OCCLUSION_ONLY)) {
          return OBJECT_FEATURE_LINE_OCCLUSION_ONLY;
        }
        else if ((c->lineart_usage == COLLECTION_LRT_EXCLUDE)) {
          return OBJECT_FEATURE_LINE_EXCLUDE;
        }
        else {
          return OBJECT_FEATURE_LINE_INHERENT;
        }
      }
      else {
        return o->lineart.usage;
      }
    }
    else {
      return OBJECT_FEATURE_LINE_INHERENT;
    }
  }

  for (cc = c->children.first; cc; cc = cc->next) {
    int result = ED_lineart_object_collection_usage_check(cc->collection, o);
    if (result > OBJECT_FEATURE_LINE_INHERENT) {
      return result;
    }
  }

  return OBJECT_FEATURE_LINE_INHERENT;
}

static void lineart_make_render_geometry_buffers(
    Depsgraph *depsgraph,
    Scene *s,
    Object *c /* Still use camera arg for convenience */,
    LineartRenderBuffer *rb)
{
  double proj[4][4], view[4][4], result[4][4];
  float inv[4][4];

  /* lock becore accessing shared status data */
  BLI_spin_lock(&lineart_share.lock_render_status);

  memset(rb->material_pointers, 0, sizeof(void *) * 2048);

  if (lineart_share.viewport_camera_override) {
    copy_m4d_m4(proj, lineart_share.persp);
    invert_m4_m4(inv, lineart_share.viewinv);
    copy_m4_m4_db(rb->view_projection, proj);
  }
  else {
    Camera *cam = c->data;
    float sensor = BKE_camera_sensor_size(cam->sensor_fit, cam->sensor_x, cam->sensor_y);
    double fov = focallength_to_fov(cam->lens, sensor);

    double asp = ((double)rb->w / (double)rb->h);

    if (cam->type == CAM_PERSP) {
      tmat_make_perspective_matrix_44d(proj, fov, asp, cam->clip_start, cam->clip_end);
    }
    else if (cam->type == CAM_ORTHO) {
      double w = cam->ortho_scale / 2;
      tmat_make_ortho_matrix_44d(proj, -w, w, -w / asp, w / asp, cam->clip_start, cam->clip_end);
    }
    invert_m4_m4(inv, c->obmat);
    mul_m4db_m4db_m4fl_uniq(result, proj, inv);
    copy_m4_m4_db(proj, result);
    copy_m4_m4_db(rb->view_projection, proj);
  }
  BLI_spin_unlock(&lineart_share.lock_render_status);

  unit_m4_db(view);

  BLI_listbase_clear(&rb->triangle_buffer_pointers);
  BLI_listbase_clear(&rb->vertex_buffer_pointers);

  DEG_OBJECT_ITER_BEGIN (depsgraph,
                         o,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY | DEG_ITER_OBJECT_FLAG_VISIBLE |
                             DEG_ITER_OBJECT_FLAG_DUPLI | DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    int usage = ED_lineart_object_collection_usage_check(s->master_collection, o);

    lineart_make_render_geometry_buffers_object(o, view, proj, rb, usage);
  }
  DEG_OBJECT_ITER_END;
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

static LineartRenderLine *lineart_another_edge(const LineartRenderTriangle *rt,
                                               const LineartRenderVert *rv)
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
static int lineart_share_edge_direct(const LineartRenderTriangle *rt, const LineartRenderLine *rl)
{
  if (rt->rl[0] == rl || rt->rl[1] == rl || rt->rl[2] == rl) {
    return 1;
  }
  return 0;
}

/** This is the main function to calculate
 * the occlusion status between 1(one) triangle and 1(one) line.
 * if returned 1, then from/to will carry the occludded segments
 * in ratio from rl->l to rl->r. the line is later cutted with
 * these two values.
 */
static int lineart_triangle_line_imagespace_intersection_v2(SpinLock *UNUSED(spl),
                                                            const LineartRenderTriangle *rt,
                                                            const LineartRenderLine *rl,
                                                            const double *override_cam_loc,
                                                            const char override_cam_is_persp,
                                                            const double vp[4][4],
                                                            const double *camera_dir,
                                                            const double cam_shift_x,
                                                            const double cam_shift_y,
                                                            double *from,
                                                            double *to)
{
  double is[3] = {0};
  int order[3];
  int LCross = -1, RCross = -1;
  int a, b, c;
  int st_l = 0, st_r = 0;

  double Lv[3];
  double Rv[3];
  double vd4[4];
  double Cv[3];
  double dot_l, dot_r, dot_la, dot_ra;
  double dot_f;
  double gloc[4], trans[4];
  double cut = -1;

  double *LFBC = rl->l->fbcoord, *RFBC = rl->r->fbcoord, *FBC0 = rt->v[0]->fbcoord,
         *FBC1 = rt->v[1]->fbcoord, *FBC2 = rt->v[2]->fbcoord;

  /* No potential overlapping, return early. */
  if ((MAX3(FBC0[0], FBC1[0], FBC2[0]) < MIN2(LFBC[0], RFBC[0])) ||
      (MIN3(FBC0[0], FBC1[0], FBC2[0]) > MAX2(LFBC[0], RFBC[0])) ||
      (MAX3(FBC0[1], FBC1[1], FBC2[1]) < MIN2(LFBC[1], RFBC[1])) ||
      (MIN3(FBC0[1], FBC1[1], FBC2[1]) > MAX2(LFBC[1], RFBC[1]))) {
    return 0;
  }

  /* If the the line is one of the edge in the triangle, then it's not occludded. */
  if (lineart_share_edge_direct(rt, rl)) {
    return 0;
  }

  /* If the line visually crosses one of the edge in the triangle */
  a = lineart_LineIntersectTest2d(LFBC, RFBC, FBC0, FBC1, &is[0]);
  b = lineart_LineIntersectTest2d(LFBC, RFBC, FBC1, FBC2, &is[1]);
  c = lineart_LineIntersectTest2d(LFBC, RFBC, FBC2, FBC0, &is[2]);

  INTERSECT_SORT_MIN_TO_MAX_3(is[0], is[1], is[2], order);

  sub_v3_v3v3_db(Lv, rl->l->gloc, rt->v[0]->gloc);
  sub_v3_v3v3_db(Rv, rl->r->gloc, rt->v[0]->gloc);

  copy_v3_v3_db(Cv, camera_dir);

  if (override_cam_is_persp) {
    copy_v3_v3_db(vd4, override_cam_loc);
  }
  else {
    copy_v4_v4_db(vd4, override_cam_loc);
  }
  if (override_cam_is_persp) {
    sub_v3_v3v3_db(Cv, vd4, rt->v[0]->gloc);
  }

  dot_l = dot_v3v3_db(Lv, rt->gn);
  dot_r = dot_v3v3_db(Rv, rt->gn);
  dot_f = dot_v3v3_db(Cv, rt->gn);

  if (!dot_f) {
    return 0;
  }

  if (!a && !b && !c) {
    if (!(st_l = lineart_point_triangle_relation(LFBC, FBC0, FBC1, FBC2)) &&
        !(st_r = lineart_point_triangle_relation(RFBC, FBC0, FBC1, FBC2))) {
      return 0; /*  not occluding */
    }
  }

  st_l = lineart_point_triangle_relation(LFBC, FBC0, FBC1, FBC2);
  st_r = lineart_point_triangle_relation(RFBC, FBC0, FBC1, FBC2);

  dot_la = fabs(dot_l);
  if (dot_la < DBL_EPSILON) {
    dot_la = 0;
    dot_l = 0;
  }
  dot_ra = fabs(dot_r);
  if (dot_ra < DBL_EPSILON) {
    dot_ra = 0;
    dot_r = 0;
  }
  if (dot_l - dot_r == 0) {
    cut = 100000;
  }
  else if (dot_l * dot_r <= 0) {
    cut = dot_la / fabs(dot_l - dot_r);
  }
  else {
    cut = fabs(dot_r + dot_l) / fabs(dot_l - dot_r);
    cut = dot_ra > dot_la ? 1 - cut : cut;
  }

  if (override_cam_is_persp) {
    interp_v3_v3v3_db(gloc, rl->l->gloc, rl->r->gloc, cut);
    mul_v4_m4v3_db(trans, vp, gloc);
    mul_v3db_db(trans, (1 / trans[3]));
  }
  else {
    interp_v3_v3v3_db(trans, rl->l->fbcoord, rl->r->fbcoord, cut);
  }
  trans[0] -= cam_shift_x * 2;
  trans[1] -= cam_shift_y * 2;

  /* To accomodate k=0 and k=inf (vertical) lines. */
  if (fabs(rl->l->fbcoord[0] - rl->r->fbcoord[0]) > fabs(rl->l->fbcoord[1] - rl->r->fbcoord[1])) {
    cut = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], trans[0]);
  }
  else {
    cut = tmat_get_linear_ratio(rl->l->fbcoord[1], rl->r->fbcoord[1], trans[1]);
  }

  if (st_l == 2) {
    if (st_r == 2) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (st_r == 1) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (st_r == 0) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 0, RCross);
    }
  }
  else if (st_l == 1) {
    if (st_r == 2) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (st_r == 1) {
      INTERSECT_JUST_SMALLER(is, order, DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (st_r == 0) {
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
  else if (st_l == 0) {
    if (st_r == 2) {
      INTERSECT_JUST_SMALLER(is, order, 1 - DBL_TRIANGLE_LIM, LCross);
      INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
    }
    else if (st_r == 1) {
      INTERSECT_JUST_SMALLER(is, order, 1 - DBL_TRIANGLE_LIM, LCross);
      if (TNS_ABC(LCross) && is[LCross] < (1 - DBL_TRIANGLE_LIM)) {
        INTERSECT_JUST_GREATER(is, order, 1 - DBL_TRIANGLE_LIM, RCross);
      }
      else {
        INTERSECT_JUST_SMALLER(is, order, 1 + DBL_TRIANGLE_LIM, LCross);
        INTERSECT_JUST_GREATER(is, order, 1 + DBL_TRIANGLE_LIM, RCross);
      }
    }
    else if (st_r == 0) {
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

  double LF = dot_l * dot_f, RF = dot_r * dot_f;

  if (LF <= 0 && RF <= 0 && (dot_l || dot_r)) {

    *from = MAX2(0, is[LCross]);
    *to = MIN2(1, is[RCross]);
    if (*from >= *to) {
      return 0;
    }
    /*  printf("1 From %f to %f\n",*From, *To); */
    return 1;
  }
  else if (LF >= 0 && RF <= 0 && (dot_l || dot_r)) {
    *from = MAX2(cut, is[LCross]);
    *to = MIN2(1, is[RCross]);
    if (*from >= *to) {
      return 0;
    }
    /*  printf("2 From %f to %f\n",*From, *To); */
    return 1;
  }
  else if (LF <= 0 && RF >= 0 && (dot_l || dot_r)) {
    *from = MAX2(0, is[LCross]);
    *to = MIN2(cut, is[RCross]);
    if (*from >= *to) {
      return 0;
    }
    /*  printf("3 From %f to %f\n",*From, *To); */
    return 1;
  }
  else
    return 0;
  return 1;
}

static LineartRenderLine *lineart_triangle_share_edge(const LineartRenderTriangle *l,
                                                      const LineartRenderTriangle *r)
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
static LineartRenderVert *lineart_triangle_share_point(const LineartRenderTriangle *l,
                                                       const LineartRenderTriangle *r)
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

static LineartRenderVert *lineart_triangle_line_intersection_test(LineartRenderBuffer *rb,
                                                                  LineartRenderLine *rl,
                                                                  LineartRenderTriangle *rt,
                                                                  LineartRenderTriangle *testing,
                                                                  LineartRenderVert *last)
{
  double Lv[3];
  double Rv[3];
  double dot_l, dot_r;
  LineartRenderVert *result, *rv;
  double gloc[3];
  LineartRenderVert *l = rl->l, *r = rl->r;

  for (rv = testing->intersecting_verts.first; rv; rv = rv->next) {
    if (rv->intersecting_with == rt && rv->intersecting_line == rl) {
      return rv;
    }
  }

  sub_v3_v3v3_db(Lv, l->gloc, testing->v[0]->gloc);
  sub_v3_v3v3_db(Rv, r->gloc, testing->v[0]->gloc);

  dot_l = dot_v3v3_db(Lv, testing->gn);
  dot_r = dot_v3v3_db(Rv, testing->gn);

  if (dot_l * dot_r > 0 || (!dot_l && !dot_r)) {
    return 0;
  }

  dot_l = fabs(dot_l);
  dot_r = fabs(dot_r);

  interp_v3_v3v3_db(gloc, l->gloc, r->gloc, dot_l / (dot_l + dot_r));

  if (last && TNS_DOUBLE_CLOSE_ENOUGH(last->gloc[0], gloc[0]) &&
      TNS_DOUBLE_CLOSE_ENOUGH(last->gloc[1], gloc[1]) &&
      TNS_DOUBLE_CLOSE_ENOUGH(last->gloc[2], gloc[2])) {

    last->intersecting_line2 = rl;
    return NULL;
  }

  if (!(lineart_point_inside_triangle3de(
          gloc, testing->v[0]->gloc, testing->v[1]->gloc, testing->v[2]->gloc))) {
    return NULL;
  }

  result = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderVert));

  result->edge_used = 1;

  /** Caution! BMVert* result->v is reused to save a intersecting render vert.
   * this saves memory when the scene is very large.
   */
  result->v = (void *)r;

  copy_v3_v3_db(result->gloc, gloc);

  BLI_addtail(&testing->intersecting_verts, result);

  return result;
}
static LineartRenderLine *lineart_triangle_generate_intersection_line_only(
    LineartRenderBuffer *rb, LineartRenderTriangle *rt, LineartRenderTriangle *testing)
{
  LineartRenderVert *l = 0, *r = 0;
  LineartRenderVert **next = &l;
  LineartRenderLine *result;
  LineartRenderVert *E0T = 0;
  LineartRenderVert *E1T = 0;
  LineartRenderVert *E2T = 0;
  LineartRenderVert *TE0 = 0;
  LineartRenderVert *TE1 = 0;
  LineartRenderVert *TE2 = 0;
  double cl[3];

  double ZMin, ZMax;
  ZMax = rb->far_clip;
  ZMin = rb->near_clip;
  copy_v3_v3_db(cl, rb->camera_pos);
  LineartRenderVert *share = lineart_triangle_share_point(testing, rt);

  if (share) {
    LineartRenderVert *new_share;
    LineartRenderLine *rl = lineart_another_edge(rt, share);

    l = new_share = mem_static_aquire(&rb->render_data_pool, (sizeof(LineartRenderVert)));

    new_share->edge_used = 1;
    new_share->v = (void *)
        r; /*  Caution!  BMVert* result->v is reused to save a intersecting render vert. */
    copy_v3_v3_db(new_share->gloc, share->gloc);

    r = lineart_triangle_line_intersection_test(rb, rl, rt, testing, 0);

    if (r == NULL) {
      rl = lineart_another_edge(testing, share);
      r = lineart_triangle_line_intersection_test(rb, rl, testing, rt, 0);
      if (r == NULL) {
        return 0;
      }
      BLI_addtail(&testing->intersecting_verts, new_share);
    }
    else {
      BLI_addtail(&rt->intersecting_verts, new_share);
    }
  }
  else {
    if (UNLIKELY(!rt->rl[0] || !rt->rl[1] || !rt->rl[2])) {
      /** If we enter here, then there must be problems in culling,
       * extremely rare condition where floating point precision can't handle.
       */
      return 0;
    }
    E0T = lineart_triangle_line_intersection_test(rb, rt->rl[0], rt, testing, 0);
    if (E0T && (!(*next))) {
      (*next) = E0T;
      (*next)->intersecting_line = rt->rl[0];
      next = &r;
    }
    E1T = lineart_triangle_line_intersection_test(rb, rt->rl[1], rt, testing, l);
    if (E1T && (!(*next))) {
      (*next) = E1T;
      (*next)->intersecting_line = rt->rl[1];
      next = &r;
    }
    if (!(*next)) {
      E2T = lineart_triangle_line_intersection_test(rb, rt->rl[2], rt, testing, l);
    }
    if (E2T && (!(*next))) {
      (*next) = E2T;
      (*next)->intersecting_line = rt->rl[2];
      next = &r;
    }

    if (!(*next)) {
      TE0 = lineart_triangle_line_intersection_test(rb, testing->rl[0], testing, rt, l);
    }
    if (TE0 && (!(*next))) {
      (*next) = TE0;
      (*next)->intersecting_line = testing->rl[0];
      next = &r;
    }
    if (!(*next)) {
      TE1 = lineart_triangle_line_intersection_test(rb, testing->rl[1], testing, rt, l);
    }
    if (TE1 && (!(*next))) {
      (*next) = TE1;
      (*next)->intersecting_line = testing->rl[1];
      next = &r;
    }
    if (!(*next)) {
      TE2 = lineart_triangle_line_intersection_test(rb, testing->rl[2], testing, rt, l);
    }
    if (TE2 && (!(*next))) {
      (*next) = TE2;
      (*next)->intersecting_line = testing->rl[2];
      next = &r;
    }

    if (!(*next)) {
      return 0;
    }
  }
  mul_v4_m4v3_db(l->fbcoord, rb->view_projection, l->gloc);
  mul_v4_m4v3_db(r->fbcoord, rb->view_projection, r->gloc);
  mul_v3db_db(l->fbcoord, (1 / l->fbcoord[3]));
  mul_v3db_db(r->fbcoord, (1 / r->fbcoord[3]));

  l->fbcoord[0] -= rb->shift_x * 2;
  l->fbcoord[1] -= rb->shift_y * 2;
  r->fbcoord[0] -= rb->shift_x * 2;
  r->fbcoord[1] -= rb->shift_y * 2;

  l->fbcoord[2] = ZMin * ZMax / (ZMax - fabs(l->fbcoord[2]) * (ZMax - ZMin));
  r->fbcoord[2] = ZMin * ZMax / (ZMax - fabs(r->fbcoord[2]) * (ZMax - ZMin));

  l->intersecting_with = rt;
  r->intersecting_with = testing;

  result = mem_static_aquire(&rb->render_data_pool, sizeof(LineartRenderLine));
  result->l = l;
  result->r = r;
  result->tl = rt;
  result->tr = testing;
  LineartRenderLineSegment *rls = mem_static_aquire(&rb->render_data_pool,
                                                    sizeof(LineartRenderLineSegment));
  BLI_addtail(&result->segments, rls);
  BLI_addtail(&rb->all_render_lines, result);
  result->flags |= LRT_EDGE_FLAG_INTERSECTION;
  list_append_pointer_static(&rb->intersection_lines, &rb->render_data_pool, result);
  int r1, r2, c1, c2, row, col;
  if (lineart_get_line_bounding_areas(rb, result, &r1, &r2, &c1, &c2)) {
    for (row = r1; row != r2 + 1; row++) {
      for (col = c1; col != c2 + 1; col++) {
        lineart_link_line_with_bounding_area(
            rb, &rb->initial_bounding_areas[row * 4 + col], result);
      }
    }
  }

  rb->intersection_count++;

  return result;
}
static void lineart_triangle_calculate_intersections_in_bounding_area(LineartRenderBuffer *rb,
                                                                      LineartRenderTriangle *rt,
                                                                      LineartBoundingArea *ba)
{
  LineartRenderTriangle *testing_triangle;
  LinkData *lip, *next_lip;

  double *FBC0 = rt->v[0]->fbcoord, *FBC1 = rt->v[1]->fbcoord, *FBC2 = rt->v[2]->fbcoord;

  if (ba->child) {
    lineart_triangle_calculate_intersections_in_bounding_area(rb, rt, &ba->child[0]);
    lineart_triangle_calculate_intersections_in_bounding_area(rb, rt, &ba->child[1]);
    lineart_triangle_calculate_intersections_in_bounding_area(rb, rt, &ba->child[2]);
    lineart_triangle_calculate_intersections_in_bounding_area(rb, rt, &ba->child[3]);
    return;
  }

  for (lip = ba->linked_triangles.first; lip; lip = next_lip) {
    next_lip = lip->next;
    testing_triangle = lip->data;
    if (testing_triangle == rt || testing_triangle->testing == rt ||
        lineart_triangle_share_edge(rt, testing_triangle)) {

      continue;
    }

    testing_triangle->testing = rt;
    double *RFBC0 = testing_triangle->v[0]->fbcoord, *RFBC1 = testing_triangle->v[1]->fbcoord,
           *RFBC2 = testing_triangle->v[2]->fbcoord;

    if ((MIN3(FBC0[2], FBC1[2], FBC2[2]) > MAX3(RFBC0[2], RFBC1[2], RFBC2[2])) ||
        (MAX3(FBC0[2], FBC1[2], FBC2[2]) < MIN3(RFBC0[2], RFBC1[2], RFBC2[2])) ||
        (MIN3(FBC0[0], FBC1[0], FBC2[0]) > MAX3(RFBC0[0], RFBC1[0], RFBC2[0])) ||
        (MAX3(FBC0[0], FBC1[0], FBC2[0]) < MIN3(RFBC0[0], RFBC1[0], RFBC2[0])) ||
        (MIN3(FBC0[1], FBC1[1], FBC2[1]) > MAX3(RFBC0[1], RFBC1[1], RFBC2[1])) ||
        (MAX3(FBC0[1], FBC1[1], FBC2[1]) < MIN3(RFBC0[1], RFBC1[1], RFBC2[1]))) {
      continue;
    }

    lineart_triangle_generate_intersection_line_only(rb, rt, testing_triangle);
  }
}

static void lineart_compute_view_vector(LineartRenderBuffer *rb)
{
  float direction[3] = {0, 0, 1};
  float trans[3];
  float inv[4][4];

  BLI_spin_lock(&lineart_share.lock_render_status);
  if (lineart_share.viewport_camera_override) {
    if (lineart_share.camera_is_persp) {
      invert_m4_m4(inv, lineart_share.viewinv);
    }
    else {
      quat_to_mat4(inv, lineart_share.viewquat);
    }
  }
  else {
    invert_m4_m4(inv, rb->cam_obmat);
  }
  BLI_spin_unlock(&lineart_share.lock_render_status);
  transpose_m4(inv);
  mul_v3_mat3_m4v3(trans, inv, direction);
  copy_v3db_v3fl(rb->view_vector, trans);
}

static void lineart_compute_scene_contours(LineartRenderBuffer *rb, const float threshold)
{
  double *view_vector = rb->view_vector;
  double dot_1 = 0, dot_2 = 0;
  double result;
  int add = 0;
  LineartRenderLine *rl;
  int contour_count = 0;
  int crease_count = 0;
  int material_count = 0;

  if (!rb->cam_is_persp) {
    lineart_compute_view_vector(rb);
  }

  for (rl = rb->all_render_lines.first; rl; rl = rl->next) {

    add = 0;
    dot_1 = 0;
    dot_2 = 0;

    if (rb->cam_is_persp) {
      sub_v3_v3v3_db(view_vector, rl->l->gloc, rb->camera_pos);
    }

    if (use_smooth_contour_modifier_contour) {
      if (rl->flags & LRT_EDGE_FLAG_CONTOUR) {
        add = 1;
      }
    }
    else {
      if (rl->tl) {
        dot_1 = dot_v3v3_db(view_vector, rl->tl->gn);
      }
      else {
        add = 1;
      }
      if (rl->tr) {
        dot_2 = dot_v3v3_db(view_vector, rl->tr->gn);
      }
      else {
        add = 1;
      }
    }

    if (!add) {
      if ((result = dot_1 * dot_2) <= 0 && (dot_1 + dot_2)) {
        add = 1;
      }
      else if (dot_v3v3_db(rl->tl->gn, rl->tr->gn) < threshold) {
        add = 2;
      }
      else if (rl->tl && rl->tr && rl->tl->material_id != rl->tr->material_id) {
        add = 3;
      }
    }

    if (add == 1) {
      rl->flags |= LRT_EDGE_FLAG_CONTOUR;
      list_append_pointer_static(&rb->contours, &rb->render_data_pool, rl);
      contour_count++;
    }
    else if (add == 2) {
      rl->flags |= LRT_EDGE_FLAG_CREASE;
      list_append_pointer_static(&rb->crease_lines, &rb->render_data_pool, rl);
      crease_count++;
    }
    else if (add == 3) {
      rl->flags |= LRT_EDGE_FLAG_MATERIAL;
      list_append_pointer_static(&rb->material_lines, &rb->render_data_pool, rl);
      material_count++;
    }
    if (rl->flags & LRT_EDGE_FLAG_EDGE_MARK) {
      /*  no need to mark again */
      add = 4;
      list_append_pointer_static(&rb->edge_marks, &rb->render_data_pool, rl);
      /*  continue; */
    }
    if (add) {
      int r1, r2, c1, c2, row, col;
      if (lineart_get_line_bounding_areas(rb, rl, &r1, &r2, &c1, &c2)) {
        for (row = r1; row != r2 + 1; row++) {
          for (col = c1; col != c2 + 1; col++) {
            lineart_link_line_with_bounding_area(
                rb, &rb->initial_bounding_areas[row * 4 + col], rl);
          }
        }
      }
    }

    /*  line count reserved for feature such as progress feedback */
  }
}

/* Buffer operations */

static void lineart_destroy_render_data(void)
{
  LineartRenderBuffer *rb = lineart_share.render_buffer_shared;
  if (rb == NULL) {
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

  BLI_spin_end(&rb->lock_task);
  BLI_spin_end(&rb->render_data_pool.lock_mem);

  mem_static_destroy(&rb->render_data_pool);
}

void ED_lineart_destroy_render_data(void)
{

  lineart_destroy_render_data();
  LineartRenderBuffer *rb = lineart_share.render_buffer_shared;
  if (rb) {
    MEM_freeN(rb);
    lineart_share.render_buffer_shared = NULL;
  }
}

void ED_lineart_destroy_render_data_external(void)
{
  if (!lineart_share.init_complete) {
    return;
  }
  while (ED_lineart_calculation_flag_check(LRT_RENDER_RUNNING)) {
    /* Wait to finish, XXX: should cancel here */
  }

  BLI_spin_lock(&lineart_share.lock_render_status);
  TaskPool *tp_read = lineart_share.background_render_task;
  BLI_spin_unlock(&lineart_share.lock_render_status);

  if (tp_read) {
    BLI_task_pool_work_and_wait(lineart_share.background_render_task);
    BLI_task_pool_free(lineart_share.background_render_task);
    lineart_share.background_render_task = NULL;
  }

  ED_lineart_destroy_render_data();
}

LineartRenderBuffer *ED_lineart_create_render_buffer(Scene *s)
{
  /* Re-init render_buffer_shared */
  if (lineart_share.render_buffer_shared) {
    ED_lineart_destroy_render_data();
  }

  LineartRenderBuffer *rb = MEM_callocN(sizeof(LineartRenderBuffer), "LRT render buffer");

  lineart_share.render_buffer_shared = rb;
  if (lineart_share.viewport_camera_override) {
    copy_v3db_v3fl(rb->camera_pos, lineart_share.camera_pos);
    rb->cam_is_persp = lineart_share.camera_is_persp;
    rb->near_clip = lineart_share.near_clip;
    rb->far_clip = lineart_share.far_clip;
    rb->shift_x = rb->shift_y = 0.0f;
  }
  else {
    Camera *c = s->camera->data;
    copy_v3db_v3fl(rb->camera_pos, s->camera->obmat[3]);
    copy_m4_m4(rb->cam_obmat, s->camera->obmat);
    rb->cam_is_persp = (c->type == CAM_PERSP);
    rb->near_clip = c->clip_start;
    rb->far_clip = c->clip_end;
    rb->shift_x = c->shiftx;
    rb->shift_y = c->shifty;
  }

  rb->chaining_image_threshold = s->lineart.chaining_image_threshold;
  rb->chaining_geometry_threshold = s->lineart.chaining_geometry_threshold;

  BLI_spin_init(&rb->lock_task);
  BLI_spin_init(&rb->render_data_pool.lock_mem);

  return rb;
}

void ED_lineart_init_locks()
{
  if (!(lineart_share.init_complete & LRT_INIT_LOCKS)) {
    BLI_spin_init(&lineart_share.lock_loader);
    BLI_spin_init(&lineart_share.lock_render_status);
    lineart_share.init_complete |= LRT_INIT_LOCKS;
  }
}

void ED_lineart_calculation_set_flag(eLineartRenderStatus flag)
{
  BLI_spin_lock(&lineart_share.lock_render_status);

  if (flag == LRT_RENDER_FINISHED && lineart_share.flag_render_status == LRT_RENDER_INCOMPELTE) {
    ; /* Don't set the finished flag when it's canceled from any one of the thread.*/
  }
  else {
    lineart_share.flag_render_status = flag;
  }

  BLI_spin_unlock(&lineart_share.lock_render_status);
}

bool ED_lineart_calculation_flag_check(eLineartRenderStatus flag)
{
  bool match;
  BLI_spin_lock(&lineart_share.lock_render_status);
  match = (lineart_share.flag_render_status == flag);
  BLI_spin_unlock(&lineart_share.lock_render_status);
  return match;
}

void ED_lineart_modifier_sync_set_flag(eLineartModifierSyncStatus flag, bool is_from_modifier)
{
  BLI_spin_lock(&lineart_share.lock_render_status);

  lineart_share.fflag_sync_staus = flag;

  BLI_spin_unlock(&lineart_share.lock_render_status);
}

bool ED_lineart_modifier_sync_flag_check(eLineartModifierSyncStatus flag)
{
  bool match;
  BLI_spin_lock(&lineart_share.lock_render_status);
  match = (lineart_share.fflag_sync_staus == flag);
  BLI_spin_unlock(&lineart_share.lock_render_status);
  return match;
}

static int lineart_get_max_occlusion_level(Depsgraph *dg)
{
  LineartGpencilModifierData *lmd;
  GpencilModifierData *md;
  Object *o;
  int max_occ = 0;
  int max;

  DEG_OBJECT_ITER_BEGIN (dg,
                         o,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY | DEG_ITER_OBJECT_FLAG_VISIBLE |
                             DEG_ITER_OBJECT_FLAG_DUPLI | DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    if (o->type == OB_GPENCIL) {
      LISTBASE_FOREACH (GpencilModifierData *, md, &o->greasepencil_modifiers) {
        if (md->type == eGpencilModifierType_Lineart) {
          lmd = (LineartGpencilModifierData *)md;
          max = MAX2(lmd->level_start, lmd->level_end);
          max_occ = MAX2(max, max_occ);
        }
      }
    }
  }
  DEG_OBJECT_ITER_END;

  return max_occ;
}

static int lineart_get_render_triangle_size(LineartRenderBuffer *rb, const Scene *s)
{
  if (rb->thread_count == 0) {
    rb->thread_count = BKE_render_num_threads(&s->r);
  }
  return sizeof(LineartRenderTriangle) + (sizeof(LineartRenderLine *) * rb->thread_count);
}

int ED_lineart_count_leveled_edge_segment_count(const ListBase *line_list,
                                                const LineartLineLayer *ll)
{
  LinkData *lip;
  LineartRenderLine *rl;
  LineartRenderLineSegment *rls;
  int count = 0;
  for (lip = line_list->first; lip; lip = lip->next) {
    rl = lip->data;

    for (rls = rl->segments.first; rls; rls = rls->next) {

      if (!(ll->flags & LRT_LINE_LAYER_USE_MULTIPLE_LEVELS)) {
        if (rls->occlusion == ll->level_start) {
          count++;
        }
      }
      else {
        if (rls->occlusion >= ll->level_start && rls->occlusion <= ll->level_end) {
          count++;
        }
      }
    }
  }
  return count;
}
int lineart_count_intersection_segment_count(LineartRenderBuffer *rb)
{
  LineartRenderLine *rl;
  int count = 0;
  for (rl = rb->intersection_lines.first; rl; rl = rl->next) {
    count++;
  }
  return count;
}
void *ED_lineart_make_leveled_edge_vertex_array(LineartRenderBuffer *UNUSED(rb),
                                                const ListBase *line_list,
                                                float *vertexArray,
                                                float *normal_array,
                                                float **next_normal,
                                                const LineartLineLayer *ll,
                                                const float componet_id)
{
  LinkData *lip;
  LineartRenderLine *rl;
  LineartRenderLineSegment *rls, *irls;
  float *v = vertexArray;
  float *N = normal_array;
  for (lip = line_list->first; lip; lip = lip->next) {
    rl = lip->data;

    for (rls = rl->segments.first; rls; rls = rls->next) {
      int use = 0;
      if (!(ll->flags & LRT_LINE_LAYER_USE_MULTIPLE_LEVELS)) {
        if (rls->occlusion == ll->level_start) {
          use = 1;
        }
      }
      else {
        if (rls->occlusion >= ll->level_start && rls->occlusion <= ll->level_end) {
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
  *next_normal = N;
  return v;
}

void lineart_chain_generate_draw_command(LineartRenderBuffer *rb);

#define TNS_BOUND_AREA_CROSSES(b1, b2) \
  ((b1)[0] < (b2)[1] && (b1)[1] > (b2)[0] && (b1)[3] < (b2)[2] && (b1)[2] > (b2)[3])

static void lineart_make_initial_bounding_areas(LineartRenderBuffer *rb)
{
  int sp_w = 4; /*  20; */
  int sp_h = 4; /*  rb->H / (rb->W / sp_w); */
  int row, col;
  LineartBoundingArea *ba;
  double span_w = (double)1 / sp_w * 2.0;
  double span_h = (double)1 / sp_h * 2.0;

  rb->tile_count_x = sp_w;
  rb->tile_count_y = sp_h;
  rb->width_per_tile = span_w;
  rb->height_per_tile = span_h;

  rb->bounding_area_count = sp_w * sp_h;
  rb->initial_bounding_areas = mem_static_aquire(
      &rb->render_data_pool, sizeof(LineartBoundingArea) * rb->bounding_area_count);

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
static void lineart_connect_new_bounding_areas(LineartRenderBuffer *rb, LineartBoundingArea *root)
{
  LineartBoundingArea *ba = root->child, *tba;
  LinkData *lip, *lip2, *next_lip;
  LineartStaticMemPool *mph = &rb->render_data_pool;

  /* Inter-connection with newly created 4 child bounding areas. */
  list_append_pointer_static_pool(mph, &ba[1].rp, &ba[0]);
  list_append_pointer_static_pool(mph, &ba[0].lp, &ba[1]);
  list_append_pointer_static_pool(mph, &ba[1].bp, &ba[2]);
  list_append_pointer_static_pool(mph, &ba[2].up, &ba[1]);
  list_append_pointer_static_pool(mph, &ba[2].rp, &ba[3]);
  list_append_pointer_static_pool(mph, &ba[3].lp, &ba[2]);
  list_append_pointer_static_pool(mph, &ba[3].up, &ba[0]);
  list_append_pointer_static_pool(mph, &ba[0].bp, &ba[3]);

  /** Connect 4 child bounding areas to other areas that are
   * adjacent to their original parents */
  for (lip = root->lp.first; lip; lip = lip->next) {

    /** For example, we are dealing with parent's left side
     * tba represents each adjacent neighbor of the parent.
     */
    tba = lip->data;

    /** if this neighbor is adjacent to
     * the two new areas on the left side of the parent,
     * then add them to the adjacent list as well. */
    if (ba[1].u > tba->b && ba[1].b < tba->u) {
      list_append_pointer_static_pool(mph, &ba[1].lp, tba);
      list_append_pointer_static_pool(mph, &tba->rp, &ba[1]);
    }
    if (ba[2].u > tba->b && ba[2].b < tba->u) {
      list_append_pointer_static_pool(mph, &ba[2].lp, tba);
      list_append_pointer_static_pool(mph, &tba->rp, &ba[2]);
    }
  }
  for (lip = root->rp.first; lip; lip = lip->next) {
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
  for (lip = root->up.first; lip; lip = lip->next) {
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
  for (lip = root->bp.first; lip; lip = lip->next) {
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

  /** Then remove the parent bounding areas from
   * their original adjacent areas. */
  for (lip = root->lp.first; lip; lip = lip->next) {
    for (lip2 = ((LineartBoundingArea *)lip->data)->rp.first; lip2; lip2 = next_lip) {
      next_lip = lip2->next;
      tba = lip2->data;
      if (tba == root) {
        list_remove_pointer_item_no_free(&((LineartBoundingArea *)lip->data)->rp, lip2);
        if (ba[1].u > tba->b && ba[1].b < tba->u) {
          list_append_pointer_static_pool(mph, &tba->rp, &ba[1]);
        }
        if (ba[2].u > tba->b && ba[2].b < tba->u) {
          list_append_pointer_static_pool(mph, &tba->rp, &ba[2]);
        }
      }
    }
  }
  for (lip = root->rp.first; lip; lip = lip->next) {
    for (lip2 = ((LineartBoundingArea *)lip->data)->lp.first; lip2; lip2 = next_lip) {
      next_lip = lip2->next;
      tba = lip2->data;
      if (tba == root) {
        list_remove_pointer_item_no_free(&((LineartBoundingArea *)lip->data)->lp, lip2);
        if (ba[0].u > tba->b && ba[0].b < tba->u) {
          list_append_pointer_static_pool(mph, &tba->lp, &ba[0]);
        }
        if (ba[3].u > tba->b && ba[3].b < tba->u) {
          list_append_pointer_static_pool(mph, &tba->lp, &ba[3]);
        }
      }
    }
  }
  for (lip = root->up.first; lip; lip = lip->next) {
    for (lip2 = ((LineartBoundingArea *)lip->data)->bp.first; lip2; lip2 = next_lip) {
      next_lip = lip2->next;
      tba = lip2->data;
      if (tba == root) {
        list_remove_pointer_item_no_free(&((LineartBoundingArea *)lip->data)->bp, lip2);
        if (ba[0].r > tba->l && ba[0].l < tba->r) {
          list_append_pointer_static_pool(mph, &tba->up, &ba[0]);
        }
        if (ba[1].r > tba->l && ba[1].l < tba->r) {
          list_append_pointer_static_pool(mph, &tba->up, &ba[1]);
        }
      }
    }
  }
  for (lip = root->bp.first; lip; lip = lip->next) {
    for (lip2 = ((LineartBoundingArea *)lip->data)->up.first; lip2; lip2 = next_lip) {
      next_lip = lip2->next;
      tba = lip2->data;
      if (tba == root) {
        list_remove_pointer_item_no_free(&((LineartBoundingArea *)lip->data)->up, lip2);
        if (ba[2].r > tba->l && ba[2].l < tba->r) {
          list_append_pointer_static_pool(mph, &tba->bp, &ba[2]);
        }
        if (ba[3].r > tba->l && ba[3].l < tba->r) {
          list_append_pointer_static_pool(mph, &tba->bp, &ba[3]);
        }
      }
    }
  }

  /* Finally clear parent's adjacent list. */
  while (list_pop_pointer_no_free(&root->lp))
    ;
  while (list_pop_pointer_no_free(&root->rp))
    ;
  while (list_pop_pointer_no_free(&root->up))
    ;
  while (list_pop_pointer_no_free(&root->bp))
    ;
}
static void lineart_link_triangle_with_bounding_area(LineartRenderBuffer *rb,
                                                     LineartBoundingArea *root_ba,
                                                     LineartRenderTriangle *rt,
                                                     double *LRUB,
                                                     int recursive);
static void lineart_split_bounding_area(LineartRenderBuffer *rb, LineartBoundingArea *root)
{
  LineartBoundingArea *ba = mem_static_aquire(&rb->render_data_pool,
                                              sizeof(LineartBoundingArea) * 4);
  LineartRenderTriangle *rt;
  LineartRenderLine *rl;

  ba[0].l = root->cx;
  ba[0].r = root->r;
  ba[0].u = root->u;
  ba[0].b = root->cy;
  ba[0].cx = (ba[0].l + ba[0].r) / 2;
  ba[0].cy = (ba[0].u + ba[0].b) / 2;

  ba[1].l = root->l;
  ba[1].r = root->cx;
  ba[1].u = root->u;
  ba[1].b = root->cy;
  ba[1].cx = (ba[1].l + ba[1].r) / 2;
  ba[1].cy = (ba[1].u + ba[1].b) / 2;

  ba[2].l = root->l;
  ba[2].r = root->cx;
  ba[2].u = root->cy;
  ba[2].b = root->b;
  ba[2].cx = (ba[2].l + ba[2].r) / 2;
  ba[2].cy = (ba[2].u + ba[2].b) / 2;

  ba[3].l = root->cx;
  ba[3].r = root->r;
  ba[3].u = root->cy;
  ba[3].b = root->b;
  ba[3].cx = (ba[3].l + ba[3].r) / 2;
  ba[3].cy = (ba[3].u + ba[3].b) / 2;

  root->child = ba;

  lineart_connect_new_bounding_areas(rb, root);

  while ((rt = list_pop_pointer_no_free(&root->linked_triangles)) != NULL) {
    LineartBoundingArea *cba = root->child;
    double b[4];
    b[0] = MIN3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
    b[1] = MAX3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
    b[2] = MAX3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
    b[3] = MIN3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
    if (TNS_BOUND_AREA_CROSSES(b, &cba[0].l)) {
      lineart_link_triangle_with_bounding_area(rb, &cba[0], rt, b, 0);
    }
    if (TNS_BOUND_AREA_CROSSES(b, &cba[1].l)) {
      lineart_link_triangle_with_bounding_area(rb, &cba[1], rt, b, 0);
    }
    if (TNS_BOUND_AREA_CROSSES(b, &cba[2].l)) {
      lineart_link_triangle_with_bounding_area(rb, &cba[2], rt, b, 0);
    }
    if (TNS_BOUND_AREA_CROSSES(b, &cba[3].l)) {
      lineart_link_triangle_with_bounding_area(rb, &cba[3], rt, b, 0);
    }
  }

  while ((rl = list_pop_pointer_no_free(&root->linked_lines)) != NULL) {
    lineart_link_line_with_bounding_area(rb, root, rl);
  }

  rb->bounding_area_count += 3;
}
static int lineart_line_crosses_bounding_area(LineartRenderBuffer *UNUSED(fb),
                                              double l[2],
                                              double r[2],
                                              LineartBoundingArea *ba)
{
  double vx, vy;
  double converted[4];
  double c1, c;

  if (((converted[0] = (double)ba->l) > MAX2(l[0], r[0])) ||
      ((converted[1] = (double)ba->r) < MIN2(l[0], r[0])) ||
      ((converted[2] = (double)ba->b) > MAX2(l[1], r[1])) ||
      ((converted[3] = (double)ba->u) < MIN2(l[1], r[1]))) {
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
static int lineart_triangle_covers_bounding_area(LineartRenderBuffer *fb,
                                                 LineartRenderTriangle *rt,
                                                 LineartBoundingArea *ba)
{
  double p1[2], p2[2], p3[2], p4[2];
  double *FBC1 = rt->v[0]->fbcoord, *FBC2 = rt->v[1]->fbcoord, *FBC3 = rt->v[2]->fbcoord;

  p3[0] = p1[0] = (double)ba->l;
  p2[1] = p1[1] = (double)ba->b;
  p2[0] = p4[0] = (double)ba->r;
  p3[1] = p4[1] = (double)ba->u;

  if ((FBC1[0] >= p1[0] && FBC1[0] <= p2[0] && FBC1[1] >= p1[1] && FBC1[1] <= p3[1]) ||
      (FBC2[0] >= p1[0] && FBC2[0] <= p2[0] && FBC2[1] >= p1[1] && FBC2[1] <= p3[1]) ||
      (FBC3[0] >= p1[0] && FBC3[0] <= p2[0] && FBC3[1] >= p1[1] && FBC3[1] <= p3[1])) {
    return 1;
  }

  if (ED_lineart_point_inside_triangled(p1, FBC1, FBC2, FBC3) ||
      ED_lineart_point_inside_triangled(p2, FBC1, FBC2, FBC3) ||
      ED_lineart_point_inside_triangled(p3, FBC1, FBC2, FBC3) ||
      ED_lineart_point_inside_triangled(p4, FBC1, FBC2, FBC3)) {
    return 1;
  }

  if ((lineart_line_crosses_bounding_area(fb, FBC1, FBC2, ba)) ||
      (lineart_line_crosses_bounding_area(fb, FBC2, FBC3, ba)) ||
      (lineart_line_crosses_bounding_area(fb, FBC3, FBC1, ba))) {
    return 1;
  }

  return 0;
}
static void lineart_link_triangle_with_bounding_area(LineartRenderBuffer *rb,
                                                     LineartBoundingArea *root_ba,
                                                     LineartRenderTriangle *rt,
                                                     double *LRUB,
                                                     int recursive)
{
  if (!lineart_triangle_covers_bounding_area(rb, rt, root_ba)) {
    return;
  }
  if (root_ba->child == NULL) {
    list_append_pointer_static_pool(&rb->render_data_pool, &root_ba->linked_triangles, rt);
    root_ba->triangle_count++;
    if (root_ba->triangle_count > 200 && recursive) {
      lineart_split_bounding_area(rb, root_ba);
    }
    if (recursive && rb->use_intersections) {
      lineart_triangle_calculate_intersections_in_bounding_area(rb, rt, root_ba);
    }
  }
  else {
    LineartBoundingArea *ba = root_ba->child;
    double *B1 = LRUB;
    double b[4];
    if (!LRUB) {
      b[0] = MIN3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
      b[1] = MAX3(rt->v[0]->fbcoord[0], rt->v[1]->fbcoord[0], rt->v[2]->fbcoord[0]);
      b[2] = MAX3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
      b[3] = MIN3(rt->v[0]->fbcoord[1], rt->v[1]->fbcoord[1], rt->v[2]->fbcoord[1]);
      B1 = b;
    }
    if (TNS_BOUND_AREA_CROSSES(B1, &ba[0].l)) {
      lineart_link_triangle_with_bounding_area(rb, &ba[0], rt, B1, recursive);
    }
    if (TNS_BOUND_AREA_CROSSES(B1, &ba[1].l)) {
      lineart_link_triangle_with_bounding_area(rb, &ba[1], rt, B1, recursive);
    }
    if (TNS_BOUND_AREA_CROSSES(B1, &ba[2].l)) {
      lineart_link_triangle_with_bounding_area(rb, &ba[2], rt, B1, recursive);
    }
    if (TNS_BOUND_AREA_CROSSES(B1, &ba[3].l)) {
      lineart_link_triangle_with_bounding_area(rb, &ba[3], rt, B1, recursive);
    }
  }
}
static void lineart_link_line_with_bounding_area(LineartRenderBuffer *rb,
                                                 LineartBoundingArea *root_ba,
                                                 LineartRenderLine *rl)
{
  if (root_ba->child == NULL) {
    list_append_pointer_static_pool(&rb->render_data_pool, &root_ba->linked_lines, rl);
  }
  else {
    if (lineart_line_crosses_bounding_area(
            rb, rl->l->fbcoord, rl->r->fbcoord, &root_ba->child[0])) {
      lineart_link_line_with_bounding_area(rb, &root_ba->child[0], rl);
    }
    if (lineart_line_crosses_bounding_area(
            rb, rl->l->fbcoord, rl->r->fbcoord, &root_ba->child[1])) {
      lineart_link_line_with_bounding_area(rb, &root_ba->child[1], rl);
    }
    if (lineart_line_crosses_bounding_area(
            rb, rl->l->fbcoord, rl->r->fbcoord, &root_ba->child[2])) {
      lineart_link_line_with_bounding_area(rb, &root_ba->child[2], rl);
    }
    if (lineart_line_crosses_bounding_area(
            rb, rl->l->fbcoord, rl->r->fbcoord, &root_ba->child[3])) {
      lineart_link_line_with_bounding_area(rb, &root_ba->child[3], rl);
    }
  }
}
static int lineart_get_triangle_bounding_areas(LineartRenderBuffer *rb,
                                               LineartRenderTriangle *rt,
                                               int *rowbegin,
                                               int *rowend,
                                               int *colbegin,
                                               int *colend)
{
  double sp_w = rb->width_per_tile, sp_h = rb->height_per_tile;
  double b[4];

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

  (*colbegin) = (int)((b[0] + 1.0) / sp_w);
  (*colend) = (int)((b[1] + 1.0) / sp_w);
  (*rowend) = rb->tile_count_y - (int)((b[2] + 1.0) / sp_h) - 1;
  (*rowbegin) = rb->tile_count_y - (int)((b[3] + 1.0) / sp_h) - 1;

  if ((*colend) >= rb->tile_count_x) {
    (*colend) = rb->tile_count_x - 1;
  }
  if ((*rowend) >= rb->tile_count_y) {
    (*rowend) = rb->tile_count_y - 1;
  }
  if ((*colbegin) < 0) {
    (*colbegin) = 0;
  }
  if ((*rowbegin) < 0) {
    (*rowbegin) = 0;
  }

  return 1;
}
static int lineart_get_line_bounding_areas(LineartRenderBuffer *rb,
                                           LineartRenderLine *rl,
                                           int *rowbegin,
                                           int *rowend,
                                           int *colbegin,
                                           int *colend)
{
  double sp_w = rb->width_per_tile, sp_h = rb->height_per_tile;
  double b[4];

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

  (*colbegin) = (int)((b[0] + 1.0) / sp_w);
  (*colend) = (int)((b[1] + 1.0) / sp_w);
  (*rowend) = rb->tile_count_y - (int)((b[2] + 1.0) / sp_h) - 1;
  (*rowbegin) = rb->tile_count_y - (int)((b[3] + 1.0) / sp_h) - 1;

  /* It's possible that the line stretches too much out to the side, resulting negative value */
  if ((*rowend) < (*rowbegin)) {
    (*rowend) = rb->tile_count_y - 1;
  }

  if ((*colend) < (*colbegin)) {
    (*colend) = rb->tile_count_x - 1;
  }

  CLAMP((*colbegin), 0, rb->tile_count_x - 1);
  CLAMP((*rowbegin), 0, rb->tile_count_y - 1);
  CLAMP((*colend), 0, rb->tile_count_x - 1);
  CLAMP((*rowend), 0, rb->tile_count_y - 1);

  return 1;
}
LineartBoundingArea *ED_lineart_get_point_bounding_area(LineartRenderBuffer *rb,
                                                        double x,
                                                        double y)
{
  double sp_w = rb->width_per_tile, sp_h = rb->height_per_tile;
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
static LineartBoundingArea *lineart_get_point_bounding_area_recursive(LineartBoundingArea *ba,
                                                                      double x,
                                                                      double y)
{
  if (ba->child == NULL) {
    return ba;
  }
  else {
#define IN_BOUND(i, x, y) \
  ba->child[i].l <= x && ba->child[i].r >= x && ba->child[i].b <= y && ba->child[i].u >= y

    if (IN_BOUND(0, x, y)) {
      return lineart_get_point_bounding_area_recursive(&ba->child[0], x, y);
    }
    else if (IN_BOUND(1, x, y)) {
      return lineart_get_point_bounding_area_recursive(&ba->child[1], x, y);
    }
    else if (IN_BOUND(2, x, y)) {
      return lineart_get_point_bounding_area_recursive(&ba->child[2], x, y);
    }
    else if (IN_BOUND(3, x, y)) {
      return lineart_get_point_bounding_area_recursive(&ba->child[3], x, y);
    }
  }
  return NULL;
}
LineartBoundingArea *ED_lineart_get_point_bounding_area_deep(LineartRenderBuffer *rb,
                                                             double x,
                                                             double y)
{
  LineartBoundingArea *ba;
  if ((ba = ED_lineart_get_point_bounding_area(rb, x, y)) != NULL) {
    return lineart_get_point_bounding_area_recursive(ba, x, y);
  }
  return NULL;
}

static void lineart_add_triangles(LineartRenderBuffer *rb)
{
  LineartRenderElementLinkNode *reln;
  LineartRenderTriangle *rt;
  int i, lim;
  int x1, x2, y1, y2;
  int r, co;

  for (reln = rb->triangle_buffer_pointers.first; reln; reln = reln->next) {
    rt = reln->pointer;
    lim = reln->element_count;
    for (i = 0; i < lim; i++) {
      if (rt->cull_status) {
        rt = (void *)(((unsigned char *)rt) + rb->triangle_size);
        continue;
      }
      if (lineart_get_triangle_bounding_areas(rb, rt, &y1, &y2, &x1, &x2)) {
        for (co = x1; co <= x2; co++) {
          for (r = y1; r <= y2; r++) {
            lineart_link_triangle_with_bounding_area(
                rb, &rb->initial_bounding_areas[r * 4 + co], rt, 0, 1);
          }
        }
      } /* else throw away. */
      rt = (void *)(((unsigned char *)rt) + rb->triangle_size);
    }
  }
}

/** This march along one render line in image space and
 * get the next bounding area the line is crossing. */
static LineartBoundingArea *lineart_get_next_bounding_area(LineartBoundingArea *this,
                                                           LineartRenderLine *rl,
                                                           double x,
                                                           double y,
                                                           double k,
                                                           int positive_x,
                                                           int positive_y,
                                                           double *next_x,
                                                           double *next_y)
{
  double rx, ry, ux, uy, lx, ly, bx, by;
  double r1, r2;
  LineartBoundingArea *ba;
  LinkData *lip;

  /* If we are marching towards the right */
  if (positive_x > 0) {
    rx = this->r;
    ry = y + k * (rx - x);

    /* If we are marching towards the top */
    if (positive_y > 0) {
      uy = this->u;
      ux = x + (uy - y) / k;
      r1 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], rx);
      r2 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], ux);
      if (MIN2(r1, r2) > 1) {
        return 0;
      }

      /* we reached the right side before the top side */
      if (r1 <= r2) {
        for (lip = this->rp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->u >= ry && ba->b < ry) {
            *next_x = rx;
            *next_y = ry;
            return ba;
          }
        }
      }
      /* we reached the top side before the right side */
      else {
        for (lip = this->up.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->r >= ux && ba->l < ux) {
            *next_x = ux;
            *next_y = uy;
            return ba;
          }
        }
      }
    }
    /* If we are marching towards the bottom */
    else if (positive_y < 0) {
      by = this->b;
      bx = x + (by - y) / k;
      r1 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], rx);
      r2 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], bx);
      if (MIN2(r1, r2) > 1) {
        return 0;
      }
      if (r1 <= r2) {
        for (lip = this->rp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->u >= ry && ba->b < ry) {
            *next_x = rx;
            *next_y = ry;
            return ba;
          }
        }
      }
      else {
        for (lip = this->bp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->r >= bx && ba->l < bx) {
            *next_x = bx;
            *next_y = by;
            return ba;
          }
        }
      }
    }
    /* If the line is compeletely horizontal, in which Y diffence == 0 */
    else {
      r1 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], this->r);
      if (r1 > 1) {
        return 0;
      }
      for (lip = this->rp.first; lip; lip = lip->next) {
        ba = lip->data;
        if (ba->u >= y && ba->b < y) {
          *next_x = this->r;
          *next_y = y;
          return ba;
        }
      }
    }
  }

  /* If we are marching towards the left */
  else if (positive_x < 0) {
    lx = this->l;
    ly = y + k * (lx - x);

    /* If we are marching towards the top */
    if (positive_y > 0) {
      uy = this->u;
      ux = x + (uy - y) / k;
      r1 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], lx);
      r2 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], ux);
      if (MIN2(r1, r2) > 1) {
        return 0;
      }
      if (r1 <= r2) {
        for (lip = this->lp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->u >= ly && ba->b < ly) {
            *next_x = lx;
            *next_y = ly;
            return ba;
          }
        }
      }
      else {
        for (lip = this->up.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->r >= ux && ba->l < ux) {
            *next_x = ux;
            *next_y = uy;
            return ba;
          }
        }
      }
    }

    /* If we are marching towards the bottom */
    else if (positive_y < 0) {
      by = this->b;
      bx = x + (by - y) / k;
      r1 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], lx);
      r2 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], bx);
      if (MIN2(r1, r2) > 1) {
        return 0;
      }
      if (r1 <= r2) {
        for (lip = this->lp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->u >= ly && ba->b < ly) {
            *next_x = lx;
            *next_y = ly;
            return ba;
          }
        }
      }
      else {
        for (lip = this->bp.first; lip; lip = lip->next) {
          ba = lip->data;
          if (ba->r >= bx && ba->l < bx) {
            *next_x = bx;
            *next_y = by;
            return ba;
          }
        }
      }
    }
    /* Again, horizontal */
    else {
      r1 = tmat_get_linear_ratio(rl->l->fbcoord[0], rl->r->fbcoord[0], this->l);
      if (r1 > 1) {
        return 0;
      }
      for (lip = this->lp.first; lip; lip = lip->next) {
        ba = lip->data;
        if (ba->u >= y && ba->b < y) {
          *next_x = this->l;
          *next_y = y;
          return ba;
        }
      }
    }
  }
  /* If the line is completely vertical, hence X difference == 0 */
  else {
    if (positive_y > 0) {
      r1 = tmat_get_linear_ratio(rl->l->fbcoord[1], rl->r->fbcoord[1], this->u);
      if (r1 > 1) {
        return 0;
      }
      for (lip = this->up.first; lip; lip = lip->next) {
        ba = lip->data;
        if (ba->r > x && ba->l <= x) {
          *next_x = x;
          *next_y = this->u;
          return ba;
        }
      }
    }
    else if (positive_y < 0) {
      r1 = tmat_get_linear_ratio(rl->l->fbcoord[1], rl->r->fbcoord[1], this->b);
      if (r1 > 1) {
        return 0;
      }
      for (lip = this->bp.first; lip; lip = lip->next) {
        ba = lip->data;
        if (ba->r > x && ba->l <= x) {
          *next_x = x;
          *next_y = this->b;
          return ba;
        }
      }
    }
    else
      return 0; /*  segment has no length */
  }
  return 0;
}

static LineartBoundingArea *lineart_get_bounding_area(LineartRenderBuffer *rb, double x, double y)
{
  LineartBoundingArea *iba;
  double sp_w = rb->width_per_tile, sp_h = rb->height_per_tile;
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
static LineartBoundingArea *lineart_get_first_possible_bounding_area(LineartRenderBuffer *rb,
                                                                     LineartRenderLine *rl)
{
  LineartBoundingArea *iba;
  double data[2] = {rl->l->fbcoord[0], rl->l->fbcoord[1]};
  double LU[2] = {-1, 1}, RU[2] = {1, 1}, LB[2] = {-1, -1}, RB[2] = {1, -1};
  double r = 1, sr = 1;

  if (data[0] > -1 && data[0] < 1 && data[1] > -1 && data[1] < 1) {
    return lineart_get_bounding_area(rb, data[0], data[1]);
  }
  else {
    if ((lineart_LineIntersectTest2d(rl->l->fbcoord, rl->r->fbcoord, LU, RU, &sr) && sr < r &&
         sr > 0) ||
        (lineart_LineIntersectTest2d(rl->l->fbcoord, rl->r->fbcoord, LB, RB, &sr) && sr < r &&
         sr > 0) ||
        (lineart_LineIntersectTest2d(rl->l->fbcoord, rl->r->fbcoord, LB, LU, &sr) && sr < r &&
         sr > 0) ||
        (lineart_LineIntersectTest2d(rl->l->fbcoord, rl->r->fbcoord, RB, RU, &sr) && sr < r &&
         sr > 0)) {
      r = sr;
    }
    interp_v2_v2v2_db(data, rl->l->fbcoord, rl->r->fbcoord, r);

    return lineart_get_bounding_area(rb, data[0], data[1]);
  }

  return iba;
}

/* Calculations */

/** Parent thread locking should be done before this very function is called. */
int ED_lineart_compute_feature_lines_internal(Depsgraph *depsgraph, const int intersectons_only)
{
  LineartRenderBuffer *rb;
  Scene *s = DEG_get_evaluated_scene(depsgraph);
  SceneLineart *lineart = &s->lineart;

  if ((lineart->flags & LRT_ENABLED) == 0) {
    /* Release lock when early return. */
    BLI_spin_unlock(&lineart_share.lock_loader);
    return OPERATOR_CANCELLED;
  }

  rb = ED_lineart_create_render_buffer(s);

  /* Has to be set after render buffer creation, to avoid locking from editor undo. */
  ED_lineart_calculation_set_flag(LRT_RENDER_RUNNING);

  lineart_share.render_buffer_shared = rb;

  rb->w = s->r.xsch;
  rb->h = s->r.ysch;
  rb->use_intersections = (lineart->flags & LRT_USE_INTERSECTIONS);

  rb->triangle_size = lineart_get_render_triangle_size(rb, s);

  rb->max_occlusion_level = lineart_get_max_occlusion_level(depsgraph);

  ED_lineart_update_render_progress("LRT: Loading geometries.");

  lineart_make_render_geometry_buffers(depsgraph, s, s->camera, rb);

  /** We had everything we need,
   * Unlock parent thread, it's safe to run independently from now. */
  BLI_spin_unlock(&lineart_share.lock_loader);

  lineart_compute_view_vector(rb);
  lineart_cull_triangles(rb);

  lineart_perspective_division(rb);

  lineart_make_initial_bounding_areas(rb);

  if (!intersectons_only) {
    lineart_compute_scene_contours(rb, lineart->crease_threshold);
  }

  ED_lineart_update_render_progress("LRT: Computing intersections.");

  lineart_add_triangles(rb);

  ED_lineart_update_render_progress("LRT: Computing line occlusion.");

  if (!intersectons_only) {
    lineart_calculate_line_occlusion_begin(rb);
  }

  ED_lineart_update_render_progress("LRT: Chaining.");

  /* intersection_only is preserved for furure functions.*/
  if (!intersectons_only) {
    float t_image = s->lineart.chaining_image_threshold;
    float t_geom = s->lineart.chaining_geometry_threshold;

    ED_lineart_NO_THREAD_chain_feature_lines(rb);

    // if (is_lineart_engine) {
    //  /* Enough with it. We can provide an option after we have Line Art internal smoothing */
    //  ED_lineart_calculation_set_flag(LRT_RENDER_FINISHED);
    //  return OPERATOR_FINISHED;
    //}

    /* Below are simply for better GPencil experience. */

    ED_lineart_split_chains_for_fixed_occlusion(rb);

    if (t_image < FLT_EPSILON && t_geom < FLT_EPSILON) {
      t_geom = 0.0f;
      t_image = 0.01f;
    }

    ED_lineart_connect_chains(rb, 1);
    ED_lineart_connect_chains(rb, 0);

    /* This configuration ensures there won't be accidental lost of short segments */
    ED_lineart_discard_short_chains(rb, MIN3(t_image, t_geom, 0.01f) - FLT_EPSILON);
  }
  // Set after GP done.
  // ED_lineart_calculation_set_flag(LRT_RENDER_FINISHED);

  return OPERATOR_FINISHED;
}

typedef struct LRT_FeatureLineWorker {
  Depsgraph *dg;
  int intersection_only;
} LRT_FeatureLineWorker;

static void lineart_update_gp_strokes_actual(Scene *scene, Depsgraph *dg);
static void lineart_notify_gpencil_targets(Depsgraph *dg);

static void lineart_compute_feature_lines_worker(TaskPool *__restrict UNUSED(pool),
                                                 LRT_FeatureLineWorker *worker_data)
{
  ED_lineart_compute_feature_lines_internal(worker_data->dg, worker_data->intersection_only);
  ED_lineart_chain_clear_picked_flag(lineart_share.render_buffer_shared);

  /* Calculation is done, give fresh data. */
  ED_lineart_modifier_sync_set_flag(LRT_SYNC_FRESH, false);

  lineart_notify_gpencil_targets(worker_data->dg);

  ED_lineart_calculation_set_flag(LRT_RENDER_FINISHED);
}

void ED_lineart_compute_feature_lines_background(Depsgraph *dg, const int intersection_only)
{
  TaskPool *tp_read;
  BLI_spin_lock(&lineart_share.lock_render_status);
  tp_read = lineart_share.background_render_task;
  BLI_spin_unlock(&lineart_share.lock_render_status);

  /* If the calculation is already started then bypass it. */
  if (ED_lineart_calculation_flag_check(LRT_RENDER_RUNNING)) {
    /* Release lock when early return. TODO: Canceling */
    BLI_spin_unlock(&lineart_share.lock_loader);
    return;
  }

  if (tp_read) {
    BLI_task_pool_work_and_wait(lineart_share.background_render_task);
    BLI_task_pool_free(lineart_share.background_render_task);
    lineart_share.background_render_task = NULL;
  }

  LRT_FeatureLineWorker *flw = MEM_callocN(sizeof(LRT_FeatureLineWorker), "LRT Worker");

  flw->dg = dg;
  flw->intersection_only = intersection_only;

  TaskPool *tp = BLI_task_pool_create_background(0, TASK_PRIORITY_HIGH);
  BLI_spin_lock(&lineart_share.lock_render_status);
  lineart_share.background_render_task = tp;
  BLI_spin_unlock(&lineart_share.lock_render_status);

  BLI_task_pool_push(tp, (TaskRunFunction)lineart_compute_feature_lines_worker, flw, true, NULL);
}

static bool lineart_camera_exists(bContext *c)
{
  Scene *s = CTX_data_scene(c);
  return s->camera ? true : false;
}
static int lineart_compute_feature_lines_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  SceneLineart *lineart = &scene->lineart;
  int result;

  if ((lineart->flags & LRT_ENABLED) == 0) {
    return OPERATOR_CANCELLED;
  }

  if (scene->camera == NULL) {
    BKE_report(op->reports, RPT_ERROR, "There is no active camera in this scene!");
    printf("LRT Warning: There is no active camera in this scene!\n");
    return OPERATOR_FINISHED;
  }

  // int intersections_only = (lineart->master_mode != LRT_MASTER_MODE_SOFTWARE);

  /** Lock caller thread before calling feature line computation.
   * This worker is not a background task, so we don't need to try another lock
   * to wait for the worker to finish. The lock will be released in the compute function.
   */
  BLI_spin_lock(&lineart_share.lock_loader);

  ED_lineart_compute_feature_lines_background(CTX_data_depsgraph_pointer(C),
                                              0);  // intersections_only);

  WM_event_add_notifier(C, NC_OBJECT | ND_DRAW, NULL);

  return OPERATOR_FINISHED;
}
static void lineart_compute_feature_lines_cancel(bContext *UNUSED(C), wmOperator *UNUSED(op))
{
  return;
}

void SCENE_OT_lineart_calculate_feature_lines(wmOperatorType *ot)
{

  /* identifiers */
  ot->name = "Calculate Feature Lines";
  ot->description = "LRT calculates feature line in current scene";
  ot->idname = "SCENE_OT_lineart_calculate";

  ot->poll = lineart_camera_exists;
  ot->cancel = lineart_compute_feature_lines_cancel;
  ot->exec = lineart_compute_feature_lines_exec;
}

/* Grease Pencil bindings */

static void lineart_notify_gpencil_targets(Depsgraph *dg)
{
  DEG_OBJECT_ITER_BEGIN (dg,
                         o,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY | DEG_ITER_OBJECT_FLAG_VISIBLE |
                             DEG_ITER_OBJECT_FLAG_DUPLI | DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    if (o->type == OB_GPENCIL) {
      if (BKE_gpencil_modifiers_findby_type(o, eGpencilModifierType_Lineart)) {
        bGPdata *gpd = ((Object *)o->id.orig_id)->data;
        DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);
      }
    }
  }
  DEG_OBJECT_ITER_END;
}

/* returns flags from LineartEdgeFlag */
static int lineart_object_line_types(Object *ob)
{
  ObjectLineart *obl = &ob->lineart;
  int result = 0;
  if (obl->contour.use) {
    result |= LRT_EDGE_FLAG_CONTOUR;
  }
  if (obl->crease.use) {
    result |= LRT_EDGE_FLAG_CREASE;
  }
  if (obl->material.use) {
    result |= LRT_EDGE_FLAG_MATERIAL;
  }
  if (obl->edge_mark.use) {
    result |= LRT_EDGE_FLAG_EDGE_MARK;
  }
  return result;
}

void ED_lineart_generate_gpencil_from_chain(Depsgraph *depsgraph,
                                            Object *ob,
                                            bGPDlayer *UNUSED(gpl),
                                            bGPDframe *gpf,
                                            int level_start,
                                            int level_end,
                                            int material_nr,
                                            Collection *col,
                                            int types)
{
  LineartRenderBuffer *rb = lineart_share.render_buffer_shared;

  if (rb == NULL) {
    printf("NULL Lineart rb!\n");
    return;
  }

  if ((!lineart_share.init_complete) || !ED_lineart_calculation_flag_check(LRT_RENDER_FINISHED)) {
    /* cache not ready */
    printf("Line art cache not ready.\n");
    return;
  }
  else {
    /* lock the cache, prevent rendering job from starting */
    BLI_spin_lock(&lineart_share.lock_render_status);
  }
  static int tempnum = 0;
  tempnum++;
  int color_idx = 0;
  short thickness = 100;

  float mat[4][4];

  unit_m4(mat);

  LineartRenderLineChain *rlc;
  LineartRenderLineChainItem *rlci;
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
    if (rlc->level > level_end || rlc->level < level_start) {
      continue;
    }
    if (ob && ob->id.orig_id != rlc->object_ref->id.orig_id) {
      /* Note: not object_ref and ob are both (same?) copy on write data, if legacy mode, use
       * object_ref->id.orig_id. Same below.
       * TODO? Should we always use orig_id in the future? */
      continue;
    }
    if (col && rlc->object_ref) {
      if (col->id.orig_id) {
        col = (Collection *)col->id.orig_id;
        /* Need this for using the same code for modifier preview and applying. */
      }
      if (!BKE_collection_has_object_recursive(col, (Object *)rlc->object_ref->id.orig_id)) {
        continue;
      }
    }

    /* Modifier for different GP objects are not evaluated in order, thus picked flag doesn't quite
     * make sense. Should have a batter solution if we don't want to pick the same stroke twice. */
    /* rlc->picked = 1; */

    int array_idx = 0;
    int count = ED_lineart_count_chain(rlc);
    bGPDstroke *gps = BKE_gpencil_stroke_add(gpf, color_idx, count, thickness, false);

    float *stroke_data = MEM_callocN(sizeof(float) * count * GP_PRIM_DATABUF_SIZE,
                                     "line art add stroke");

    for (rlci = rlc->chain.first; rlci; rlci = rlci->next) {
      float opacity = 1.0f; /* rlci->occlusion ? 0.0f : 1.0f; */
      stroke_data[array_idx] = rlci->gpos[0];
      stroke_data[array_idx + 1] = rlci->gpos[1];
      stroke_data[array_idx + 2] = rlci->gpos[2];
      stroke_data[array_idx + 3] = 1;       /*  thickness */
      stroke_data[array_idx + 4] = opacity; /*  hardness? */
      array_idx += 5;
    }

    BKE_gpencil_stroke_add_points(gps, stroke_data, count, mat);
    gps->mat_nr = material_nr;
    BKE_gpencil_stroke_geometry_update(gps);
    MEM_freeN(stroke_data);
  }

  /* release render lock so cache is free to be manipulated. */
  BLI_spin_unlock(&lineart_share.lock_render_status);
}
static void lineart_clear_gp_flags(Depsgraph *dg, int frame)
{
  DEG_OBJECT_ITER_BEGIN (dg,
                         o,
                         DEG_ITER_OBJECT_FLAG_LINKED_DIRECTLY | DEG_ITER_OBJECT_FLAG_VISIBLE |
                             DEG_ITER_OBJECT_FLAG_DUPLI | DEG_ITER_OBJECT_FLAG_LINKED_VIA_SET) {
    if (o->type == OB_GPENCIL) {
      bGPdata *gpd = ((Object *)o->id.orig_id)->data;
      bGPDlayer *gpl;
      for (gpl = gpd->layers.first; gpl; gpl = gpl->next) {
        bGPDframe *gpf = BKE_gpencil_layer_frame_find(gpl, frame);
        if (gpf == NULL) {
          continue;
        }
        gpf->flag &= ~GP_FRAME_LRT_CLEARED;
      }
    }
  }
  DEG_OBJECT_ITER_END;
}
static void lineart_update_gp_strokes_single(Depsgraph *dg,
                                             Object *gpobj,
                                             Object *ob,
                                             int frame,
                                             int level_start,
                                             int level_end,
                                             char *target_layer,
                                             char *target_material,
                                             Collection *col,
                                             int type)
{
  bGPdata *gpd;
  bGPDlayer *gpl;
  bGPDframe *gpf;
  gpd = gpobj->data;
  gpl = BKE_gpencil_layer_get_by_name(gpd, target_layer, 1);
  if (gpl == NULL) {
    gpl = BKE_gpencil_layer_addnew(gpd, "lineart_layer", true);
  }
  gpf = BKE_gpencil_layer_frame_get(gpl, frame, GP_GETFRAME_ADD_NEW);

  if (gpf->strokes.first &&
      !(DEG_get_evaluated_scene(dg)->lineart.flags & LRT_GPENCIL_OVERWRITE)) {
    return;
  }

  if (!(gpf->flag & GP_FRAME_LRT_CLEARED)) {
    BKE_gpencil_free_strokes(gpf);
    gpf->flag |= GP_FRAME_LRT_CLEARED;
  }

  int use_material = BKE_gpencil_object_material_get_index_name(gpobj, target_material);
  if (use_material < 0) {
    use_material = 0;
  }

  ED_lineart_generate_gpencil_from_chain(
      dg, ob, gpl, gpf, level_start, level_end, use_material, col, type);

  DEG_id_tag_update(&gpd->id, ID_RECALC_GEOMETRY);
}
static void lineart_update_gp_strokes_recursive(
    Depsgraph *dg, struct Collection *col, int frame, Object *source_only, Object *target_only)
{
  Object *ob;
  Object *gpobj;
  bGPdata *gpd;
  CollectionObject *co;
  CollectionChild *cc;

  for (co = col->gobject.first; co || source_only; co = co->next) {
    ob = source_only ? source_only : co->ob;

    ObjectLineart *obl = &ob->lineart;
    if (obl->target && obl->target->type == OB_GPENCIL) {
      gpobj = obl->target;
      gpd = gpobj->data;

      if (target_only && target_only != gpobj) {
        continue;
      }

      int level_start = obl->level_start;
      int level_end = (obl->flags & LRT_LINE_LAYER_USE_MULTIPLE_LEVELS) ? obl->level_end :
                                                                          obl->level_start;

      if (obl->flags & LRT_LINE_LAYER_USE_SAME_STYLE) {
        lineart_update_gp_strokes_single(dg,
                                         gpobj,
                                         ob,
                                         frame,
                                         level_start,
                                         level_end,
                                         obl->target_layer,
                                         obl->target_material,
                                         NULL,
                                         lineart_object_line_types(ob));
      }
      else {
        if (obl->contour.use) {
          lineart_update_gp_strokes_single(dg,
                                           gpobj,
                                           ob,
                                           frame,
                                           level_start,
                                           level_end,
                                           obl->contour.target_layer,
                                           obl->contour.target_material,
                                           NULL,
                                           LRT_EDGE_FLAG_CONTOUR);
        }
        if (obl->crease.use) {
          lineart_update_gp_strokes_single(dg,
                                           gpobj,
                                           ob,
                                           frame,
                                           level_start,
                                           level_end,
                                           obl->crease.target_layer,
                                           obl->crease.target_material,
                                           NULL,
                                           LRT_EDGE_FLAG_CREASE);
        }
        if (obl->material.use) {
          lineart_update_gp_strokes_single(dg,
                                           gpobj,
                                           ob,
                                           frame,
                                           level_start,
                                           level_end,
                                           obl->material.target_layer,
                                           obl->material.target_material,
                                           NULL,
                                           LRT_EDGE_FLAG_MATERIAL);
        }
        if (obl->edge_mark.use) {
          lineart_update_gp_strokes_single(dg,
                                           gpobj,
                                           ob,
                                           frame,
                                           level_start,
                                           level_end,
                                           obl->edge_mark.target_layer,
                                           obl->edge_mark.target_material,
                                           NULL,
                                           LRT_EDGE_FLAG_EDGE_MARK);
        }
      }

      DEG_id_tag_update(&gpd->id,
                        ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY | ID_RECALC_COPY_ON_WRITE);
    }
    if (source_only) {
      return;
    }
  }
  for (cc = col->children.first; cc; cc = cc->next) {
    lineart_update_gp_strokes_recursive(dg, cc->collection, frame, source_only, target_only);
  }
}

static int lineart_update_gp_strokes_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph_pointer(C);

  BLI_spin_lock(&lineart_share.lock_loader);
  ED_lineart_compute_feature_lines_internal(dg, 0);

  ED_lineart_calculation_set_flag(LRT_RENDER_FINISHED);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lineart_bake_gp_strokes_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph_pointer(C);
  int frame;
  int frame_begin = scene->r.sfra;
  int frame_end = scene->r.efra;

  for (frame = frame_begin; frame <= frame_end; frame++) {
    BKE_scene_frame_set(scene, frame);
    BKE_scene_graph_update_for_newframe(dg, CTX_data_main(C));

    BLI_spin_lock(&lineart_share.lock_loader);
    ED_lineart_compute_feature_lines_internal(dg, 0);

    ED_lineart_chain_clear_picked_flag(lineart_share.render_buffer_shared);
  }

  ED_lineart_calculation_set_flag(LRT_RENDER_FINISHED);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lineart_update_gp_target_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph_pointer(C);
  Object *gpo = CTX_data_active_object(C);

  int frame = scene->r.cfra;

  if (scene->lineart.flags & LRT_AUTO_UPDATE) {
    ED_lineart_compute_feature_lines_internal(dg, 0);
  }

  ED_lineart_chain_clear_picked_flag(lineart_share.render_buffer_shared);

  lineart_update_gp_strokes_recursive(dg, scene->master_collection, frame, NULL, gpo);

  lineart_clear_gp_flags(dg, frame);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}
static int lineart_update_gp_source_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  Depsgraph *dg = CTX_data_depsgraph_pointer(C);
  Object *source_obj = CTX_data_active_object(C);

  int frame = scene->r.cfra;

  if (scene->lineart.flags & LRT_AUTO_UPDATE) {
    ED_lineart_compute_feature_lines_internal(dg, 0);
  }

  ED_lineart_chain_clear_picked_flag(lineart_share.render_buffer_shared);

  lineart_update_gp_strokes_recursive(dg, scene->master_collection, frame, source_obj, NULL);

  lineart_clear_gp_flags(dg, frame);

  WM_event_add_notifier(C, NC_GPENCIL | ND_DATA | NA_EDITED | ND_SPACE_PROPERTIES, NULL);

  return OPERATOR_FINISHED;
}

static bool lineart_active_is_gpencil_object(bContext *C)
{
  Object *o = CTX_data_active_object(C);
  return o->type == OB_GPENCIL;
}
static bool lineart_active_is_source_object(bContext *C)
{
  Object *o = CTX_data_active_object(C);
  if (o->type != OB_MESH) {
    return false;
  }
  else {
    if (o->lineart.usage == OBJECT_FEATURE_LINE_INCLUDE) {
      return true;
    }
  }
  return false;
}

void SCENE_OT_lineart_update_gp_strokes(wmOperatorType *ot)
{
  ot->name = "Update LRT Strokes";
  ot->description = "Update strokes for LRT grease pencil targets";
  ot->idname = "SCENE_OT_lineart_update_gp_strokes";

  ot->exec = lineart_update_gp_strokes_exec;
}
void SCENE_OT_lineart_bake_gp_strokes(wmOperatorType *ot)
{
  ot->name = "Bake LRT Strokes";
  ot->description = "Bake strokes for LRT grease pencil targets in all frames";
  ot->idname = "SCENE_OT_lineart_bake_gp_strokes";

  ot->exec = lineart_bake_gp_strokes_exec;
}
void OBJECT_OT_lineart_update_gp_target(wmOperatorType *ot)
{
  ot->name = "Update Strokes";
  ot->description = "Update LRT strokes for selected GPencil object";
  ot->idname = "OBJECT_OT_lineart_update_gp_target";

  ot->poll = lineart_active_is_gpencil_object;
  ot->exec = lineart_update_gp_target_exec;
}
/* Not working due to lack of GP flags for the object */
void OBJECT_OT_lineart_update_gp_source(wmOperatorType *ot)
{
  ot->name = "Update Strokes";
  ot->description = "Update LRT strokes for selected Mesh object.";
  ot->idname = "OBJECT_OT_lineart_update_gp_source";

  ot->poll = lineart_active_is_source_object;
  ot->exec = lineart_update_gp_source_exec;
}

/* Post-frame updater */

void ED_lineart_post_frame_update_external(Scene *s, Depsgraph *dg)
{
  if ((s->lineart.flags & LRT_ENABLED) == 0 || !(s->lineart.flags & LRT_AUTO_UPDATE)) {
    return;
  }
  if (ED_lineart_modifier_sync_flag_check(LRT_SYNC_WAITING)) {
    /* Modifier is waiting for data, trigger update (will wait/cancel if already running) */
    if (s->lineart.flags & LRT_AUTO_UPDATE) {
      ED_lineart_compute_feature_lines_background(dg, 0);

      /* Wait for loading finish */
      BLI_spin_lock(&lineart_share.lock_loader);
      BLI_spin_unlock(&lineart_share.lock_loader);
    }
  }
  else if (ED_lineart_modifier_sync_flag_check(LRT_SYNC_FRESH)) {
    /* At this stage GP should have all the data. We clear the flag */
    ED_lineart_modifier_sync_set_flag(LRT_SYNC_IDLE, false);
  }
}

void ED_lineart_update_render_progress(const char *text)
{
  // TODO
}
