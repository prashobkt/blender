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

#include "draw_cache_impl.h"

#include "DNA_mesh_types.h"
#include "DNA_space_types.h"

#include "ED_image.h"

#include "UI_resources.h"

#include "BLI_listbase.h"
#include "BLI_math_color.h"

#include "overlay2d_engine.h"
#include "overlay2d_private.h"

void OVERLAY2D_uv_stretching_engine_init(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;

  const DRWContextState *drw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)drw_ctx->space_data;

  pd->uv_stretching.draw_type = sima->dt_uvstretch;
  BLI_listbase_clear(&pd->uv_stretching.totals);
  pd->uv_stretching.total_area_ratio = 0.0f;
  pd->uv_stretching.total_area_ratio_inv = 0.0f;

  /* Disable uv faces when uv stretching is enabled. */
  pd->uv.do_faces = false;
}

void OVERLAY2D_uv_stretching_cache_init(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_PassList *psl = vedata->psl;
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;

  /* uv stretching */
  {
    DRW_PASS_CREATE(psl->uv_stretching,
                    DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_ALWAYS | DRW_STATE_BLEND_ALPHA);
    if (pd->uv_stretching.draw_type == SI_UVDT_STRETCH_ANGLE) {
      GPUShader *sh = OVERLAY2D_shaders_uv_stretching_angle_get();
      pd->uv_stretching_grp = DRW_shgroup_create(sh, psl->uv_stretching);
      float asp[2];
      ED_space_image_get_uv_aspect(sima, &asp[0], &asp[1]);
      DRW_shgroup_uniform_block(pd->uv_stretching_grp, "globalsBlock", G_draw.block_ubo);
      DRW_shgroup_uniform_vec2_copy(pd->uv_stretching_grp, "aspect", asp);
    }
    else /* SI_UVDT_STRETCH_AREA */ {
      GPUShader *sh = OVERLAY2D_shaders_uv_stretching_area_get();
      pd->uv_stretching_grp = DRW_shgroup_create(sh, psl->uv_stretching);
      DRW_shgroup_uniform_block(pd->uv_stretching_grp, "globalsBlock", G_draw.block_ubo);
      DRW_shgroup_uniform_float(
          pd->uv_stretching_grp, "totalAreaRatio", &pd->uv_stretching.total_area_ratio, 1);
      DRW_shgroup_uniform_float(
          pd->uv_stretching_grp, "totalAreaRatioInv", &pd->uv_stretching.total_area_ratio_inv, 1);
    }
  }
}

void OVERLAY2D_uv_stretching_cache_populate(OVERLAY2D_Data *vedata, Object *ob)
{
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;
  Mesh *me = ob->data;
  struct GPUBatch *geom;
  if (pd->uv_stretching.draw_type == SI_UVDT_STRETCH_ANGLE) {
    geom = DRW_mesh_batch_cache_get_edituv_faces_stretch_angle(me);
  }
  else /* SI_UVDT_STRETCH_AREA */ {
    OVERLAY2D_UvStretchingAreaTotals *totals = MEM_mallocN(
        sizeof(OVERLAY2D_UvStretchingAreaTotals), __func__);
    BLI_addtail(&pd->uv_stretching.totals, totals);
    geom = DRW_mesh_batch_cache_get_edituv_faces_stretch_area(
        me, &totals->total_area, &totals->total_area_uv);
  }

  if (geom) {
    DRW_shgroup_call_obmat(pd->uv_stretching_grp, geom, pd->unit_mat);
  }
}

static void overlay_uv_stretching_update_ratios(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;

  if (pd->uv_stretching.draw_type == SI_UVDT_STRETCH_AREA) {
    float total_area = 0.0f;
    float total_area_uv = 0.0f;

    LISTBASE_FOREACH (OVERLAY2D_UvStretchingAreaTotals *, totals, &pd->uv_stretching.totals) {
      total_area += *totals->total_area;
      total_area_uv += *totals->total_area_uv;
    }

    if (total_area > FLT_EPSILON && total_area_uv > FLT_EPSILON) {
      pd->uv_stretching.total_area_ratio = total_area / total_area_uv;
      pd->uv_stretching.total_area_ratio_inv = total_area_uv / total_area;
    }
  }
  BLI_freelistN(&pd->uv_stretching.totals);
}

void OVERLAY2D_uv_stretching_draw_scene_faces(OVERLAY2D_Data *vedata)
{
  /* This is the only place that ensures that all totals are available and we can calculate the uv
   * stretching rations */
  overlay_uv_stretching_update_ratios(vedata);

  OVERLAY2D_PassList *psl = vedata->psl;
  DRW_draw_pass(psl->uv_stretching);
}
