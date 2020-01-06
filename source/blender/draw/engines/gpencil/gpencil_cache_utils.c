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

#include "ED_gpencil.h"
#include "ED_view3d.h"

#include "DNA_gpencil_types.h"
#include "DNA_view3d_types.h"

#include "BKE_library.h"
#include "BKE_gpencil.h"
#include "BKE_object.h"

#include "BLI_memblock.h"
#include "BLI_link_utils.h"

#include "gpencil_engine.h"

#include "draw_cache_impl.h"

#include "DEG_depsgraph.h"

/* TODO remove the _new suffix. */
GPENCIL_tObject *gpencil_object_cache_add_new(GPENCIL_PrivateData *pd, Object *ob)
{
  bGPdata *gpd = (bGPdata *)ob->data;
  GPENCIL_tObject *tgp_ob = BLI_memblock_alloc(pd->gp_object_pool);

  tgp_ob->layers.first = tgp_ob->layers.last = NULL;
  tgp_ob->vfx.first = tgp_ob->vfx.last = NULL;
  tgp_ob->camera_z = dot_v3v3(pd->camera_z_axis, ob->obmat[3]);
  tgp_ob->is_drawmode3d = (gpd->draw_mode == GP_DRAWMODE_3D);

  /* Find the normal most likely to represent the gpObject. */
  /* TODO: This does not work quite well if you use
   * strokes not aligned with the object axes. Maybe we could try to
   * compute the minimum axis of all strokes. But this would be more
   * computationaly heavy and should go into the GPData evaluation. */
  BoundBox *bbox = BKE_object_boundbox_get(ob);
  /* Convert bbox to matrix */
  float mat[4][4], size[3], center[3];
  BKE_boundbox_calc_size_aabb(bbox, size);
  BKE_boundbox_calc_center_aabb(bbox, center);
  unit_m4(mat);
  copy_v3_v3(mat[3], center);
  /* Avoid division by 0.0 later. */
  add_v3_fl(size, 1e-8f);
  rescale_m4(mat, size);
  /* BBox space to World. */
  mul_m4_m4m4(mat, ob->obmat, mat);
  if (DRW_view_is_persp_get(NULL)) {
    /* BBox center to camera vector. */
    sub_v3_v3v3(tgp_ob->plane_normal, pd->camera_pos, mat[3]);
  }
  else {
    copy_v3_v3(tgp_ob->plane_normal, pd->camera_z_axis);
  }
  /* World to BBox space. */
  invert_m4(mat);
  /* Normalize the vector in BBox space. */
  mul_mat3_m4_v3(mat, tgp_ob->plane_normal);
  normalize_v3(tgp_ob->plane_normal);

  transpose_m4(mat);
  /* mat is now a "normal" matrix which will transform
   * BBox space normal to world space.  */
  mul_mat3_m4_v3(mat, tgp_ob->plane_normal);
  normalize_v3(tgp_ob->plane_normal);

  /* Define a matrix that will be used to render a triangle to merge the depth of the rendered
   * gpencil object with the rest of the scene. */
  unit_m4(tgp_ob->plane_mat);
  copy_v3_v3(tgp_ob->plane_mat[2], tgp_ob->plane_normal);
  orthogonalize_m4(tgp_ob->plane_mat, 2);
  mul_mat3_m4_v3(ob->obmat, size);
  float radius = len_v3(size);
  mul_m4_v3(ob->obmat, center);
  rescale_m4(tgp_ob->plane_mat, (float[3]){radius, radius, radius});
  copy_v3_v3(tgp_ob->plane_mat[3], center);

  BLI_LINKS_APPEND(&pd->tobjects, tgp_ob);

  return tgp_ob;
}

