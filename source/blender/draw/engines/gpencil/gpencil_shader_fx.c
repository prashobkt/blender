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
 * Copyright 2017, Blender Foundation.
 */

/** \file
 * \ingroup draw
 */
#include "DNA_gpencil_types.h"
#include "DNA_shader_fx_types.h"
#include "DNA_view3d_types.h"
#include "DNA_camera_types.h"

#include "BKE_gpencil.h"

#include "BLI_link_utils.h"
#include "BLI_memblock.h"

#include "DRW_render.h"

#include "BKE_camera.h"

#include "gpencil_engine.h"

extern char datatoc_gpencil_fx_blur_frag_glsl[];
extern char datatoc_gpencil_fx_colorize_frag_glsl[];
extern char datatoc_gpencil_fx_flip_frag_glsl[];
extern char datatoc_gpencil_fx_pixel_frag_glsl[];
extern char datatoc_gpencil_fx_rim_prepare_frag_glsl[];
extern char datatoc_gpencil_fx_rim_resolve_frag_glsl[];
extern char datatoc_gpencil_fx_shadow_prepare_frag_glsl[];
extern char datatoc_gpencil_fx_shadow_resolve_frag_glsl[];
extern char datatoc_gpencil_fx_glow_prepare_frag_glsl[];
extern char datatoc_gpencil_fx_glow_resolve_frag_glsl[];
extern char datatoc_gpencil_fx_swirl_frag_glsl[];
extern char datatoc_gpencil_fx_wave_frag_glsl[];

/* verify if this fx is active */
static bool effect_is_active(bGPdata *gpd, ShaderFxData *fx, bool is_render)
{
  if (fx == NULL) {
    return false;
  }

  if (gpd == NULL) {
    return false;
  }

  bool is_edit = GPENCIL_ANY_EDIT_MODE(gpd);
  if (((fx->mode & eShaderFxMode_Editmode) == 0) && (is_edit) && (!is_render)) {
    return false;
  }

  if (((fx->mode & eShaderFxMode_Realtime) && (is_render == false)) ||
      ((fx->mode & eShaderFxMode_Render) && (is_render == true))) {
    return true;
  }

  return false;
}

/**
 * Get normal of draw using one stroke of visible layer
 * \param gpd: GP datablock
 * \param r_point: Point on plane
 * \param r_normal: Normal vector
 */
static bool get_normal_vector(bGPdata *gpd, float r_point[3], float r_normal[3])
{
  for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
    if (gpl->flag & GP_LAYER_HIDE) {
      continue;
    }

    /* get frame  */
    bGPDframe *gpf = gpl->actframe;
    if (gpf == NULL) {
      continue;
    }
    for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
      if (gps->totpoints >= 3) {
        bGPDspoint *pt = &gps->points[0];
        BKE_gpencil_stroke_normal(gps, r_normal);
        /* in some weird situations, the normal cannot be calculated, so try next stroke */
        if ((r_normal[0] != 0.0f) || (r_normal[1] != 0.0f) || (r_normal[2] != 0.0f)) {
          copy_v3_v3(r_point, &pt->x);
          return true;
        }
      }
    }
  }

  return false;
}

/* helper to get near and far depth of field values */
static void GPENCIL_dof_nearfar(Object *camera, float coc, float nearfar[2])
{
  if (camera == NULL) {
    return;
  }
  Camera *cam = camera->data;

  float fstop = cam->dof.aperture_fstop;
  float focus_dist = BKE_camera_object_dof_distance(camera);
  float focal_len = cam->lens;

  const float scale_camera = 0.001f;
  /* we want radius here for the aperture number  */
  float aperture_scaled = 0.5f * scale_camera * focal_len / fstop;
  float focal_len_scaled = scale_camera * focal_len;

  float hyperfocal = (focal_len_scaled * focal_len_scaled) / (aperture_scaled * coc);
  nearfar[0] = (hyperfocal * focus_dist) / (hyperfocal + focal_len);
  nearfar[1] = (hyperfocal * focus_dist) / (hyperfocal - focal_len);
}

/* ****************  Shader Effects ***************************** */

/* Gaussian Blur FX
 * The effect is done using two shading groups because is faster to apply horizontal
 * and vertical in different operations.
 */
static void gpencil_fx_blur(ShaderFxData *fx,
                            int ob_idx,
                            GPENCIL_e_data *e_data,
                            GPENCIL_Data *vedata,
                            tGPencilObjectCache *cache)
{
  if (fx == NULL) {
    return;
  }

  BlurShaderFxData *fxd = (BlurShaderFxData *)fx;

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  View3D *v3d = draw_ctx->v3d;
  RegionView3D *rv3d = draw_ctx->rv3d;
  DRWShadingGroup *fx_shgrp;
  bGPdata *gpd = cache->gpd;
  copy_v3_v3(fxd->runtime.loc, cache->loc);

  fxd->blur[0] = fxd->radius[0];
  fxd->blur[1] = fxd->radius[1];

  /* init weight */
  if (fxd->flag & FX_BLUR_DOF_MODE) {
    /* viewport and opengl render */
    Object *camera = NULL;
    if (rv3d) {
      if (rv3d->persp == RV3D_CAMOB) {
        camera = v3d->camera;
      }
    }
    else {
      camera = stl->storage->camera;
    }

    if (camera) {
      float nearfar[2];
      GPENCIL_dof_nearfar(camera, fxd->coc, nearfar);
      float zdepth = stl->g_data->gp_object_cache[ob_idx].zdepth;
      /* the object is on focus area */
      if ((zdepth >= nearfar[0]) && (zdepth <= nearfar[1])) {
        fxd->blur[0] = 0;
        fxd->blur[1] = 0;
      }
      else {
        float f;
        if (zdepth < nearfar[0]) {
          f = nearfar[0] - zdepth;
        }
        else {
          f = zdepth - nearfar[1];
        }
        fxd->blur[0] = f;
        fxd->blur[1] = f;
        CLAMP2(&fxd->blur[0], 0, fxd->radius[0]);
      }
    }
    else {
      /* if not camera view, the blur is disabled */
      fxd->blur[0] = 0;
      fxd->blur[1] = 0;
    }
  }

  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();

  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_blur_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_vec2(fx_shgrp, "Viewport", DRW_viewport_size_get(), 1);
  DRW_shgroup_uniform_int(fx_shgrp, "blur", &fxd->blur[0], 2);

  DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->runtime.loc, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixsize", stl->storage->pixsize, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixfactor", &gpd->pixfactor, 1);

  fxd->runtime.fx_sh = fx_shgrp;
}

/* Colorize FX */
static void gpencil_fx_colorize(ShaderFxData *fx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata)
{
  if (fx == NULL) {
    return;
  }
  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  ColorizeShaderFxData *fxd = (ColorizeShaderFxData *)fx;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  DRWShadingGroup *fx_shgrp;

  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_colorize_sh, psl->fx_shader_pass);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_vec4(fx_shgrp, "low_color", &fxd->low_color[0], 1);
  DRW_shgroup_uniform_vec4(fx_shgrp, "high_color", &fxd->high_color[0], 1);
  DRW_shgroup_uniform_int(fx_shgrp, "mode", &fxd->mode, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "factor", &fxd->factor, 1);

  fxd->runtime.fx_sh = fx_shgrp;
}

/* Flip FX */
static void gpencil_fx_flip(ShaderFxData *fx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata)
{
  if (fx == NULL) {
    return;
  }
  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  FlipShaderFxData *fxd = (FlipShaderFxData *)fx;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  DRWShadingGroup *fx_shgrp;

  fxd->flipmode = 100;
  /* the number works as bit flag */
  if (fxd->flag & FX_FLIP_HORIZONTAL) {
    fxd->flipmode += 10;
  }
  if (fxd->flag & FX_FLIP_VERTICAL) {
    fxd->flipmode += 1;
  }

  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_flip_sh, psl->fx_shader_pass);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_int(fx_shgrp, "flipmode", &fxd->flipmode, 1);

  DRW_shgroup_uniform_vec2(fx_shgrp, "wsize", DRW_viewport_size_get(), 1);

  fxd->runtime.fx_sh = fx_shgrp;
}

