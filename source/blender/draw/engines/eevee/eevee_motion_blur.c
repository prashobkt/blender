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
 * Gather all screen space effects technique such as Bloom, Motion Blur, DoF, SSAO, SSR, ...
 */

#include "DRW_render.h"

#include "BLI_rand.h"

#include "BKE_animsys.h"
#include "BKE_camera.h"
#include "BKE_object.h"
#include "BKE_screen.h"

#include "DNA_anim_types.h"
#include "DNA_camera_types.h"
#include "DNA_screen_types.h"

#include "ED_screen.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "GPU_texture.h"
#include "eevee_private.h"

static struct {
  /* Motion Blur */
  struct GPUShader *motion_blur_sh;
  struct GPUShader *motion_blur_object_sh;
} e_data = {NULL}; /* Engine data */

extern char datatoc_effect_motion_blur_frag_glsl[];
extern char datatoc_object_motion_frag_glsl[];
extern char datatoc_object_motion_vert_glsl[];
extern char datatoc_common_view_lib_glsl[];

static void eevee_create_shader_motion_blur(void)
{
  e_data.motion_blur_sh = DRW_shader_create_fullscreen(datatoc_effect_motion_blur_frag_glsl, NULL);
  e_data.motion_blur_object_sh = DRW_shader_create_with_lib(datatoc_object_motion_vert_glsl,
                                                            NULL,
                                                            datatoc_object_motion_frag_glsl,
                                                            datatoc_common_view_lib_glsl,
                                                            NULL);
}

static void eevee_motion_blur_past_persmat_get(const CameraParams *past_params,
                                               const CameraParams *current_params,
                                               const RegionView3D *rv3d,
                                               const ARegion *region,
                                               const float (*world_to_view)[4],
                                               float (*r_world_to_ndc)[4])
{
  CameraParams params = *past_params;
  params.offsetx = current_params->offsetx;
  params.offsety = current_params->offsety;
  params.zoom = current_params->zoom;

  float zoom = BKE_screen_view3d_zoom_to_fac(rv3d->camzoom);
  params.shiftx *= zoom;
  params.shifty *= zoom;

  BKE_camera_params_compute_viewplane(&params, region->winx, region->winy, 1.0f, 1.0f);
  BKE_camera_params_compute_matrix(&params);

  mul_m4_m4m4(r_world_to_ndc, params.winmat, world_to_view);
}

int EEVEE_motion_blur_init(EEVEE_ViewLayerData *UNUSED(sldata), EEVEE_Data *vedata, Object *camera)
{
  EEVEE_StorageList *stl = vedata->stl;
  EEVEE_EffectsInfo *effects = stl->effects;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = draw_ctx->scene;

  /* Viewport support is experimental. */
  if (!DRW_state_is_scene_render() && !U.experimental.use_viewport_motion_blur) {
    return 0;
  }

  if (scene->eevee.flag & SCE_EEVEE_MOTION_BLUR_ENABLED) {
    float ctime = DEG_get_ctime(draw_ctx->depsgraph);

    if (!e_data.motion_blur_sh) {
      eevee_create_shader_motion_blur();
    }

    /* Update Motion Blur Matrices */
    if (camera && (camera->type == OB_CAMERA) && (camera->data != NULL)) {
      if (effects->current_time != ctime) {
        copy_m4_m4(effects->past_world_to_ndc, effects->current_world_to_ndc);
        copy_m4_m4(effects->past_world_to_view, effects->current_world_to_view);
        effects->past_time = effects->current_time;
        effects->past_cam_params = effects->current_cam_params;
      }
      DRW_view_viewmat_get(NULL, effects->current_world_to_view, false);
      DRW_view_persmat_get(NULL, effects->current_world_to_ndc, false);
      DRW_view_persmat_get(NULL, effects->current_ndc_to_world, true);

      if (draw_ctx->v3d) {
        CameraParams params;
        /* Save object params for next frame. */
        BKE_camera_params_init(&effects->current_cam_params);
        BKE_camera_params_from_object(&effects->current_cam_params, camera);
        /* Compute v3d params to apply on last frame object params. */
        BKE_camera_params_init(&params);
        BKE_camera_params_from_view3d(&params, draw_ctx->depsgraph, draw_ctx->v3d, draw_ctx->rv3d);

        eevee_motion_blur_past_persmat_get(&effects->past_cam_params,
                                           &params,
                                           draw_ctx->rv3d,
                                           draw_ctx->region,
                                           effects->past_world_to_view,
                                           effects->past_world_to_ndc);
      }

      effects->current_time = ctime;

      if (effects->cam_params_init == false) {
        /* Disable motion blur if not initialized. */
        copy_m4_m4(effects->past_world_to_ndc, effects->current_world_to_ndc);
        copy_m4_m4(effects->past_world_to_view, effects->current_world_to_view);
        effects->past_time = effects->current_time;
        effects->past_cam_params = effects->current_cam_params;
        effects->cam_params_init = true;
      }
    }
    else {
      /* Make no camera motion blur by using the same matrix for previous and current transform. */
      DRW_view_persmat_get(NULL, effects->past_world_to_ndc, false);
      DRW_view_persmat_get(NULL, effects->current_world_to_ndc, false);
      DRW_view_persmat_get(NULL, effects->current_ndc_to_world, true);
      effects->past_time = effects->current_time = ctime;
      effects->cam_params_init = false;
    }
    return EFFECT_MOTION_BLUR | EFFECT_POST_BUFFER | EFFECT_VELOCITY_BUFFER;
  }
  return 0;
}

