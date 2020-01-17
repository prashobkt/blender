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
#include "DRW_engine.h"
#include "DRW_render.h"

#include "BKE_gpencil.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_shader_fx.h"

#include "BKE_camera.h"
#include "BKE_global.h" /* for G.debug */

#include "BLI_link_utils.h"
#include "BLI_memblock.h"

#include "DNA_camera_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "GPU_texture.h"
#include "GPU_uniformbuffer.h"

#include "gpencil_engine.h"

#include "DEG_depsgraph_query.h"

#include "ED_view3d.h"
#include "ED_screen.h"

#include "UI_resources.h"

/* *********** FUNCTIONS *********** */

void GPENCIL_engine_init(void *ved)
{
  GPENCIL_Data *vedata = (GPENCIL_Data *)ved;
  GPENCIL_StorageList *stl = vedata->stl;
  GPENCIL_TextureList *txl = vedata->txl;
  GPENCIL_FramebufferList *fbl = vedata->fbl;
  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  const DRWContextState *ctx = DRW_context_state_get();
  const View3D *v3d = ctx->v3d;

  if (!stl->pd) {
    stl->pd = MEM_callocN(sizeof(GPENCIL_PrivateData), "GPENCIL_PrivateData");
  }

  if (txl->dummy_texture == NULL) {
    float pixels[1][4] = {{1.0f, 0.0f, 1.0f, 1.0f}};
    txl->dummy_texture = DRW_texture_create_2d(1, 1, GPU_RGBA8, DRW_TEX_WRAP, (float *)pixels);
  }

  GPENCIL_ViewLayerData *vldata = GPENCIL_view_layer_data_ensure();

  /* Resize and reset memblocks. */
  BLI_memblock_clear(vldata->gp_light_pool, gpencil_light_pool_free);
  BLI_memblock_clear(vldata->gp_material_pool, gpencil_material_pool_free);
  BLI_memblock_clear(vldata->gp_object_pool, NULL);
  BLI_memblock_clear(vldata->gp_layer_pool, NULL);
  BLI_memblock_clear(vldata->gp_vfx_pool, NULL);

  stl->pd->gp_light_pool = vldata->gp_light_pool;
  stl->pd->gp_material_pool = vldata->gp_material_pool;
  stl->pd->gp_object_pool = vldata->gp_object_pool;
  stl->pd->gp_layer_pool = vldata->gp_layer_pool;
  stl->pd->gp_vfx_pool = vldata->gp_vfx_pool;
  stl->pd->last_light_pool = NULL;
  stl->pd->last_material_pool = NULL;
  stl->pd->tobjects.first = NULL;
  stl->pd->tobjects.last = NULL;
  stl->pd->sbuffer_tobjects.first = NULL;
  stl->pd->sbuffer_tobjects.last = NULL;
  stl->pd->dummy_tx = txl->dummy_texture;
  stl->pd->draw_depth_only = !DRW_state_is_fbo() || (v3d && v3d->shading.type == OB_WIRE);
  stl->pd->scene_depth_tx = stl->pd->draw_depth_only ? txl->dummy_texture : dtxl->depth;
  stl->pd->scene_fb = dfbl->default_fb;
  stl->pd->is_render = txl->render_depth_tx || (v3d && v3d->shading.type == OB_RENDER);
  stl->pd->global_light_pool = gpencil_light_pool_add(stl->pd);
  stl->pd->shadeless_light_pool = gpencil_light_pool_add(stl->pd);
  /* Small HACK: we don't want the global pool to be reused,
   * so we set the last light pool to NULL. */
  stl->pd->last_light_pool = NULL;

  bool use_scene_lights = false;
  bool use_scene_world = false;

  if (v3d) {
    use_scene_lights = ((v3d->shading.type == OB_MATERIAL) &&
                        (v3d->shading.flag & V3D_SHADING_SCENE_LIGHTS)) ||
                       ((v3d->shading.type == OB_RENDER) &&
                        (v3d->shading.flag & V3D_SHADING_SCENE_LIGHTS_RENDER));

    use_scene_world = ((v3d->shading.type == OB_MATERIAL) &&
                       (v3d->shading.flag & V3D_SHADING_SCENE_WORLD)) ||
                      ((v3d->shading.type == OB_RENDER) &&
                       (v3d->shading.flag & V3D_SHADING_SCENE_WORLD_RENDER));

    stl->pd->v3d_color_type = (v3d->shading.type == OB_SOLID) ? v3d->shading.color_type : -1;
    copy_v3_v3(stl->pd->v3d_single_color, v3d->shading.single_color);
  }

  stl->pd->use_lighting = (v3d && v3d->shading.type > OB_SOLID);
  stl->pd->use_lights = use_scene_lights;

  if (txl->render_depth_tx != NULL) {
    stl->pd->scene_depth_tx = txl->render_depth_tx;
    stl->pd->scene_fb = fbl->render_fb;
  }

  gpencil_light_ambient_add(stl->pd->shadeless_light_pool, (float[3]){1.0f, 1.0f, 1.0f});

  World *world = ctx->scene->world;
  if (world != NULL && use_scene_world) {
    gpencil_light_ambient_add(stl->pd->global_light_pool, &world->horr);
  }
  else if (v3d) {
    float world_light[3];
    copy_v3_fl(world_light, v3d->shading.studiolight_intensity);
    gpencil_light_ambient_add(stl->pd->global_light_pool, world_light);
  }

  float viewmatinv[4][4];
  DRW_view_viewmat_get(NULL, viewmatinv, true);
  copy_v3_v3(stl->pd->camera_z_axis, viewmatinv[2]);
  copy_v3_v3(stl->pd->camera_pos, viewmatinv[3]);
  stl->pd->camera_z_offset = dot_v3v3(viewmatinv[3], viewmatinv[2]);

  if (ctx && ctx->rv3d && v3d) {
    stl->pd->camera = (ctx->rv3d->persp == RV3D_CAMOB) ? v3d->camera : NULL;
  }
  else {
    stl->pd->camera = NULL;
  }
}

