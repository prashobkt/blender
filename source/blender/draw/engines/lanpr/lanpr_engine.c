#include "DRW_engine.h"
#include "DRW_render.h"
#include "BLI_listbase.h"
#include "BLI_linklist.h"
#include "BLI_math_matrix.h"
#include "lanpr_all.h"
#include "DRW_render.h"
#include "BKE_object.h"
#include "DNA_mesh_types.h"
#include "DNA_camera_types.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_framebuffer.h"
#include "DNA_lanpr_types.h"
#include "GPU_draw.h"
#include "DEG_depsgraph_query.h"
#include "RE_pipeline.h"
#include "BLI_rect.h"

#include "GPU_batch.h"
#include "GPU_framebuffer.h"
#include "GPU_shader.h"
#include "GPU_uniformbuffer.h"
#include "GPU_viewport.h"
#include "bmesh.h"

#include <math.h>

extern char datatoc_common_fullscreen_vert_glsl[];
extern char datatoc_gpu_shader_3D_smooth_color_vert_glsl[];
extern char datatoc_gpu_shader_3D_smooth_color_frag_glsl[];
extern char datatoc_lanpr_snake_multichannel_frag_glsl[];
extern char datatoc_lanpr_snake_edge_frag_glsl[];
extern char datatoc_lanpr_snake_image_peel_frag_glsl[];
extern char datatoc_lanpr_snake_line_connection_vert_glsl[];
extern char datatoc_lanpr_snake_line_connection_frag_glsl[];
extern char datatoc_lanpr_snake_line_connection_geom_glsl[];
extern char datatoc_lanpr_software_line_chain_geom_glsl[];
extern char datatoc_lanpr_software_chain_geom_glsl[];
extern char datatoc_lanpr_dpix_project_passthrough_vert_glsl[];
extern char datatoc_lanpr_dpix_project_clip_frag_glsl[];
extern char datatoc_lanpr_dpix_preview_frag_glsl[];
extern char datatoc_lanpr_software_passthrough_vert_glsl[];
extern char datatoc_gpu_shader_2D_smooth_color_vert_glsl[];
extern char datatoc_gpu_shader_2D_smooth_color_frag_glsl[];

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

#if 0 /* Deprecated: snake mode */
  GPU_framebuffer_ensure_config(&fbl->edge_thinning,
                                {GPU_ATTACHMENT_LEAVE,
                                 GPU_ATTACHMENT_TEXTURE(txl->color)});

  if (!lanpr_share.edge_detect_shader) {
    lanpr_share.edge_detect_shader = DRW_shader_create(
        datatoc_common_fullscreen_vert_glsl, NULL, datatoc_lanpr_snake_edge_frag_glsl, NULL);
  }
  if (!lanpr_share.edge_thinning_shader) {
    lanpr_share.edge_thinning_shader = DRW_shader_create(
        datatoc_common_fullscreen_vert_glsl, NULL, datatoc_lanpr_snake_image_peel_frag_glsl, NULL);
  }
  if (!lanpr_share.snake_connection_shader) {
    lanpr_share.snake_connection_shader = DRW_shader_create(
        datatoc_lanpr_snake_line_connection_vert_glsl,
        datatoc_lanpr_snake_line_connection_geom_glsl,
        datatoc_lanpr_snake_line_connection_frag_glsl,
        NULL);
  }
