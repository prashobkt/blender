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

#include "draw_manager.h"

#include "DRW_render.h"

#include "GPU_batch.h"
#include "GPU_framebuffer.h"
#include "GPU_matrix.h"
#include "GPU_texture.h"

#include "BKE_colortools.h"

#include "IMB_colormanagement.h"

#include "draw_color_management.h"

/* -------------------------------------------------------------------- */
/** \name Color Management
 * \{ */

/* Use color management profile to draw texture to framebuffer */
void DRW_transform_to_display(GPUTexture *tex, bool use_view_transform, bool use_render_settings)
{
  drw_state_set(DRW_STATE_WRITE_COLOR);

  GPUBatch *geom = DRW_cache_fullscreen_quad_get();

  const float dither = 1.0f;

  bool use_ocio = false;

  GPU_matrix_identity_set();
  GPU_matrix_identity_projection_set();

  /* Should we apply the view transform */
  if (DRW_state_do_color_management()) {
    Scene *scene = DST.draw_ctx.scene;
    ColorManagedDisplaySettings *display_settings = &scene->display_settings;
    ColorManagedViewSettings view_settings;
    if (use_render_settings) {
      /* Use full render settings, for renders with scene lighting. */
      view_settings = scene->view_settings;
    }
    else if (use_view_transform) {
      /* Use only view transform + look and nothing else for lookdev without
       * scene lighting, as exposure depends on scene light intensity. */
      BKE_color_managed_view_settings_init_render(&view_settings, display_settings, NULL);
      STRNCPY(view_settings.view_transform, scene->view_settings.view_transform);
      STRNCPY(view_settings.look, scene->view_settings.look);
    }
    else {
      /* For workbench use only default view transform in configuration,
       * using no scene settings. */
      BKE_color_managed_view_settings_init_render(&view_settings, display_settings, NULL);
    }

    use_ocio = IMB_colormanagement_setup_glsl_draw_from_space(
        &view_settings, display_settings, NULL, dither, false);
  }

  if (use_ocio) {
    GPU_batch_program_set_imm_shader(geom);
    /* End IMM session. */
    IMB_colormanagement_finish_glsl_draw();
  }
  else {
    /* View transform is already applied for offscreen, don't apply again, see: T52046 */
    if (DST.options.is_image_render && !DST.options.is_scene_render) {
      GPU_batch_program_set_builtin(geom, GPU_SHADER_2D_IMAGE_COLOR);
      GPU_batch_uniform_4f(geom, "color", 1.0f, 1.0f, 1.0f, 1.0f);
    }
    else {
      GPU_batch_program_set_builtin(geom, GPU_SHADER_2D_IMAGE_LINEAR_TO_SRGB);
    }
    GPU_batch_uniform_1i(geom, "image", 0);
  }

  GPU_texture_bind(tex, 0); /* OCIO texture bind point is 0 */
  GPU_batch_draw(geom);
  GPU_texture_unbind(tex);
}

/* Draw texture to framebuffer without any color transforms */
void DRW_transform_none(GPUTexture *tex)
{
  drw_state_set(DRW_STATE_WRITE_COLOR);

  /* Draw as texture for final render (without immediate mode). */
  GPUBatch *geom = DRW_cache_fullscreen_quad_get();
  GPU_batch_program_set_builtin(geom, GPU_SHADER_2D_IMAGE_COLOR);

  GPU_texture_bind(tex, 0);

  const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  GPU_batch_uniform_4fv(geom, "color", white);

  float mat[4][4];
  unit_m4(mat);
  GPU_batch_uniform_mat4(geom, "ModelViewProjectionMatrix", mat);

  GPU_batch_program_use_begin(geom);
  GPU_batch_bind(geom);
  GPU_batch_draw_advanced(geom, 0, 0, 0, 0);
  GPU_batch_program_use_end(geom);

  GPU_texture_unbind(tex);
}

void DRW_transform_to_display_linear(void)
{
  /* TODO */
}

void DRW_transform_to_display_encoded(void)
{
  /* TODO */
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  DefaultTextureList *dtxl = DRW_viewport_texture_list_get();
  GPU_framebuffer_bind(dfbl->default_display_fb);
  DRW_transform_to_display(dtxl->color, true, true);
}

/** \} */
