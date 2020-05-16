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
 * Copyright 2019, Blender Foundation.
 *
 */

/** \file
 * \ingroup draw
 */

#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math_matrix.h"
#include "BLI_rect.h"
#include "BLI_task.h"

#include "BKE_object.h"

#include "DEG_depsgraph_query.h"

#include "DNA_camera_types.h"
#include "DNA_lanpr_types.h"
#include "DNA_mesh_types.h"

#include "DRW_engine.h"
#include "DRW_render.h"

#include "GPU_batch.h"
#include "GPU_draw.h"
#include "GPU_framebuffer.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_shader.h"
#include "GPU_uniformbuffer.h"
#include "GPU_viewport.h"

#include "RE_pipeline.h"

#include "bmesh.h"
#include "lanpr_all.h"

#include <math.h>

extern char datatoc_common_fullscreen_vert_glsl[];
extern char datatoc_gpu_shader_3D_smooth_color_vert_glsl[];
extern char datatoc_gpu_shader_3D_smooth_color_frag_glsl[];
extern char datatoc_lanpr_software_line_chain_geom_glsl[];
extern char datatoc_lanpr_software_chain_geom_glsl[];
extern char datatoc_lanpr_dpix_project_passthrough_vert_glsl[];
extern char datatoc_lanpr_dpix_project_clip_frag_glsl[];
extern char datatoc_lanpr_dpix_preview_frag_glsl[];
extern char datatoc_lanpr_software_passthrough_vert_glsl[];
extern char datatoc_gpu_shader_2D_smooth_color_vert_glsl[];
extern char datatoc_gpu_shader_2D_smooth_color_frag_glsl[];
extern char datatoc_gpu_shader_colorspace_lib_glsl[];

LANPR_SharedResource lanpr_share;

static void lanpr_engine_init(void *ved)
{
  lanpr_share.ved_viewport = ved;
  LANPR_Data *vedata = (LANPR_Data *)ved;
  LANPR_TextureList *txl = vedata->txl;
  LANPR_FramebufferList *fbl = vedata->fbl;

  if (!lanpr_share.init_complete) {
    BLI_spin_init(&lanpr_share.lock_render_status);
  }

  DRW_texture_ensure_fullscreen_2d(&txl->depth, GPU_DEPTH_COMPONENT32F, 0);
  DRW_texture_ensure_fullscreen_2d(&txl->color, GPU_RGBA32F, 0);
  DRW_texture_ensure_fullscreen_2d(&txl->normal, GPU_RGBA32F, 0);
  DRW_texture_ensure_fullscreen_2d(&txl->edge_intermediate, GPU_RGBA32F, 0);

  DRW_texture_ensure_fullscreen_2d(&txl->ms_resolve_depth, GPU_DEPTH_COMPONENT32F, 0);
  DRW_texture_ensure_fullscreen_2d(&txl->ms_resolve_color, GPU_RGBA32F, 0);

  GPU_framebuffer_ensure_config(&fbl->passes,
                                {GPU_ATTACHMENT_TEXTURE(txl->depth),
                                 GPU_ATTACHMENT_TEXTURE(txl->color),
                                 GPU_ATTACHMENT_TEXTURE(txl->normal)});

  GPU_framebuffer_ensure_config(
      &fbl->edge_intermediate,
      {GPU_ATTACHMENT_TEXTURE(txl->depth), GPU_ATTACHMENT_TEXTURE(txl->edge_intermediate)});

  if (lanpr_share.multichannel_shader == NULL) {
    lanpr_share.multichannel_shader = GPU_shader_create_from_arrays({
        .vert = (const char *[]){datatoc_gpu_shader_3D_smooth_color_vert_glsl, NULL},
        .geom = (const char *[]){NULL},
        .frag = (const char *[]){datatoc_gpu_shader_colorspace_lib_glsl,
                                 datatoc_gpu_shader_3D_smooth_color_frag_glsl,
                                 NULL},
        .defs = (const char *[]){NULL},
    });
  }

  /* DPIX */
  lanpr_init_atlas_inputs(ved);

  /* SOFTWARE */
  if (lanpr_share.software_shader == NULL) {
    lanpr_share.software_shader = DRW_shader_create(datatoc_lanpr_software_passthrough_vert_glsl,
                                                    datatoc_lanpr_software_line_chain_geom_glsl,
                                                    datatoc_lanpr_dpix_preview_frag_glsl,
                                                    NULL);
  }

  if (lanpr_share.software_chaining_shader == NULL) {
    lanpr_share.software_chaining_shader = DRW_shader_create(
        datatoc_lanpr_software_passthrough_vert_glsl,
        datatoc_lanpr_software_chain_geom_glsl,
        datatoc_lanpr_dpix_preview_frag_glsl,
        NULL);
  }

  GPU_framebuffer_ensure_config(&fbl->software_ms,
                                {GPU_ATTACHMENT_TEXTURE(txl->ms_resolve_depth),
                                 GPU_ATTACHMENT_TEXTURE(txl->ms_resolve_color)});

  if (!(lanpr_share.init_complete & LANPR_INIT_LOCKS)) {
    BLI_spin_init(&lanpr_share.lock_loader);
    BLI_spin_init(&lanpr_share.lock_render_status);
    lanpr_share.init_complete |= LANPR_INIT_LOCKS;
  }

  lanpr_share.init_complete |= LANPR_INIT_ENGINE;
}

