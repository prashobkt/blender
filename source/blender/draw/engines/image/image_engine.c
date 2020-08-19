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
 * \ingroup draw_editors
 *
 * Draw engine to draw the Image/UV editor
 */

#include "DRW_render.h"

#include "BKE_image.h"
#include "BKE_object.h"

#include "BLI_rect.h"

#include "DNA_camera_types.h"

#include "IMB_imbuf_types.h"

#include "ED_image.h"

#include "GPU_batch.h"

#include "image_engine.h"
#include "image_private.h"

#define SIMA_DRAW_FLAG_SHOW_ALPHA (1 << 0)
#define SIMA_DRAW_FLAG_APPLY_ALPHA (1 << 1)
#define SIMA_DRAW_FLAG_SHUFFLING (1 << 2)
#define SIMA_DRAW_FLAG_DEPTH (1 << 3)
#define SIMA_DRAW_FLAG_TILED (1 << 4)

static void image_batch_instances_update(IMAGE_Data *id, Image *image)
{
  IMAGE_StorageList *stl = id->stl;
  IMAGE_PrivateData *pd = stl->pd;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;
  const bool is_tiled_texture = image && image->source == IMA_SRC_TILED;
  rcti instances;

  if (is_tiled_texture) {
    pd->draw_batch = IMAGE_batches_image_tiled_create(image);
  }
  else {
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

    pd->draw_batch = IMAGE_batches_image_create(&instances);
  }
}

static void image_cache_image(IMAGE_Data *id, Image *ima, ImageUser *iuser, ImBuf *ibuf)
{
  IMAGE_PassList *psl = id->psl;
  IMAGE_StorageList *stl = id->stl;
  IMAGE_PrivateData *pd = stl->pd;

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
        pd->texture = GPU_texture_create_2d(ibuf->x, ibuf->y, GPU_R16F, ibuf->zbuf_float, NULL);
        pd->owns_texture = true;
      }
      else if (ibuf->rect_float && ibuf->channels == 1) {
        pd->texture = GPU_texture_create_2d(ibuf->x, ibuf->y, GPU_R16F, ibuf->rect_float, NULL);
        pd->owns_texture = true;
      }
    }
    else if (ima->source == IMA_SRC_TILED) {
      pd->texture = BKE_image_get_gpu_tiles(ima, iuser, ibuf);
      tex_tile_data = BKE_image_get_gpu_tilemap(ima, iuser, NULL);
      pd->owns_texture = false;
    }
    else {
      pd->texture = BKE_image_get_gpu_texture(ima, iuser, ibuf);
      pd->owns_texture = false;
    }
  }

  if (pd->texture) {
    eGPUSamplerState state = 0;
    GPUShader *shader = IMAGE_shaders_image_get();
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
      DRW_shgroup_uniform_texture_ex(shgrp, "imageTileArray", pd->texture, state);
      DRW_shgroup_uniform_texture(shgrp, "imageTileData", tex_tile_data);
    }
    else {
      DRW_shgroup_uniform_texture_ex(shgrp, "imageTexture", pd->texture, state);
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

    DRW_shgroup_call(shgrp, pd->draw_batch, NULL);
  }
  else {
    /* No image available. use the image unavailable shader. */
    GPUShader *shader = IMAGE_shaders_image_unavailable_get();
    DRWShadingGroup *grp = DRW_shgroup_create(shader, psl->image_pass);
    DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);

    /* sima->zoom = 1 texel covers (sima->zoom * sima->zoom) screen pixels.
     * Creates a curve function for better visual result. */
    float zoom_level = powf(MAX2(sima->zoom - 1.0, 0.1), 0.33f);
    zoom_level = clamp_f(zoom_level, 1.25, 4.75);
    DRW_shgroup_uniform_float_copy(grp, "zoomScale", sima->zoom);
    DRW_shgroup_uniform_float_copy(grp, "zoomLevel", zoom_level);
    DRW_shgroup_call(grp, pd->draw_batch, NULL);
  }
}

/* -------------------------------------------------------------------- */
/** \name Engine Callbacks
 * \{ */
static void IMAGE_engine_init(void *vedata)
{
  IMAGE_shader_library_ensure();
  IMAGE_Data *id = (IMAGE_Data *)vedata;
  IMAGE_StorageList *stl = id->stl;
  if (!stl->pd) {
    stl->pd = MEM_callocN(sizeof(IMAGE_PrivateData), __func__);
  }
  IMAGE_PrivateData *pd = stl->pd;

  pd->ibuf = NULL;
  pd->lock = NULL;
  pd->texture = NULL;
}

static void IMAGE_cache_init(void *vedata)
{
  IMAGE_Data *id = (IMAGE_Data *)vedata;
  IMAGE_StorageList *stl = id->stl;
  IMAGE_PrivateData *pd = stl->pd;
  IMAGE_PassList *psl = id->psl;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;

  /* e_data.image needs to be set as first other calls may access it to determine
   * if we are looking at a texture, viewer or render result */
  Image *image = ED_space_image(sima);
  const bool has_image = image != NULL;
  const bool show_multilayer = has_image && BKE_image_is_multilayer(image);

  if (has_image) {
    if (show_multilayer) {
      /* update multiindex and pass for the current eye */
      BKE_image_multilayer_index(image->rr, &sima->iuser);
    }
    else {
      BKE_image_multiview_index(image, &sima->iuser);
    }
  }

  image_batch_instances_update(id, image);

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
    ImBuf *ibuf = ED_space_image_acquire_buffer(sima, &pd->lock, 0);
    image_cache_image(id, image, &sima->iuser, ibuf);
    pd->ibuf = ibuf;
  }
}

static void IMAGE_cache_populate(void *UNUSED(vedata), Object *UNUSED(ob))
{
}

static void image_draw_finish(IMAGE_Data *vedata)
{
  IMAGE_Data *id = (IMAGE_Data *)vedata;
  IMAGE_StorageList *stl = id->stl;
  IMAGE_PrivateData *pd = stl->pd;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;

  ED_space_image_release_buffer(sima, pd->ibuf, pd->lock);

  if (pd->owns_texture) {
    GPU_texture_free(pd->texture);
    pd->owns_texture = false;
  }
  pd->texture = NULL;

  GPU_BATCH_DISCARD_SAFE(pd->draw_batch);
}

static void IMAGE_draw_scene(void *vedata)
{
  IMAGE_Data *id = (IMAGE_Data *)vedata;
  IMAGE_PassList *psl = id->psl;

  DRW_draw_pass(psl->image_pass);

  image_draw_finish(vedata);
}

static void IMAGE_engine_free(void)
{
  IMAGE_shaders_free();
}

/* \} */
static const DrawEngineDataSize IMAGE_data_size = DRW_VIEWPORT_DATA_SIZE(IMAGE_Data);

DrawEngineType draw_engine_image_type = {
    NULL,                  /* next */
    NULL,                  /* prev */
    N_("UV/Image"),        /* idname */
    &IMAGE_data_size,      /*vedata_size */
    &IMAGE_engine_init,    /* engine_init */
    &IMAGE_engine_free,    /* engine_free */
    &IMAGE_cache_init,     /* cache_init */
    &IMAGE_cache_populate, /* cache_populate */
    NULL,                  /* cache_finish */
    &IMAGE_draw_scene,     /* draw_scene */
    NULL,                  /* view_update */
    NULL,                  /* id_update */
    NULL,                  /* render_to_image */
};