void GPENCIL_cache_init(void *ved)
{
  GPENCIL_Data *vedata = (GPENCIL_Data *)ved;
  GPENCIL_PassList *psl = vedata->psl;
  GPENCIL_TextureList *txl = vedata->txl;
  GPENCIL_FramebufferList *fbl = vedata->fbl;
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  DRWShadingGroup *grp;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  pd->cfra = (int)DEG_get_ctime(draw_ctx->depsgraph);
  pd->use_layer_fb = false;
  pd->use_object_fb = false;
  pd->use_mask_fb = false;
  pd->use_signed_fb = false;

  if (draw_ctx->v3d) {
    const bool hide_overlay = ((draw_ctx->v3d->flag2 & V3D_HIDE_OVERLAYS) != 0);
    const bool show_onion = ((draw_ctx->v3d->gp_flag & V3D_GP_SHOW_ONION_SKIN) != 0);
    const bool playing = (draw_ctx->evil_C != NULL) ?
                             ED_screen_animation_playing(CTX_wm_manager(draw_ctx->evil_C)) !=
                                 NULL :
                             false;
    pd->do_onion = show_onion && !hide_overlay && !playing;
    /* Save simplify flags (can change while drawing, so it's better to save). */
    Scene *scene = draw_ctx->scene;
    pd->simplify_fill = GPENCIL_SIMPLIFY_FILL(scene, playing);
    pd->simplify_fx = GPENCIL_SIMPLIFY_FX(scene, playing);
  }
  else {
    pd->do_onion = true;
    pd->simplify_fill = false;
    pd->simplify_fx = false;
  }

  {
    pd->sbuffer_stroke = NULL;
    pd->sbuffer_gpd = NULL;
    pd->sbuffer_layer = NULL;
    pd->stroke_batch = NULL;
    pd->fill_batch = NULL;
    pd->do_fast_drawing = false;

    pd->obact = draw_ctx->obact;
    if (pd->obact && pd->obact->type == OB_GPENCIL) {
      /* Check if active object has a temp stroke data. */
      bGPdata *gpd = (bGPdata *)pd->obact->data;
      /* Current stroke data is stored in the original id. This is waiting refactor of the
       * Depsgraph to support more granular update of the GPencil data. */
      bGPdata *gpd_orig = (bGPdata *)DEG_get_original_id(&gpd->id);
      if (gpd_orig->runtime.sbuffer_used > 0) {
        pd->sbuffer_gpd = gpd;
        pd->sbuffer_stroke = DRW_cache_gpencil_sbuffer_stroke_data_get(pd->obact);
        pd->sbuffer_layer = BKE_gpencil_layer_active_get(pd->sbuffer_gpd);
        pd->do_fast_drawing = false; /* TODO option */
      }
    }
  }

  if (pd->do_fast_drawing) {
    pd->snapshot_buffer_dirty = (txl->snapshot_color_tx == NULL);
    const float *size = DRW_viewport_size_get();
    DRW_texture_ensure_2d(&txl->snapshot_depth_tx, size[0], size[1], GPU_DEPTH24_STENCIL8, 0);
    DRW_texture_ensure_2d(&txl->snapshot_color_tx, size[0], size[1], GPU_R11F_G11F_B10F, 0);
    DRW_texture_ensure_2d(&txl->snapshot_reveal_tx, size[0], size[1], GPU_R11F_G11F_B10F, 0);

    GPU_framebuffer_ensure_config(&fbl->snapshot_fb,
                                  {
                                      GPU_ATTACHMENT_TEXTURE(txl->snapshot_depth_tx),
                                      GPU_ATTACHMENT_TEXTURE(txl->snapshot_color_tx),
                                      GPU_ATTACHMENT_TEXTURE(txl->snapshot_reveal_tx),
                                  });
  }
  else {
    /* Free uneeded buffers. */
    GPU_FRAMEBUFFER_FREE_SAFE(fbl->snapshot_fb);
    DRW_TEXTURE_FREE_SAFE(txl->snapshot_depth_tx);
    DRW_TEXTURE_FREE_SAFE(txl->snapshot_color_tx);
    DRW_TEXTURE_FREE_SAFE(txl->snapshot_reveal_tx);
  }

  {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_CUSTOM;
    DRW_PASS_CREATE(psl->composite_ps, state);

    GPUShader *sh = GPENCIL_shader_composite_get();
    grp = DRW_shgroup_create(sh, psl->composite_ps);
    DRW_shgroup_uniform_texture_ref(grp, "colorBuf", &pd->color_tx);
    DRW_shgroup_uniform_texture_ref(grp, "revealBuf", &pd->reveal_tx);
    DRW_shgroup_uniform_bool_copy(grp, "doSrgb", txl->render_depth_tx == NULL);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
  {
    DRWState state = DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS;
    DRW_PASS_CREATE(psl->merge_depth_ps, state);

    GPUShader *sh = GPENCIL_shader_depth_merge_get();
    grp = DRW_shgroup_create(sh, psl->merge_depth_ps);
    DRW_shgroup_uniform_texture_ref(grp, "depthBuf", &pd->depth_tx);
    DRW_shgroup_uniform_bool(grp, "strokeOrder3d", &pd->is_stroke_order_3d, 1);
    DRW_shgroup_uniform_vec4(grp, "gpModelMatrix[0]", pd->object_bound_mat[0], 4);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }

  Camera *cam = (pd->camera != NULL) ? pd->camera->data : NULL;

  /* Pseudo DOF setup. */
  if (cam && (cam->dof.flag & CAM_DOF_ENABLED)) {
    const float *vp_size = DRW_viewport_size_get();
    float fstop = cam->dof.aperture_fstop;
    float sensor = BKE_camera_sensor_size(cam->sensor_fit, cam->sensor_x, cam->sensor_y);
    float focus_dist = BKE_camera_object_dof_distance(pd->camera);
    float focal_len = cam->lens;

    const float scale_camera = 0.001f;
    /* we want radius here for the aperture number  */
    float aperture = 0.5f * scale_camera * focal_len / fstop;
    float focal_len_scaled = scale_camera * focal_len;
    float sensor_scaled = scale_camera * sensor;

    if (draw_ctx->rv3d != NULL) {
      sensor_scaled *= draw_ctx->rv3d->viewcamtexcofac[0];
    }

    pd->dof_params[1] = aperture * fabsf(focal_len_scaled / (focus_dist - focal_len_scaled));
    pd->dof_params[1] *= vp_size[0] / sensor_scaled;
    pd->dof_params[0] = -focus_dist * pd->dof_params[1];
  }
  else {
    /* Disable DoF blur scalling. */
    pd->camera = NULL;
  }
}

#define DRAW_NOW 2

typedef struct gpIterPopulateData {
  Object *ob;
  GPENCIL_tObject *tgp_ob;
  GPENCIL_PrivateData *pd;
  GPENCIL_MaterialPool *matpool;
  DRWShadingGroup *grp;
  /* Last material UBO bound. Used to avoid uneeded buffer binding. */
  GPUUniformBuffer *ubo_mat;
  GPUUniformBuffer *ubo_lights;
  /* Last texture bound. */
  GPUTexture *tex_fill;
  GPUTexture *tex_stroke;
  /* Offset in the material pool to the first material of this object. */
  int mat_ofs;
  /* Is the sbuffer call need to be issued. */
  int do_sbuffer_call;
  /* Indices to do correct insertion of the sbuffer stroke. */
  int stroke_index_last;
  int stroke_index_offset;
} gpIterPopulateData;

static void gp_stroke_cache_populate(bGPDlayer *UNUSED(gpl),
                                     bGPDframe *UNUSED(gpf),
                                     bGPDstroke *gps,
                                     void *thunk);

static void gp_sbuffer_cache_populate(gpIterPopulateData *iter)
{
  iter->do_sbuffer_call = DRAW_NOW;
  /* In order to draw the sbuffer stroke correctly mixed with other strokes,
   * we need to offset the stroke index of the sbuffer stroke and the subsequent strokes.
   * Remember, sbuffer stroke indices start from 0. So we add last index to avoid
   * masking issues. */
  iter->grp = DRW_shgroup_create_sub(iter->grp);
  DRW_shgroup_uniform_block(iter->grp, "gpMaterialBlock", iter->ubo_mat);
  DRW_shgroup_uniform_float_copy(iter->grp, "strokeIndexOffset", iter->stroke_index_last);

  const DRWContextState *ctx = DRW_context_state_get();
  ToolSettings *ts = ctx->scene->toolsettings;
  if (ts->gpencil_v3d_align & (GP_PROJECT_DEPTH_VIEW | GP_PROJECT_DEPTH_STROKE)) {
    /* In this case we can't do correct projection during stroke. We just disable depth test. */
    DRW_shgroup_uniform_texture(iter->grp, "gpSceneDepthTexture", iter->pd->dummy_tx);
  }

  gp_stroke_cache_populate(NULL, NULL, iter->pd->sbuffer_stroke, iter);

  iter->stroke_index_offset = iter->pd->sbuffer_stroke->totpoints + 1;
  iter->do_sbuffer_call = 0;
}

static void gp_layer_cache_populate(bGPDlayer *gpl,
                                    bGPDframe *gpf,
                                    bGPDstroke *UNUSED(gps),
                                    void *thunk)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  const Scene *scene = draw_ctx->scene;
  gpIterPopulateData *iter = (gpIterPopulateData *)thunk;
  bGPdata *gpd = (bGPdata *)iter->ob->data;

  if (iter->do_sbuffer_call) {
    gp_sbuffer_cache_populate(iter);
  }
  else {
    iter->do_sbuffer_call = !iter->pd->do_fast_drawing && (gpd == iter->pd->sbuffer_gpd) &&
                            (gpl == iter->pd->sbuffer_layer);
  }

  GPENCIL_tLayer *tgp_layer_prev = iter->tgp_ob->layers.last;
  GPENCIL_tLayer *tgp_layer = gpencil_layer_cache_add(iter->pd, iter->ob, gpl);
  BLI_LINKS_APPEND(&iter->tgp_ob->layers, tgp_layer);

  if (tgp_layer->is_masked && (tgp_layer_prev == NULL || !tgp_layer_prev->is_masked)) {
    tgp_layer->do_masked_clear = true;
  }

  gpencil_material_resources_get(iter->matpool, 0, NULL, NULL, &iter->ubo_mat);

  const bool is_stroke_order_3d = (gpd->draw_mode == GP_DRAWMODE_3D) || iter->pd->draw_depth_only;
  const bool is_screenspace = (gpd->flag & GP_DATA_STROKE_KEEPTHICKNESS) != 0;

  float object_scale = mat4_to_scale(iter->ob->obmat);
  /* Negate thickness sign to tag that strokes are in screen space.
   * Convert to world units (by default, 1 meter = 2000 px). */
  float thickness_scale = (is_screenspace) ? -1.0f : (gpd->pixfactor / GPENCIL_PIXEL_FACTOR);

  const bool use_lights = iter->pd->use_lighting && ((gpl->flag & GP_LAYER_USE_LIGHTS) != 0) &&
                          (iter->ob->dtx & OB_USE_GPENCIL_LIGHTS);
  iter->ubo_lights = (use_lights) ? iter->pd->global_light_pool->ubo :
                                    iter->pd->shadeless_light_pool->ubo;

  bool overide_vertcol = (iter->pd->v3d_color_type != -1);
  bool is_vert_col_mode = (iter->pd->v3d_color_type == V3D_SHADING_VERTEX_COLOR) ||
                          GPENCIL_VERTEX_MODE(gpd);
  float vert_col_opacity = (overide_vertcol) ? (is_vert_col_mode ? 1.0f : 0.0f) :
                                               gpl->vertex_paint_opacity;

  struct GPUShader *sh = GPENCIL_shader_geometry_get();
  iter->grp = DRW_shgroup_create(sh, tgp_layer->geom_ps);
  DRW_shgroup_uniform_block_persistent(iter->grp, "gpLightBlock", iter->ubo_lights);
  DRW_shgroup_uniform_block(iter->grp, "gpMaterialBlock", iter->ubo_mat);
  DRW_shgroup_uniform_texture(iter->grp, "gpFillTexture", iter->tex_fill);
  DRW_shgroup_uniform_texture(iter->grp, "gpStrokeTexture", iter->tex_stroke);
  DRW_shgroup_uniform_texture(iter->grp, "gpSceneDepthTexture", iter->pd->scene_depth_tx);
  DRW_shgroup_uniform_bool_copy(iter->grp, "strokeOrder3d", is_stroke_order_3d);
  DRW_shgroup_uniform_vec3_copy(iter->grp, "gpNormal", iter->tgp_ob->plane_normal);
  DRW_shgroup_uniform_vec2_copy(iter->grp, "sizeViewportInv", DRW_viewport_invert_size_get());
  DRW_shgroup_uniform_vec2_copy(iter->grp, "sizeViewport", DRW_viewport_size_get());
  DRW_shgroup_uniform_float_copy(iter->grp, "thicknessScale", object_scale);
  DRW_shgroup_uniform_float_copy(iter->grp, "thicknessOffset", (float)gpl->line_change);
  DRW_shgroup_uniform_float_copy(iter->grp, "thicknessWorldScale", thickness_scale);
  DRW_shgroup_uniform_float_copy(iter->grp, "vertexColorOpacity", vert_col_opacity);
  DRW_shgroup_uniform_float_copy(iter->grp, "strokeIndexOffset", iter->stroke_index_offset);
  DRW_shgroup_stencil_mask(iter->grp, 0xFF);

  bool use_onion = gpf->runtime.onion_id != 0.0f;
  if (use_onion) {
    const bool use_onion_custom_col = (gpd->onion_flag & GP_ONION_GHOST_PREVCOL) != 0;
    const bool use_onion_fade = (gpd->onion_flag & GP_ONION_FADE) != 0;
    const bool use_next_col = gpf->runtime.onion_id > 0.0f;
    float *onion_col_custom = (use_next_col) ? gpd->gcolor_next : gpd->gcolor_prev;
    onion_col_custom = (use_onion_custom_col) ? onion_col_custom : U.gpencil_new_layer_col;
    float onion_col[4] = {UNPACK3(onion_col_custom), 1.0f};
    float onion_alpha = use_onion_fade ? (1.0f / abs(gpf->runtime.onion_id)) : 0.5f;
    onion_alpha += (gpd->onion_factor * 2.0f - 1.0f);
    onion_alpha = clamp_f(onion_alpha, 0.01f, 1.0f);

    DRW_shgroup_uniform_vec4_copy(iter->grp, "layerTint", onion_col);
    DRW_shgroup_uniform_float_copy(iter->grp, "layerOpacity", onion_alpha);
  }
  else {
    float alpha = GPENCIL_SIMPLIFY_TINT(scene) ? 0.0f : gpl->tintcolor[3];
    float tintcolor[4] = {gpl->tintcolor[0], gpl->tintcolor[1], gpl->tintcolor[2], alpha};
    DRW_shgroup_uniform_vec4_copy(iter->grp, "layerTint", tintcolor);
    DRW_shgroup_uniform_float_copy(iter->grp, "layerOpacity", 1.0f);
  }
}