/* TODO remove the _new suffix. */
GPENCIL_tLayer *gpencil_layer_cache_add_new(GPENCIL_PrivateData *pd, Object *ob, bGPDlayer *gpl)
{
  bGPdata *gpd = (bGPdata *)ob->data;
  GPENCIL_tLayer *tgp_layer = BLI_memblock_alloc(pd->gp_layer_pool);

  const bool is_mask = (gpl->flag & GP_LAYER_USE_MASK) != 0;
  tgp_layer->is_mask = is_mask;
  tgp_layer->do_masked_clear = false;

  if (!is_mask) {
    tgp_layer->is_masked = false;
    for (bGPDlayer *gpl_m = gpl->next; gpl_m; gpl_m = gpl_m->next) {
      if (gpl_m->flag & GP_LAYER_USE_MASK) {
        if (gpl_m->flag & GP_LAYER_HIDE) {
          /* We don't mask but we dont try to mask with further layers. */
        }
        else {
          tgp_layer->is_masked = true;
        }
        break;
      }
    }
  }

  {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA_PREMUL;
    if (GPENCIL_3D_DRAWMODE(ob, gpd) || pd->draw_depth_only) {
      /* TODO better 3D mode. */
      state |= DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    }
    else {
      /* We render all strokes with uniform depth (increasing with stroke id). */
      state |= DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_GREATER;
    }

    if (gpl->flag & GP_LAYER_USE_MASK) {
      state |= DRW_STATE_STENCIL_EQUAL;
    }
    else {
      state |= DRW_STATE_WRITE_STENCIL | DRW_STATE_STENCIL_ALWAYS;
    }

    tgp_layer->geom_ps = DRW_pass_create("GPencil Layer", state);
  }

  if (is_mask) {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_STENCIL_EQUAL | DRW_STATE_BLEND_MUL;
    tgp_layer->blend_ps = DRW_pass_create("GPencil Mask Layer", state);

    GPUShader *sh = GPENCIL_shader_layer_mask_get(&en_data);
    DRWShadingGroup *grp = DRW_shgroup_create(sh, tgp_layer->blend_ps);
    DRW_shgroup_uniform_int_copy(grp, "isFirstPass", true);
    DRW_shgroup_uniform_float_copy(grp, "maskOpacity", gpl->opacity);
    DRW_shgroup_uniform_bool_copy(grp, "maskInvert", gpl->flag & GP_LAYER_MASK_INVERT);
    DRW_shgroup_uniform_texture_ref(grp, "colorBuf", &pd->color_masked_tx);
    DRW_shgroup_uniform_texture_ref(grp, "revealBuf", &pd->reveal_masked_tx);
    DRW_shgroup_uniform_texture_ref(grp, "maskBuf", &pd->reveal_layer_tx);
    DRW_shgroup_stencil_mask(grp, 0xFF);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

    /* We cannot do custom blending on MultiTarget framebuffers.
     * Workaround by doing 2 passes. */
    grp = DRW_shgroup_create_sub(grp);
    DRW_shgroup_state_disable(grp, DRW_STATE_BLEND_MUL);
    DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ADD_FULL);
    DRW_shgroup_uniform_int_copy(grp, "isFirstPass", false);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
  else if ((gpl->blend_mode != eGplBlendMode_Regular) || (gpl->opacity < 1.0f)) {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_STENCIL_EQUAL;
    switch (gpl->blend_mode) {
      case eGplBlendMode_Regular:
        state |= DRW_STATE_BLEND_ALPHA_PREMUL;
        break;
      case eGplBlendMode_Add:
        state |= DRW_STATE_BLEND_ADD_FULL;
        break;
      case eGplBlendMode_Subtract:
        /* Caveat. This effect only propagates if target buffer has
         * a signed floating point color buffer.
         * i.e: This will not be conserved after this blending step.
         * TODO(fclem) To make things consistent, we might create a dummy vfx
         * for objects that use this blend type to always avoid the subtract
         * affecting other objects. */
        state |= DRW_STATE_BLEND_SUB;
        break;
      case eGplBlendMode_Multiply:
      case eGplBlendMode_Divide:
        /* Same Caveat as Subtract. This is conserved until there is a blend with a LDR buffer. */
      case eGplBlendMode_Overlay:
        state |= DRW_STATE_BLEND_MUL;
        break;
    }

    tgp_layer->blend_ps = DRW_pass_create("GPencil Blend Layer", state);

    GPUShader *sh = GPENCIL_shader_layer_blend_get(&en_data);
    DRWShadingGroup *grp = DRW_shgroup_create(sh, tgp_layer->blend_ps);
    DRW_shgroup_uniform_int_copy(grp, "blendMode", gpl->blend_mode);
    DRW_shgroup_uniform_float_copy(grp, "blendOpacity", gpl->opacity);
    DRW_shgroup_uniform_texture_ref(grp, "colorBuf", &pd->color_layer_tx);
    DRW_shgroup_uniform_texture_ref(grp, "revealBuf", &pd->reveal_layer_tx);
    DRW_shgroup_stencil_mask(grp, 0xFF);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

    if (gpl->blend_mode == eGplBlendMode_Overlay) {
      /* We cannot do custom blending on MultiTarget framebuffers.
       * Workaround by doing 2 passes. */
      grp = DRW_shgroup_create(sh, tgp_layer->blend_ps);
      DRW_shgroup_state_disable(grp, DRW_STATE_BLEND_MUL);
      DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ADD_FULL);
      DRW_shgroup_uniform_int_copy(grp, "blendMode", 999);
      DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
    }
  }
  else {
    tgp_layer->blend_ps = NULL;
  }

  return tgp_layer;
}

