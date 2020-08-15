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

#include "DNA_space_types.h"

#include "UI_resources.h"

#include "BLI_math_color.h"

#include "overlay2d_engine.h"
#include "overlay2d_private.h"

void OVERLAY2D_uv_shadow_cache_init(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_PassList *psl = vedata->psl;
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;

  /* uv shadow edges */
  {
    DRW_PASS_CREATE(psl->uv_shadow_edges,
                    DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_ALWAYS | DRW_STATE_BLEND_ALPHA);
    GPUShader *sh = OVERLAY2D_shaders_wireframe_get();
    pd->uv_shadow_edges_grp = DRW_shgroup_create(sh, psl->uv_shadow_edges);
    DRW_shgroup_uniform_block(pd->uv_shadow_edges_grp, "globalsBlock", G_draw.block_ubo);
    DRW_shgroup_uniform_int_copy(
        pd->uv_shadow_edges_grp, "lineStyle", OVERLAY2D_LINE_STYLE_SHADOW);
    DRW_shgroup_uniform_bool_copy(pd->uv_shadow_edges_grp, "alpha", pd->uv_opacity);
    DRW_shgroup_uniform_bool(
        pd->uv_shadow_edges_grp, "doSmoothWire", &pd->wireframe.do_smooth_wire, 1);
  }
}

void OVERLAY2D_uv_shadow_cache_populate(OVERLAY2D_Data *vedata, Object *ob)
{
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;
  const DRWContextState *draw_ctx = DRW_context_state_get();

  if (ob->type == OB_MESH && ((ob->mode & draw_ctx->object_mode) != 0)) {
    struct GPUBatch *geom = DRW_mesh_batch_cache_get_uv_edges(ob->data);
    if (geom) {
      DRW_shgroup_call_obmat(pd->uv_shadow_edges_grp, geom, pd->unit_mat);
    }
  }
}

void OVERLAY2D_uv_shadow_draw_scene(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_PassList *psl = vedata->psl;
  DRW_draw_pass(psl->uv_shadow_edges);
}
