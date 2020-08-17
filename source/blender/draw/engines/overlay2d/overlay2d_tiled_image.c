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

#include "draw_cache_impl.h"
#include "draw_manager_text.h"

#include "GPU_batch.h"

#include "DNA_mesh_types.h"
#include "DNA_space_types.h"

#include "ED_image.h"

#include "UI_resources.h"

#include "BLI_listbase.h"
#include "BLI_math_color.h"

#include "BKE_image.h"

#include "overlay2d_engine.h"
#include "overlay2d_private.h"

static struct {
  GPUBatch *gpu_batch_instances;
} e_data = {0}; /* Engine data */

void OVERLAY2D_tiled_image_cache_init(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_PassList *psl = vedata->psl;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)draw_ctx->space_data;
  Image *image = sima->image;

  /* Image tiling borders */
  {
    DRW_PASS_CREATE(psl->tiled_image_borders, DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_ALWAYS);
    GPUShader *sh = OVERLAY2D_shaders_tiled_image_border_get();

    float theme_color[4], selected_color[4];
    UI_GetThemeColorShade4fv(TH_BACK, 60, theme_color);
    UI_GetThemeColor4fv(TH_FACE_SELECT, selected_color);
    srgb_to_linearrgb_v4(theme_color, theme_color);
    srgb_to_linearrgb_v4(selected_color, selected_color);

    DRWShadingGroup *grp = DRW_shgroup_create(sh, psl->tiled_image_borders);
    DRW_shgroup_uniform_vec4_copy(grp, "color", theme_color);
    DRW_shgroup_uniform_vec3_copy(grp, "offset", (float[3]){0.0f, 0.0f, 0.0f});

    GPU_BATCH_DISCARD_SAFE(e_data.gpu_batch_instances);
    e_data.gpu_batch_instances = BKE_image_tiled_gpu_instance_batch_create(image);
    DRW_shgroup_call_instances_with_attrs(
        grp, NULL, DRW_cache_quad_image_wires_get(), e_data.gpu_batch_instances);

    /* Active tile border */
    ImageTile *tile = BLI_findlink(&image->tiles, image->active_tile_index);
    float offset[3] = {((tile->tile_number - 1001) % 10), ((tile->tile_number - 1001) / 10), 0.0f};
    grp = DRW_shgroup_create(sh, psl->tiled_image_borders);
    DRW_shgroup_uniform_vec4_copy(grp, "color", selected_color);
    DRW_shgroup_uniform_vec3_copy(grp, "offset", offset);
    DRW_shgroup_call(grp, DRW_cache_quad_image_wires_get(), NULL);
  }

  {
    struct DRWTextStore *dt = DRW_text_cache_ensure();
    uchar color[4];
    /* Color Management: Exception here as texts are drawn in sRGB space directly.  */
    UI_GetThemeColorShade4ubv(TH_BACK, 60, color);
    char text[16];
    LISTBASE_FOREACH (ImageTile *, tile, &image->tiles) {
      BLI_snprintf(text, 5, "%d", tile->tile_number);
      float tile_location[3] = {
          ((tile->tile_number - 1001) % 10), ((tile->tile_number - 1001) / 10), 0.0f};
      DRW_text_cache_add(dt,
                         tile_location,
                         text,
                         strlen(text),
                         10,
                         10,
                         DRW_TEXT_CACHE_GLOBALSPACE | DRW_TEXT_CACHE_ASCII,
                         color);
    }
  }
}

static void overlay2d_tiled_image_draw_finish(OVERLAY2D_Data *UNUSED(vedata))
{
  GPU_BATCH_DISCARD_SAFE(e_data.gpu_batch_instances);
}

void OVERLAY2D_tiled_image_draw_scene(OVERLAY2D_Data *vedata)
{
  OVERLAY2D_PassList *psl = vedata->psl;
  DRW_draw_pass(psl->tiled_image_borders);

  overlay2d_tiled_image_draw_finish(vedata);
}