static void gp_stroke_cache_populate(bGPDlayer *UNUSED(gpl),
                                     bGPDframe *UNUSED(gpf),
                                     bGPDstroke *gps,
                                     void *thunk)
{
  gpIterPopulateData *iter = (gpIterPopulateData *)thunk;

  MaterialGPencilStyle *gp_style = BKE_material_gpencil_settings_get(iter->ob, gps->mat_nr + 1);

  bool hide_material = (gp_style->flag & GP_MATERIAL_HIDE) != 0;
  bool show_stroke = (gp_style->flag & GP_MATERIAL_STROKE_SHOW) != 0;
  bool show_fill = (gps->tot_triangles > 0) && ((gp_style->flag & GP_MATERIAL_FILL_SHOW) != 0) &&
                   (!iter->pd->simplify_fill);

  if (hide_material || (!show_stroke && !show_fill)) {
    return;
  }

  GPUUniformBuffer *ubo_mat;
  GPUTexture *tex_stroke, *tex_fill;
  gpencil_material_resources_get(iter->matpool, gps->mat_nr, &tex_stroke, &tex_fill, &ubo_mat);

  bool resource_changed = (iter->ubo_mat != ubo_mat) ||
                          (tex_fill && (iter->tex_fill != tex_fill)) ||
                          (tex_stroke && (iter->tex_stroke != tex_stroke));

  if (resource_changed) {
    iter->grp = DRW_shgroup_create_sub(iter->grp);
    if (iter->ubo_mat != ubo_mat) {
      DRW_shgroup_uniform_block(iter->grp, "gpMaterialBlock", ubo_mat);
      iter->ubo_mat = ubo_mat;
    }
    if (tex_fill) {
      DRW_shgroup_uniform_texture(iter->grp, "gpFillTexture", tex_fill);
      iter->tex_fill = tex_fill;
    }
    if (tex_stroke) {
      DRW_shgroup_uniform_texture(iter->grp, "gpStrokeTexture", tex_stroke);
      iter->tex_stroke = tex_stroke;
    }
  }

  bool do_sbuffer = (iter->do_sbuffer_call == DRAW_NOW);

  if (show_fill) {
    GPUBatch *geom = do_sbuffer ? DRW_cache_gpencil_sbuffer_fill_get(iter->ob) :
                                  DRW_cache_gpencil_fills_get(iter->ob, iter->pd->cfra);
    int vfirst = gps->runtime.fill_start * 3;
    int vcount = gps->tot_triangles * 3;
    DRW_shgroup_call_range(iter->grp, iter->ob, geom, vfirst, vcount);
  }

  if (show_stroke) {
    GPUBatch *geom = do_sbuffer ? DRW_cache_gpencil_sbuffer_stroke_get(iter->ob) :
                                  DRW_cache_gpencil_strokes_get(iter->ob, iter->pd->cfra);
    /* Start one vert before to have gl_InstanceID > 0 (see shader). */
    int vfirst = gps->runtime.stroke_start - 1;
    /* Include "potential" cyclic vertex and start adj vertex (see shader). */
    int vcount = gps->totpoints + 1 + 1;
    DRW_shgroup_call_instance_range(iter->grp, iter->ob, geom, vfirst, vcount);
  }

  iter->stroke_index_last = gps->runtime.stroke_start + gps->totpoints + 1;
}