#endif

  DRW_texture_ensure_fullscreen_2D_multisample(&txl->depth, GPU_DEPTH_COMPONENT32F, 8, 0);
  DRW_texture_ensure_fullscreen_2D_multisample(&txl->color, GPU_RGBA32F, 8, 0);
  DRW_texture_ensure_fullscreen_2D_multisample(&txl->normal, GPU_RGBA32F, 8, 0);
  DRW_texture_ensure_fullscreen_2D_multisample(&txl->edge_intermediate, GPU_RGBA32F, 8, 0);

  DRW_texture_ensure_fullscreen_2D_multisample(
      &txl->ms_resolve_depth, GPU_DEPTH_COMPONENT32F, 8, 0);
  DRW_texture_ensure_fullscreen_2D_multisample(&txl->ms_resolve_color, GPU_RGBA32F, 8, 0);

  GPU_framebuffer_ensure_config(&fbl->passes,
                                {GPU_ATTACHMENT_TEXTURE(txl->depth),
                                 GPU_ATTACHMENT_TEXTURE(txl->color),
                                 GPU_ATTACHMENT_TEXTURE(txl->normal)});

  GPU_framebuffer_ensure_config(
      &fbl->edge_intermediate,
      {GPU_ATTACHMENT_TEXTURE(txl->depth), GPU_ATTACHMENT_TEXTURE(txl->edge_intermediate)});

  if (!lanpr_share.multichannel_shader) {
    lanpr_share.multichannel_shader = DRW_shader_create(
        datatoc_gpu_shader_3D_smooth_color_vert_glsl,
        NULL,
        datatoc_gpu_shader_3D_smooth_color_frag_glsl,
        NULL);
  }

  /* DPIX */
  lanpr_init_atlas_inputs(ved);

  /* SOFTWARE */
  if (!lanpr_share.software_shader) {
    lanpr_share.software_shader = DRW_shader_create(datatoc_lanpr_software_passthrough_vert_glsl,
                                                    datatoc_lanpr_software_line_chain_geom_glsl,
                                                    datatoc_lanpr_dpix_preview_frag_glsl,
                                                    NULL);
  }

  if (!lanpr_share.software_chaining_shader) {
    lanpr_share.software_chaining_shader = DRW_shader_create(
        datatoc_lanpr_software_passthrough_vert_glsl,
        datatoc_lanpr_software_chain_geom_glsl,
        datatoc_lanpr_dpix_preview_frag_glsl,
        NULL);
  }

  GPU_framebuffer_ensure_config(&fbl->software_ms,
                                {GPU_ATTACHMENT_TEXTURE(txl->ms_resolve_depth),
                                 GPU_ATTACHMENT_TEXTURE(txl->ms_resolve_color)});

  lanpr_share.init_complete = 1;
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