/* Pixelate FX */
static void gpencil_fx_pixel(ShaderFxData *fx,
                             GPENCIL_e_data *e_data,
                             GPENCIL_Data *vedata,
                             tGPencilObjectCache *cache)
{
  if (fx == NULL) {
    return;
  }
  PixelShaderFxData *fxd = (PixelShaderFxData *)fx;

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  DRWShadingGroup *fx_shgrp;
  bGPdata *gpd = cache->gpd;
  copy_v3_v3(fxd->runtime.loc, cache->loc);

  fxd->size[2] = (int)fxd->flag & FX_PIXEL_USE_LINES;

  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_pixel_sh, psl->fx_shader_pass);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_int(fx_shgrp, "size", &fxd->size[0], 3);
  DRW_shgroup_uniform_vec4(fx_shgrp, "color", &fxd->rgba[0], 1);

  DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->runtime.loc, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixsize", stl->storage->pixsize, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixfactor", &gpd->pixfactor, 1);

  fxd->runtime.fx_sh = fx_shgrp;
}

/* Rim FX */
static void gpencil_fx_rim(ShaderFxData *fx,
                           GPENCIL_e_data *e_data,
                           GPENCIL_Data *vedata,
                           tGPencilObjectCache *cache)
{
  if (fx == NULL) {
    return;
  }
  RimShaderFxData *fxd = (RimShaderFxData *)fx;
  bGPdata *gpd = cache->gpd;

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  DRWShadingGroup *fx_shgrp;

  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();
  copy_v3_v3(fxd->runtime.loc, cache->loc);

  /* prepare pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_rim_prepare_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_vec2(fx_shgrp, "Viewport", DRW_viewport_size_get(), 1);

  DRW_shgroup_uniform_int(fx_shgrp, "offset", &fxd->offset[0], 2);
  DRW_shgroup_uniform_vec3(fx_shgrp, "rim_color", &fxd->rim_rgb[0], 1);
  DRW_shgroup_uniform_vec3(fx_shgrp, "mask_color", &fxd->mask_rgb[0], 1);

  DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->runtime.loc, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixsize", stl->storage->pixsize, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixfactor", &gpd->pixfactor, 1);

  fxd->runtime.fx_sh = fx_shgrp;

  /* blur pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_blur_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_fx);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_fx);
  DRW_shgroup_uniform_vec2(fx_shgrp, "Viewport", DRW_viewport_size_get(), 1);
  DRW_shgroup_uniform_int(fx_shgrp, "blur", &fxd->blur[0], 2);

  DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->runtime.loc, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixsize", stl->storage->pixsize, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixfactor", &gpd->pixfactor, 1);

  fxd->runtime.fx_sh_b = fx_shgrp;

  /* resolve pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_rim_resolve_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeRim", &stl->g_data->temp_color_tx_fx);
  DRW_shgroup_uniform_vec3(fx_shgrp, "mask_color", &fxd->mask_rgb[0], 1);
  DRW_shgroup_uniform_int(fx_shgrp, "mode", &fxd->mode, 1);

  fxd->runtime.fx_sh_c = fx_shgrp;
}

/* Shadow FX */
static void gpencil_fx_shadow(ShaderFxData *fx,
                              GPENCIL_e_data *e_data,
                              GPENCIL_Data *vedata,
                              tGPencilObjectCache *cache)
{
  if (fx == NULL) {
    return;
  }
  ShadowShaderFxData *fxd = (ShadowShaderFxData *)fx;
  if ((!fxd->object) && (fxd->flag & FX_SHADOW_USE_OBJECT)) {
    fxd->runtime.fx_sh = NULL;
    fxd->runtime.fx_sh_b = NULL;
    fxd->runtime.fx_sh_c = NULL;
    return;
  }

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  DRWShadingGroup *fx_shgrp;
  bGPdata *gpd = cache->gpd;
  copy_v3_v3(fxd->runtime.loc, cache->loc);

  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();
  /* prepare pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_shadow_prepare_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_vec2(fx_shgrp, "Viewport", DRW_viewport_size_get(), 1);

  DRW_shgroup_uniform_int(fx_shgrp, "offset", &fxd->offset[0], 2);
  DRW_shgroup_uniform_float(fx_shgrp, "scale", &fxd->scale[0], 2);
  DRW_shgroup_uniform_float(fx_shgrp, "rotation", &fxd->rotation, 1);
  DRW_shgroup_uniform_vec4(fx_shgrp, "shadow_color", &fxd->shadow_rgba[0], 1);

  if ((fxd->object) && (fxd->flag & FX_SHADOW_USE_OBJECT)) {
    DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->object->obmat[3], 1);
  }
  else {
    DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->runtime.loc, 1);
  }

  if (fxd->flag & FX_SHADOW_USE_WAVE) {
    DRW_shgroup_uniform_int(fx_shgrp, "orientation", &fxd->orientation, 1);
  }
  else {
    DRW_shgroup_uniform_int_copy(fx_shgrp, "orientation", -1);
  }
  DRW_shgroup_uniform_float(fx_shgrp, "amplitude", &fxd->amplitude, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "period", &fxd->period, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "phase", &fxd->phase, 1);

  DRW_shgroup_uniform_float(fx_shgrp, "pixsize", stl->storage->pixsize, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixfactor", &gpd->pixfactor, 1);

  fxd->runtime.fx_sh = fx_shgrp;

  /* blur pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_blur_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_fx);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_fx);
  DRW_shgroup_uniform_vec2(fx_shgrp, "Viewport", DRW_viewport_size_get(), 1);
  DRW_shgroup_uniform_int(fx_shgrp, "blur", &fxd->blur[0], 2);

  DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->runtime.loc, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixsize", stl->storage->pixsize, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixfactor", &gpd->pixfactor, 1);

  fxd->runtime.fx_sh_b = fx_shgrp;

  /* resolve pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_shadow_resolve_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "shadowColor", &stl->g_data->temp_color_tx_fx);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "shadowDepth", &stl->g_data->temp_depth_tx_fx);

  fxd->runtime.fx_sh_c = fx_shgrp;
}

/* Glow FX */
static void gpencil_fx_glow(ShaderFxData *fx,
                            GPENCIL_e_data *e_data,
                            GPENCIL_Data *vedata,
                            tGPencilObjectCache *cache)
{
  if (fx == NULL) {
    return;
  }
  GlowShaderFxData *fxd = (GlowShaderFxData *)fx;
  bGPdata *gpd = cache->gpd;
  copy_v3_v3(fxd->runtime.loc, cache->loc);

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  DRWShadingGroup *fx_shgrp;

  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();
  /* prepare pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_glow_prepare_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);

  DRW_shgroup_uniform_vec3(fx_shgrp, "glow_color", &fxd->glow_color[0], 1);
  DRW_shgroup_uniform_vec3(fx_shgrp, "select_color", &fxd->select_color[0], 1);
  DRW_shgroup_uniform_int(fx_shgrp, "mode", &fxd->mode, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "threshold", &fxd->threshold, 1);

  fxd->runtime.fx_sh = fx_shgrp;

  /* blur pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_blur_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_fx);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_fx);
  DRW_shgroup_uniform_vec2(fx_shgrp, "Viewport", DRW_viewport_size_get(), 1);
  DRW_shgroup_uniform_int(fx_shgrp, "blur", &fxd->blur[0], 2);

  DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->runtime.loc, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixsize", stl->storage->pixsize, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixfactor", &gpd->pixfactor, 1);

  fxd->runtime.fx_sh_b = fx_shgrp;

  /* resolve pass */
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_glow_resolve_sh, psl->fx_shader_pass_blend);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "glowColor", &stl->g_data->temp_color_tx_fx);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "glowDepth", &stl->g_data->temp_depth_tx_fx);

  /* reuse field */
  DRW_shgroup_uniform_int(fx_shgrp, "alpha_mode", &fxd->blur[1], 1);

  fxd->runtime.fx_sh_c = fx_shgrp;
}

