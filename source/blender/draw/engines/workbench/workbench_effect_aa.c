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

#include "ED_screen.h"

#include "BLI_jitter_2d.h"

#include "smaa_textures.h"

#include "workbench_private.h"

static struct {
  bool init;
  float jitter_5[5][2];
  float jitter_8[8][2];
  float jitter_11[11][2];
  float jitter_16[16][2];
  float jitter_32[32][2];
} e_data = {false};

static void workbench_taa_jitter_init_order(float (*table)[2], int num)
{
  BLI_jitter_init(table, num);

  /* find closest element to center */
  int closest_index = 0;
  float closest_squared_distance = 1.0f;

  for (int index = 0; index < num; index++) {
    const float squared_dist = SQUARE(table[index][0]) + SQUARE(table[index][1]);
    if (squared_dist < closest_squared_distance) {
      closest_squared_distance = squared_dist;
      closest_index = index;
    }
  }

  /* move jitter table so that closest sample is in center */
  for (int index = 0; index < num; index++) {
    sub_v2_v2(table[index], table[closest_index]);
    mul_v2_fl(table[index], 2.0f);
  }

  /* swap center sample to the start of the table */
  if (closest_index != 0) {
    swap_v2_v2(table[0], table[closest_index]);
  }

  /* sort list based on furtest distance with previous */
  for (int i = 0; i < num - 2; i++) {
    float f_squared_dist = 0.0;
    int f_index = i;
    for (int j = i + 1; j < num; j++) {
      const float squared_dist = SQUARE(table[i][0] - table[j][0]) +
                                 SQUARE(table[i][1] - table[j][1]);
      if (squared_dist > f_squared_dist) {
        f_squared_dist = squared_dist;
        f_index = j;
      }
    }
    swap_v2_v2(table[i + 1], table[f_index]);
  }
}

static void workbench_taa_jitter_init(void)
{
  if (e_data.init == false) {
    workbench_taa_jitter_init_order(e_data.jitter_5, 5);
    workbench_taa_jitter_init_order(e_data.jitter_8, 8);
    workbench_taa_jitter_init_order(e_data.jitter_11, 11);
    workbench_taa_jitter_init_order(e_data.jitter_16, 16);
    workbench_taa_jitter_init_order(e_data.jitter_32, 32);
  }
}

BLI_INLINE bool workbench_taa_enabled(WORKBENCH_PrivateData *wpd)
{
  if (DRW_state_is_image_render()) {
    const DRWContextState *draw_ctx = DRW_context_state_get();
    if (draw_ctx->v3d) {
      return draw_ctx->scene->display.viewport_aa > SCE_DISPLAY_AA_FXAA;
    }
    else {
      return draw_ctx->scene->display.render_aa > SCE_DISPLAY_AA_FXAA;
    }
  }
  else {
    return !(IS_NAVIGATING(wpd) || wpd->is_playback) &&
           wpd->preferences->viewport_aa > SCE_DISPLAY_AA_FXAA;
  }
}

int workbench_aa_sample_count_get(WORKBENCH_PrivateData *wpd)
{
  const Scene *scene = DRW_context_state_get()->scene;
  if (workbench_taa_enabled(wpd)) {
    if (DRW_state_is_image_render()) {
      const DRWContextState *draw_ctx = DRW_context_state_get();
      if (draw_ctx->v3d) {
        return scene->display.viewport_aa;
      }
      else {
        return scene->display.render_aa;
      }
    }
    else {
      return wpd->preferences->viewport_aa;
    }
  }
  else {
    /* when no TAA is disabled return 0 to render a single sample
     * see `workbench_render.c` */
    return 0;
  }
}

