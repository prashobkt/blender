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
 * Copyright 2018, Blender Foundation.
 */

/** \file
 * \ingroup draw_engine
 */

#include "workbench_private.h"

#include "BLI_memblock.h"

#include "DNA_userdef_types.h"

#include "ED_view3d.h"

#include "UI_resources.h"

#include "GPU_uniformbuffer.h"

/* -------------------------------------------------------------------- */
/** \name World Data
 * \{ */

GPUUniformBuffer *workbench_material_ubo_alloc(WORKBENCH_PrivateData *wpd)
{
  struct GPUUniformBuffer **ubo = BLI_memblock_alloc(wpd->material_ubo);
  if (*ubo == NULL) {
    *ubo = GPU_uniformbuffer_create(sizeof(WORKBENCH_UBO_Material) * MAX_MATERIAL, NULL, NULL);
  }
  return *ubo;
}

static void workbench_ubo_free(void *elem)
{
  GPUUniformBuffer **ubo = elem;
  DRW_UBO_FREE_SAFE(*ubo);
}

static void workbench_view_layer_data_free(void *storage)
{
  WORKBENCH_ViewLayerData *vldata = (WORKBENCH_ViewLayerData *)storage;

  DRW_UBO_FREE_SAFE(vldata->world_ubo);

  BLI_memblock_destroy(vldata->material_ubo_data, NULL);
  BLI_memblock_destroy(vldata->material_ubo, workbench_ubo_free);
}

static WORKBENCH_ViewLayerData *workbench_view_layer_data_ensure_ex(struct ViewLayer *view_layer)
{
  WORKBENCH_ViewLayerData **vldata = (WORKBENCH_ViewLayerData **)
      DRW_view_layer_engine_data_ensure_ex(view_layer,
                                           (DrawEngineType *)&workbench_view_layer_data_ensure_ex,
                                           &workbench_view_layer_data_free);

  if (*vldata == NULL) {
    *vldata = MEM_callocN(sizeof(**vldata), "WORKBENCH_ViewLayerData");
    size_t matbuf_size = sizeof(WORKBENCH_UBO_Material) * MAX_MATERIAL;
    (*vldata)->material_ubo_data = BLI_memblock_create_ex(matbuf_size, matbuf_size * 2);
    (*vldata)->material_ubo = BLI_memblock_create_ex(sizeof(void *), sizeof(void *) * 8);
    (*vldata)->world_ubo = DRW_uniformbuffer_create(sizeof(WORKBENCH_UBO_World), NULL);
  }

  return *vldata;
}

static void workbench_world_data_update_shadow_direction_vs(WORKBENCH_PrivateData *wpd)
{
  WORKBENCH_UBO_World *wd = &wpd->world_data;
  float light_direction[3];
  float view_matrix[4][4];
  DRW_view_viewmat_get(NULL, view_matrix, false);

  workbench_private_data_get_light_direction(light_direction);

  /* Shadow direction. */
  mul_v3_mat3_m4v3(wd->shadow_direction_vs, view_matrix, light_direction);
}

/* \} */

static void workbench_viewvecs_update(float r_viewvecs[3][4])
{
  float invproj[4][4];
  const bool is_persp = DRW_view_is_persp_get(NULL);
  DRW_view_winmat_get(NULL, invproj, true);

  /* view vectors for the corners of the view frustum.
   * Can be used to recreate the world space position easily */
  copy_v4_fl4(r_viewvecs[0], -1.0f, -1.0f, -1.0f, 1.0f);
  copy_v4_fl4(r_viewvecs[1], 1.0f, -1.0f, -1.0f, 1.0f);
  copy_v4_fl4(r_viewvecs[2], -1.0f, 1.0f, -1.0f, 1.0f);

  /* convert the view vectors to view space */
  for (int i = 0; i < 3; i++) {
    mul_m4_v4(invproj, r_viewvecs[i]);
    /* normalized trick see:
     * http://www.derschmale.com/2014/01/26/reconstructing-positions-from-the-depth-buffer */
    mul_v3_fl(r_viewvecs[i], 1.0f / r_viewvecs[i][3]);
    if (is_persp) {
      mul_v3_fl(r_viewvecs[i], 1.0f / r_viewvecs[i][2]);
    }
    r_viewvecs[i][3] = 1.0;
  }

  /* we need to store the differences */
  r_viewvecs[1][0] -= r_viewvecs[0][0];
  r_viewvecs[1][1] = r_viewvecs[2][1] - r_viewvecs[0][1];

  /* calculate a depth offset as well */
  if (!is_persp) {
    float vec_far[] = {-1.0f, -1.0f, 1.0f, 1.0f};
    mul_m4_v4(invproj, vec_far);
    mul_v3_fl(vec_far, 1.0f / vec_far[3]);
    r_viewvecs[1][2] = vec_far[2] - r_viewvecs[0][2];
  }
}