/* verify if exist a non instanced version of the object */
static bool gpencil_has_noninstanced_object(Object *ob_instance)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  const ViewLayer *view_layer = draw_ctx->view_layer;
  Object *ob = NULL;
  for (Base *base = view_layer->object_bases.first; base; base = base->next) {
    ob = base->object;
    if (ob->type != OB_GPENCIL) {
      continue;
    }
    /* is not duplicated and the name is equals */
    if ((ob->base_flag & BASE_FROM_DUPLI) == 0) {
      if (STREQ(ob->id.name, ob_instance->id.name)) {
        return true;
      }
    }
  }

  return false;
}

/* add a gpencil object to cache to defer drawing */
tGPencilObjectCache *gpencil_object_cache_add(tGPencilObjectCache *cache_array,
                                              Object *ob,
                                              int *gp_cache_size,
                                              int *gp_cache_used)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  tGPencilObjectCache *cache_elem = NULL;
  RegionView3D *rv3d = draw_ctx->rv3d;
  View3D *v3d = draw_ctx->v3d;
  tGPencilObjectCache *p = NULL;

  /* By default a cache is created with one block with a predefined number of free slots,
   * if the size is not enough, the cache is reallocated adding a new block of free slots.
   * This is done in order to keep cache small. */
  if (*gp_cache_used + 1 > *gp_cache_size) {
    if ((*gp_cache_size == 0) || (cache_array == NULL)) {
      p = MEM_callocN(sizeof(struct tGPencilObjectCache) * GP_CACHE_BLOCK_SIZE,
                      "tGPencilObjectCache");
      *gp_cache_size = GP_CACHE_BLOCK_SIZE;
    }
    else {
      *gp_cache_size += GP_CACHE_BLOCK_SIZE;
      p = MEM_recallocN(cache_array, sizeof(struct tGPencilObjectCache) * *gp_cache_size);
    }
    cache_array = p;
  }
  /* zero out all pointers */
  cache_elem = &cache_array[*gp_cache_used];
  memset(cache_elem, 0, sizeof(*cache_elem));

  cache_elem->ob = ob;
  cache_elem->gpd = (bGPdata *)ob->data;
  cache_elem->name = BKE_id_to_unique_string_key(&ob->id);

  copy_v3_v3(cache_elem->loc, ob->obmat[3]);
  copy_m4_m4(cache_elem->obmat, ob->obmat);
  cache_elem->idx = *gp_cache_used;

  /* object is duplicated (particle) */
  if (ob->base_flag & BASE_FROM_DUPLI) {
    /* Check if the original object is not in the viewlayer
     * and cannot be managed as dupli. This is slower, but required to keep
     * the particle drawing FPS and display instanced objects in scene
     * without the original object */
    bool has_original = gpencil_has_noninstanced_object(ob);
    cache_elem->is_dup_ob = (has_original) ? ob->base_flag & BASE_FROM_DUPLI : false;
  }
  else {
    cache_elem->is_dup_ob = false;
  }

  cache_elem->scale = mat4_to_scale(ob->obmat);

  /* save FXs */
  cache_elem->pixfactor = cache_elem->gpd->pixfactor;
  cache_elem->shader_fx = ob->shader_fx;

  /* save wire mode (object mode is always primary option) */
  if (ob->dt == OB_WIRE) {
    cache_elem->shading_type[0] = (int)OB_WIRE;
  }
  else {
    if (v3d) {
      cache_elem->shading_type[0] = (int)v3d->shading.type;
    }
  }

  /* shgrp array */
  cache_elem->tot_layers = 0;
  int totgpl = BLI_listbase_count(&cache_elem->gpd->layers);
  if (totgpl > 0) {
    cache_elem->shgrp_array = MEM_callocN(sizeof(tGPencilObjectCache_shgrp) * totgpl, __func__);
  }

  /* calculate zdepth from point of view */
  float zdepth = 0.0;
  if (rv3d) {
    if (rv3d->is_persp) {
      zdepth = ED_view3d_calc_zfac(rv3d, ob->obmat[3], NULL);
    }
    else {
      zdepth = -dot_v3v3(rv3d->viewinv[2], ob->obmat[3]);
    }
  }
  else {
    /* In render mode, rv3d is not available, so use the distance to camera.
     * The real distance is not important, but the relative distance to the camera plane
     * in order to sort by z_depth of the objects
     */
    float vn[3] = {0.0f, 0.0f, -1.0f}; /* always face down */
    float plane_cam[4];
    struct Object *camera = draw_ctx->scene->camera;
    if (camera) {
      mul_m4_v3(camera->obmat, vn);
      normalize_v3(vn);
      plane_from_point_normal_v3(plane_cam, camera->loc, vn);
      zdepth = dist_squared_to_plane_v3(ob->obmat[3], plane_cam);
    }
  }
  cache_elem->zdepth = zdepth;
  /* increase slots used in cache */
  (*gp_cache_used)++;

  return cache_array;
}

