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
 * Copyright 2020, Blender Foundation.
 */

/** \file
 * \ingroup draw_engine
 */

#include "DRW_render.h"

#include "BKE_global.h" /* REFACTOR(fclem) remove */

#include "UI_resources.h"

#include "DNA_gpencil_types.h"

#include "DEG_depsgraph_query.h"

#include "ED_view3d.h"

#include "overlay_private.h"

#include "draw_common.h"
#include "draw_manager_text.h"

void OVERLAY_edit_gpencil_cache_init(OVERLAY_Data *vedata)
{
  OVERLAY_PassList *psl = vedata->psl;
  OVERLAY_PrivateData *pd = vedata->stl->pd;
  struct GPUShader *sh;
  DRWShadingGroup *grp;

  /* Default: Display nothing. */
  pd->edit_gpencil_points_grp = NULL;
  pd->edit_gpencil_wires_grp = NULL;

  /* REFACTOR(fclem) remove */
  if (G.debug_value != 50) {
    return;
  }

  const DRWContextState *draw_ctx = DRW_context_state_get();
  View3D *v3d = draw_ctx->v3d;
  Object *ob = draw_ctx->obact;
  bGPdata *gpd = (bGPdata *)ob->data;
  ToolSettings *ts = draw_ctx->scene->toolsettings;

  if (gpd == NULL) {
    return;
  }

  // const bool use_sculpt_mask = (GPENCIL_SCULPT_MODE(gpd) &&
  //                               GPENCIL_ANY_SCULPT_MASK(ts->gpencil_selectmode_sculpt));
  const bool show_sculpt_points = (GPENCIL_SCULPT_MODE(gpd) &&
                                   (ts->gpencil_selectmode_sculpt &
                                    (GP_SCULPT_MASK_SELECTMODE_POINT |
                                     GP_SCULPT_MASK_SELECTMODE_SEGMENT)));
  const bool use_vertex_mask = (GPENCIL_VERTEX_MODE(gpd) &&
                                GPENCIL_ANY_VERTEX_MASK(ts->gpencil_selectmode_vertex));
  const bool show_vertex_points = (GPENCIL_VERTEX_MODE(gpd) &&
                                   (ts->gpencil_selectmode_vertex &
                                    (GP_VERTEX_MASK_SELECTMODE_POINT |
                                     GP_VERTEX_MASK_SELECTMODE_SEGMENT)));
  const bool do_multiedit = GPENCIL_MULTIEDIT_SESSIONS_ON(gpd);
  const bool show_multi_edit_lines = do_multiedit &&
                                     (v3d->gp_flag & V3D_GP_SHOW_MULTIEDIT_LINES) != 0;

  const bool show_lines = (v3d->gp_flag & V3D_GP_SHOW_EDIT_LINES);
  const bool hide_lines = GPENCIL_VERTEX_MODE(gpd) && use_vertex_mask && !show_multi_edit_lines;

  const bool is_weight_paint = (gpd) && (gpd->flag & GP_DATA_STROKE_WEIGHTMODE);
  const bool is_vertex_paint = (gpd) && (gpd->flag & GP_DATA_STROKE_VERTEXMODE);

  /* If Sculpt/Vertex mode and the mask is disabled, the select must be hidden. */
  // const bool hide_select = ((GPENCIL_SCULPT_MODE(gpd) && !use_sculpt_mask) ||
  //                           (GPENCIL_VERTEX_MODE(gpd) && !use_vertex_mask));

  /* Show Edit points if:
   *  Edit mode: Not in Stroke selection mode
   *  Sculpt mode: Not in Stroke mask mode and any other mask mode enabled
   *  Weight mode: Always
   *  Vertex mode: Always
   */
  const bool show_points = show_sculpt_points || show_vertex_points || is_weight_paint ||
                           is_vertex_paint ||
                           (GPENCIL_EDIT_MODE(gpd) &&
                            (ts->gpencil_selectmode_edit != GP_SELECTMODE_STROKE));

  if (GPENCIL_VERTEX_MODE(gpd) && !use_vertex_mask && !show_multi_edit_lines) {
    return;
  }

  {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL |
                     DRW_STATE_BLEND_ALPHA;
    DRW_PASS_CREATE(psl->edit_gpencil_ps, state | pd->clipping_state);

    if (show_lines && !hide_lines) {
      sh = OVERLAY_shader_edit_gpencil_wire();
      pd->edit_gpencil_wires_grp = grp = DRW_shgroup_create(sh, psl->edit_gpencil_ps);
      DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      DRW_shgroup_uniform_bool_copy(grp, "doMultiframe", show_multi_edit_lines);
      DRW_shgroup_uniform_float_copy(grp, "gpEditOpacity", v3d->vertex_opacity);
    }

    if (show_points) {
      sh = OVERLAY_shader_edit_gpencil_point();
      pd->edit_gpencil_points_grp = grp = DRW_shgroup_create(sh, psl->edit_gpencil_ps);
      DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      DRW_shgroup_uniform_bool_copy(grp, "doMultiframe", do_multiedit);
      DRW_shgroup_uniform_float_copy(grp, "gpEditOpacity", v3d->vertex_opacity);
    }
  }
}