void workbench_clear_color_get(float color[4])
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  const Scene *scene = draw_ctx->scene;

  if (!DRW_state_is_scene_render() || !DRW_state_draw_background()) {
    zero_v4(color);
  }
  else if (scene->world) {
    copy_v3_v3(color, &scene->world->horr);
    color[3] = 1.0f;
  }
  else {
    zero_v3(color);
    color[3] = 1.0f;
  }
}

void workbench_effect_info_init(WORKBENCH_EffectInfo *effect_info)
{
  effect_info->jitter_index = 0;
  effect_info->view_updated = true;
}

void workbench_private_data_init(WORKBENCH_PrivateData *wpd)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  const Scene *scene = draw_ctx->scene;
  WORKBENCH_ViewLayerData *vldata = workbench_view_layer_data_ensure_ex(draw_ctx->view_layer);
  View3D *v3d = draw_ctx->v3d;

  wpd->ctx_mode = CTX_data_mode_enum_ex(
      draw_ctx->object_edit, draw_ctx->obact, draw_ctx->object_mode);

  wpd->preferences = &U;
  wpd->sh_cfg = draw_ctx->sh_cfg;

  wpd->world_ubo = vldata->world_ubo;

  wpd->material_ubo_data = vldata->material_ubo_data;
  wpd->material_ubo = vldata->material_ubo;
  wpd->material_chunk_count = 1;
  wpd->material_chunk_curr = 0;
  wpd->material_index = 1;
  /* Create default material ubo. */
  wpd->material_ubo_data_curr = BLI_memblock_alloc(wpd->material_ubo_data);
  wpd->material_ubo_curr = workbench_material_ubo_alloc(wpd);

  if (!v3d || (v3d->shading.type == OB_RENDER && BKE_scene_uses_blender_workbench(scene))) {
    wpd->shading = scene->display.shading;
    wpd->shading.xray_alpha = XRAY_ALPHA((&scene->display));
  }
  else {
    wpd->shading = v3d->shading;
    if (XRAY_ENABLED(v3d)) {
      wpd->shading.xray_alpha = XRAY_ALPHA(v3d);
    }
    else {
      wpd->shading.xray_alpha = 1.0f;
    }
  }

  if (wpd->shading.light == V3D_LIGHTING_MATCAP) {
    wpd->studio_light = BKE_studiolight_find(wpd->shading.matcap, STUDIOLIGHT_TYPE_MATCAP);
  }
  else {
    wpd->studio_light = BKE_studiolight_find(wpd->shading.studio_light, STUDIOLIGHT_TYPE_STUDIO);
  }

  /* If matcaps are missing, use this as fallback. */
  if (UNLIKELY(wpd->studio_light == NULL)) {
    wpd->studio_light = BKE_studiolight_find(wpd->shading.studio_light, STUDIOLIGHT_TYPE_STUDIO);
  }

  float shadow_focus = scene->display.shadow_focus;
  /* Clamp to avoid overshadowing and shading errors. */
  CLAMP(shadow_focus, 0.0001f, 0.99999f);
  wpd->shadow_shift = scene->display.shadow_shift;
  wpd->shadow_focus = 1.0f - shadow_focus * (1.0f - wpd->shadow_shift);
  wpd->shadow_multiplier = 1.0 - wpd->shading.shadow_intensity;

  WORKBENCH_UBO_World *wd = &wpd->world_data;
  wd->matcap_orientation = (wpd->shading.flag & V3D_SHADING_MATCAP_FLIP_X) != 0;

  studiolight_update_world(wpd, wpd->studio_light, wd);

  /* Init default material used by vertex color & texture. */
  workbench_material_ubo_data(
      wpd, NULL, NULL, &wpd->material_ubo_data_curr[0], V3D_SHADING_MATERIAL_COLOR);

  copy_v3_v3(wd->object_outline_color, wpd->shading.object_outline_color);
  wd->object_outline_color[3] = 1.0f;

  wd->curvature_ridge = 0.5f / max_ff(SQUARE(wpd->shading.curvature_ridge_factor), 1e-4f);
  wd->curvature_valley = 0.7f / max_ff(SQUARE(wpd->shading.curvature_valley_factor), 1e-4f);

  workbench_world_data_update_shadow_direction_vs(wpd);
  workbench_viewvecs_update(wpd->world_data.viewvecs);
  copy_v2_v2(wpd->world_data.viewport_size, DRW_viewport_size_get());
  copy_v2_v2(wpd->world_data.viewport_size_inv, DRW_viewport_invert_size_get());

  DRW_uniformbuffer_update(wpd->world_ubo, &wpd->world_data);

  /* Cavity settings */
  {
    const int ssao_samples = scene->display.matcap_ssao_samples;

    float invproj[4][4];
    const bool is_persp = DRW_view_is_persp_get(NULL);
    /* view vectors for the corners of the view frustum.
     * Can be used to recreate the world space position easily */
    float viewvecs[3][4] = {
        {-1.0f, -1.0f, -1.0f, 1.0f},
        {1.0f, -1.0f, -1.0f, 1.0f},
        {-1.0f, 1.0f, -1.0f, 1.0f},
    };
    int i;
    const float *size = DRW_viewport_size_get();

    wpd->ssao_params[0] = ssao_samples;
    wpd->ssao_params[1] = size[0] / 64.0;
    wpd->ssao_params[2] = size[1] / 64.0;
    wpd->ssao_params[3] = 0;

    /* distance, factor, factor, attenuation */
    copy_v4_fl4(wpd->ssao_settings,
                scene->display.matcap_ssao_distance,
                wpd->shading.cavity_valley_factor,
                wpd->shading.cavity_ridge_factor,
                scene->display.matcap_ssao_attenuation);

    DRW_view_winmat_get(NULL, wpd->winmat, false);
    DRW_view_winmat_get(NULL, invproj, true);

    /* convert the view vectors to view space */
    for (i = 0; i < 3; i++) {
      mul_m4_v4(invproj, viewvecs[i]);
      /* normalized trick see:
       * http://www.derschmale.com/2014/01/26/reconstructing-positions-from-the-depth-buffer */
      mul_v3_fl(viewvecs[i], 1.0f / viewvecs[i][3]);
      if (is_persp) {
        mul_v3_fl(viewvecs[i], 1.0f / viewvecs[i][2]);
      }
      viewvecs[i][3] = 1.0;

      copy_v4_v4(wpd->viewvecs[i], viewvecs[i]);
    }

    /* we need to store the differences */
    wpd->viewvecs[1][0] -= wpd->viewvecs[0][0];
    wpd->viewvecs[1][1] = wpd->viewvecs[2][1] - wpd->viewvecs[0][1];

    /* calculate a depth offset as well */
    if (!is_persp) {
      float vec_far[] = {-1.0f, -1.0f, 1.0f, 1.0f};
      mul_m4_v4(invproj, vec_far);
      mul_v3_fl(vec_far, 1.0f / vec_far[3]);
      wpd->viewvecs[1][2] = vec_far[2] - wpd->viewvecs[0][2];
    }
  }

  wpd->volumes_do = false;
  BLI_listbase_clear(&wpd->smoke_domains);
}