/* add a shading group to the cache to create later */
GpencilBatchGroup *gpencil_group_cache_add(GpencilBatchGroup *cache_array,
                                           bGPDlayer *gpl,
                                           bGPDframe *gpf,
                                           bGPDstroke *gps,
                                           const short type,
                                           const bool onion,
                                           const int vertex_idx,
                                           int *grp_size,
                                           int *grp_used)
{
  GpencilBatchGroup *cache_elem = NULL;
  GpencilBatchGroup *p = NULL;

  /* By default a cache is created with one block with a predefined number of free slots,
   * if the size is not enough, the cache is reallocated adding a new block of free slots.
   * This is done in order to keep cache small. */
  if (*grp_used + 1 > *grp_size) {
    if ((*grp_size == 0) || (cache_array == NULL)) {
      p = MEM_callocN(sizeof(struct GpencilBatchGroup) * GPENCIL_GROUPS_BLOCK_SIZE,
                      "GpencilBatchGroup");
      *grp_size = GPENCIL_GROUPS_BLOCK_SIZE;
    }
    else {
      *grp_size += GPENCIL_GROUPS_BLOCK_SIZE;
      p = MEM_recallocN(cache_array, sizeof(struct GpencilBatchGroup) * *grp_size);
    }
    cache_array = p;
  }
  /* zero out all data */
  cache_elem = &cache_array[*grp_used];
  memset(cache_elem, 0, sizeof(*cache_elem));

  cache_elem->gpl = gpl;
  cache_elem->gpf = gpf;
  cache_elem->gps = gps;
  cache_elem->type = type;
  cache_elem->onion = onion;
  cache_elem->vertex_idx = vertex_idx;

  /* increase slots used in cache */
  (*grp_used)++;

  return cache_array;
}