void DRW_scene_lanpr_freecache(Scene *sce)
{
  LANPR_LineLayer *ll;

  for (ll = sce->lanpr.line_layers.first; ll; ll = ll->next) {
    if (ll->batch) {
      GPU_batch_discard(ll->batch);
      ll->batch = NULL;
    }
  }
}

static void lanpr_dpix_batch_free(void)
{
  LANPR_BatchItem *dpbi;
  while ((dpbi = BLI_pophead(&lanpr_share.dpix_batch_list)) != NULL) {
    GPU_BATCH_DISCARD_SAFE(dpbi->dpix_preview_batch);
    GPU_BATCH_DISCARD_SAFE(dpbi->dpix_transform_batch);
  }
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;
  if (rb) {
    if (rb->DPIXIntersectionBatch) {
      GPU_BATCH_DISCARD_SAFE(rb->DPIXIntersectionBatch);
    }
    if (rb->DPIXIntersectionTransformBatch) {
      GPU_BATCH_DISCARD_SAFE(rb->DPIXIntersectionTransformBatch);
    }
  }
}

static void lanpr_chain_batch_free(void)
{
  LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;
  if (rb) {
    if (rb->chain_draw_batch) {
      GPU_BATCH_DISCARD_SAFE(rb->DPIXIntersectionBatch);
    }
  }
}

static void lanpr_engine_free(void)
{
  DRW_SHADER_FREE_SAFE(lanpr_share.multichannel_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.software_chaining_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.dpix_preview_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.dpix_transform_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.software_shader);

  lanpr_dpix_batch_free();
  lanpr_chain_batch_free();

  BLI_spin_end(&lanpr_share.lock_loader);
  BLI_spin_end(&lanpr_share.lock_render_status);

  if (lanpr_share.render_buffer_shared) {
    LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;
    ED_lanpr_destroy_render_data(rb);

    if (lanpr_share.background_render_task) {
      BLI_task_pool_free(lanpr_share.background_render_task);
    }

    GPU_BATCH_DISCARD_SAFE(rb->chain_draw_batch);

    MEM_freeN(rb);
    lanpr_share.render_buffer_shared = NULL;
  }

  BLI_mempool *mp = lanpr_share.mp_batch_list;

  if (mp) {
    BLI_mempool_destroy(mp);
  }
}

