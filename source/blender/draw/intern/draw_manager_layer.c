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

  GPUFrameBuffer *framebuffer;
  GPUTexture *color;
} DRWLayer;

static GHash *DRW_layers_hash = NULL;

static void drw_layer_recreate_textures(DRWLayer *layer)
{
  DRW_TEXTURE_FREE_SAFE(layer->color);

  DRW_texture_ensure_fullscreen_2d(&layer->color, GPU_RGBA8, 0);
  GPU_framebuffer_ensure_config(&layer->framebuffer,
                                {GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(layer->color)});
}

static DRWLayer *drw_layer_create(const DRWLayerType *type)
{
  DRWLayer *layer = MEM_callocN(sizeof(*layer), __func__);

  layer->type = type;
  drw_layer_recreate_textures(layer);

  return layer;
}

static void drw_layer_free(DRWLayer *layer)
{
  DRW_TEXTURE_FREE_SAFE(layer->color);
  GPU_FRAMEBUFFER_FREE_SAFE(layer->framebuffer);

  MEM_SAFE_FREE(layer);
}

static void drw_layer_free_cb(void *layer)
{
  drw_layer_free(layer);
}

static void drw_layer_ensure_updated_textures(DRWLayer *layer)
{
  const float *size = DRW_viewport_size_get();

  BLI_assert(layer->color);

  if ((GPU_texture_width(layer->color) != size[0]) ||
      (GPU_texture_height(layer->color) != size[1])) {
    drw_layer_recreate_textures(layer);
  }
}

static DRWLayer *drw_layer_for_type_updated_ensure(const DRWLayerType *type)
{
  if (DRW_layers_hash == NULL) {
    DRW_layers_hash = BLI_ghash_ptr_new_ex("DRW_layers_hash", DRW_layer_types_count);
  }
  DRWLayer *layer = BLI_ghash_lookup(DRW_layers_hash, type);

  if (layer) {
    /* Ensure updated texture dimensions. */
    drw_layer_ensure_updated_textures(layer);
  }
  else {
    layer = drw_layer_create(type);
    BLI_ghash_insert(DRW_layers_hash, (void *)type, layer);
  }

  /* Could reinsert layer at tail here, so that the next layer to be drawn is likely first in the
   * list (or at least close to the top). Iterating isn't that expensive though. */

  return layer;
}

void DRW_layers_free(void)
{
  if (DRW_layers_hash) {
    BLI_ghash_free(DRW_layers_hash, NULL, drw_layer_free_cb);
  }
}

static void drw_layers_textures_draw_composited(GPUTexture *tex)
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

  GPU_framebuffer_bind(DST.default_framebuffer);
  DRW_clear_background();

  /* First pass: Update dirty framebuffers and blit into cache. */
  for (int i = 0; i < DRW_layer_types_count; i++) {
    const DRWLayerType *layer_type = &DRW_layer_types[i];

    if (layer_type->poll && !layer_type->poll()) {
      is_layer_visible[i] = false;
      continue;
    }
    is_layer_visible[i] = true;

    if (layer_type->may_skip && layer_type->may_skip()) {
      continue;
    }

    DRWLayer *layer = drw_layer_for_type_updated_ensure(layer_type);

    DRW_clear_background();
    DRW_state_reset();
    layer_type->draw_layer();

    /* Blit the default framebuffer into the layer framebuffer cache. */
    GPU_framebuffer_bind(DST.default_framebuffer);
    GPU_framebuffer_blit(DST.default_framebuffer, 0, layer->framebuffer, 0, GPU_COLOR_BIT);
  }

  BLI_assert(GPU_framebuffer_active_get() == DST.default_framebuffer);
  DRW_clear_background();

  for (int i = 0; i < DRW_layer_types_count; i++) {
    if (!is_layer_visible[i]) {
      continue;
    }

    const DRWLayerType *layer_type = &DRW_layer_types[i];
    const DRWLayer *layer = drw_layer_for_type_updated_ensure(layer_type);

    drw_layers_textures_draw_composited(layer->color);
  }

  GPU_framebuffer_bind(DST.default_framebuffer);
}