void EEVEE_motion_blur_cache_init(EEVEE_ViewLayerData *UNUSED(sldata), EEVEE_Data *vedata)
{
  EEVEE_PassList *psl = vedata->psl;
  EEVEE_StorageList *stl = vedata->stl;
  EEVEE_EffectsInfo *effects = stl->effects;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = draw_ctx->scene;

  if ((effects->enabled_effects & EFFECT_MOTION_BLUR) != 0) {
    {
      DRW_PASS_CREATE(psl->motion_blur, DRW_STATE_WRITE_COLOR);

      DRWShadingGroup *grp = DRW_shgroup_create(e_data.motion_blur_sh, psl->motion_blur);
      DRW_shgroup_uniform_int_copy(grp, "samples", scene->eevee.motion_blur_samples);
      DRW_shgroup_uniform_float_copy(grp, "shutter", scene->eevee.motion_blur_shutter);
      DRW_shgroup_uniform_float(grp, "sampleOffset", &effects->motion_blur_sample_offset, 1);
      DRW_shgroup_uniform_mat4(grp, "currInvViewProjMatrix", effects->current_ndc_to_world);
      DRW_shgroup_uniform_mat4(grp, "pastViewProjMatrix", effects->past_world_to_ndc);
      DRW_shgroup_uniform_texture_ref(grp, "colorBuffer", &effects->source_buffer);
      DRW_shgroup_uniform_texture_ref(grp, "velocityBuffer", &effects->velocity_tx);
      DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
    }
    {
      float delta_time = fabsf(effects->current_time - effects->past_time);

      DRW_PASS_CREATE(psl->velocity_object, DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_EQUAL);

      DRWShadingGroup *grp = DRW_shgroup_create(e_data.motion_blur_object_sh,
                                                psl->velocity_object);

      DRW_shgroup_uniform_mat4(grp, "prevViewProjectionMatrix", effects->past_world_to_ndc);
      DRW_shgroup_uniform_mat4(grp, "currViewProjectionMatrix", effects->current_world_to_ndc);
      DRW_shgroup_uniform_float_copy(grp, "deltaTimeInv", 1.0f / delta_time);
    }
  }
  else {
    psl->motion_blur = NULL;
    psl->velocity_object = NULL;
  }
}

void EEVEE_motion_blur_cache_populate(EEVEE_Data *vedata,
                                      Object *ob,
                                      EEVEE_ObjectEngineData *oedata,
                                      struct GPUBatch *geom)
{
  EEVEE_PassList *psl = vedata->psl;
  EEVEE_StorageList *stl = vedata->stl;
  EEVEE_EffectsInfo *effects = stl->effects;
  DRWShadingGroup *grp = NULL;

  if ((effects->enabled_effects & EFFECT_MOTION_BLUR) && oedata) {
    if (oedata->curr_time != effects->current_time) {
      copy_m4_m4(oedata->prev_matrix, oedata->curr_matrix);
      oedata->prev_time = oedata->curr_time;
    }
    copy_m4_m4(oedata->curr_matrix, ob->obmat);
    oedata->curr_time = effects->current_time;

    if (oedata->motion_mats_init == false) {
      /* Disable motion blur if not initialized. */
      copy_m4_m4(oedata->prev_matrix, oedata->curr_matrix);
      oedata->prev_time = oedata->curr_time;
      oedata->motion_mats_init = true;
    }

    float delta_time = oedata->curr_time - oedata->prev_time;
    if (delta_time != 0.0f) {
      grp = DRW_shgroup_create(e_data.motion_blur_object_sh, psl->velocity_object);
      DRW_shgroup_uniform_mat4(grp, "prevModelMatrix", oedata->prev_matrix);
      DRW_shgroup_uniform_mat4(grp, "currModelMatrix", oedata->curr_matrix);
      DRW_shgroup_uniform_float_copy(grp, "deltaTimeInv", 1.0f / delta_time);
      DRW_shgroup_call(grp, geom, ob);
    }
  }
  else if (oedata) {
    oedata->motion_mats_init = false;
  }
}

void EEVEE_motion_blur_draw(EEVEE_Data *vedata)
{
  EEVEE_PassList *psl = vedata->psl;
  EEVEE_TextureList *txl = vedata->txl;
  EEVEE_FramebufferList *fbl = vedata->fbl;
  EEVEE_StorageList *stl = vedata->stl;
  EEVEE_EffectsInfo *effects = stl->effects;

  /* Motion Blur */
  if ((effects->enabled_effects & EFFECT_MOTION_BLUR) != 0) {
    int sample = DRW_state_is_image_render() ? effects->taa_render_sample :
                                               effects->taa_current_sample;
    double r;
    BLI_halton_1d(2, 0.0, sample - 1, &r);
    effects->motion_blur_sample_offset = r;

    GPU_framebuffer_bind(effects->target_buffer);
    DRW_draw_pass(psl->motion_blur);
    SWAP_BUFFERS();
  }
}

void EEVEE_motion_blur_free(void)
{
  DRW_SHADER_FREE_SAFE(e_data.motion_blur_sh);
  DRW_SHADER_FREE_SAFE(e_data.motion_blur_object_sh);
}
