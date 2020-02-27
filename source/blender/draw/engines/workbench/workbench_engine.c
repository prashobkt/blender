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
 * Copyright 2016, Blender Foundation.
 */

/** \file
 * \ingroup draw_engine
 *
 * Optimized engine to draw the working viewport with solid and transparent geometry.
 */

#include "DRW_render.h"

#include "workbench_engine.h"
#include "workbench_private.h"

#define WORKBENCH_ENGINE "BLENDER_WORKBENCH"

static void workbench_engine_init(void *ved)
{
  WORKBENCH_Data *vedata = ved;
  WORKBENCH_StorageList *stl = vedata->stl;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  RegionView3D *rv3d = draw_ctx->rv3d;
  View3D *v3d = draw_ctx->v3d;
  Scene *scene = draw_ctx->scene;
  Object *camera;

  workbench_shader_library_ensure();

  if (v3d && rv3d) {
    camera = (rv3d->persp == RV3D_CAMOB) ? v3d->camera : NULL;
  }
  else {
    camera = scene->camera;
  }

  if (!stl->wpd) {
    stl->wpd = MEM_callocN(sizeof(*stl->wpd), __func__);
  }

  if (!stl->effects) {
    stl->effects = MEM_callocN(sizeof(*stl->effects), __func__);
    workbench_effect_info_init(stl->effects);
  }

  WORKBENCH_PrivateData *wpd = stl->wpd;
  workbench_private_data_init(wpd);

  workbench_opaque_engine_init(vedata);
  //   workbench_volume_engine_init();
  //   workbench_fxaa_engine_init();
  //   workbench_taa_engine_init(vedata);
  //   workbench_dof_engine_init(vedata, camera);
}

static void workbench_cache_init(void *ved)
{
  WORKBENCH_Data *vedata = ved;

  workbench_opaque_cache_init(vedata);

  return;

  //   workbench_aa_create_pass(vedata);
  //   workbench_dof_create_pass(vedata);
}

static void workbench_cache_populate(void *ved, Object *ob)
{
  WORKBENCH_Data *vedata = ved;
  WORKBENCH_PassList *psl = vedata->psl;
  WORKBENCH_StorageList *stl = vedata->stl;
  WORKBENCH_PrivateData *wpd = stl->wpd;

  if (!DRW_object_is_renderable(ob)) {
    return;
  }

  //   if (ob->type == OB_MESH) {
  //     workbench_cache_populate_particles(vedata, ob);
  //   }

  /* TODO volume */

  //   if (!(DRW_object_visibility_in_active_context(ob) & OB_VISIBLE_SELF)) {
  //     return;
  //   }

  if ((ob->dt < OB_SOLID) && !DRW_state_is_scene_render()) {
    return;
  }

  if (ELEM(ob->type, OB_MESH, OB_CURVE, OB_SURF, OB_FONT, OB_MBALL)) {
    struct GPUBatch *geom = DRW_cache_object_surface_get(ob);

    if (geom) {
      DRW_shgroup_call(wpd->prepass_shgrp, geom, ob);
    }
  }
}

static void workbench_cache_finish(void *ved)
{
}

static void workbench_draw_scene(void *ved)
{
  WORKBENCH_Data *vedata = ved;
  WORKBENCH_FramebufferList *fbl = vedata->fbl;
  WORKBENCH_PassList *psl = vedata->psl;
  float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  GPU_framebuffer_bind(fbl->prepass_fb);
  DRW_draw_pass(psl->prepass_pass);

  GPU_framebuffer_bind(fbl->composite_fb);
  GPU_framebuffer_clear_color(fbl->composite_fb, clear_col);

  DRW_draw_pass(psl->composite_pass);
}

static void workbench_engine_free(void)
{
  workbench_shader_free();
}

static void workbench_view_update(void *ved)
{
}

static const DrawEngineDataSize workbench_data_size = DRW_VIEWPORT_DATA_SIZE(WORKBENCH_Data);

DrawEngineType draw_engine_workbench = {
    NULL,
    NULL,
    N_("Workbench"),
    &workbench_data_size,
    &workbench_engine_init,
    &workbench_engine_free,
    &workbench_cache_init,
    &workbench_cache_populate,
    &workbench_cache_finish,
    &workbench_draw_scene,
    &workbench_view_update,
    NULL,
    NULL,
};

RenderEngineType DRW_engine_viewport_workbench_type = {
    NULL,
    NULL,
    WORKBENCH_ENGINE,
    N_("Workbench"),
    RE_INTERNAL,
    NULL,
    &DRW_render_to_image,
    NULL,
    NULL,
    NULL,
    NULL,
    &workbench_render_update_passes,
    &draw_engine_workbench,
    {NULL, NULL, NULL},
};

#undef WORKBENCH_ENGINE