void workbench_aa_engine_init(WORKBENCH_Data *vedata)
{
  WORKBENCH_FramebufferList *fbl = vedata->fbl;
  WORKBENCH_TextureList *txl = vedata->txl;
  WORKBENCH_PrivateData *wpd = vedata->stl->g_data;
  DrawEngineType *owner = (DrawEngineType *)&workbench_aa_engine_init;

  if (wpd->taa_sample_len > 0) {
    workbench_taa_jitter_init();

    DRW_texture_ensure_fullscreen_2d(&txl->history_buffer_tx, GPU_RGBA16F, 0);
    DRW_texture_ensure_fullscreen_2d(&txl->depth_buffer_tx, GPU_DEPTH24_STENCIL8, 0);

    wpd->smaa_edge_tx = DRW_texture_pool_query_fullscreen(GPU_RG8, owner);
    wpd->smaa_weight_tx = DRW_texture_pool_query_fullscreen(GPU_RGBA8, owner);

    GPU_framebuffer_ensure_config(&fbl->antialiasing_fb,
                                  {
                                      GPU_ATTACHMENT_TEXTURE(txl->depth_buffer_tx),
                                      GPU_ATTACHMENT_TEXTURE(txl->history_buffer_tx),
                                  });

    GPU_framebuffer_ensure_config(&fbl->smaa_edge_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(wpd->smaa_edge_tx),
                                  });

    GPU_framebuffer_ensure_config(&fbl->smaa_weight_fb,
                                  {
                                      GPU_ATTACHMENT_NONE,
                                      GPU_ATTACHMENT_TEXTURE(wpd->smaa_weight_tx),
                                  });

    /* TODO could be shared for all viewports. */
    if (txl->smaa_search_tx == NULL) {
      txl->smaa_search_tx = GPU_texture_create_nD(SEARCHTEX_WIDTH,
                                                  SEARCHTEX_HEIGHT,
                                                  0,
                                                  2,
                                                  searchTexBytes,
                                                  GPU_R8,
                                                  GPU_DATA_UNSIGNED_BYTE,
                                                  0,
                                                  false,
                                                  NULL);

      txl->smaa_area_tx = GPU_texture_create_nD(AREATEX_WIDTH,
                                                AREATEX_HEIGHT,
                                                0,
                                                2,
                                                areaTexBytes,
                                                GPU_RG8,
                                                GPU_DATA_UNSIGNED_BYTE,
                                                0,
                                                false,
                                                NULL);

      GPU_texture_bind(txl->smaa_search_tx, 0);
      GPU_texture_filter_mode(txl->smaa_search_tx, true);
      GPU_texture_unbind(txl->smaa_search_tx);

      GPU_texture_bind(txl->smaa_area_tx, 0);
      GPU_texture_filter_mode(txl->smaa_area_tx, true);
      GPU_texture_unbind(txl->smaa_area_tx);
    }
  }
  else {
    /* Cleanup */
    DRW_TEXTURE_FREE_SAFE(txl->history_buffer_tx);
    DRW_TEXTURE_FREE_SAFE(txl->depth_buffer_tx);
    DRW_TEXTURE_FREE_SAFE(txl->smaa_search_tx);
    DRW_TEXTURE_FREE_SAFE(txl->smaa_area_tx);
  }
}

