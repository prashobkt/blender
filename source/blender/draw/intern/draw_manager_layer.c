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
 * \ingroup draw
 */

#include <stdio.h>

#include "BLI_alloca.h"
#include "BLI_sys_types.h"

#include "draw_manager.h"

typedef struct DRWLayer {
  struct DRWLayer *next, *prev;

  const DRWLayerType *type;

  struct {
    /* Store which viewport this layer was cached for, so we invalidate these buffers if the
     * viewport changed. */
    const GPUViewport *viewport;

    GPUFrameBuffer *framebuffer;
    /* The texture attached to the framebuffer containing the actual cache. */
    GPUTexture *color;
  } cache;
} DRWLayer;

/**
 * If a layer never skips redrawing, it doesn't make sense to keep its framebuffer attachements
 * cached, they just take up GPU memory.
 */
static bool drw_layer_supports_caching(const DRWLayer *layer)
{
  return layer->type->may_skip != NULL;
}

static void drw_layer_recreate_textures(DRWLayer *layer)
{
  BLI_assert(drw_layer_supports_caching(layer));

  DRW_TEXTURE_FREE_SAFE(layer->cache.color);

  DRW_texture_ensure_fullscreen_2d(&layer->cache.color, GPU_RGBA8, 0);
  GPU_framebuffer_ensure_config(&layer->cache.framebuffer,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(layer->cache.color)});
}

static DRWLayer *drw_layer_create(const DRWLayerType *type, const GPUViewport *viewport)
{
  DRWLayer *layer = MEM_callocN(sizeof(*layer), __func__);

  layer->type = type;

  if (drw_layer_supports_caching(layer)) {
    layer->cache.viewport = viewport;
    drw_layer_recreate_textures(layer);
  }

  return layer;
}

static void drw_layer_free(DRWLayer *layer)
{
  DRW_TEXTURE_FREE_SAFE(layer->cache.color);
  GPU_FRAMEBUFFER_FREE_SAFE(layer->cache.framebuffer);

  MEM_SAFE_FREE(layer);
}

static void drw_layer_free_cb(void *layer)
{
  drw_layer_free(layer);
}

static void drw_layer_cache_ensure_updated(DRWLayer *layer)
{
  const float *size = DRW_viewport_size_get();

  BLI_assert(drw_layer_supports_caching(layer));
  BLI_assert(layer->cache.color);

  layer->cache.viewport = DST.viewport;

  /* Ensure updated texture dimensions. */
  if ((GPU_texture_width(layer->cache.color) != size[0]) ||
      (GPU_texture_height(layer->cache.color) != size[1])) {
    drw_layer_recreate_textures(layer);
  }
}

static void drw_layer_draw(const DRWLayer *layer)
{
  DRW_state_reset();
  layer->type->draw_layer();
}

static bool drw_layer_needs_cache_update(const DRWLayer *layer)
{
  const float *size = DRW_viewport_size_get();

  BLI_assert(drw_layer_supports_caching(layer));

  if ((DST.viewport != layer->cache.viewport) ||
      (GPU_texture_width(layer->cache.color) != size[0]) ||
      (GPU_texture_height(layer->cache.color) != size[1])) {
    /* Always update after viewport changed. */
    return true;
  }

  return layer->type->may_skip() == false;
}

static DRWLayer *drw_layer_for_type_ensure(const DRWLayerType *type)
{
  if (DST.layers_hash == NULL) {
    DST.layers_hash = BLI_ghash_ptr_new_ex("DRW_layers_hash", DRW_layer_types_count);
  }

  DRWLayer *layer = BLI_ghash_lookup(DST.layers_hash, type);

  if (!layer) {
    layer = drw_layer_create(type, DST.viewport);
    BLI_ghash_insert(DST.layers_hash, (void *)type, layer);
  }

  /* Could reinsert layer at tail here, so that the next layer to be drawn is likely first in the
   * list (or at least close to the top). Iterating isn't that expensive though. */

  return layer;
}

void DRW_layers_free(void)
{
  if (DST.layers_hash) {
    BLI_ghash_free(DST.layers_hash, NULL, drw_layer_free_cb);
    DST.layers_hash = NULL;
  }
}

static void drw_layers_textures_draw_alpha_over(GPUTexture *tex)
{
  drw_state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA_PREMUL);

  /* Draw as texture for final render (without immediate mode). */
  GPUBatch *geom = DRW_cache_fullscreen_quad_get();
  GPU_batch_program_set_builtin(geom, GPU_SHADER_2D_IMAGE);

  GPU_texture_bind(tex, 0);

  float mat[4][4];
  unit_m4(mat);
  GPU_batch_uniform_mat4(geom, "ModelViewProjectionMatrix", mat);

  GPU_batch_program_use_begin(geom);
  GPU_batch_bind(geom);
  GPU_batch_draw_advanced(geom, 0, 0, 0, 0);
  GPU_batch_program_use_end(geom);

  GPU_texture_unbind(tex);
}

void DRW_layers_draw_combined_cached(void)
{
  /* Store if poll succeeded, to avoid calling it twice. */
  bool *is_layer_visible = BLI_array_alloca(is_layer_visible, DRW_layer_types_count);

  BLI_assert(!DST.layers_hash || (DRW_layer_types_count >= BLI_ghash_len(DST.layers_hash)));

  GPU_framebuffer_bind(DST.default_framebuffer);
  DRW_clear_background();

  /* First pass: Update dirty framebuffers and blit into cache. Skip layers that don't support
   * caching, we can avoid the costs of a separate framebuffer (and compositing it) then. */
  for (int i = 0; i < DRW_layer_types_count; i++) {
    const DRWLayerType *layer_type = &DRW_layer_types[i];

    if (layer_type->poll && !layer_type->poll()) {
      is_layer_visible[i] = false;
      continue;
    }
    is_layer_visible[i] = true;

    DRWLayer *layer = drw_layer_for_type_ensure(layer_type);

    if (!drw_layer_supports_caching(layer)) {
      /* Layer always redraws -> Skip caching and draw directly into the default framebuffer in
       * second pass. */
      continue;
    }

    if (!drw_layer_needs_cache_update(layer)) {
      continue;
    }

    drw_layer_cache_ensure_updated(layer);

    DRW_clear_background();

    drw_layer_draw(layer);

    /* Blit the default framebuffer into the layer framebuffer cache. */
    GPU_framebuffer_bind(DST.default_framebuffer);
    GPU_framebuffer_blit(DST.default_framebuffer, 0, layer->cache.framebuffer, 0, GPU_COLOR_BIT);
  }

  BLI_assert(GPU_framebuffer_active_get() == DST.default_framebuffer);
  DRW_clear_background();

  for (int i = 0; i < DRW_layer_types_count; i++) {
    if (!is_layer_visible[i]) {
      continue;
    }

    const DRWLayerType *layer_type = &DRW_layer_types[i];
    const DRWLayer *layer = drw_layer_for_type_ensure(layer_type);

    if (drw_layer_supports_caching(layer)) {
      drw_layers_textures_draw_alpha_over(layer->cache.color);
    }
    else {
      /* Uncached layer, draw directly into default framebuffer. */
      BLI_assert(GPU_framebuffer_active_get() == DST.default_framebuffer);
      drw_layer_draw(layer);
    }
  }

  GPU_framebuffer_bind(DST.default_framebuffer);
}