/* get current cache data */
static GpencilBatchCache *gpencil_batch_get_element(Object *ob)
{
  return ob->runtime.gpencil_cache;
}

/* verify if cache is valid */
static bool gpencil_batch_cache_valid(GpencilBatchCache *cache, bGPdata *gpd, int cfra)
{
  bool valid = true;
  if (cache == NULL) {
    return false;
  }

  cache->is_editmode = GPENCIL_ANY_EDIT_MODE(gpd);
  if (cfra != cache->cache_frame) {
    valid = false;
  }
  else if (gpd->flag & GP_DATA_CACHE_IS_DIRTY) {
    valid = false;
  }
  else if (gpd->flag & GP_DATA_PYTHON_UPDATED) {
    gpd->flag &= ~GP_DATA_PYTHON_UPDATED;
    valid = false;
  }
  else if (cache->is_editmode) {
    /* XXX FIXME This is bad as it means we cannot call gpencil_batch_cache_get twice in a row.
     * Disabling for now. Edit: seems to work without it... */
    // valid = false;
  }
  else if (cache->is_dirty) {
    /* TODO, maybe get rid of the other dirty flags. */
    valid = false;
  }

  return valid;
}

/* cache init */
static GpencilBatchCache *gpencil_batch_cache_init(Object *ob, int cfra)
{
  bGPdata *gpd = (bGPdata *)ob->data;

  GpencilBatchCache *cache = gpencil_batch_get_element(ob);

  if (!cache) {
    cache = MEM_callocN(sizeof(*cache), __func__);
    ob->runtime.gpencil_cache = cache;
  }
  else {
    memset(cache, 0, sizeof(*cache));
  }

  cache->is_editmode = GPENCIL_ANY_EDIT_MODE(gpd);

  cache->is_dirty = true;

  cache->cache_frame = cfra;

  return cache;
}

/* clear cache */
static void gpencil_batch_cache_clear(GpencilBatchCache *cache)
{
  if (!cache) {
    return;
  }

  GPU_BATCH_DISCARD_SAFE(cache->b_stroke.batch);
  GPU_BATCH_DISCARD_SAFE(cache->b_point.batch);
  GPU_BATCH_DISCARD_SAFE(cache->b_fill.batch);
  GPU_BATCH_DISCARD_SAFE(cache->b_edit.batch);
  GPU_BATCH_DISCARD_SAFE(cache->b_edlin.batch);

  MEM_SAFE_FREE(cache->b_stroke.batch);
  MEM_SAFE_FREE(cache->b_point.batch);
  MEM_SAFE_FREE(cache->b_fill.batch);
  MEM_SAFE_FREE(cache->b_edit.batch);
  MEM_SAFE_FREE(cache->b_edlin.batch);

  /* internal format data */
  MEM_SAFE_FREE(cache->b_stroke.format);
  MEM_SAFE_FREE(cache->b_point.format);
  MEM_SAFE_FREE(cache->b_fill.format);
  MEM_SAFE_FREE(cache->b_edit.format);
  MEM_SAFE_FREE(cache->b_edlin.format);

  MEM_SAFE_FREE(cache->grp_cache);
  cache->grp_size = 0;
  cache->grp_used = 0;

  /* New code */
  GPU_BATCH_DISCARD_SAFE(cache->fill_batch);
  GPU_BATCH_DISCARD_SAFE(cache->stroke_batch);
  GPU_VERTBUF_DISCARD_SAFE(cache->vbo);
  GPU_INDEXBUF_DISCARD_SAFE(cache->ibo);
}

/* get cache */
GpencilBatchCache *gpencil_batch_cache_get(Object *ob, int cfra)
{
  bGPdata *gpd = (bGPdata *)ob->data;

  GpencilBatchCache *cache = gpencil_batch_get_element(ob);
  if (!gpencil_batch_cache_valid(cache, gpd, cfra)) {
    if (cache) {
      gpencil_batch_cache_clear(cache);
    }
    return gpencil_batch_cache_init(ob, cfra);
  }
  else {
    return cache;
  }
}