void workbench_private_data_get_light_direction(float r_light_direction[3])
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = draw_ctx->scene;

  copy_v3_v3(r_light_direction, scene->display.light_direction);
  SWAP(float, r_light_direction[2], r_light_direction[1]);
  r_light_direction[2] = -r_light_direction[2];
  r_light_direction[0] = -r_light_direction[0];
}

void workbench_update_material_ubos(WORKBENCH_PrivateData *UNUSED(wpd))
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  WORKBENCH_ViewLayerData *vldata = workbench_view_layer_data_ensure_ex(draw_ctx->view_layer);

  BLI_memblock_iter iter, iter_data;
  BLI_memblock_iternew(vldata->material_ubo, &iter);
  BLI_memblock_iternew(vldata->material_ubo_data, &iter_data);
  WORKBENCH_UBO_Material *matchunk;
  while ((matchunk = BLI_memblock_iterstep(&iter_data))) {
    GPUUniformBuffer **ubo = BLI_memblock_iterstep(&iter);
    BLI_assert(*ubo != NULL);
    GPU_uniformbuffer_update(*ubo, matchunk);
  }

  BLI_memblock_clear(vldata->material_ubo, workbench_ubo_free);
  BLI_memblock_clear(vldata->material_ubo_data, NULL);
}

void workbench_private_data_free(WORKBENCH_PrivateData *wpd)
{
  if (wpd->is_world_ubo_owner) {
    DRW_UBO_FREE_SAFE(wpd->world_ubo);
  }
  else {
    wpd->world_ubo = NULL;
  }

  DRW_UBO_FREE_SAFE(wpd->dof_ubo);
}
