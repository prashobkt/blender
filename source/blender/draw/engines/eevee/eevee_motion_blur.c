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
#include "DNA_mesh_types.h"
#include "DNA_screen_types.h"

#include "ED_screen.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "GPU_batch.h"
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

  /* Viewport not supported for now. */
  if (!DRW_state_is_scene_render()) {
    return 0;
  }

  effects->motion_blur_step = 0;

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

void EEVEE_motion_blur_step_set(EEVEE_Data *vedata, int step)
{
  /* This might do more things in the future. */
  BLI_assert(step < MAX_MB_DATA_STEP);
  vedata->stl->effects->motion_blur_step = step;
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

    EEVEE_motion_blur_data_init(&effects->motion_blur);
  }
  else {
    psl->motion_blur = NULL;
    psl->velocity_object = NULL;
  }
}

void EEVEE_motion_blur_cache_populate(EEVEE_ViewLayerData *UNUSED(sldata),
                                      EEVEE_Data *vedata,
                                      Object *ob)
{
  EEVEE_PassList *psl = vedata->psl;
  EEVEE_StorageList *stl = vedata->stl;
  EEVEE_EffectsInfo *effects = stl->effects;
  DRWShadingGroup *grp = NULL;

  EEVEE_ObjectMotionData *mb_data = EEVEE_motion_blur_object_data_get(&effects->motion_blur, ob);

  if (mb_data) {
    int mb_step = effects->motion_blur_step;
    /* Store transform  */
    copy_m4_m4(mb_data->obmat[mb_step], ob->obmat);

    EEVEE_GeometryMotionData *mb_geom = EEVEE_motion_blur_geometry_data_get(&effects->motion_blur,
                                                                            ob);

    if (effects->motion_blur_step == MAX_MB_DATA_STEP - 1) {
      GPUBatch *batch = DRW_cache_object_surface_get(ob);
      if (batch == NULL || mb_geom->vbo[mb_step]) {
        return;
      }

      grp = DRW_shgroup_create(e_data.motion_blur_object_sh, psl->velocity_object);
      DRW_shgroup_uniform_mat4(grp, "prevModelMatrix", mb_data->obmat[0]);
      DRW_shgroup_uniform_mat4(grp, "currModelMatrix", mb_data->obmat[1]);
      DRW_shgroup_uniform_bool(grp, "useDeform", &mb_geom->use_deform, 1);

      DRW_shgroup_call(grp, batch, ob);
      /* Keep to modify later (after init). */
      mb_geom->batch = batch;
    }
    else {
      /* Store vertex position buffer. */
      mb_geom->vbo[mb_step] = DRW_cache_object_pos_vertbuf_get(ob);
      /* TODO(fclem) only limit deform motion blur to object that needs it. */
      mb_geom->use_deform = true;
    }
  }
}

void EEVEE_motion_blur_cache_finish(EEVEE_Data *vedata)
{
  EEVEE_StorageList *stl = vedata->stl;
  EEVEE_EffectsInfo *effects = stl->effects;
  GHashIterator ghi;
  for (BLI_ghashIterator_init(&ghi, effects->motion_blur.geom);
       BLI_ghashIterator_done(&ghi) == false;
       BLI_ghashIterator_step(&ghi)) {
    EEVEE_GeometryMotionData *mb_geom = BLI_ghashIterator_getValue(&ghi);

    int mb_step = effects->motion_blur_step;

    if (effects->motion_blur_step == MAX_MB_DATA_STEP - 1) {
      /* Modify batch to have data from adjacent frames. */
      GPUBatch *batch = mb_geom->batch;
      GPUVertBuf *vbo = mb_geom->vbo[0];
      if (vbo && batch) {
        if (vbo->vertex_len != batch->verts[0]->vertex_len) {
          /* Vertex count mismatch, disable deform motion blur. */
          mb_geom->use_deform = false;
          GPU_VERTBUF_DISCARD_SAFE(mb_geom->vbo[0]);
          return;
        }
        /* Modify the batch to include the previous position. */
        GPU_batch_vertbuf_add_ex(batch, vbo, true);
        /* TODO(fclem) keep the vbo around for next (sub)frames. */
        /* Only do once. */
        mb_geom->vbo[0] = NULL;
      }
    }
    else {
      GPUVertBuf *vbo = mb_geom->vbo[mb_step];
      /* Use the vbo to perform the copy on the GPU. */
      GPU_vertbuf_use(vbo);
      /* Perform a copy to avoid loosing it after RE_engine_frame_set(). */
      mb_geom->vbo[mb_step] = vbo = GPU_vertbuf_duplicate(vbo);
      /* Find and replace "pos" attrib name. */
      int attrib_id = GPU_vertformat_attr_id_get(&vbo->format, "pos");
      GPU_vertformat_attr_rename(&vbo->format, attrib_id, "prv");
    }
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