/* Swirl FX */
static void gpencil_fx_swirl(ShaderFxData *fx,
                             GPENCIL_e_data *e_data,
                             GPENCIL_Data *vedata,
                             tGPencilObjectCache *cache)
{
  if (fx == NULL) {
    return;
  }
  SwirlShaderFxData *fxd = (SwirlShaderFxData *)fx;
  if (fxd->object == NULL) {
    return;
  }

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  DRWShadingGroup *fx_shgrp;
  bGPdata *gpd = cache->gpd;

  fxd->transparent = (int)fxd->flag & FX_SWIRL_MAKE_TRANSPARENT;

  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();
  fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_swirl_sh, psl->fx_shader_pass);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);

  DRW_shgroup_uniform_vec2(fx_shgrp, "Viewport", DRW_viewport_size_get(), 1);

  DRW_shgroup_uniform_vec3(fx_shgrp, "loc", fxd->object->obmat[3], 1);

  DRW_shgroup_uniform_int(fx_shgrp, "radius", &fxd->radius, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "angle", &fxd->angle, 1);
  DRW_shgroup_uniform_int(fx_shgrp, "transparent", &fxd->transparent, 1);

  DRW_shgroup_uniform_float(fx_shgrp, "pixsize", stl->storage->pixsize, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "pixfactor", &gpd->pixfactor, 1);

  fxd->runtime.fx_sh = fx_shgrp;
}

/* Wave Distortion FX */
static void gpencil_fx_wave(ShaderFxData *fx, GPENCIL_e_data *e_data, GPENCIL_Data *vedata)
{
  if (fx == NULL) {
    return;
  }

  WaveShaderFxData *fxd = (WaveShaderFxData *)fx;

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  GPUBatch *fxquad = DRW_cache_fullscreen_quad_get();

  DRWShadingGroup *fx_shgrp = DRW_shgroup_create(e_data->gpencil_fx_wave_sh, psl->fx_shader_pass);
  DRW_shgroup_call(fx_shgrp, fxquad, NULL);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeColor", &stl->g_data->temp_color_tx_a);
  DRW_shgroup_uniform_texture_ref(fx_shgrp, "strokeDepth", &stl->g_data->temp_depth_tx_a);
  DRW_shgroup_uniform_float(fx_shgrp, "amplitude", &fxd->amplitude, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "period", &fxd->period, 1);
  DRW_shgroup_uniform_float(fx_shgrp, "phase", &fxd->phase, 1);
  DRW_shgroup_uniform_int(fx_shgrp, "orientation", &fxd->orientation, 1);
  DRW_shgroup_uniform_vec2(fx_shgrp, "wsize", DRW_viewport_size_get(), 1);

  fxd->runtime.fx_sh = fx_shgrp;
}

/* ************************************************************** */

/* create all FX shaders */
void GPENCIL_create_fx_shaders(GPENCIL_e_data *e_data)
{
  /* fx shaders (all in screen space) */
  if (!e_data->gpencil_fx_blur_sh) {
    e_data->gpencil_fx_blur_sh = DRW_shader_create_fullscreen(datatoc_gpencil_fx_blur_frag_glsl,
                                                              NULL);
  }
  if (!e_data->gpencil_fx_colorize_sh) {
    e_data->gpencil_fx_colorize_sh = DRW_shader_create_fullscreen(
        datatoc_gpencil_fx_colorize_frag_glsl, NULL);
  }
  if (!e_data->gpencil_fx_flip_sh) {
    e_data->gpencil_fx_flip_sh = DRW_shader_create_fullscreen(datatoc_gpencil_fx_flip_frag_glsl,
                                                              NULL);
  }
  if (!e_data->gpencil_fx_pixel_sh) {
    e_data->gpencil_fx_pixel_sh = DRW_shader_create_fullscreen(datatoc_gpencil_fx_pixel_frag_glsl,
                                                               NULL);
  }
  if (!e_data->gpencil_fx_rim_prepare_sh) {
    e_data->gpencil_fx_rim_prepare_sh = DRW_shader_create_fullscreen(
        datatoc_gpencil_fx_rim_prepare_frag_glsl, NULL);

    e_data->gpencil_fx_rim_resolve_sh = DRW_shader_create_fullscreen(
        datatoc_gpencil_fx_rim_resolve_frag_glsl, NULL);
  }
  if (!e_data->gpencil_fx_shadow_prepare_sh) {
    e_data->gpencil_fx_shadow_prepare_sh = DRW_shader_create_fullscreen(
        datatoc_gpencil_fx_shadow_prepare_frag_glsl, NULL);

    e_data->gpencil_fx_shadow_resolve_sh = DRW_shader_create_fullscreen(
        datatoc_gpencil_fx_shadow_resolve_frag_glsl, NULL);
  }
  if (!e_data->gpencil_fx_glow_prepare_sh) {
    e_data->gpencil_fx_glow_prepare_sh = DRW_shader_create_fullscreen(
        datatoc_gpencil_fx_glow_prepare_frag_glsl, NULL);

    e_data->gpencil_fx_glow_resolve_sh = DRW_shader_create_fullscreen(
        datatoc_gpencil_fx_glow_resolve_frag_glsl, NULL);
  }
  if (!e_data->gpencil_fx_swirl_sh) {
    e_data->gpencil_fx_swirl_sh = DRW_shader_create_fullscreen(datatoc_gpencil_fx_swirl_frag_glsl,
                                                               NULL);
  }
  if (!e_data->gpencil_fx_wave_sh) {
    e_data->gpencil_fx_wave_sh = DRW_shader_create_fullscreen(datatoc_gpencil_fx_wave_frag_glsl,
                                                              NULL);
  }
}

/* free FX shaders */
void GPENCIL_delete_fx_shaders(GPENCIL_e_data *e_data)
{
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_blur_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_colorize_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_flip_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_pixel_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_rim_prepare_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_rim_resolve_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_shadow_prepare_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_shadow_resolve_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_glow_prepare_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_glow_resolve_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_swirl_sh);
  DRW_SHADER_FREE_SAFE(e_data->gpencil_fx_wave_sh);
}

/* create all passes used by FX */
void GPENCIL_create_fx_passes(GPENCIL_PassList *psl)
{
  psl->fx_shader_pass = DRW_pass_create("GPencil Shader FX Pass",
                                        DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                                            DRW_STATE_DEPTH_LESS);
  psl->fx_shader_pass_blend = DRW_pass_create("GPencil Shader FX Pass",
                                              DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA |
                                                  DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS);
}

/* prepare fx shading groups */
void gpencil_fx_prepare(GPENCIL_e_data *e_data,
                        GPENCIL_Data *vedata,
                        tGPencilObjectCache *cache_ob)
{
  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  const bool wiremode = (bool)(cache_ob->shading_type[0] == OB_WIRE);

  int ob_idx = cache_ob->idx;

  if ((wiremode) || (cache_ob->shader_fx.first == NULL)) {
    return;
  }
  /* loop FX */
  for (ShaderFxData *fx = cache_ob->shader_fx.first; fx; fx = fx->next) {
    if (effect_is_active(cache_ob->gpd, fx, stl->storage->is_render)) {
      switch (fx->type) {
        case eShaderFxType_Blur:
          gpencil_fx_blur(fx, ob_idx, e_data, vedata, cache_ob);
          break;
        case eShaderFxType_Colorize:
          gpencil_fx_colorize(fx, e_data, vedata);
          break;
        case eShaderFxType_Flip:
          gpencil_fx_flip(fx, e_data, vedata);
          break;
        case eShaderFxType_Pixel:
          gpencil_fx_pixel(fx, e_data, vedata, cache_ob);
          break;
        case eShaderFxType_Rim:
          gpencil_fx_rim(fx, e_data, vedata, cache_ob);
          break;
        case eShaderFxType_Shadow:
          gpencil_fx_shadow(fx, e_data, vedata, cache_ob);
          break;
        case eShaderFxType_Glow:
          gpencil_fx_glow(fx, e_data, vedata, cache_ob);
          break;
        case eShaderFxType_Swirl:
          gpencil_fx_swirl(fx, e_data, vedata, cache_ob);
          break;
        case eShaderFxType_Wave:
          gpencil_fx_wave(fx, e_data, vedata);
          break;
        default:
          break;
      }
    }
  }
}

