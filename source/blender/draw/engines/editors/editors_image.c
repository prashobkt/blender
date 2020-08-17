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
 * \ingroup draw_engine
 */
#include "DRW_render.h"

#include "BKE_image.h"

#include "BLI_dynstr.h"
#include "BLI_rect.h"

#include "DNA_camera_types.h"
#include "DNA_space_types.h"

#include "UI_resources.h"

#include "IMB_imbuf_types.h"

#include "ED_image.h"

#include "GPU_batch.h"

#include "editors_engine.h"
#include "editors_private.h"

#define DEFAULT_IMAGE_SIZE_PX 256

#define SIMA_DRAW_FLAG_SHOW_ALPHA (1 << 0)
#define SIMA_DRAW_FLAG_APPLY_ALPHA (1 << 1)
#define SIMA_DRAW_FLAG_SHUFFLING (1 << 2)
#define SIMA_DRAW_FLAG_DEPTH (1 << 3)
#define SIMA_DRAW_FLAG_TILED (1 << 4)

static struct {
  void *lock;
  ImBuf *ibuf;
  Image *image;
  GPUTexture *texture;
  /* Does `e_data` own the texture so it needs to be cleanup after usage. */
  bool owns_texture;

  GPUBatch *gpu_batch_image;

  rcti gpu_batch_instances_rect;
  GPUBatch *gpu_batch_instances;
} e_data = {0}; /* Engine data */

/* -------------------------------------------------------------------- */
/** \name Image Pass
 * \{ */

