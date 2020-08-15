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

void OVERLAY2D_uv_engine_init(OVERLAY2D_Data *vedata)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  const SpaceImage *sima = (const SpaceImage *)draw_ctx->space_data;
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;
  const Scene *scene = draw_ctx->scene;
  const ToolSettings *ts = scene->toolsettings;

  const bool do_faces = ((sima->flag & SI_NO_DRAWFACES) == 0);
  const bool do_face_dots = (ts->uv_flag & UV_SYNC_SELECTION) ?
                                (ts->selectmode & SCE_SELECT_FACE) != 0 :
                                (ts->uv_selectmode == UV_SELECT_FACE);
  pd->uv.do_faces = do_faces;
  pd->uv.do_face_dots = do_faces && do_face_dots;
}

void OVERLAY2D_uv_cache_init(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_PassList *psl = vedata->psl;
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;

  /* uv verts */
  {
    DRW_PASS_CREATE(psl->uv_verts,
                    DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_ALWAYS | DRW_STATE_BLEND_ALPHA);
    GPUShader *sh = OVERLAY2D_shaders_uv_verts_get();
    pd->uv_verts_grp = DRW_shgroup_create(sh, psl->uv_verts);

    const float point_size = UI_GetThemeValuef(TH_FACEDOT_SIZE);

    DRW_shgroup_uniform_block(pd->uv_verts_grp, "globalsBlock", G_draw.block_ubo);
    DRW_shgroup_uniform_float_copy(pd->uv_verts_grp, "pointSize", (point_size + 1.5f) * M_SQRT2);
    DRW_shgroup_uniform_float_copy(pd->uv_verts_grp, "outlineWidth", 0.75f);
  }

  /* uv edges */
  {
    DRW_PASS_CREATE(psl->uv_edges,
                    DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_ALWAYS | DRW_STATE_BLEND_ALPHA);
    GPUShader *sh = OVERLAY2D_shaders_wireframe_get();
    pd->uv_edges_grp = DRW_shgroup_create(sh, psl->uv_edges);
    DRW_shgroup_uniform_block(pd->uv_edges_grp, "globalsBlock", G_draw.block_ubo);
    DRW_shgroup_uniform_int_copy(pd->uv_edges_grp, "lineStyle", pd->wireframe.line_style);
    DRW_shgroup_uniform_float_copy(pd->uv_edges_grp, "alpha", pd->uv_opacity);
    DRW_shgroup_uniform_float(pd->uv_edges_grp, "dashLength", &pd->wireframe.dash_length, 1);
    DRW_shgroup_uniform_bool(pd->uv_edges_grp, "doSmoothWire", &pd->wireframe.do_smooth_wire, 1);
  }

  /* uv faces */
  if (pd->uv.do_faces) {
    DRW_PASS_CREATE(psl->uv_faces,
                    DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_ALWAYS | DRW_STATE_BLEND_ALPHA);
    GPUShader *sh = OVERLAY2D_shaders_uv_face_get();
    pd->uv_faces_grp = DRW_shgroup_create(sh, psl->uv_faces);
    DRW_shgroup_uniform_block(pd->uv_faces_grp, "globalsBlock", G_draw.block_ubo);
    DRW_shgroup_uniform_float(pd->uv_faces_grp, "uvOpacity", &pd->uv_opacity, 1);
  }

  /* uv face dots */
  if (pd->uv.do_face_dots) {
    const float point_size = UI_GetThemeValuef(TH_FACEDOT_SIZE);
    GPUShader *sh = OVERLAY2D_shaders_uv_face_dots_get();
    pd->uv_face_dots_grp = DRW_shgroup_create(sh, psl->uv_verts);
    DRW_shgroup_uniform_block(pd->uv_face_dots_grp, "globalsBlock", G_draw.block_ubo);
    DRW_shgroup_uniform_float_copy(pd->uv_face_dots_grp, "pointSize", point_size);
  }
}

void OVERLAY2D_uv_cache_populate(OVERLAY2D_Data *vedata, Object *ob)
{
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;
  struct GPUBatch *geom;

  geom = DRW_mesh_batch_cache_get_edituv_edges(ob->data);
  if (geom) {
    DRW_shgroup_call_obmat(pd->uv_edges_grp, geom, pd->unit_mat);
  }
  geom = DRW_mesh_batch_cache_get_edituv_verts(ob->data);
  if (geom) {
    DRW_shgroup_call_obmat(pd->uv_verts_grp, geom, pd->unit_mat);
  }

  if (pd->uv.do_faces) {
    geom = DRW_mesh_batch_cache_get_edituv_faces(ob->data);
    if (geom) {
      DRW_shgroup_call_obmat(pd->uv_faces_grp, geom, pd->unit_mat);
    }
  }
  if (pd->uv.do_face_dots) {
    geom = DRW_mesh_batch_cache_get_edituv_facedots(ob->data);
    if (geom) {
      DRW_shgroup_call_obmat(pd->uv_face_dots_grp, geom, pd->unit_mat);
    }
  }
}

void OVERLAY2D_uv_draw_scene_faces(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_PassList *psl = vedata->psl;
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;

  if (pd->uv.do_faces) {
    DRW_draw_pass(psl->uv_faces);
  }
}

void OVERLAY2D_uv_draw_scene_edges_and_verts(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_PassList *psl = vedata->psl;
  DRW_draw_pass(psl->uv_edges);
  DRW_draw_pass(psl->uv_verts);
}