/* helper to draw one FX pass and do ping-pong copy */
static void gpencil_draw_fx_pass(GPENCIL_Data *vedata, DRWShadingGroup *shgrp, bool blend)
{
  if (shgrp == NULL) {
    return;
  }

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;

  const float clearcol[4] = {0.0f};
  GPU_framebuffer_bind(fbl->temp_fb_b);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_b, clearcol, 1.0f);

  /* draw effect pass in temp texture (B) using as source the previous image
   * existing in the other temp texture (A) */
  if (!blend) {
    DRW_draw_pass_subset(psl->fx_shader_pass, shgrp, shgrp);
  }
  else {
    DRW_draw_pass_subset(psl->fx_shader_pass_blend, shgrp, shgrp);
  }

  /* copy pass from b to a for ping-pong frame buffers */
  stl->g_data->input_depth_tx = stl->g_data->temp_depth_tx_b;
  stl->g_data->input_color_tx = stl->g_data->temp_color_tx_b;

  GPU_framebuffer_bind(fbl->temp_fb_a);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_a, clearcol, 1.0f);
  DRW_draw_pass(psl->mix_pass_noblend);
}

/* helper to manage gaussian blur passes */
static void draw_gpencil_blur_passes(GPENCIL_Data *vedata, BlurShaderFxData *fxd)
{
  if (fxd->runtime.fx_sh == NULL) {
    return;
  }

  DRWShadingGroup *shgrp = fxd->runtime.fx_sh;
  int samples = fxd->samples;

  float bx = fxd->blur[0];
  float by = fxd->blur[1];

  /* the blur is done in two steps (Hor/Ver) because is faster and
   * gets better result
   *
   * samples could be 0 and disable de blur effects because sometimes
   * is easier animate the number of samples only, instead to animate the
   * hide/unhide and the number of samples to make some effects.
   */
  for (int b = 0; b < samples; b++) {
    /* horizontal */
    if (bx > 0) {
      fxd->blur[0] = bx;
      fxd->blur[1] = 0;
      gpencil_draw_fx_pass(vedata, shgrp, true);
    }
    /* vertical */
    if (by > 0) {
      fxd->blur[0] = 0;
      fxd->blur[1] = by;
      gpencil_draw_fx_pass(vedata, shgrp, true);
    }
  }
}

/* blur intermediate pass */
static void draw_gpencil_midpass_blur(GPENCIL_Data *vedata, ShaderFxData_Runtime *runtime)
{
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;
  const float clearcol[4] = {0.0f};

  GPU_framebuffer_bind(fbl->temp_fb_b);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_b, clearcol, 1.0f);
  DRW_draw_pass_subset(psl->fx_shader_pass_blend, runtime->fx_sh_b, runtime->fx_sh_b);

  /* copy pass from b for ping-pong frame buffers */
  GPU_framebuffer_bind(fbl->temp_fb_fx);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_fx, clearcol, 1.0f);
  DRW_draw_pass(psl->mix_pass_noblend);
}

/* do blur of mid passes */
static void draw_gpencil_do_blur(
    GPENCIL_Data *vedata, ShaderFxData_Runtime *runtime, int samples, int bx, int by, int blur[2])
{
  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

  stl->g_data->input_depth_tx = stl->g_data->temp_depth_tx_b;
  stl->g_data->input_color_tx = stl->g_data->temp_color_tx_b;

  if ((samples > 0) && ((bx > 0) || (by > 0))) {
    for (int x = 0; x < samples; x++) {

      /* horizontal */
      blur[0] = bx;
      blur[1] = 0;
      draw_gpencil_midpass_blur(vedata, runtime);

      /* Vertical */
      blur[0] = 0;
      blur[1] = by;
      draw_gpencil_midpass_blur(vedata, runtime);

      blur[0] = bx;
      blur[1] = by;
    }
  }
}

/* helper to draw RIM passes */
static void draw_gpencil_rim_passes(GPENCIL_Data *vedata, RimShaderFxData *fxd)
{
  if (fxd->runtime.fx_sh_b == NULL) {
    return;
  }

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;

  const float clearcol[4] = {0.0f};

  /* prepare mask */
  GPU_framebuffer_bind(fbl->temp_fb_fx);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_fx, clearcol, 1.0f);
  DRW_draw_pass_subset(psl->fx_shader_pass_blend, fxd->runtime.fx_sh, fxd->runtime.fx_sh);

  /* blur rim */
  draw_gpencil_do_blur(
      vedata, &fxd->runtime, fxd->samples, fxd->blur[0], fxd->blur[1], &fxd->blur[0]);

  /* resolve */
  GPU_framebuffer_bind(fbl->temp_fb_b);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_b, clearcol, 1.0f);
  DRW_draw_pass_subset(psl->fx_shader_pass_blend, fxd->runtime.fx_sh_c, fxd->runtime.fx_sh_c);

  /* copy pass from b to a for ping-pong frame buffers */
  stl->g_data->input_depth_tx = stl->g_data->temp_depth_tx_b;
  stl->g_data->input_color_tx = stl->g_data->temp_color_tx_b;

  GPU_framebuffer_bind(fbl->temp_fb_a);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_a, clearcol, 1.0f);
  DRW_draw_pass(psl->mix_pass_noblend);
}

/* helper to draw SHADOW passes */
static void draw_gpencil_shadow_passes(GPENCIL_Data *vedata, ShadowShaderFxData *fxd)
{
  if (fxd->runtime.fx_sh_b == NULL) {
    return;
  }

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;
  const float clearcol[4] = {0.0f};

  /* prepare shadow */
  GPU_framebuffer_bind(fbl->temp_fb_fx);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_fx, clearcol, 1.0f);
  DRW_draw_pass_subset(psl->fx_shader_pass_blend, fxd->runtime.fx_sh, fxd->runtime.fx_sh);

  /* blur shadow */
  draw_gpencil_do_blur(
      vedata, &fxd->runtime, fxd->samples, fxd->blur[0], fxd->blur[1], &fxd->blur[0]);

  /* resolve */
  GPU_framebuffer_bind(fbl->temp_fb_b);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_b, clearcol, 1.0f);
  DRW_draw_pass_subset(psl->fx_shader_pass_blend, fxd->runtime.fx_sh_c, fxd->runtime.fx_sh_c);

  /* copy pass from b to a for ping-pong frame buffers */
  stl->g_data->input_depth_tx = stl->g_data->temp_depth_tx_b;
  stl->g_data->input_color_tx = stl->g_data->temp_color_tx_b;

  GPU_framebuffer_bind(fbl->temp_fb_a);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_a, clearcol, 1.0f);
  DRW_draw_pass(psl->mix_pass_noblend);
}