static void OVERLAY_edit_gpencil_cache_populate(OVERLAY_Data *vedata, Object *ob)
{
  OVERLAY_PrivateData *pd = vedata->stl->pd;
  bGPdata *gpd = (bGPdata *)ob->data;
  if (gpd == NULL) {
    return;
  }

  if (pd->edit_gpencil_wires_grp) {
    DRWShadingGroup *grp = DRW_shgroup_create_sub(pd->edit_gpencil_wires_grp);
    DRW_shgroup_uniform_vec4_copy(grp, "gpEditColor", gpd->line_color);

    struct GPUBatch *geom = DRW_cache_gpencil_edit_lines_get(ob, pd->cfra);
    DRW_shgroup_call_no_cull(pd->edit_gpencil_wires_grp, geom, ob);
  }

  if (pd->edit_gpencil_points_grp) {
    const bool show_direction = (gpd->flag & GP_DATA_SHOW_DIRECTION) != 0;

    DRWShadingGroup *grp = DRW_shgroup_create_sub(pd->edit_gpencil_points_grp);
    DRW_shgroup_uniform_float_copy(grp, "doStrokeEndpoints", show_direction);

    struct GPUBatch *geom = DRW_cache_gpencil_edit_points_get(ob, pd->cfra);
    DRW_shgroup_call_no_cull(grp, geom, ob);
  }
}

static void OVERLAY_gpencil_color_names(Object *ob)
{
  bGPdata *gpd = (bGPdata *)ob->data;
  if (gpd == NULL) {
    return;
  }

  const DRWContextState *draw_ctx = DRW_context_state_get();
  ViewLayer *view_layer = draw_ctx->view_layer;
  int theme_id = DRW_object_wire_theme_get(ob, view_layer, NULL);
  uchar color[4];
  UI_GetThemeColor4ubv(theme_id, color);
  struct DRWTextStore *dt = DRW_text_cache_ensure();

  for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
    if (gpl->flag & GP_LAYER_HIDE) {
      continue;
    }
    bGPDframe *gpf = gpl->actframe;
    if (gpf == NULL) {
      continue;
    }
    for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
      Material *ma = give_current_material(ob, gps->mat_nr + 1);
      if (ma == NULL) {
        continue;
      }

      MaterialGPencilStyle *gp_style = ma->gp_style;
      /* skip stroke if it doesn't have any valid data */
      if ((gps->points == NULL) || (gps->totpoints < 1) || (gp_style == NULL)) {
        continue;
      }
      /* check if the color is visible */
      if (gp_style->flag & GP_STYLE_COLOR_HIDE) {
        continue;
      }

      /* only if selected */
      if (gps->flag & GP_STROKE_SELECT) {
        float fpt[3];
        for (int i = 0; i < gps->totpoints; i++) {
          bGPDspoint *pt = &gps->points[i];
          if (pt->flag & GP_SPOINT_SELECT) {
            mul_v3_m4v3(fpt, ob->obmat, &pt->x);
            DRW_text_cache_add(dt,
                               fpt,
                               ma->id.name + 2,
                               strlen(ma->id.name + 2),
                               10,
                               0,
                               DRW_TEXT_CACHE_GLOBALSPACE | DRW_TEXT_CACHE_STRING_PTR,
                               color);
            break;
          }
        }
      }
    }
  }
}

void OVERLAY_gpencil_cache_populate(OVERLAY_Data *vedata, Object *ob)
{
  if (ob->mode == OB_MODE_EDIT_GPENCIL) {
    OVERLAY_edit_gpencil_cache_populate(vedata, ob);
  }

  /* don't show object extras in set's */
  if ((ob->base_flag & (BASE_FROM_SET | BASE_FROM_DUPLI)) == 0) {
    if ((ob->dtx & OB_DRAWNAME) && (ob->mode == OB_MODE_EDIT_GPENCIL) && DRW_state_show_text()) {
      OVERLAY_gpencil_color_names(ob);
    }
  }
}

void OVERLAY_edit_gpencil_draw(OVERLAY_Data *vedata)
{
  OVERLAY_PassList *psl = vedata->psl;

  /* REFACTOR(fclem) remove */
  if (G.debug_value != 50) {
    return;
  }

  DRW_draw_pass(psl->edit_gpencil_ps);
}