static void lanpr_cache_init(void *vedata)
{

  LANPR_PassList *psl = ((LANPR_Data *)vedata)->psl;
  LANPR_StorageList *stl = ((LANPR_Data *)vedata)->stl;
  LANPR_TextureList *txl = ((LANPR_Data *)vedata)->txl;

  static float normal_object_direction[3] = {0, 0, 1};

  /* Transfer reload state */
  lanpr_share.dpix_reloaded = lanpr_share.dpix_reloaded_deg;

  if (stl->g_data == NULL) {
    /* Alloc transient pointers */
    stl->g_data = MEM_callocN(sizeof(*stl->g_data), __func__);
  }

  if (lanpr_share.mp_batch_list == NULL) {
    lanpr_share.mp_batch_list = BLI_mempool_create(
        sizeof(LANPR_BatchItem), 0, 128, BLI_MEMPOOL_NOP);
  }

  LANPR_PrivateData *pd = stl->g_data;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = DEG_get_evaluated_scene(draw_ctx->depsgraph);
  SceneLANPR *lanpr = &scene->lanpr;
  View3D *v3d = draw_ctx->v3d;
  RegionView3D *rv3d = v3d ? draw_ctx->rv3d : NULL;

  bool is_render = (lanpr_share.viewport_camera_override < 0);

  BLI_spin_lock(&lanpr_share.lock_render_status);
  if (rv3d && lanpr_share.viewport_camera_override >= 0) {
    copy_v3_v3(lanpr_share.camera_pos, rv3d->viewinv[3]);
    copy_m4_m4(lanpr_share.viewinv, rv3d->viewinv);
    copy_m4_m4(lanpr_share.persp, rv3d->persmat);
    copy_v4_v4(lanpr_share.viewquat, rv3d->viewquat);
    lanpr_share.near_clip = v3d->clip_start;
    lanpr_share.far_clip = v3d->clip_end;
    lanpr_share.viewport_camera_override = 1;
    lanpr_share.camera_is_persp = rv3d->is_persp;
  }
  else {
    lanpr_share.viewport_camera_override = 0;
  }
  BLI_spin_unlock(&lanpr_share.lock_render_status);

  int texture_size = lanpr_dpix_texture_size(lanpr);
  lanpr_share.texture_size = texture_size;

  psl->color_pass = DRW_pass_create(
      "color Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_WRITE_DEPTH);
  stl->g_data->multipass_shgrp = DRW_shgroup_create(lanpr_share.multichannel_shader,
                                                    psl->color_pass);

  if (lanpr->master_mode == LANPR_MASTER_MODE_DPIX && lanpr->active_layer &&
      !lanpr_share.dpix_shader_error) {
    LANPR_LineLayer *ll = lanpr->line_layers.first;
    psl->dpix_transform_pass = DRW_pass_create("DPIX Transform Stage", DRW_STATE_WRITE_COLOR);
    stl->g_data->dpix_transform_shgrp = DRW_shgroup_create(lanpr_share.dpix_transform_shader,
                                                           psl->dpix_transform_pass);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "tex_vert0", &txl->dpix_in_pl);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "tex_vert1", &txl->dpix_in_pr);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "tex_fnormal0", &txl->dpix_in_nl);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "tex_fnormal1", &txl->dpix_in_nr);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "tex_edge_mask", &txl->dpix_in_edge_mask);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "sample_step", &stl->g_data->dpix_sample_step, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "is_perspective", &stl->g_data->dpix_is_perspective, 1);
    DRW_shgroup_uniform_vec4(
        stl->g_data->dpix_transform_shgrp, "viewport", stl->g_data->dpix_viewport, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "buffer_width", &stl->g_data->dpix_buffer_width, 1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_transform_shgrp, "crease_threshold", &lanpr->crease_threshold, 1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_transform_shgrp,
                              "crease_fade_threshold",
                              &lanpr->crease_fade_threshold,
                              1);
    DRW_shgroup_uniform_int(stl->g_data->dpix_transform_shgrp, "use_contour", &ll->contour.use, 1);
    DRW_shgroup_uniform_int(stl->g_data->dpix_transform_shgrp, "use_crease", &ll->crease.use, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "use_material", &ll->material_separate.use, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "use_edge_mark", &ll->edge_mark.use, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "use_intersection", &ll->intersection.use, 1);

    psl->dpix_preview_pass = DRW_pass_create("DPIX Preview",
                                             DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                                                 DRW_STATE_DEPTH_LESS_EQUAL);
    stl->g_data->dpix_preview_shgrp = DRW_shgroup_create(lanpr_share.dpix_preview_shader,
                                                         psl->dpix_preview_pass);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_preview_shgrp, "tex_vert0", &txl->dpix_out_pl);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_preview_shgrp, "tex_vert1", &txl->dpix_out_pr);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_preview_shgrp, "tex_fnormal0", &txl->dpix_in_nl);
    DRW_shgroup_uniform_texture_ref(stl->g_data->dpix_preview_shgrp,
                                    "tex_fnormal1",
                                    &txl->dpix_in_nr); /*  these are for normal shading */
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_preview_shgrp, "tex_edge_mask", &txl->dpix_in_edge_mask);
    DRW_shgroup_uniform_vec4(
        stl->g_data->dpix_preview_shgrp, "viewport", stl->g_data->dpix_viewport, 1);
    DRW_shgroup_uniform_vec4(stl->g_data->dpix_preview_shgrp,
                             "color_contour",
                             (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color :
                                                                             ll->contour.color,
                             1);
    DRW_shgroup_uniform_vec4(stl->g_data->dpix_preview_shgrp,
                             "color_crease",
                             (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color :
                                                                             ll->crease.color,
                             1);
    DRW_shgroup_uniform_vec4(
        stl->g_data->dpix_preview_shgrp,
        "color_material",
        (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color : ll->material_separate.color,
        1);
    DRW_shgroup_uniform_vec4(stl->g_data->dpix_preview_shgrp,
                             "color_edge_mark",
                             (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color :
                                                                             ll->edge_mark.color,
                             1);
    DRW_shgroup_uniform_vec4(
        stl->g_data->dpix_preview_shgrp,
        "color_intersection",
        (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? ll->color : ll->intersection.color,
        1);
    static float use_background_color[4];
    copy_v3_v3(use_background_color, &scene->world->horr);
    use_background_color[3] = scene->r.alphamode ? 0.0f : 1.0f;

    DRW_shgroup_uniform_vec4(
        stl->g_data->dpix_preview_shgrp, "background_color", use_background_color, 1);

    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "depth_offset", &stl->g_data->dpix_depth_offset, 1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "depth_width_influence",
                              &lanpr->depth_width_influence,
                              1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "depth_width_curve", &lanpr->depth_width_curve, 1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "depth_alpha_influence",
                              &lanpr->depth_alpha_influence,
                              1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "depth_alpha_curve", &lanpr->depth_alpha_curve, 1);
    static float unit_thickness = 1.0f;
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "line_thickness", &ll->thickness, 1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp,
        "thickness_contour",
        (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? &unit_thickness : &ll->contour.thickness,
        1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp,
        "thickness_crease",
        (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? &unit_thickness : &ll->crease.thickness,
        1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "thickness_material",
                              (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                  &unit_thickness :
                                  &ll->material_separate.thickness,
                              1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp,
        "thickness_edge_mark",
        (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ? &unit_thickness : &ll->edge_mark.thickness,
        1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "thickness_intersection",
                              (ll->flags & LANPR_LINE_LAYER_USE_SAME_STYLE) ?
                                  &unit_thickness :
                                  &ll->intersection.thickness,
                              1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "z_near", &stl->g_data->dpix_znear, 1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "z_far", &stl->g_data->dpix_zfar, 1);

    ED_lanpr_calculate_normal_object_vector(ll, normal_object_direction);

    static int normal_effect_inverse;
    static int zero_value = 0;
    normal_effect_inverse = (ll->flags & LANPR_LINE_LAYER_NORMAL_INVERSE);

    DRW_shgroup_uniform_int(stl->g_data->dpix_preview_shgrp,
                            "normal_mode",
                            (ll->flags & LANPR_LINE_LAYER_NORMAL_ENABLED) ? &ll->normal_mode :
                                                                            &zero_value,
                            1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_preview_shgrp, "normal_effect_inverse", &normal_effect_inverse, 1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "normal_ramp_begin", &ll->normal_ramp_begin, 1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "normal_ramp_end", &ll->normal_ramp_end, 1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "normal_thickness_start", &ll->normal_thickness_start, 1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "normal_thickness_end", &ll->normal_thickness_end, 1);
    DRW_shgroup_uniform_vec3(
        stl->g_data->dpix_preview_shgrp, "normal_direction", normal_object_direction, 1);

    pd->begin_index = 0;
    int fsize = sizeof(float) * 4 * texture_size * texture_size;

    if (lanpr_share.dpix_reloaded) {
      pd->atlas_pl = MEM_callocN(fsize, "atlas_point_l");
      pd->atlas_pr = MEM_callocN(fsize, "atlas_point_r");
      pd->atlas_nl = MEM_callocN(fsize, "atlas_normal_l");
      pd->atlas_nr = MEM_callocN(fsize, "atlas_normal_l");
      pd->atlas_edge_mask = MEM_callocN(fsize, "atlas_edge_mask"); /*  should always be float */

      lanpr_dpix_batch_free();

      BLI_mempool_clear(lanpr_share.mp_batch_list);
    }
  }
  else if (lanpr->master_mode == LANPR_MASTER_MODE_SOFTWARE) {
    ;
  }

  /* Intersection cache must be calculated before drawing. */
  if (draw_ctx->scene->lanpr.flags & LANPR_AUTO_UPDATE) {

    /** This will be released by compute function when loading is finished. */
    BLI_spin_lock(&lanpr_share.lock_loader);

    if (draw_ctx->scene->lanpr.master_mode == LANPR_MASTER_MODE_SOFTWARE) {
      if (is_render) {
        ED_lanpr_compute_feature_lines_internal(draw_ctx->depsgraph, 0);
      }
      else {
        ED_lanpr_compute_feature_lines_background(draw_ctx->depsgraph, 0);
      }
    }
    else if (draw_ctx->scene->lanpr.master_mode == LANPR_MASTER_MODE_DPIX) {
      /* Don't do threaded intersection calculation. It's pointless in GPU mode anyway. */
      ED_lanpr_compute_feature_lines_internal(draw_ctx->depsgraph, 1);
    }
  }

  /** Important: This ensures we don't proceed beyond this point until everything is loaded. */
  BLI_spin_lock(&lanpr_share.lock_loader);
  BLI_spin_unlock(&lanpr_share.lock_loader);

  if (ED_lanpr_calculation_flag_check(LANPR_RENDER_FINISHED) ||
      ED_lanpr_calculation_flag_check(LANPR_RENDER_IDLE)) {
    ED_lanpr_rebuild_all_command(draw_ctx->scene);
    ED_lanpr_calculation_set_flag(LANPR_RENDER_IDLE);
  }
  else if (!is_render) {
    DRW_viewport_request_redraw();
  }
}

static void lanpr_cache_populate(void *vedata, Object *ob)
{

  LANPR_StorageList *stl = ((LANPR_Data *)vedata)->stl;
  LANPR_PrivateData *pd = stl->g_data;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  SceneLANPR *lanpr = &draw_ctx->scene->lanpr;
  int usage = OBJECT_FEATURE_LINE_INHERENT, dpix_ok = 0;

  if (!DRW_object_is_renderable(ob)) {
    return;
  }
  if (ob == draw_ctx->object_edit) {
    return;
  }
  if (ob->type != OB_MESH) {
    return;
  }

  struct GPUBatch *geom = DRW_cache_object_surface_get(ob);
  if (geom) {
    if ((dpix_ok = (lanpr->master_mode == LANPR_MASTER_MODE_DPIX && lanpr->active_layer &&
                    !lanpr_share.dpix_shader_error)) != 0) {
      usage = ED_lanpr_object_collection_usage_check(draw_ctx->scene->master_collection, ob);
      if (usage == OBJECT_FEATURE_LINE_EXCLUDE) {
        return;
      }
    }
    DRW_shgroup_call_no_cull(stl->g_data->multipass_shgrp, geom, ob);
  }

  if (dpix_ok) {

    /* usage already set */
    if (usage == OBJECT_FEATURE_LINE_OCCLUSION_ONLY) {
      return;
    }

    int idx = pd->begin_index;
    if (lanpr_share.dpix_reloaded) {
      pd->begin_index = lanpr_feed_atlas_data_obj(vedata,
                                                  pd->atlas_pl,
                                                  pd->atlas_pr,
                                                  pd->atlas_nl,
                                                  pd->atlas_nr,
                                                  pd->atlas_edge_mask,
                                                  ob,
                                                  idx);
      if (idx != pd->begin_index) { /* Which means we are actually able to feed object data */
        lanpr_feed_atlas_trigger_preview_obj(vedata, ob, idx);
      }
    }
  }
}

static void lanpr_cache_finish(void *vedata)
{
  LANPR_StorageList *stl = ((LANPR_Data *)vedata)->stl;
  LANPR_PrivateData *pd = stl->g_data;
  LANPR_TextureList *txl = ((LANPR_Data *)vedata)->txl;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  SceneLANPR *lanpr = &draw_ctx->scene->lanpr;
  float mat[4][4];
  unit_m4(mat);

  if (lanpr->master_mode == LANPR_MASTER_MODE_DPIX && lanpr->active_layer &&
      !lanpr_share.dpix_shader_error) {
    if (lanpr_share.dpix_reloaded) {
      if (lanpr_share.render_buffer_shared) {
        lanpr_feed_atlas_data_intersection_cache(vedata,
                                                 pd->atlas_pl,
                                                 pd->atlas_pr,
                                                 pd->atlas_nl,
                                                 pd->atlas_nr,
                                                 pd->atlas_edge_mask,
                                                 pd->begin_index);
        lanpr_create_atlas_intersection_preview(vedata, pd->begin_index);
      }
      GPU_texture_update(txl->dpix_in_pl, GPU_DATA_FLOAT, pd->atlas_pl);
      GPU_texture_update(txl->dpix_in_pr, GPU_DATA_FLOAT, pd->atlas_pr);
      GPU_texture_update(txl->dpix_in_nl, GPU_DATA_FLOAT, pd->atlas_nl);
      GPU_texture_update(txl->dpix_in_nr, GPU_DATA_FLOAT, pd->atlas_nr);
      GPU_texture_update(txl->dpix_in_edge_mask, GPU_DATA_FLOAT, pd->atlas_edge_mask);

      MEM_freeN(pd->atlas_pl);
      MEM_freeN(pd->atlas_pr);
      MEM_freeN(pd->atlas_nl);
      MEM_freeN(pd->atlas_nr);
      MEM_freeN(pd->atlas_edge_mask);
      pd->atlas_pl = 0;
      lanpr_share.dpix_reloaded = 0;
    }

    LANPR_BatchItem *bi;
    for (bi = lanpr_share.dpix_batch_list.first; bi; bi = (void *)bi->item.next) {
      DRW_shgroup_call_ex(
          pd->dpix_transform_shgrp, 0, bi->ob->obmat, bi->dpix_transform_batch, true, NULL);
      DRW_shgroup_call(pd->dpix_preview_shgrp, bi->dpix_preview_batch, 0);
    }

    if (lanpr_share.render_buffer_shared &&
        lanpr_share.render_buffer_shared->DPIXIntersectionBatch) {
      DRW_shgroup_call(pd->dpix_transform_shgrp,
                       lanpr_share.render_buffer_shared->DPIXIntersectionTransformBatch,
                       0);
      DRW_shgroup_call(
          pd->dpix_preview_shgrp, lanpr_share.render_buffer_shared->DPIXIntersectionBatch, 0);
    }
  }
}

static void lanpr_draw_scene_exec(void *vedata, GPUFrameBuffer *dfb, int is_render)
{
  LANPR_PassList *psl = ((LANPR_Data *)vedata)->psl;
  LANPR_TextureList *txl = ((LANPR_Data *)vedata)->txl;
  LANPR_StorageList *stl = ((LANPR_Data *)vedata)->stl;
  LANPR_FramebufferList *fbl = ((LANPR_Data *)vedata)->fbl;

  float clear_col[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  float clear_depth = 1.0f;
  uint clear_stencil = 0xFF;

  GPU_framebuffer_bind(fbl->passes);
  eGPUFrameBufferBits clear_bits = GPU_DEPTH_BIT | GPU_COLOR_BIT;
  GPU_framebuffer_clear(fbl->passes, clear_bits, clear_col, clear_depth, clear_stencil);

  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = DEG_get_evaluated_scene(draw_ctx->depsgraph);
  SceneLANPR *lanpr = &scene->lanpr;

  if (lanpr->master_mode == LANPR_MASTER_MODE_DPIX && !lanpr_share.dpix_shader_error) {
    DRW_draw_pass(psl->color_pass);
    lanpr_dpix_draw_scene(txl, fbl, psl, stl->g_data, lanpr, dfb, is_render);
  }
  else if (lanpr->master_mode == LANPR_MASTER_MODE_SOFTWARE) {
    lanpr_software_draw_scene(vedata, dfb, is_render);
  }
}

static void lanpr_draw_scene(void *vedata)
{
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

  lanpr_draw_scene_exec(vedata, dfbl->default_fb, 0);
}

static void lanpr_render_cache(void *vedata,
                               struct Object *ob,
                               struct RenderEngine *UNUSED(engine),
                               struct Depsgraph *UNUSED(depsgraph))
{

  lanpr_cache_populate(vedata, ob);
}

static void lanpr_render_matrices_init(RenderEngine *engine, Depsgraph *depsgraph)
{
  /* TODO(sergey): Shall render hold pointer to an evaluated camera instead? */
  Scene *scene = DEG_get_evaluated_scene(depsgraph);
  struct Object *ob_camera_eval = DEG_get_evaluated_object(depsgraph, RE_GetCamera(engine->re));
  float frame = BKE_scene_frame_get(scene);

  /* Set the persective, view and window matrix. */
  float winmat[4][4], wininv[4][4];
  float viewmat[4][4], viewinv[4][4];
  float persmat[4][4], persinv[4][4];
  float unitmat[4][4];

  RE_GetCameraWindow(engine->re, ob_camera_eval, winmat);
  RE_GetCameraModelMatrix(engine->re, ob_camera_eval, viewinv);

  invert_m4_m4(viewmat, viewinv);
  mul_m4_m4m4(persmat, winmat, viewmat);
  invert_m4_m4(persinv, persmat);
  invert_m4_m4(wininv, winmat);

  unit_m4(unitmat);

  DRWView *view = DRW_view_create(viewmat, winmat, NULL, NULL, NULL);
  DRW_view_default_set(view);
  DRW_view_set_active(view);
}

static int LANPR_GLOBAL_update_tag;

static void lanpr_id_update(void *UNUSED(vedata), ID *id)
{
  /*  if (vedata->engine_type != &draw_engine_lanpr_type) return; */

  /* Handle updates based on ID type. */
  switch (GS(id->name)) {
    case ID_WO:
    case ID_OB:
    case ID_ME:
      LANPR_GLOBAL_update_tag = 1;
    default:
      /* pass */
      break;
  }
}

static void lanpr_render_to_image(void *vedata,
                                  RenderEngine *engine,
                                  struct RenderLayer *render_layer,
                                  const rcti *rect)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  lanpr_share.re_render = engine;

  RE_engine_update_stats(engine, NULL, "LANPR: Initializing");

  lanpr_render_matrices_init(engine, draw_ctx->depsgraph);

  /* refered to eevee's code */

  /* Init default FB and render targets:
   * In render mode the default framebuffer is not generated
   * because there is no viewport. So we need to manually create it or
   * not use it. For code clarity we just allocate it make use of it. */
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();

  DRW_texture_ensure_fullscreen_2d(&dtxl->depth, GPU_DEPTH_COMPONENT32F, 0);
  DRW_texture_ensure_fullscreen_2d(&dtxl->color, GPU_RGBA32F, 0);

  GPU_framebuffer_ensure_config(
      &dfbl->default_fb,
      {GPU_ATTACHMENT_TEXTURE(dtxl->depth), GPU_ATTACHMENT_TEXTURE(dtxl->color)});

  lanpr_engine_init(vedata);

  /*  force dpix batch to re-create */
  lanpr_share.dpix_reloaded_deg = 1;

  /* force use actual camera instead of viewport camera. */
  /* Do this before creating render buffer. */
  lanpr_share.viewport_camera_override = -1;
  lanpr_cache_init(vedata);
  DRW_render_object_iter(vedata, engine, draw_ctx->depsgraph, lanpr_render_cache);
  lanpr_cache_finish(vedata);

  /* get ref for destroy data */
  /*  lanpr_share.rb_ref = lanpr->render_buffer; */

  DRW_render_instance_buffer_finish();

  float clear_col[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float clear_depth = 1.0f;
  uint clear_stencil = 0xFF;
  eGPUFrameBufferBits clear_bits = GPU_DEPTH_BIT | GPU_COLOR_BIT;

  GPU_framebuffer_bind(dfbl->default_fb);
  GPU_framebuffer_clear(dfbl->default_fb, clear_bits, clear_col, clear_depth, clear_stencil);

  lanpr_draw_scene_exec(vedata, dfbl->default_fb, 1);

  /*  read it back so we can again display and save it. */
  const char *viewname = RE_GetActiveRenderView(engine->re);
  RenderPass *rp = RE_pass_find_by_name(render_layer, RE_PASSNAME_COMBINED, viewname);
  GPU_framebuffer_bind(dfbl->default_fb);
  GPU_framebuffer_read_color(dfbl->default_fb,
                             rect->xmin,
                             rect->ymin,
                             BLI_rcti_size_x(rect),
                             BLI_rcti_size_y(rect),
                             4,
                             0,
                             rp->rect);

  /* Must clear to avoid other problems. */
  lanpr_share.re_render = NULL;
}

static void lanpr_view_update(void *UNUSED(vedata))
{
  lanpr_share.dpix_reloaded_deg = 1; /*  very bad solution, this will slow down animation. */
}

static const DrawEngineDataSize lanpr_data_size = DRW_VIEWPORT_DATA_SIZE(LANPR_Data);

DrawEngineType draw_engine_lanpr_type = {
    NULL,
    NULL,
    N_("LANPR"),
    &lanpr_data_size,
    &lanpr_engine_init,
    &lanpr_engine_free,
    &lanpr_cache_init,
    &lanpr_cache_populate,
    &lanpr_cache_finish,
    NULL,              /*  draw background */
    &lanpr_draw_scene, /*  draw scene */
    &lanpr_view_update,
    &lanpr_id_update,
    &lanpr_render_to_image,
};

RenderEngineType DRW_engine_viewport_lanpr_type = {NULL,
                                                   NULL,
                                                   LANPR_ENGINE,
                                                   N_("LANPR"),
                                                   RE_INTERNAL,
                                                   NULL,                 /*  update */
                                                   &DRW_render_to_image, /*  render to img */
                                                   NULL,                 /*  bake */
                                                   NULL,                 /*  view update */
                                                   NULL,                 /*  render to view */
                                                   NULL,                 /*  update in script */
                                                   NULL, /*  update in render pass */
                                                   &draw_engine_lanpr_type,
                                                   {NULL, NULL, NULL}};