/* helper to draw GLOW passes */
static void draw_gpencil_glow_passes(GPENCIL_Data *vedata, GlowShaderFxData *fxd)
{
  if (fxd->runtime.fx_sh_b == NULL) {
    return;
  }

  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;
  GPENCIL_PassList *psl = ((GPENCIL_Data *)vedata)->psl;
  GPENCIL_FramebufferList *fbl = ((GPENCIL_Data *)vedata)->fbl;

  const float clearcol[4] = {0.0f};

  /* prepare glow */
  GPU_framebuffer_bind(fbl->temp_fb_fx);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_fx, clearcol, 1.0f);
  DRW_draw_pass_subset(psl->fx_shader_pass_blend, fxd->runtime.fx_sh, fxd->runtime.fx_sh);

  /* blur glow */
  draw_gpencil_do_blur(
      vedata, &fxd->runtime, fxd->samples, fxd->blur[0], fxd->blur[0], &fxd->blur[0]);

  /* resolve */
  GPU_framebuffer_bind(fbl->temp_fb_b);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_b, clearcol, 1.0f);

  /* reuses blur field to keep alpha mode */
  fxd->blur[1] = (fxd->flag & FX_GLOW_USE_ALPHA) ? 1 : 0;

  DRW_draw_pass_subset(psl->fx_shader_pass_blend, fxd->runtime.fx_sh_c, fxd->runtime.fx_sh_c);

  /* copy pass from b to a for ping-pong frame buffers */
  stl->g_data->input_depth_tx = stl->g_data->temp_depth_tx_b;
  stl->g_data->input_color_tx = stl->g_data->temp_color_tx_b;

  GPU_framebuffer_bind(fbl->temp_fb_a);
  GPU_framebuffer_clear_color_depth(fbl->temp_fb_a, clearcol, 1.0f);
  DRW_draw_pass(psl->mix_pass_noblend);
}

/* apply all object fx effects */
void gpencil_fx_draw(GPENCIL_e_data *UNUSED(e_data),
                     GPENCIL_Data *vedata,
                     tGPencilObjectCache *cache_ob)
{
  GPENCIL_StorageList *stl = ((GPENCIL_Data *)vedata)->stl;

  /* loop FX modifiers */
  for (ShaderFxData *fx = cache_ob->shader_fx.first; fx; fx = fx->next) {
    if (effect_is_active(cache_ob->gpd, fx, stl->storage->is_render)) {
      switch (fx->type) {

        case eShaderFxType_Blur: {
          BlurShaderFxData *fxd = (BlurShaderFxData *)fx;
          draw_gpencil_blur_passes(vedata, fxd);
          break;
        }
        case eShaderFxType_Colorize: {
          ColorizeShaderFxData *fxd = (ColorizeShaderFxData *)fx;
          gpencil_draw_fx_pass(vedata, fxd->runtime.fx_sh, false);
          break;
        }
        case eShaderFxType_Flip: {
          FlipShaderFxData *fxd = (FlipShaderFxData *)fx;
          gpencil_draw_fx_pass(vedata, fxd->runtime.fx_sh, false);
          break;
        }
        case eShaderFxType_Pixel: {
          PixelShaderFxData *fxd = (PixelShaderFxData *)fx;
          gpencil_draw_fx_pass(vedata, fxd->runtime.fx_sh, false);
          break;
        }
        case eShaderFxType_Rim: {
          RimShaderFxData *fxd = (RimShaderFxData *)fx;
          draw_gpencil_rim_passes(vedata, fxd);
          break;
        }
        case eShaderFxType_Shadow: {
          ShadowShaderFxData *fxd = (ShadowShaderFxData *)fx;
          draw_gpencil_shadow_passes(vedata, fxd);
          break;
        }
        case eShaderFxType_Glow: {
          GlowShaderFxData *fxd = (GlowShaderFxData *)fx;
          draw_gpencil_glow_passes(vedata, fxd);
          break;
        }
        case eShaderFxType_Swirl: {
          SwirlShaderFxData *fxd = (SwirlShaderFxData *)fx;
          gpencil_draw_fx_pass(vedata, fxd->runtime.fx_sh, false);
          break;
        }
        case eShaderFxType_Wave: {
          WaveShaderFxData *fxd = (WaveShaderFxData *)fx;
          gpencil_draw_fx_pass(vedata, fxd->runtime.fx_sh, false);
          break;
        }
        default:
          break;
      }
    }
  }
}

/* ------------- Refactored Code ------------ */

typedef struct gpIterVfxData {
  GPENCIL_PrivateData *pd;
  GPENCIL_tObject *tgp_ob;
  GPUFrameBuffer **target_fb;
  GPUFrameBuffer **source_fb;
  GPUTexture **target_color_tx;
  GPUTexture **source_color_tx;
  GPUTexture **target_reveal_tx;
  GPUTexture **source_reveal_tx;
} gpIterVfxData;

static DRWShadingGroup *gpencil_vfx_pass_create(const char *name,
                                                DRWState state,
                                                gpIterVfxData *iter,
                                                GPUShader *sh)
{
  DRWPass *pass = DRW_pass_create(name, state);
  DRWShadingGroup *grp = DRW_shgroup_create(sh, pass);
  DRW_shgroup_uniform_texture_ref(grp, "colorBuf", iter->source_color_tx);
  DRW_shgroup_uniform_texture_ref(grp, "revealBuf", iter->source_reveal_tx);

  GPENCIL_tVfx *tgp_vfx = BLI_memblock_alloc(iter->pd->gp_vfx_pool);
  tgp_vfx->target_fb = iter->target_fb;
  tgp_vfx->vfx_ps = pass;

  SWAP(GPUFrameBuffer **, iter->target_fb, iter->source_fb);
  SWAP(GPUTexture **, iter->target_color_tx, iter->source_color_tx);
  SWAP(GPUTexture **, iter->target_reveal_tx, iter->source_reveal_tx);

  BLI_LINKS_APPEND(&iter->tgp_ob->vfx, tgp_vfx);

  return grp;
}

static void gpencil_vfx_blur(BlurShaderFxData *fx, Object *ob, gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  float winmat[4][4], persmat[4][4];
  float blur_size[2] = {fx->radius[0], fx->radius[1]};
  DRW_view_persmat_get(NULL, persmat, false);
  const float w = fabsf(mul_project_m4_v3_zfac(persmat, ob->obmat[3]));

  if ((fx->flag & FX_BLUR_DOF_MODE) && iter->pd->camera != NULL) {
    /* Compute circle of confusion size. */
    float coc = (iter->pd->dof_params[0] / -w) - iter->pd->dof_params[1];
    copy_v2_fl(blur_size, fabsf(coc));
  }
  else {
    /* Modify by distance to camera and object scale. */
    DRW_view_winmat_get(NULL, winmat, false);
    const float *vp_size = DRW_viewport_size_get();
    float world_pixel_scale = 1.0f / GPENCIL_PIXEL_FACTOR;
    float scale = mat4_to_scale(ob->obmat);
    float distance_factor = world_pixel_scale * scale * winmat[1][1] * vp_size[1] / w;
    mul_v2_fl(blur_size, distance_factor);
  }

  GPUShader *sh = GPENCIL_shader_fx_blur_get(&en_data);

  DRWState state = DRW_STATE_WRITE_COLOR;
  grp = gpencil_vfx_pass_create("Fx Blur H", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "offset", (float[2]){blur_size[0], 0.0f});
  DRW_shgroup_uniform_int_copy(grp, "sampCount", max_ii(1, min_ii(fx->samples, blur_size[0])));
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

  grp = gpencil_vfx_pass_create("Fx Blur V", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "offset", (float[2]){0.0f, blur_size[1]});
  DRW_shgroup_uniform_int_copy(grp, "sampCount", max_ii(1, min_ii(fx->samples, blur_size[1])));
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
}

static void gpencil_vfx_colorize(ColorizeShaderFxData *fx, Object *UNUSED(ob), gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  GPUShader *sh = GPENCIL_shader_fx_colorize_get(&en_data);

  DRWState state = DRW_STATE_WRITE_COLOR;
  grp = gpencil_vfx_pass_create("Fx Colorize", state, iter, sh);
  DRW_shgroup_uniform_vec3_copy(grp, "lowColor", fx->low_color);
  DRW_shgroup_uniform_vec3_copy(grp, "highColor", fx->high_color);
  DRW_shgroup_uniform_float_copy(grp, "factor", fx->factor);
  DRW_shgroup_uniform_int_copy(grp, "mode", fx->mode);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
}