static void gp_sbuffer_cache_populate_fast(GPENCIL_Data *vedata, gpIterPopulateData *iter)
{
  bGPdata *gpd = (bGPdata *)iter->ob->data;
  if (gpd != iter->pd->sbuffer_gpd) {
    return;
  }

  GPENCIL_TextureList *txl = vedata->txl;
  GPUTexture *depth_texture = iter->pd->scene_depth_tx;
  GPENCIL_tObject *last_tgp_ob = iter->pd->tobjects.last;
  /* Create another temp object that only contain the stroke. */
  iter->tgp_ob = gpencil_object_cache_add(iter->pd, iter->ob);
  /* Remove from the main list. */
  iter->pd->tobjects.last = last_tgp_ob;
  last_tgp_ob->next = NULL;
  /* Add to sbuffer tgpobject list. */
  BLI_LINKS_APPEND(&iter->pd->sbuffer_tobjects, iter->tgp_ob);
  /* Remove depth test with scene (avoid self occlusion). */
  iter->pd->scene_depth_tx = txl->dummy_texture;

  gp_layer_cache_populate(iter->pd->sbuffer_layer, iter->pd->sbuffer_layer->actframe, NULL, iter);

  const DRWContextState *ctx = DRW_context_state_get();
  ToolSettings *ts = ctx->scene->toolsettings;
  if (ts->gpencil_v3d_align & (GP_PROJECT_DEPTH_VIEW | GP_PROJECT_DEPTH_STROKE)) {
    /* In this case we can't do correct projection during stroke. We just disable depth test. */
    DRW_shgroup_uniform_texture(iter->grp, "gpSceneDepthTexture", iter->pd->dummy_tx);
  }

  iter->do_sbuffer_call = DRAW_NOW;
  gp_stroke_cache_populate(NULL, NULL, iter->pd->sbuffer_stroke, iter);

  gpencil_vfx_cache_populate(vedata, iter->ob, iter->tgp_ob);

  /* Restore state. */
  iter->do_sbuffer_call = 0;
  iter->pd->scene_depth_tx = depth_texture;
}

