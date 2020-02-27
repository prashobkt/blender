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

#include "workbench_engine.h"
#include "workbench_private.h"

#if 0
{

  {
    /* AO Samples Tex */
    int num_iterations = workbench_taa_calculate_num_iterations(vedata);

    const int ssao_samples_single_iteration = scene->display.matcap_ssao_samples;
    const int ssao_samples = MIN2(num_iterations * ssao_samples_single_iteration, 500);

    if (e_data.sampling_ubo && (e_data.cached_sample_num != ssao_samples)) {
      DRW_UBO_FREE_SAFE(e_data.sampling_ubo);
      DRW_TEXTURE_FREE_SAFE(e_data.jitter_tx);
    }

    if (e_data.sampling_ubo == NULL) {
      float *samples = create_disk_samples(ssao_samples_single_iteration, num_iterations);
      e_data.jitter_tx = create_jitter_texture(ssao_samples);
      e_data.sampling_ubo = DRW_uniformbuffer_create(sizeof(float[4]) * ssao_samples, samples);
      e_data.cached_sample_num = ssao_samples;
      MEM_freeN(samples);
    }
  }
}

  if (CAVITY_ENABLED(wpd)) {
    int state = DRW_STATE_WRITE_COLOR;
    GPUShader *shader = workbench_cavity_shader_get(SSAO_ENABLED(wpd), CURVATURE_ENABLED(wpd));
    psl->cavity_pass = DRW_pass_create("Cavity", state);
    DRWShadingGroup *grp = DRW_shgroup_create(shader, psl->cavity_pass);
    DRW_shgroup_uniform_texture_ref(grp, "normalBuffer", &e_data.normal_buffer_tx);
    DRW_shgroup_uniform_block(grp, "samples_block", e_data.sampling_ubo);

    if (SSAO_ENABLED(wpd)) {
      DRW_shgroup_uniform_texture_ref(grp, "depthBuffer", &dtxl->depth);
      DRW_shgroup_uniform_vec2(grp, "invertedViewportSize", DRW_viewport_invert_size_get(), 1);
      DRW_shgroup_uniform_vec4(grp, "viewvecs[0]", (float *)wpd->viewvecs, 3);
      DRW_shgroup_uniform_vec4(grp, "ssao_params", wpd->ssao_params, 1);
      DRW_shgroup_uniform_vec4(grp, "ssao_settings", wpd->ssao_settings, 1);
      DRW_shgroup_uniform_mat4(grp, "WinMatrix", wpd->winmat);
      DRW_shgroup_uniform_texture(grp, "ssao_jitter", e_data.jitter_tx);
    }

    if (CURVATURE_ENABLED(wpd)) {
      DRW_shgroup_uniform_texture_ref(grp, "objectId", &e_data.object_id_tx);
      DRW_shgroup_uniform_vec2(grp, "curvature_settings", &wpd->world_data.curvature_ridge, 1);
    }

    DRW_shgroup_call(grp, DRW_cache_fullscreen_quad_get(), NULL);
  }
#endif