static void lanpr_engine_free(void)
{
  DRW_SHADER_FREE_SAFE(lanpr_share.multichannel_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.snake_connection_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.software_chaining_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.dpix_preview_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.dpix_transform_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.edge_detect_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.edge_thinning_shader);
  DRW_SHADER_FREE_SAFE(lanpr_share.software_shader);

  lanpr_dpix_batch_free();

  if (lanpr_share.render_buffer_shared) {
    LANPR_RenderBuffer *rb = lanpr_share.render_buffer_shared;
    ED_lanpr_destroy_render_data(rb);

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

  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();

  static float normal_object_direction[3] = {0, 0, 1};

  /* Transfer reload state */
  lanpr_share.dpix_reloaded = lanpr_share.dpix_reloaded_deg;

  if (!stl->g_data) {
    /* Alloc transient pointers */
    stl->g_data = MEM_callocN(sizeof(*stl->g_data), __func__);
  }

  if (!lanpr_share.mp_batch_list) {
    lanpr_share.mp_batch_list = BLI_mempool_create(
        sizeof(LANPR_BatchItem), 0, 128, BLI_MEMPOOL_NOP);
  }

  LANPR_PrivateData *pd = stl->g_data;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = DEG_get_evaluated_scene(draw_ctx->depsgraph);
  SceneLANPR *lanpr = &scene->lanpr;

  int texture_size = lanpr_dpix_texture_size(lanpr);
  lanpr_share.texture_size = texture_size;

  psl->color_pass = DRW_pass_create(
      "color Pass", DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_WRITE_DEPTH);
  stl->g_data->multipass_shgrp = DRW_shgroup_create(lanpr_share.multichannel_shader,
                                                    psl->color_pass);

  if (lanpr->master_mode == LANPR_MASTER_MODE_SNAKE) {
    struct GPUBatch *quad = DRW_cache_fullscreen_quad_get();

    psl->edge_intermediate = DRW_pass_create("Edge Detection", DRW_STATE_WRITE_COLOR);
    stl->g_data->edge_detect_shgrp = DRW_shgroup_create(lanpr_share.edge_detect_shader,
                                                        psl->edge_intermediate);
    DRW_shgroup_uniform_texture_ref(stl->g_data->edge_detect_shgrp, "tex_sample_0", &txl->depth);
    DRW_shgroup_uniform_texture_ref(stl->g_data->edge_detect_shgrp, "tex_sample_1", &txl->color);
    DRW_shgroup_uniform_texture_ref(stl->g_data->edge_detect_shgrp, "tex_sample_2", &txl->normal);

    DRW_shgroup_uniform_float(stl->g_data->edge_detect_shgrp, "z_near", &stl->g_data->znear, 1);
    DRW_shgroup_uniform_float(stl->g_data->edge_detect_shgrp, "z_far", &stl->g_data->zfar, 1);

    DRW_shgroup_uniform_float(stl->g_data->edge_detect_shgrp,
                              "normal_clamp",
                              &stl->g_data->normal_clamp,
                              1); /*  normal clamp */
    DRW_shgroup_uniform_float(stl->g_data->edge_detect_shgrp,
                              "normal_strength",
                              &stl->g_data->normal_strength,
                              1); /*  normal strength */
    DRW_shgroup_uniform_float(stl->g_data->edge_detect_shgrp,
                              "depth_clamp",
                              &stl->g_data->depth_clamp,
                              1); /*  depth clamp */
    DRW_shgroup_uniform_float(stl->g_data->edge_detect_shgrp,
                              "depth_strength",
                              &stl->g_data->depth_strength,
                              1); /*  depth strength */
    DRW_shgroup_call(stl->g_data->edge_detect_shgrp, quad, NULL);

    psl->edge_thinning = DRW_pass_create("Edge Thinning Stage 1", DRW_STATE_WRITE_COLOR);
    stl->g_data->edge_thinning_shgrp = DRW_shgroup_create(lanpr_share.edge_thinning_shader,
                                                          psl->edge_thinning);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->edge_thinning_shgrp, "tex_sample_0", &dtxl->color);
    DRW_shgroup_uniform_int(stl->g_data->edge_thinning_shgrp, "stage", &stl->g_data->stage, 1);
    DRW_shgroup_call(stl->g_data->edge_thinning_shgrp, quad, NULL);
  }
  else if (lanpr->master_mode == LANPR_MASTER_MODE_DPIX && lanpr->active_layer &&
           !lanpr_share.dpix_shader_error) {
    LANPR_LineLayer *ll = lanpr->line_layers.first;
    psl->dpix_transform_pass = DRW_pass_create("DPIX Transform Stage", DRW_STATE_WRITE_COLOR);
    stl->g_data->dpix_transform_shgrp = DRW_shgroup_create(lanpr_share.dpix_transform_shader,
                                                           psl->dpix_transform_pass);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "vert0_tex", &txl->dpix_in_pl);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "vert1_tex", &txl->dpix_in_pr);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "face_normal0_tex", &txl->dpix_in_nl);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "face_normal1_tex", &txl->dpix_in_nr);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_transform_shgrp, "edge_mask_tex", &txl->dpix_in_edge_mask);
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
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "enable_contour", &ll->contour.enabled, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "enable_crease", &ll->crease.enabled, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "enable_material", &ll->material_separate.enabled, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "enable_edge_mark", &ll->edge_mark.enabled, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_transform_shgrp, "enable_intersection", &ll->intersection.enabled, 1);

    psl->dpix_preview_pass = DRW_pass_create("DPIX Preview",
                                             DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                                                 DRW_STATE_DEPTH_LESS_EQUAL);
    stl->g_data->dpix_preview_shgrp = DRW_shgroup_create(lanpr_share.dpix_preview_shader,
                                                         psl->dpix_preview_pass);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_preview_shgrp, "vert0_tex", &txl->dpix_out_pl);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_preview_shgrp, "vert1_tex", &txl->dpix_out_pr);
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_preview_shgrp, "face_normal0_tex", &txl->dpix_in_nl);
    DRW_shgroup_uniform_texture_ref(stl->g_data->dpix_preview_shgrp,
                                    "face_normal1_tex",
                                    &txl->dpix_in_nr); /*  these are for normal shading */
    DRW_shgroup_uniform_texture_ref(
        stl->g_data->dpix_preview_shgrp, "edge_mask_tex", &txl->dpix_in_edge_mask);
    DRW_shgroup_uniform_vec4(
        stl->g_data->dpix_preview_shgrp, "viewport", stl->g_data->dpix_viewport, 1);
    DRW_shgroup_uniform_vec4(stl->g_data->dpix_preview_shgrp,
                             "contour_color",
                             ll->use_same_style ? ll->color : ll->contour.color,
                             1);
    DRW_shgroup_uniform_vec4(stl->g_data->dpix_preview_shgrp,
                             "crease_color",
                             ll->use_same_style ? ll->color : ll->crease.color,
                             1);
    DRW_shgroup_uniform_vec4(stl->g_data->dpix_preview_shgrp,
                             "material_color",
                             ll->use_same_style ? ll->color : ll->material_separate.color,
                             1);
    DRW_shgroup_uniform_vec4(stl->g_data->dpix_preview_shgrp,
                             "edge_mark_color",
                             ll->use_same_style ? ll->color : ll->edge_mark.color,
                             1);
    DRW_shgroup_uniform_vec4(stl->g_data->dpix_preview_shgrp,
                             "intersection_color",
                             ll->use_same_style ? ll->color : ll->intersection.color,
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
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "line_thickness_contour",
                              ll->use_same_style ? &unit_thickness : &ll->contour.thickness,
                              1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "line_thickness_crease",
                              ll->use_same_style ? &unit_thickness : &ll->crease.thickness,
                              1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "line_thickness_material",
                              ll->use_same_style ? &unit_thickness :
                                                   &ll->material_separate.thickness,
                              1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "line_thickness_edge_mark",
                              ll->use_same_style ? &unit_thickness : &ll->edge_mark.thickness,
                              1);
    DRW_shgroup_uniform_float(stl->g_data->dpix_preview_shgrp,
                              "line_thickness_intersection",
                              ll->use_same_style ? &unit_thickness : &ll->intersection.thickness,
                              1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "z_near", &stl->g_data->dpix_znear, 1);
    DRW_shgroup_uniform_float(
        stl->g_data->dpix_preview_shgrp, "z_far", &stl->g_data->dpix_zfar, 1);

    ED_lanpr_calculate_normal_object_vector(ll, normal_object_direction);

    DRW_shgroup_uniform_int(stl->g_data->dpix_preview_shgrp, "normal_mode", &ll->normal_mode, 1);
    DRW_shgroup_uniform_int(
        stl->g_data->dpix_preview_shgrp, "normal_effect_inverse", &ll->normal_effect_inverse, 1);
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
  int updated = 0;
  if (draw_ctx->scene->lanpr.auto_update &&
      (!lanpr_share.render_buffer_shared ||
       lanpr_share.render_buffer_shared->cached_for_frame != draw_ctx->scene->r.cfra)) {
    if (draw_ctx->scene->lanpr.master_mode == LANPR_MASTER_MODE_SOFTWARE) {
      
      ED_lanpr_compute_feature_lines_background(draw_ctx->depsgraph, 0);
    }
    else if (draw_ctx->scene->lanpr.master_mode == LANPR_MASTER_MODE_DPIX) {
      ED_lanpr_compute_feature_lines_background(draw_ctx->depsgraph, 1);
    }
  }

  if (ED_lanpr_calculation_flag_check(LANPR_RENDER_FINISHED)) {
    ED_lanpr_rebuild_all_command(&draw_ctx->scene->lanpr);
    ED_lanpr_calculation_set_flag(LANPR_RENDER_IDLE);
  }else{
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
      lanpr_feed_atlas_trigger_preview_obj(vedata, ob, idx);
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
          pd->dpix_transform_shgrp, 0, bi->ob->obmat, bi->dpix_transform_batch, 0, 0, true, NULL);
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
  /* Deprecated
  else if (lanpr->master_mode == LANPR_MASTER_MODE_SNAKE) {
    DRW_draw_pass(psl->color_pass);
    lanpr_snake_draw_scene(txl, fbl, psl, stl->g_data, lanpr, dfb, is_render);
  }
  */
  else if (lanpr->master_mode == LANPR_MASTER_MODE_SOFTWARE) {
    /*  should isolate these into a seperate function. */
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

  RE_GetCameraWindow(engine->re, ob_camera_eval, frame, winmat);
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
  /*  int update_mark = DEG_id_type_any_updated(draw_ctx->depsgraph); */
  Scene *scene = DEG_get_evaluated_scene(draw_ctx->depsgraph);
  SceneLANPR *lanpr = &scene->lanpr;

  lanpr_share.re_render = engine;

  RE_engine_update_stats(engine, NULL, "LANPR: Initializing");

  if (lanpr->master_mode == LANPR_MASTER_MODE_SOFTWARE ||
      (lanpr->master_mode == LANPR_MASTER_MODE_DPIX && lanpr->enable_intersections)) {
    if (!lanpr_share.render_buffer_shared) {
      ED_lanpr_create_render_buffer();
    }
    if (lanpr_share.render_buffer_shared->cached_for_frame != scene->r.cfra ||
        LANPR_GLOBAL_update_tag) {
      int intersections_only = (lanpr->master_mode != LANPR_MASTER_MODE_SOFTWARE);
      ED_lanpr_compute_feature_lines_internal(draw_ctx->depsgraph, intersections_only);
    }
  }

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
  lanpr_share.dpix_reloaded_deg = 1; /*  force dpix batch to re-create */
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