static void editors_image_cache_image(EDITORS_PassList *psl,
                                      Image *ima,
                                      ImageUser *iuser,
                                      ImBuf *ibuf)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  const Scene *scene = draw_ctx->scene;
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;

  GPUTexture *tex_tile_data = NULL;

  if (ima && ibuf) {
    if (sima->flag & SI_SHOW_ZBUF && (ibuf->zbuf || ibuf->zbuf_float || (ibuf->channels == 1))) {
      if (ibuf->zbuf) {
        // TODO: zbuf integer based
        // sima_draw_zbuf_pixels(x, y, ibuf->x, ibuf->y, ibuf->zbuf, zoomx, zoomy);
        BLI_assert(!"Integer based depth buffers not supported");
      }
      else if (ibuf->zbuf_float) {
        e_data.texture = GPU_texture_create_2d(ibuf->x, ibuf->y, GPU_R16F, ibuf->zbuf_float, NULL);
        e_data.owns_texture = true;
      }
      else if (ibuf->rect_float && ibuf->channels == 1) {
        e_data.texture = GPU_texture_create_2d(ibuf->x, ibuf->y, GPU_R16F, ibuf->rect_float, NULL);
        e_data.owns_texture = true;
      }
    }
    else if (ima->source == IMA_SRC_TILED) {
      e_data.texture = BKE_image_get_gpu_tiles(ima, iuser, ibuf);
      tex_tile_data = BKE_image_get_gpu_tilemap(ima, iuser, NULL);
      e_data.owns_texture = false;
    }
    else {
      e_data.texture = BKE_image_get_gpu_texture(ima, iuser, ibuf);
      e_data.owns_texture = false;
    }
  }

  if (e_data.texture) {
    eGPUSamplerState state = 0;
    GPUShader *shader = EDITORS_shaders_image_get();
    DRWShadingGroup *shgrp = DRW_shgroup_create(shader, psl->image_pass);
    static float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    static float shuffle[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    int draw_flags = 0;
    static float far_near[2] = {100.0f, 0.0f};
    const bool use_premul_alpha = ima->alpha_mode == IMA_ALPHA_PREMUL;

    if (scene->camera && scene->camera->type == OB_CAMERA) {
      far_near[1] = ((Camera *)scene->camera->data)->clip_start;
      far_near[0] = ((Camera *)scene->camera->data)->clip_end;
    }

    if (tex_tile_data != NULL) {
      draw_flags |= SIMA_DRAW_FLAG_TILED;
      DRW_shgroup_uniform_texture_ex(shgrp, "imageTileArray", e_data.texture, state);
      DRW_shgroup_uniform_texture(shgrp, "imageTileData", tex_tile_data);
    }
    else {
      DRW_shgroup_uniform_texture_ex(shgrp, "imageTexture", e_data.texture, state);
    }

    if ((sima->flag & SI_USE_ALPHA) != 0) {
      /* Show RGBA */
      draw_flags |= SIMA_DRAW_FLAG_SHOW_ALPHA;
    }
    else if ((sima->flag & SI_SHOW_ALPHA) != 0) {
      draw_flags |= SIMA_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(shuffle, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    else if ((sima->flag & SI_SHOW_ZBUF) != 0) {
      draw_flags |= SIMA_DRAW_FLAG_DEPTH | SIMA_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(shuffle, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    else if ((sima->flag & SI_SHOW_R) != 0) {
      draw_flags |= SIMA_DRAW_FLAG_APPLY_ALPHA | SIMA_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(shuffle, 1.0f, 0.0f, 0.0f, 0.0f);
    }
    else if ((sima->flag & SI_SHOW_G) != 0) {
      draw_flags |= SIMA_DRAW_FLAG_APPLY_ALPHA | SIMA_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(shuffle, 0.0f, 1.0f, 0.0f, 0.0f);
    }
    else if ((sima->flag & SI_SHOW_B) != 0) {
      draw_flags |= SIMA_DRAW_FLAG_APPLY_ALPHA | SIMA_DRAW_FLAG_SHUFFLING;
      copy_v4_fl4(shuffle, 0.0f, 0.0f, 1.0f, 0.0f);
    }

    DRW_shgroup_uniform_vec2_copy(shgrp, "farNearDistances", far_near);
    DRW_shgroup_uniform_vec4_copy(shgrp, "color", color);
    DRW_shgroup_uniform_vec4_copy(shgrp, "shuffle", shuffle);
    DRW_shgroup_uniform_int_copy(shgrp, "drawFlags", draw_flags);
    DRW_shgroup_uniform_bool_copy(shgrp, "imgPremultiplied", use_premul_alpha);

    DRW_shgroup_call_instances_with_attrs(
        shgrp, NULL, e_data.gpu_batch_image, e_data.gpu_batch_instances);
  }
  else {
    /* No image available. use the image unavailable shader. */
    GPUShader *shader = EDITORS_shaders_image_unavailable_get();
    DRWShadingGroup *grp = DRW_shgroup_create(shader, psl->image_pass);
    DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    DRW_shgroup_call(grp, e_data.gpu_batch_image, NULL);
  }
}

/* \} */

/* -------------------------------------------------------------------- */
/** \name DrawEngine Interface
 * \{ */
void EDITORS_image_init(EDITORS_Data *UNUSED(vedata))
{
  e_data.image = NULL;
  e_data.ibuf = NULL;
  e_data.lock = NULL;
  e_data.texture = NULL;

  /* Create batch and unit matrix */
  if (!e_data.gpu_batch_image) {
    e_data.gpu_batch_image = DRW_cache_quad_image_get();
  }
}

static void editors_image_batch_instances_update(void)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;
  Image *image = e_data.image;
  const bool is_tiled_texture = image && image->source == IMA_SRC_TILED;
  rcti instances;

  if (is_tiled_texture) {
    GPU_BATCH_DISCARD_SAFE(e_data.gpu_batch_instances);
    e_data.gpu_batch_instances = BKE_image_tiled_gpu_instance_batch_create(image);
    return;
  }

  /* repeat */
  BLI_rcti_init(&instances, 0, 0, 0, 0);
  if ((sima->flag & SI_DRAW_TILE) != 0) {
    float view_inv_m4[4][4];
    DRW_view_viewmat_get(NULL, view_inv_m4, true);
    float v3min[3] = {0.0f, 0.0f, 0.0f};
    float v3max[3] = {1.0f, 1.0f, 0.0f};
    mul_m4_v3(view_inv_m4, v3min);
    mul_m4_v3(view_inv_m4, v3max);

    instances.xmin = (int)floorf(v3min[0]);
    instances.ymin = (int)floorf(v3min[1]);
    instances.xmax = (int)floorf(v3max[0]);
    instances.ymax = (int)floorf(v3max[1]);
  }

  if (e_data.gpu_batch_instances) {
    if (!BLI_rcti_compare(&e_data.gpu_batch_instances_rect, &instances)) {
      GPU_BATCH_DISCARD_SAFE(e_data.gpu_batch_instances);
    }
  }

  if (!e_data.gpu_batch_instances) {
    e_data.gpu_batch_instances = EDITORS_batches_image_instance_create(&instances);
    e_data.gpu_batch_instances_rect = instances;
  }
}

void EDITORS_image_cache_init(EDITORS_Data *vedata)
{
  EDITORS_PassList *psl = vedata->psl;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;

  /* e_data.image needs to be set as first other calls may access it to determine
   * if we are looking at a texture, viewer or render result */
  e_data.image = ED_space_image(sima);
  const bool has_image = e_data.image != NULL;
  const bool show_multilayer = has_image && BKE_image_is_multilayer(e_data.image);

  if (has_image) {
    if (show_multilayer) {
      /* update multiindex and pass for the current eye */
      BKE_image_multilayer_index(e_data.image->rr, &sima->iuser);
    }
    else {
      BKE_image_multiview_index(e_data.image, &sima->iuser);
    }
  }

  editors_image_batch_instances_update();

  {
    /* Write depth is needed for background rendering. Near depth is used for transparency
     * checker and Far depth is used for indicating the image size. */
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_ALWAYS |
                     DRW_STATE_BLEND_ALPHA_PREMUL;
    psl->image_pass = DRW_pass_create("Image", state);
  }

  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  GPU_framebuffer_bind(dfbl->default_fb);
  static float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  GPU_framebuffer_clear_color_depth(dfbl->default_fb, clear_col, 1.0);

  {
    ImBuf *ibuf = ED_space_image_acquire_buffer(sima, &e_data.lock, 0);
    editors_image_cache_image(psl, e_data.image, &sima->iuser, ibuf);
    e_data.ibuf = ibuf;
  }
}

static void EDITORS_image_draw_finish(EDITORS_Data *UNUSED(vedata))
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;

  ED_space_image_release_buffer(sima, e_data.ibuf, e_data.lock);
  e_data.image = NULL;

  if (e_data.owns_texture) {
    GPU_texture_free(e_data.texture);
    e_data.owns_texture = false;
  }
  e_data.texture = NULL;

  GPU_BATCH_DISCARD_SAFE(e_data.gpu_batch_instances);
}

void EDITORS_image_draw_scene(EDITORS_Data *vedata)
{
  EDITORS_PassList *psl = vedata->psl;

  DRW_draw_pass(psl->image_pass);

  EDITORS_image_draw_finish(vedata);
}

/* \} */