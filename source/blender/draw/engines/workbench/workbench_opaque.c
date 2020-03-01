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

void workbench_opaque_engine_init(WORKBENCH_Data *data)
{
  WORKBENCH_FramebufferList *fbl = data->fbl;
  WORKBENCH_PrivateData *wpd = data->stl->wpd;
  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();
  DrawEngineType *owner = (DrawEngineType *)&workbench_opaque_engine_init;

  /* Reused the same textures format for transparent pipeline to share the textures. */
  const eGPUTextureFormat col_tex_format = GPU_RGBA16F;
  const eGPUTextureFormat nor_tex_format = NORMAL_ENCODING_ENABLED() ? GPU_RG16F : GPU_RGBA16F;
  const eGPUTextureFormat id_tex_format = GPU_R16UI;

  wpd->material_buffer_tx = DRW_texture_pool_query_fullscreen(col_tex_format, owner);
  wpd->normal_buffer_tx = DRW_texture_pool_query_fullscreen(nor_tex_format, owner);
  if (OBJECT_ID_PASS_ENABLED(wpd)) {
    wpd->object_id_tx = DRW_texture_pool_query_fullscreen(id_tex_format, owner);
  }

  GPU_framebuffer_ensure_config(&fbl->prepass_fb,
                                {
                                    GPU_ATTACHMENT_TEXTURE(dtxl->depth),
                                    GPU_ATTACHMENT_TEXTURE(wpd->material_buffer_tx),
                                    GPU_ATTACHMENT_TEXTURE(wpd->normal_buffer_tx),
                                    GPU_ATTACHMENT_TEXTURE(wpd->object_id_tx),
                                });
}

void workbench_opaque_cache_init(WORKBENCH_Data *data)
{
  WORKBENCH_PassList *psl = data->psl;
  WORKBENCH_PrivateData *wpd = data->stl->wpd;
  const DRWContextState *draw_ctx = DRW_context_state_get();
  RegionView3D *rv3d = draw_ctx->rv3d;
  View3D *v3d = draw_ctx->v3d;
  struct GPUShader *sh;
  DRWShadingGroup *grp;

  const bool use_matcap = (wpd->shading.light == V3D_LIGHTING_MATCAP);

  {
    DRWState clip_state = RV3D_CLIPPING_ENABLED(v3d, rv3d) ? DRW_STATE_CLIP_PLANES : 0;
    DRWState cull_state = CULL_BACKFACE_ENABLED(wpd) ? DRW_STATE_CULL_BACK : 0;
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;

    DRW_PASS_CREATE(psl->prepass_pass, state | cull_state | clip_state);

    int opaque = 0;

    sh = workbench_shader_opaque_get(wpd);

    wpd->prepass[opaque].common_shgrp = grp = DRW_shgroup_create(sh, psl->prepass_pass);
    DRW_shgroup_uniform_block(grp, "material_block", wpd->material_ubo_curr);
    DRW_shgroup_uniform_int_copy(grp, "materialIndex", -1);

    wpd->prepass[opaque].vcol_shgrp = grp = DRW_shgroup_create(sh, psl->prepass_pass);
    DRW_shgroup_uniform_block_persistent(grp, "material_block", wpd->material_ubo_curr);
    DRW_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. (uses vcol) */

    sh = workbench_shader_opaque_image_get(wpd, false);

    wpd->prepass[opaque].image_shgrp = grp = DRW_shgroup_create(sh, psl->prepass_pass);
    DRW_shgroup_uniform_block_persistent(grp, "material_block", wpd->material_ubo_curr);
    DRW_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. */
    DRW_shgroup_uniform_bool_copy(grp, "useMatcap", use_matcap);

    sh = workbench_shader_opaque_image_get(wpd, true);

    wpd->prepass[opaque].image_tiled_shgrp = grp = DRW_shgroup_create(sh, psl->prepass_pass);
    DRW_shgroup_uniform_block_persistent(grp, "material_block", wpd->material_ubo_curr);
    DRW_shgroup_uniform_int_copy(grp, "materialIndex", 0); /* Default material. */
    DRW_shgroup_uniform_bool_copy(grp, "useMatcap", use_matcap);

    // DRW_PASS_CREATE(psl->ghost_prepass_pass, state | cull_state | clip_state);
    // grp = DRW_shgroup_create(sh, psl->ghost_prepass_pass);

    // sh = workbench_shader_opaque_hair_get(wpd);

    // DRW_PASS_CREATE(psl->prepass_hair_pass, state | clip_state);
    // DRW_PASS_CREATE(psl->ghost_prepass_hair_pass, state | clip_state);

    // grp = DRW_shgroup_create(sh, psl->ghost_prepass_hair_pass);
  }
  {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_GREATER;

    DRW_PASS_CREATE(psl->composite_pass, state);

    sh = workbench_shader_composite_get(wpd);

    grp = DRW_shgroup_create(sh, psl->composite_pass);
    DRW_shgroup_stencil_mask(grp, 0x00);
    DRW_shgroup_uniform_block(grp, "world_block", wpd->world_ubo);
    DRW_shgroup_uniform_texture(grp, "materialBuffer", wpd->material_buffer_tx);
    DRW_shgroup_uniform_texture(grp, "normalBuffer", wpd->normal_buffer_tx);

    const bool use_spec = workbench_is_specular_highlight_enabled(wpd);

    if (STUDIOLIGHT_TYPE_MATCAP_ENABLED(wpd)) {
      BKE_studiolight_ensure_flag(wpd->studio_light,
                                  STUDIOLIGHT_MATCAP_DIFFUSE_GPUTEXTURE |
                                      STUDIOLIGHT_MATCAP_SPECULAR_GPUTEXTURE);
      struct GPUTexture *diff_tx = wpd->studio_light->matcap_diffuse.gputexture;
      struct GPUTexture *spec_tx = wpd->studio_light->matcap_specular.gputexture;
      DRW_shgroup_uniform_texture(grp, "matcapDiffuseImage", diff_tx);
      DRW_shgroup_uniform_texture(grp, "matcapSpecularImage", use_spec ? spec_tx : diff_tx);
      DRW_shgroup_uniform_bool_copy(grp, "useSpecularMatcap", use_spec);
    }
    else if (STUDIOLIGHT_TYPE_STUDIO_ENABLED(wpd)) {
      DRW_shgroup_uniform_bool_copy(grp, "useSpecularLighting", use_spec);
    }
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
  }
}