void GPENCIL_cache_populate(void *ved, Object *ob)
{
  GPENCIL_Data *vedata = (GPENCIL_Data *)ved;
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  GPENCIL_TextureList *txl = vedata->txl;

  /* object must be visible */
  if (!(DRW_object_visibility_in_active_context(ob) & OB_VISIBLE_SELF)) {
    return;
  }

  if (ob->data && (ob->type == OB_GPENCIL) && (ob->dt >= OB_SOLID)) {
    gpIterPopulateData iter = {0};
    iter.ob = ob;
    iter.pd = pd;
    iter.tgp_ob = gpencil_object_cache_add(pd, ob);
    iter.matpool = gpencil_material_pool_create(pd, ob, &iter.mat_ofs);
    iter.tex_fill = txl->dummy_texture;
    iter.tex_stroke = txl->dummy_texture;

    BKE_gpencil_visible_stroke_iter(
        ob, gp_layer_cache_populate, gp_stroke_cache_populate, &iter, pd->do_onion, pd->cfra);

    if (iter.do_sbuffer_call) {
      gp_sbuffer_cache_populate(&iter);
    }

    gpencil_vfx_cache_populate(vedata, ob, iter.tgp_ob);

    if (pd->do_fast_drawing) {
      gp_sbuffer_cache_populate_fast(vedata, &iter);
    }
  }

  if (ob->type == OB_LAMP && pd->use_lights) {
    gpencil_light_pool_populate(pd->global_light_pool, ob);
  }
}