void workbench_aa_cache_init(WORKBENCH_Data *vedata)
{
  WORKBENCH_TextureList *txl = vedata->txl;
  WORKBENCH_PrivateData *wpd = vedata->stl->g_data;
  WORKBENCH_PassList *psl = vedata->psl;
  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();
  DRWShadingGroup *grp = NULL;

  if (wpd->taa_sample_len > 0) {
    return;
  }

  {
    DRW_PASS_CREATE(psl->aa_accum_pass, DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD_FULL);

    GPUShader *shader = workbench_shader_antialiasing_accumulation_get();
    grp = DRW_shgroup_create(shader, psl->aa_accum_pass);
    DRW_shgroup_uniform_texture(grp, "colorBuffer", dtxl->color);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }

  const float *size = DRW_viewport_size_get();
  const float *sizeinv = DRW_viewport_invert_size_get();
  float metrics[4] = {sizeinv[0], sizeinv[1], size[0], size[1]};

  {
    /* Stage 1: Edge detection. */
    DRW_PASS_CREATE(psl->aa_edge_pass, DRW_STATE_WRITE_COLOR);

    GPUShader *sh = workbench_shader_antialiasing_get(0);
    grp = DRW_shgroup_create(sh, psl->aa_edge_pass);
    DRW_shgroup_uniform_texture(grp, "colorTex", dtxl->color);
    DRW_shgroup_uniform_vec4_copy(grp, "viewportMetrics", metrics);

    DRW_shgroup_clear_framebuffer(grp, GPU_COLOR_BIT, 0, 0, 0, 0, 0.0f, 0x0);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
  {
    /* Stage 2: Blend Weight/Coord. */
    DRW_PASS_CREATE(psl->aa_weight_pass, DRW_STATE_WRITE_COLOR);

    GPUShader *sh = workbench_shader_antialiasing_get(1);
    grp = DRW_shgroup_create(sh, psl->aa_weight_pass);
    DRW_shgroup_uniform_texture(grp, "edgesTex", wpd->smaa_edge_tx);
    DRW_shgroup_uniform_texture(grp, "areaTex", txl->smaa_area_tx);
    DRW_shgroup_uniform_texture(grp, "searchTex", txl->smaa_search_tx);
    DRW_shgroup_uniform_vec4_copy(grp, "viewportMetrics", metrics);

    DRW_shgroup_clear_framebuffer(grp, GPU_COLOR_BIT, 0, 0, 0, 0, 0.0f, 0x0);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
  {
    /* Stage 3: Resolve. */
    DRW_PASS_CREATE(psl->aa_resolve_pass, DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_CUSTOM);

    GPUShader *sh = workbench_shader_antialiasing_get(2);
    grp = DRW_shgroup_create(sh, psl->aa_resolve_pass);
    DRW_shgroup_uniform_texture(grp, "blendTex", wpd->smaa_weight_tx);
    DRW_shgroup_uniform_texture(grp, "colorTex", txl->history_buffer_tx);
    DRW_shgroup_uniform_float(grp, "mixFactor", &wpd->smaa_mix_factor, 1);
    DRW_shgroup_uniform_float_copy(grp, "invTaaSampleCount", wpd->taa_sample_inv);
    DRW_shgroup_uniform_vec4_copy(grp, "viewportMetrics", metrics);

    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
}

void workbench_aa_draw_pass(WORKBENCH_Data *vedata)
{
  WORKBENCH_StorageList *stl = vedata->stl;
  WORKBENCH_PrivateData *wpd = stl->g_data;
  WORKBENCH_FramebufferList *fbl = vedata->fbl;
  WORKBENCH_PassList *psl = vedata->psl;

  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

  if (wpd->taa_sample_len == 0) {
    /* AA disabled. */
    return;
  }

  /* After a certain point SMAA is no longer necessary. */
  wpd->smaa_mix_factor = 1.0f - clamp_f(wpd->taa_sample / 4.0f, 0.0f, 1.0f);
  wpd->taa_sample_inv = 1.0f / wpd->taa_sample;

  /**
   * We always do SMAA on top of TAA accumulation, unless the number of samples of TAA is already
   * high. This ensure a smoother transition.
   * If TAA accumulation is finished, we only blit the result.
   */

  if (wpd->taa_sample == 1) {
    /* In playback mode, we are sure the next redraw will not use the same viewmatrix.
     * In this case no need to save the depth buffer. */
    eGPUFrameBufferBits bits = GPU_COLOR_BIT | (!wpd->is_playback ? GPU_DEPTH_BIT : 0);
    GPU_framebuffer_blit(dfbl->default_fb, 0, fbl->antialiasing_fb, 0, bits);
  }
  else if (wpd->taa_sample < wpd->taa_sample_len) {
    /* Accumulate result to the TAA buffer. */
    GPU_framebuffer_bind(fbl->antialiasing_fb);
    DRW_draw_pass(psl->aa_accum_pass);
  }

  if (wpd->taa_sample == wpd->taa_sample_len) {
    /* TAA accumulation has finish. Just copy the result back */
    eGPUFrameBufferBits bits = GPU_COLOR_BIT | GPU_DEPTH_BIT;
    GPU_framebuffer_blit(fbl->antialiasing_fb, 0, dfbl->default_fb, 0, bits);
    return;
  }
  else if (wpd->taa_sample > 1) {
    /* Copy back the saved depth buffer for correct overlays. */
    GPU_framebuffer_blit(fbl->antialiasing_fb, 0, dfbl->default_fb, 0, GPU_DEPTH_BIT);
  }

  if (wpd->smaa_mix_factor > 0.0f) {
    GPU_framebuffer_bind(fbl->smaa_edge_fb);
    DRW_draw_pass(psl->aa_edge_pass);

    GPU_framebuffer_bind(fbl->smaa_weight_fb);
    DRW_draw_pass(psl->aa_weight_pass);
  }

  GPU_framebuffer_bind(dfbl->default_fb);
  DRW_draw_pass(psl->aa_resolve_pass);

  if (!DRW_state_is_image_render() && wpd->taa_sample < wpd->taa_sample_len) {
    DRW_viewport_request_redraw();
  }
}
