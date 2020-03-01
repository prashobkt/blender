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

#include "GPU_extensions.h"

#include "workbench_engine.h"
#include "workbench_private.h"

void workbench_transparent_engine_init(WORKBENCH_Data *data)
{
  WORKBENCH_FramebufferList *fbl = data->fbl;
  WORKBENCH_PrivateData *wpd = data->stl->wpd;
  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();
  DrawEngineType *owner = (DrawEngineType *)&workbench_transparent_engine_init;

  /* Reuse same format as opaque pipeline to reuse the textures. */
  /* Note: Floating point texture is required for the reveal_tex as it is used for
   * the alpha accumulation component (see accumulation shader for more explanation). */
  const eGPUTextureFormat accum_tex_format = GPU_RGBA16F;
  const eGPUTextureFormat reveal_tex_format = NORMAL_ENCODING_ENABLED() ? GPU_RG16F : GPU_RGBA32F;

  wpd->accum_buffer_tx = DRW_texture_pool_query_fullscreen(accum_tex_format, owner);
  wpd->reveal_buffer_tx = DRW_texture_pool_query_fullscreen(reveal_tex_format, owner);

  GPU_framebuffer_ensure_config(&fbl->transp_accum_fb,
                                {
                                    GPU_ATTACHMENT_TEXTURE(dtxl->depth),
                                    GPU_ATTACHMENT_TEXTURE(wpd->accum_buffer_tx),
                                    GPU_ATTACHMENT_TEXTURE(wpd->reveal_buffer_tx),
                                });
}

static void workbench_transparent_lighting_uniforms(WORKBENCH_PrivateData *wpd,
                                                    DRWShadingGroup *grp)
{
  const bool use_spec = workbench_is_specular_highlight_enabled(wpd);
  DRW_shgroup_uniform_block_persistent(grp, "world_block", wpd->world_ubo);

  if (STUDIOLIGHT_TYPE_MATCAP_ENABLED(wpd)) {
    BKE_studiolight_ensure_flag(wpd->studio_light,
                                STUDIOLIGHT_MATCAP_DIFFUSE_GPUTEXTURE |
                                    STUDIOLIGHT_MATCAP_SPECULAR_GPUTEXTURE);
    struct GPUTexture *diff_tx = wpd->studio_light->matcap_diffuse.gputexture;
    struct GPUTexture *spec_tx = wpd->studio_light->matcap_specular.gputexture;
    spec_tx = use_spec ? spec_tx : diff_tx;
    DRW_shgroup_uniform_texture_persistent(grp, "matcapDiffuseImage", diff_tx);
    DRW_shgroup_uniform_texture_persistent(grp, "matcapSpecularImage", spec_tx);
    DRW_shgroup_uniform_bool_copy(grp, "useSpecularMatcap", use_spec);
  }
  else if (STUDIOLIGHT_TYPE_STUDIO_ENABLED(wpd)) {
    DRW_shgroup_uniform_bool_copy(grp, "useSpecularLighting", use_spec);
  }
}

void workbench_transparent_cache_init(WORKBENCH_Data *data)
{
  WORKBENCH_PassList *psl = data->psl;
  WORKBENCH_PrivateData *wpd = data->stl->wpd;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  RegionView3D *rv3d = draw_ctx->rv3d;
  View3D *v3d = draw_ctx->v3d;
  struct GPUShader *sh;
  DRWShadingGroup *grp;

  {
    DRWState clip_state = RV3D_CLIPPING_ENABLED(v3d, rv3d) ? DRW_STATE_CLIP_PLANES : 0;
    DRWState cull_state = CULL_BACKFACE_ENABLED(wpd) ? DRW_STATE_CULL_BACK : 0;
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_BLEND_OIT;

    DRW_PASS_CREATE(psl->transp_accum_pass, state | cull_state | clip_state);

    int transp = 1;

    sh = workbench_shader_transparent_get(wpd);

    wpd->prepass[transp].common_shgrp = grp = DRW_shgroup_create(sh, psl->transp_accum_pass);
    DRW_shgroup_uniform_block(grp, "material_block", wpd->material_ubo_curr);
    DRW_shgroup_uniform_int_copy(grp, "materialIndex", -1);
    workbench_transparent_lighting_uniforms(wpd, grp);

    wpd->prepass[transp].vcol_shgrp = grp = DRW_shgroup_create(sh, psl->transp_accum_pass);
    DRW_shgroup_uniform_block_persistent(grp, "material_block", wpd->material_ubo_curr);
    DRW_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. (uses vcol) */

    sh = workbench_shader_transparent_image_get(wpd, false);

    wpd->prepass[transp].image_shgrp = grp = DRW_shgroup_create(sh, psl->transp_accum_pass);
    DRW_shgroup_uniform_block_persistent(grp, "material_block", wpd->material_ubo_curr);
    DRW_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. */
    workbench_transparent_lighting_uniforms(wpd, grp);

    sh = workbench_shader_transparent_image_get(wpd, true);

    wpd->prepass[transp].image_tiled_shgrp = grp = DRW_shgroup_create(sh, psl->transp_accum_pass);
    DRW_shgroup_uniform_block_persistent(grp, "material_block", wpd->material_ubo_curr);
    DRW_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. */
    workbench_transparent_lighting_uniforms(wpd, grp);
  }
  {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA;

    DRW_PASS_CREATE(psl->transp_resolve_pass, state);

    sh = workbench_shader_transparent_resolve_get(wpd);

    grp = DRW_shgroup_create(sh, psl->transp_resolve_pass);
    DRW_shgroup_uniform_texture(grp, "transparentAccum", wpd->accum_buffer_tx);
    DRW_shgroup_uniform_texture(grp, "transparentRevealage", wpd->reveal_buffer_tx);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
}