#define SORT_IMPL_LINKTYPE GPENCIL_tObject

#define SORT_IMPL_FUNC gpencil_tobject_sort_fn_r
#include "../../blenlib/intern/list_sort_impl.h"
#undef SORT_IMPL_FUNC

#undef SORT_IMPL_LINKTYPE

static int gpencil_tobject_dist_sort(const void *a, const void *b)
{
  const GPENCIL_tObject *ob_a = (const GPENCIL_tObject *)a;
  const GPENCIL_tObject *ob_b = (const GPENCIL_tObject *)b;
  /* Reminder, camera_z is negative in front of the camera. */
  if (ob_a->camera_z > ob_b->camera_z) {
    return 1;
  }
  else if (ob_a->camera_z < ob_b->camera_z) {
    return -1;
  }
  else {
    return 0;
  }
}

void GPENCIL_cache_finish(void *ved)
{
  GPENCIL_Data *vedata = (GPENCIL_Data *)ved;
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  GPENCIL_FramebufferList *fbl = vedata->fbl;
  const DRWContextState *draw_ctx = DRW_context_state_get();

  /* Upload UBO data. */
  BLI_memblock_iter iter;
  BLI_memblock_iternew(pd->gp_material_pool, &iter);
  GPENCIL_MaterialPool *pool;
  while ((pool = (GPENCIL_MaterialPool *)BLI_memblock_iterstep(&iter))) {
    GPU_uniformbuffer_update(pool->ubo, pool->mat_data);
  }

  BLI_memblock_iternew(pd->gp_light_pool, &iter);
  GPENCIL_LightPool *lpool;
  while ((lpool = (GPENCIL_LightPool *)BLI_memblock_iterstep(&iter))) {
    GPU_uniformbuffer_update(lpool->ubo, lpool->light_data);
  }

  /* Sort object by distance to the camera. */
  pd->tobjects.first = gpencil_tobject_sort_fn_r(pd->tobjects.first, gpencil_tobject_dist_sort);

  /* Create framebuffers only if needed. */
  if (pd->tobjects.first) {
    eGPUTextureFormat format = pd->use_signed_fb ? GPU_RGBA16F : GPU_R11F_G11F_B10F;

    const float *size = DRW_viewport_size_get();
    pd->depth_tx = DRW_texture_pool_query_2d(
        size[0], size[1], GPU_DEPTH24_STENCIL8, &draw_engine_gpencil_type);
    pd->color_tx = DRW_texture_pool_query_2d(size[0], size[1], format, &draw_engine_gpencil_type);
    pd->reveal_tx = DRW_texture_pool_query_2d(size[0], size[1], format, &draw_engine_gpencil_type);

    GPU_framebuffer_ensure_config(&fbl->gpencil_fb,
                                  {
                                      GPU_ATTACHMENT_TEXTURE(pd->depth_tx),
                                      GPU_ATTACHMENT_TEXTURE(pd->color_tx),
                                      GPU_ATTACHMENT_TEXTURE(pd->reveal_tx),
                                  });

    if (pd->use_layer_fb) {
      pd->color_layer_tx = DRW_texture_pool_query_2d(
          size[0], size[1], format, &draw_engine_gpencil_type);
      pd->reveal_layer_tx = DRW_texture_pool_query_2d(
          size[0], size[1], format, &draw_engine_gpencil_type);

      GPU_framebuffer_ensure_config(&fbl->layer_fb,
                                    {
                                        GPU_ATTACHMENT_TEXTURE(pd->depth_tx),
                                        GPU_ATTACHMENT_TEXTURE(pd->color_layer_tx),
                                        GPU_ATTACHMENT_TEXTURE(pd->reveal_layer_tx),
                                    });
    };

    if (pd->use_object_fb) {
      pd->color_object_tx = DRW_texture_pool_query_2d(
          size[0], size[1], format, &draw_engine_gpencil_type);
      pd->reveal_object_tx = DRW_texture_pool_query_2d(
          size[0], size[1], format, &draw_engine_gpencil_type);

      GPU_framebuffer_ensure_config(&fbl->object_fb,
                                    {
                                        GPU_ATTACHMENT_TEXTURE(pd->depth_tx),
                                        GPU_ATTACHMENT_TEXTURE(pd->color_object_tx),
                                        GPU_ATTACHMENT_TEXTURE(pd->reveal_object_tx),
                                    });
    }

    if (pd->use_mask_fb) {
      /* We need to separate all the masked layer together in order to correctly mix them. */
      pd->color_masked_tx = DRW_texture_pool_query_2d(
          size[0], size[1], format, &draw_engine_gpencil_type);
      pd->reveal_masked_tx = DRW_texture_pool_query_2d(
          size[0], size[1], format, &draw_engine_gpencil_type);

      GPU_framebuffer_ensure_config(&fbl->masked_fb,
                                    {
                                        GPU_ATTACHMENT_TEXTURE(pd->depth_tx),
                                        GPU_ATTACHMENT_TEXTURE(pd->color_masked_tx),
                                        GPU_ATTACHMENT_TEXTURE(pd->reveal_masked_tx),
                                    });
    }

    if (!GPENCIL_SIMPLIFY_AA(draw_ctx->scene)) {
      GPENCIL_antialiasing_init(vedata);
    }
  }
}