static void gpencil_vfx_flip(FlipShaderFxData *fx, Object *UNUSED(ob), gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  float axis_flip[2];
  axis_flip[0] = (fx->flag & FX_FLIP_HORIZONTAL) ? -1.0f : 1.0f;
  axis_flip[1] = (fx->flag & FX_FLIP_VERTICAL) ? -1.0f : 1.0f;

  GPUShader *sh = GPENCIL_shader_fx_transform_get(&en_data);

  DRWState state = DRW_STATE_WRITE_COLOR;
  grp = gpencil_vfx_pass_create("Fx Flip", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "axisFlip", axis_flip);
  DRW_shgroup_uniform_vec2_copy(grp, "waveOffset", (float[2]){0.0f, 0.0f});
  DRW_shgroup_uniform_float_copy(grp, "swirlRadius", 0.0f);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
}

static void gpencil_vfx_rim(RimShaderFxData *fx, Object *ob, gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  float winmat[4][4], persmat[4][4];
  float offset[2] = {fx->offset[0], fx->offset[1]};
  float blur_size[2] = {fx->blur[0], fx->blur[1]};
  DRW_view_winmat_get(NULL, winmat, false);
  DRW_view_persmat_get(NULL, persmat, false);
  const float *vp_size = DRW_viewport_size_get();
  const float *vp_size_inv = DRW_viewport_invert_size_get();

  const float w = fabsf(mul_project_m4_v3_zfac(persmat, ob->obmat[3]));

  /* Modify by distance to camera and object scale. */
  float world_pixel_scale = 1.0f / GPENCIL_PIXEL_FACTOR;
  float scale = mat4_to_scale(ob->obmat);
  float distance_factor = (world_pixel_scale * scale * winmat[1][1] * vp_size[1]) / w;
  mul_v2_fl(offset, distance_factor);
  mul_v2_v2(offset, vp_size_inv);
  mul_v2_fl(blur_size, distance_factor);

  GPUShader *sh = GPENCIL_shader_fx_rim_get(&en_data);

  DRWState state = DRW_STATE_WRITE_COLOR;
  grp = gpencil_vfx_pass_create("Fx Rim H", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "blurDir", (float[2]){blur_size[0] * vp_size_inv[0], 0.0f});
  DRW_shgroup_uniform_vec2_copy(grp, "uvOffset", offset);
  DRW_shgroup_uniform_int_copy(grp, "sampCount", max_ii(1, min_ii(fx->samples, blur_size[0])));
  DRW_shgroup_uniform_vec3_copy(grp, "maskColor", fx->mask_rgb);
  DRW_shgroup_uniform_bool_copy(grp, "isFirstPass", true);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

  switch (fx->mode) {
    case eShaderFxRimMode_Normal:
      state |= DRW_STATE_BLEND_ALPHA_PREMUL;
      break;
    case eShaderFxRimMode_Add:
      state |= DRW_STATE_BLEND_ADD_FULL;
      break;
    case eShaderFxRimMode_Subtract:
      state |= DRW_STATE_BLEND_SUB;
      break;
    case eShaderFxRimMode_Multiply:
    case eShaderFxRimMode_Divide:
    case eShaderFxRimMode_Overlay:
      state |= DRW_STATE_BLEND_MUL;
      break;
  }

  zero_v2(offset);

  grp = gpencil_vfx_pass_create("Fx Rim V", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "blurDir", (float[2]){0.0f, blur_size[1] * vp_size_inv[1]});
  DRW_shgroup_uniform_vec2_copy(grp, "uvOffset", offset);
  DRW_shgroup_uniform_vec3_copy(grp, "rimColor", fx->rim_rgb);
  DRW_shgroup_uniform_int_copy(grp, "sampCount", max_ii(1, min_ii(fx->samples, blur_size[1])));
  DRW_shgroup_uniform_int_copy(grp, "blendMode", fx->mode);
  DRW_shgroup_uniform_bool_copy(grp, "isFirstPass", false);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

  if (fx->mode == eShaderFxRimMode_Overlay) {
    /* We cannot do custom blending on MultiTarget framebuffers.
     * Workaround by doing 2 passes. */
    grp = DRW_shgroup_create_sub(grp);
    DRW_shgroup_state_disable(grp, DRW_STATE_BLEND_MUL);
    DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ADD_FULL);
    DRW_shgroup_uniform_int_copy(grp, "blendMode", 999);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
}