static void GPENCIL_draw_scene_depth_only(void *ved)
{
  GPENCIL_Data *vedata = (GPENCIL_Data *)ved;
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

  if (DRW_state_is_fbo()) {
    GPU_framebuffer_bind(dfbl->depth_only_fb);
  }

  for (GPENCIL_tObject *ob = pd->tobjects.first; ob; ob = ob->next) {
    for (GPENCIL_tLayer *layer = ob->layers.first; layer; layer = layer->next) {
      DRW_draw_pass(layer->geom_ps);
    }
  }

  if (DRW_state_is_fbo()) {
    GPU_framebuffer_bind(dfbl->default_fb);
  }

  pd->gp_object_pool = pd->gp_layer_pool = pd->gp_vfx_pool = NULL;

  /* Free temp stroke buffers. */
  if (pd->sbuffer_gpd) {
    DRW_cache_gpencil_sbuffer_clear(pd->obact);
  }
}

static void GPENCIL_draw_object(GPENCIL_Data *vedata, GPENCIL_tObject *ob)
{
  GPENCIL_PassList *psl = vedata->psl;
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  GPENCIL_FramebufferList *fbl = vedata->fbl;
  float clear_cols[2][4] = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};

  DRW_stats_group_start("GPencil Object");

  GPUFrameBuffer *fb_object = (ob->vfx.first) ? fbl->object_fb : fbl->gpencil_fb;

  GPU_framebuffer_bind(fb_object);
  GPU_framebuffer_clear_depth_stencil(fb_object, ob->is_drawmode3d ? 1.0f : 0.0f, 0x00);

  if (ob->vfx.first) {
    GPU_framebuffer_multi_clear(fb_object, clear_cols);
  }

  for (GPENCIL_tLayer *layer = ob->layers.first; layer; layer = layer->next) {
    if (layer->blend_ps) {
      GPU_framebuffer_bind(fbl->layer_fb);
      GPU_framebuffer_multi_clear(fbl->layer_fb, clear_cols);
    }
    else if (layer->is_masked) {
      GPU_framebuffer_bind(fbl->masked_fb);
      if (layer->do_masked_clear) {
        GPU_framebuffer_multi_clear(fbl->masked_fb, clear_cols);
      }
    }
    else {
      GPU_framebuffer_bind(fb_object);
    }

    DRW_draw_pass(layer->geom_ps);

    if (layer->blend_ps) {
      if (layer->is_masked) {
        GPU_framebuffer_bind(fbl->masked_fb);
        if (layer->do_masked_clear) {
          GPU_framebuffer_multi_clear(fbl->masked_fb, clear_cols);
        }
      }
      else {
        GPU_framebuffer_bind(fb_object);
      }
      DRW_draw_pass(layer->blend_ps);
    }
  }

  for (GPENCIL_tVfx *vfx = ob->vfx.first; vfx; vfx = vfx->next) {
    GPU_framebuffer_bind(*(vfx->target_fb));
    DRW_draw_pass(vfx->vfx_ps);
  }

  copy_m4_m4(pd->object_bound_mat, ob->plane_mat);
  pd->is_stroke_order_3d = ob->is_drawmode3d;

  if (pd->scene_fb) {
    GPU_framebuffer_bind(pd->scene_fb);
    DRW_draw_pass(psl->merge_depth_ps);
  }

  DRW_stats_group_end();
}

static void GPENCIL_fast_draw_start(GPENCIL_Data *vedata)
{
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  GPENCIL_FramebufferList *fbl = vedata->fbl;
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

  if (!pd->snapshot_buffer_dirty) {
    /* Copy back cached render. */
    GPU_framebuffer_blit(fbl->snapshot_fb, 0, dfbl->default_fb, 0, GPU_DEPTH_BIT);
    GPU_framebuffer_blit(fbl->snapshot_fb, 0, fbl->gpencil_fb, 0, GPU_COLOR_BIT);
    GPU_framebuffer_blit(fbl->snapshot_fb, 1, fbl->gpencil_fb, 1, GPU_COLOR_BIT);
    /* Bypass drawing. */
    pd->tobjects.first = pd->tobjects.last = NULL;
  }
}

static void GPENCIL_fast_draw_end(GPENCIL_Data *vedata)
{
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  GPENCIL_FramebufferList *fbl = vedata->fbl;
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

  if (pd->snapshot_buffer_dirty) {
    /* Save to snapshot buffer. */
    GPU_framebuffer_blit(dfbl->default_fb, 0, fbl->snapshot_fb, 0, GPU_DEPTH_BIT);
    GPU_framebuffer_blit(fbl->gpencil_fb, 0, fbl->snapshot_fb, 0, GPU_COLOR_BIT);
    GPU_framebuffer_blit(fbl->gpencil_fb, 1, fbl->snapshot_fb, 1, GPU_COLOR_BIT);
    pd->snapshot_buffer_dirty = false;
  }
  /* Draw the sbuffer stroke(s). */
  for (GPENCIL_tObject *ob = pd->sbuffer_tobjects.first; ob; ob = ob->next) {
    GPENCIL_draw_object(vedata, ob);
  }
}

void GPENCIL_draw_scene(void *ved)
{
  GPENCIL_Data *vedata = (GPENCIL_Data *)ved;
  GPENCIL_PassList *psl = vedata->psl;
  GPENCIL_PrivateData *pd = vedata->stl->pd;
  GPENCIL_FramebufferList *fbl = vedata->fbl;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  float clear_cols[2][4] = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};

  if (pd->draw_depth_only) {
    GPENCIL_draw_scene_depth_only(vedata);
    return;
  }

  if (pd->tobjects.first == NULL) {
    return;
  }

  if (pd->do_fast_drawing) {
    GPENCIL_fast_draw_start(vedata);
  }

  if (pd->tobjects.first) {
    GPU_framebuffer_bind(fbl->gpencil_fb);
    GPU_framebuffer_multi_clear(fbl->gpencil_fb, clear_cols);
  }

  for (GPENCIL_tObject *ob = pd->tobjects.first; ob; ob = ob->next) {
    GPENCIL_draw_object(vedata, ob);
  }

  if (pd->do_fast_drawing) {
    GPENCIL_fast_draw_end(vedata);
  }

  if (!GPENCIL_SIMPLIFY_AA(draw_ctx->scene)) {
    GPENCIL_antialiasing_draw(vedata);
  }

  if (pd->scene_fb) {
    GPU_framebuffer_bind(pd->scene_fb);
    DRW_draw_pass(psl->composite_ps);
  }

  pd->gp_object_pool = pd->gp_layer_pool = pd->gp_vfx_pool = NULL;

  /* Free temp stroke buffers. */
  if (pd->sbuffer_gpd) {
    DRW_cache_gpencil_sbuffer_clear(pd->obact);
  }
}

static void GPENCIL_engine_free(void)
{
  GPENCIL_shader_free();
}

static const DrawEngineDataSize GPENCIL_data_size = DRW_VIEWPORT_DATA_SIZE(GPENCIL_Data);

DrawEngineType draw_engine_gpencil_type = {
    NULL,
    NULL,
    N_("GpencilMode"),
    &GPENCIL_data_size,
    &GPENCIL_engine_init,
    &GPENCIL_engine_free,
    &GPENCIL_cache_init,
    &GPENCIL_cache_populate,
    &GPENCIL_cache_finish,
    NULL,
    &GPENCIL_draw_scene,
    NULL,
    NULL,
    &GPENCIL_render_to_image,
};