static void gpencil_vfx_pixelize(PixelShaderFxData *fx, Object *ob, gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  float persmat[4][4], winmat[4][4], ob_center[3], pixsize_uniform[2];
  DRW_view_winmat_get(NULL, winmat, false);
  DRW_view_persmat_get(NULL, persmat, false);
  const float *vp_size = DRW_viewport_size_get();
  const float *vp_size_inv = DRW_viewport_invert_size_get();
  float pixel_size[2] = {fx->size[0], fx->size[1]};
  mul_v2_v2(pixel_size, vp_size_inv);

  /* Fixed pixelisation center from object center. */
  const float w = fabsf(mul_project_m4_v3_zfac(persmat, ob->obmat[3]));
  mul_v3_m4v3(ob_center, persmat, ob->obmat[3]);
  mul_v3_fl(ob_center, 1.0f / w);

  /* Convert to uvs. */
  mul_v2_fl(ob_center, 0.5f);
  add_v2_fl(ob_center, 0.5f);

  /* Modify by distance to camera and object scale. */
  float world_pixel_scale = 1.0f / GPENCIL_PIXEL_FACTOR;
  float scale = mat4_to_scale(ob->obmat);
  mul_v2_fl(pixel_size, (world_pixel_scale * scale * winmat[1][1] * vp_size[1]) / w);

  /* Center to texel */
  madd_v2_v2fl(ob_center, pixel_size, -0.5f);

  GPUShader *sh = GPENCIL_shader_fx_pixelize_get(&en_data);

  DRWState state = DRW_STATE_WRITE_COLOR;

  /* Only if pixelated effect is bigger than 1px. */
  if (pixel_size[0] > vp_size_inv[0]) {
    copy_v2_fl2(pixsize_uniform, pixel_size[0], vp_size_inv[1]);
    grp = gpencil_vfx_pass_create("Fx Pixelize X", state, iter, sh);
    DRW_shgroup_uniform_vec2_copy(grp, "targetPixelSize", pixsize_uniform);
    DRW_shgroup_uniform_vec2_copy(grp, "targetPixelOffset", ob_center);
    DRW_shgroup_uniform_vec2_copy(grp, "accumOffset", (float[2]){pixel_size[0], 0.0f});
    DRW_shgroup_uniform_int_copy(grp, "sampCount", (pixel_size[0] / vp_size_inv[0] > 3.0) ? 2 : 1);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }

  if (pixel_size[1] > vp_size_inv[1]) {
    copy_v2_fl2(pixsize_uniform, vp_size_inv[0], pixel_size[1]);
    grp = gpencil_vfx_pass_create("Fx Pixelize Y", state, iter, sh);
    DRW_shgroup_uniform_vec2_copy(grp, "targetPixelSize", pixsize_uniform);
    DRW_shgroup_uniform_vec2_copy(grp, "accumOffset", (float[2]){0.0f, pixel_size[1]});
    DRW_shgroup_uniform_int_copy(grp, "sampCount", (pixel_size[1] / vp_size_inv[1] > 3.0) ? 2 : 1);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
}

static void gpencil_vfx_shadow(ShadowShaderFxData *fx, Object *ob, gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  const bool use_obj_pivot = (fx->flag & FX_SHADOW_USE_OBJECT) != 0;
  const bool use_wave = (fx->flag & FX_SHADOW_USE_WAVE) != 0;

  float uv_mat[4][4], winmat[4][4], persmat[4][4], rot_center[3];
  float wave_ofs[3], wave_dir[3], wave_phase, blur_dir[2], tmp[2];
  float offset[2] = {fx->offset[0], fx->offset[1]};
  float blur_size[2] = {fx->blur[0], fx->blur[1]};
  DRW_view_winmat_get(NULL, winmat, false);
  DRW_view_persmat_get(NULL, persmat, false);
  const float *vp_size = DRW_viewport_size_get();
  const float *vp_size_inv = DRW_viewport_invert_size_get();
  const float ratio = vp_size_inv[1] / vp_size_inv[0];

  copy_v3_v3(rot_center, (use_obj_pivot && fx->object) ? fx->object->obmat[3] : ob->obmat[3]);

  const float w = fabsf(mul_project_m4_v3_zfac(persmat, rot_center));
  mul_v3_m4v3(rot_center, persmat, rot_center);
  mul_v3_fl(rot_center, 1.0f / w);

  /* Modify by distance to camera and object scale. */
  float world_pixel_scale = 1.0f / GPENCIL_PIXEL_FACTOR;
  float scale = mat4_to_scale(ob->obmat);
  float distance_factor = (world_pixel_scale * scale * winmat[1][1] * vp_size[1]) / w;
  mul_v2_fl(offset, distance_factor);
  mul_v2_v2(offset, vp_size_inv);
  mul_v2_fl(blur_size, distance_factor);

  rot_center[0] = rot_center[0] * 0.5f + 0.5f;
  rot_center[1] = rot_center[1] * 0.5f + 0.5f;

  /* UV transform matrix. (loc, rot, scale) Sent to shader as 2x3 matrix. */
  unit_m4(uv_mat);
  translate_m4(uv_mat, rot_center[0], rot_center[1], 0.0f);
  rescale_m4(uv_mat, (float[3]){1.0f / fx->scale[0], 1.0f / fx->scale[1], 1.0f});
  translate_m4(uv_mat, -offset[0], -offset[1], 0.0f);
  rescale_m4(uv_mat, (float[3]){1.0f / ratio, 1.0f, 1.0f});
  rotate_m4(uv_mat, 'Z', fx->rotation);
  rescale_m4(uv_mat, (float[3]){ratio, 1.0f, 1.0f});
  translate_m4(uv_mat, -rot_center[0], -rot_center[1], 0.0f);

  if (use_wave) {
    float dir[2];
    if (fx->orientation == 0) {
      /* Horizontal */
      copy_v2_fl2(dir, 1.0f, 0.0f);
    }
    else {
      /* Vertical */
      copy_v2_fl2(dir, 0.0f, 1.0f);
    }
    /* This is applied after rotation. Counter the rotation to keep aligned with global axis. */
    rotate_v2_v2fl(wave_dir, dir, fx->rotation);
    /* Rotate 90°. */
    copy_v2_v2(wave_ofs, wave_dir);
    SWAP(float, wave_ofs[0], wave_ofs[1]);
    wave_ofs[1] *= -1.0f;
    /* Keep world space scalling and aspect ratio. */
    mul_v2_fl(wave_dir, 1.0f / (max_ff(1e-8f, fx->period) * distance_factor));
    mul_v2_v2(wave_dir, vp_size);
    mul_v2_fl(wave_ofs, fx->amplitude * distance_factor);
    mul_v2_v2(wave_ofs, vp_size_inv);
    /* Phase start at shadow center. */
    wave_phase = fx->phase - dot_v2v2(rot_center, wave_dir);
  }
  else {
    zero_v2(wave_dir);
    zero_v2(wave_ofs);
    wave_phase = 0.0f;
  }

  GPUShader *sh = GPENCIL_shader_fx_shadow_get(&en_data);

  copy_v2_fl2(blur_dir, blur_size[0] * vp_size_inv[0], 0.0f);

  DRWState state = DRW_STATE_WRITE_COLOR;
  grp = gpencil_vfx_pass_create("Fx Shadow H", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "blurDir", blur_dir);
  DRW_shgroup_uniform_vec2_copy(grp, "waveDir", wave_dir);
  DRW_shgroup_uniform_vec2_copy(grp, "waveOffset", wave_ofs);
  DRW_shgroup_uniform_float_copy(grp, "wavePhase", wave_phase);
  DRW_shgroup_uniform_vec2_copy(grp, "uvRotX", uv_mat[0]);
  DRW_shgroup_uniform_vec2_copy(grp, "uvRotY", uv_mat[1]);
  DRW_shgroup_uniform_vec2_copy(grp, "uvOffset", uv_mat[3]);
  DRW_shgroup_uniform_int_copy(grp, "sampCount", max_ii(1, min_ii(fx->samples, blur_size[0])));
  DRW_shgroup_uniform_bool_copy(grp, "isFirstPass", true);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

  unit_m4(uv_mat);
  zero_v2(wave_ofs);

  /* We reseted the uv_mat so we need to accound for the rotation in the  */
  copy_v2_fl2(tmp, 0.0f, blur_size[1]);
  rotate_v2_v2fl(blur_dir, tmp, -fx->rotation);
  mul_v2_v2(blur_dir, vp_size_inv);

  state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA_PREMUL;
  grp = gpencil_vfx_pass_create("Fx Shadow V", state, iter, sh);
  DRW_shgroup_uniform_vec4_copy(grp, "shadowColor", fx->shadow_rgba);
  DRW_shgroup_uniform_vec2_copy(grp, "blurDir", blur_dir);
  DRW_shgroup_uniform_vec2_copy(grp, "waveOffset", wave_ofs);
  DRW_shgroup_uniform_vec2_copy(grp, "uvRotX", uv_mat[0]);
  DRW_shgroup_uniform_vec2_copy(grp, "uvRotY", uv_mat[1]);
  DRW_shgroup_uniform_vec2_copy(grp, "uvOffset", uv_mat[3]);
  DRW_shgroup_uniform_int_copy(grp, "sampCount", max_ii(1, min_ii(fx->samples, blur_size[1])));
  DRW_shgroup_uniform_bool_copy(grp, "isFirstPass", false);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
}

static void gpencil_vfx_glow(GlowShaderFxData *fx, Object *UNUSED(ob), gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  GPUShader *sh = GPENCIL_shader_fx_glow_get(&en_data);

  float ref_col[3];

  if (fx->mode == eShaderFxGlowMode_Luminance) {
    ref_col[0] = fx->threshold;
    ref_col[1] = -1.0f;
    ref_col[2] = -1.0f;
  }
  else {
    copy_v3_v3(ref_col, fx->select_color);
  }

  DRWState state = DRW_STATE_WRITE_COLOR;
  grp = gpencil_vfx_pass_create("Fx Glow H", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "offset", (float[2]){fx->blur[0], 0.0f});
  DRW_shgroup_uniform_int_copy(grp, "sampCount", max_ii(1, min_ii(fx->samples, fx->blur[0])));
  DRW_shgroup_uniform_vec3_copy(grp, "threshold", ref_col);
  DRW_shgroup_uniform_vec3_copy(grp, "glowColor", fx->glow_color);
  DRW_shgroup_uniform_bool_copy(grp, "useAlphaMode", false);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

  ref_col[0] = -1.0f;

  state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD_FULL;
  grp = gpencil_vfx_pass_create("Fx Glow V", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "offset", (float[2]){0.0f, fx->blur[0]});
  DRW_shgroup_uniform_vec3_copy(grp, "threshold", ref_col);
  DRW_shgroup_uniform_vec3_copy(grp, "glowColor", (float[3]){1.0f, 1.0f, 1.0f});
  DRW_shgroup_uniform_bool_copy(grp, "useAlphaMode", (fx->flag & FX_GLOW_USE_ALPHA) != 0);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
}

static void gpencil_vfx_wave(WaveShaderFxData *fx, Object *ob, gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  float winmat[4][4], persmat[4][4], wave_center[3];
  float wave_ofs[3], wave_dir[3], wave_phase;
  DRW_view_winmat_get(NULL, winmat, false);
  DRW_view_persmat_get(NULL, persmat, false);
  const float *vp_size = DRW_viewport_size_get();
  const float *vp_size_inv = DRW_viewport_invert_size_get();

  const float w = fabsf(mul_project_m4_v3_zfac(persmat, ob->obmat[3]));
  mul_v3_m4v3(wave_center, persmat, ob->obmat[3]);
  mul_v3_fl(wave_center, 1.0f / w);

  /* Modify by distance to camera and object scale. */
  float world_pixel_scale = 1.0f / GPENCIL_PIXEL_FACTOR;
  float scale = mat4_to_scale(ob->obmat);
  float distance_factor = (world_pixel_scale * scale * winmat[1][1] * vp_size[1]) / w;

  wave_center[0] = wave_center[0] * 0.5f + 0.5f;
  wave_center[1] = wave_center[1] * 0.5f + 0.5f;

  if (fx->orientation == 0) {
    /* Horizontal */
    copy_v2_fl2(wave_dir, 1.0f, 0.0f);
  }
  else {
    /* Vertical */
    copy_v2_fl2(wave_dir, 0.0f, 1.0f);
  }
  /* Rotate 90°. */
  copy_v2_v2(wave_ofs, wave_dir);
  SWAP(float, wave_ofs[0], wave_ofs[1]);
  wave_ofs[1] *= -1.0f;
  /* Keep world space scalling and aspect ratio. */
  mul_v2_fl(wave_dir, 1.0f / (max_ff(1e-8f, fx->period) * distance_factor));
  mul_v2_v2(wave_dir, vp_size);
  mul_v2_fl(wave_ofs, fx->amplitude * distance_factor);
  mul_v2_v2(wave_ofs, vp_size_inv);
  /* Phase start at shadow center. */
  wave_phase = fx->phase - dot_v2v2(wave_center, wave_dir);

  GPUShader *sh = GPENCIL_shader_fx_transform_get(&en_data);

  DRWState state = DRW_STATE_WRITE_COLOR;
  grp = gpencil_vfx_pass_create("Fx Wave", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "axisFlip", (float[2]){1.0f, 1.0f});
  DRW_shgroup_uniform_vec2_copy(grp, "waveDir", wave_dir);
  DRW_shgroup_uniform_vec2_copy(grp, "waveOffset", wave_ofs);
  DRW_shgroup_uniform_float_copy(grp, "wavePhase", wave_phase);
  DRW_shgroup_uniform_float_copy(grp, "swirlRadius", 0.0f);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
}

static void gpencil_vfx_swirl(SwirlShaderFxData *fx, Object *UNUSED(ob), gpIterVfxData *iter)
{
  DRWShadingGroup *grp;

  if (fx->object == NULL) {
    return;
  }

  float winmat[4][4], persmat[4][4], swirl_center[3];
  DRW_view_winmat_get(NULL, winmat, false);
  DRW_view_persmat_get(NULL, persmat, false);
  const float *vp_size = DRW_viewport_size_get();

  copy_v3_v3(swirl_center, fx->object->obmat[3]);

  const float w = fabsf(mul_project_m4_v3_zfac(persmat, swirl_center));
  mul_v3_m4v3(swirl_center, persmat, swirl_center);
  mul_v3_fl(swirl_center, 1.0f / w);

  /* Modify by distance to camera and object scale. */
  float world_pixel_scale = 1.0f / GPENCIL_PIXEL_FACTOR;
  float scale = mat4_to_scale(fx->object->obmat);
  float distance_factor = (world_pixel_scale * scale * winmat[1][1] * vp_size[1]) / w;

  mul_v2_fl(swirl_center, 0.5f);
  add_v2_fl(swirl_center, 0.5f);
  mul_v2_v2(swirl_center, vp_size);

  float radius = fx->radius * distance_factor;
  if (radius < 1.0f) {
    return;
  }

  GPUShader *sh = GPENCIL_shader_fx_transform_get(&en_data);

  DRWState state = DRW_STATE_WRITE_COLOR;
  grp = gpencil_vfx_pass_create("Fx Flip", state, iter, sh);
  DRW_shgroup_uniform_vec2_copy(grp, "axisFlip", (float[2]){1.0f, 1.0f});
  DRW_shgroup_uniform_vec2_copy(grp, "waveOffset", (float[2]){0.0f, 0.0f});
  DRW_shgroup_uniform_vec2_copy(grp, "swirlCenter", swirl_center);
  DRW_shgroup_uniform_float_copy(grp, "swirlAngle", fx->angle);
  DRW_shgroup_uniform_float_copy(grp, "swirlRadius", radius);
  DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
}

void gpencil_vfx_cache_populate(GPENCIL_Data *vedata, Object *ob, GPENCIL_tObject *tgp_ob)
{
  bGPdata *gpd = (bGPdata *)ob->data;
  GPENCIL_FramebufferList *fbl = vedata->fbl;
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  /* If simplify enabled, nothing more to do. */
  if (pd->simplify_fx) {
    return;
  }

  /* These may not be allocated yet, use adress of future pointer. */
  gpIterVfxData iter = {
      .pd = pd,
      .tgp_ob = tgp_ob,
      .target_fb = &fbl->layer_fb,
      .source_fb = &fbl->object_fb,
      .target_color_tx = &pd->color_layer_tx,
      .source_color_tx = &pd->color_object_tx,
      .target_reveal_tx = &pd->reveal_layer_tx,
      .source_reveal_tx = &pd->reveal_object_tx,
  };

  LISTBASE_FOREACH (ShaderFxData *, fx, &ob->shader_fx) {
    if (effect_is_active(gpd, fx, pd->is_render)) {
      switch (fx->type) {
        case eShaderFxType_Blur:
          gpencil_vfx_blur((BlurShaderFxData *)fx, ob, &iter);
          break;
        case eShaderFxType_Colorize:
          gpencil_vfx_colorize((ColorizeShaderFxData *)fx, ob, &iter);
          break;
        case eShaderFxType_Flip:
          gpencil_vfx_flip((FlipShaderFxData *)fx, ob, &iter);
          break;
        case eShaderFxType_Pixel:
          gpencil_vfx_pixelize((PixelShaderFxData *)fx, ob, &iter);
          break;
        case eShaderFxType_Rim:
          gpencil_vfx_rim((RimShaderFxData *)fx, ob, &iter);
          break;
        case eShaderFxType_Shadow:
          gpencil_vfx_shadow((ShadowShaderFxData *)fx, ob, &iter);
          break;
        case eShaderFxType_Glow:
          gpencil_vfx_glow((GlowShaderFxData *)fx, ob, &iter);
          break;
        case eShaderFxType_Swirl:
          gpencil_vfx_swirl((SwirlShaderFxData *)fx, ob, &iter);
          break;
        case eShaderFxType_Wave:
          gpencil_vfx_wave((WaveShaderFxData *)fx, ob, &iter);
          break;
        default:
          break;
      }
    }
  }

  if (tgp_ob->vfx.first != NULL) {
    /* We need an extra pass to combine result to main buffer. */
    iter.target_fb = &fbl->gpencil_fb;

    GPUShader *sh = GPENCIL_shader_fx_composite_get(&en_data);

    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_MUL;
    DRWShadingGroup *grp = gpencil_vfx_pass_create("GPencil Object Compose", state, &iter, sh);
    DRW_shgroup_uniform_int_copy(grp, "isFirstPass", true);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

    /* We cannot do custom blending on MultiTarget framebuffers.
     * Workaround by doing 2 passes. */
    grp = DRW_shgroup_create_sub(grp);
    DRW_shgroup_state_disable(grp, DRW_STATE_BLEND_MUL);
    DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ADD_FULL);
    DRW_shgroup_uniform_int_copy(grp, "isFirstPass", false);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
}
