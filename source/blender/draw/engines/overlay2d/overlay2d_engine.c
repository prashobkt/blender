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

#include "GPU_batch.h"
#include "GPU_framebuffer.h"

#include "DNA_space_types.h"

#include "BKE_object.h"

#include "ED_image.h"

#include "UI_interface.h"

#include "draw_cache_impl.h"

#include "overlay2d_engine.h"
#include "overlay2d_private.h"

/* Update the private data for SpaceImage. */
static OVERLAY2D_LineStyle overlay2d_line_style_from_space_image(SpaceImage *sima)
{
  const bool is_uv_editor = sima->mode == SI_MODE_UV;
  if (is_uv_editor) {
    switch (sima->dt_uv) {
      case SI_UVDT_OUTLINE:
        return OVERLAY2D_LINE_STYLE_OUTLINE;
      case SI_UVDT_BLACK:
        return OVERLAY2D_LINE_STYLE_BLACK;
      case SI_UVDT_WHITE:
        return OVERLAY2D_LINE_STYLE_WHITE;
      case SI_UVDT_DASH:
        return OVERLAY2D_LINE_STYLE_DASH;
      default:
        return OVERLAY2D_LINE_STYLE_BLACK;
    }
  }
  else {
    return OVERLAY2D_LINE_STYLE_SHADOW;
  }
}

static void OVERLAY2D_engine_init_space_image(OVERLAY2D_Data *vedata, SpaceImage *sima)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  OVERLAY2D_StorageList *stl = vedata->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;

  OVERLAY2D_shader_library_ensure();

  Image *image = sima->image;
  const bool is_image_type =
      image && ELEM(image->type, IMA_TYPE_IMAGE, IMA_TYPE_MULTILAYER, IMA_TYPE_UV_TEST);
  const bool is_uv_editor = sima->mode == SI_MODE_UV;
  const bool has_edit_object = (draw_ctx->object_edit) != NULL;
  const bool is_paint_mode = sima->mode == SI_MODE_PAINT;
  const bool is_view_mode = sima->mode == SI_MODE_VIEW;
  const bool is_edit_mode = draw_ctx->object_mode == OB_MODE_EDIT;
  const bool do_uv_overlay = is_image_type && is_uv_editor && has_edit_object;
  const bool show_modified_uvs = sima->flag & SI_DRAWSHADOW;
  const bool is_tiled_image = image && (image->source == IMA_SRC_TILED);

  pd->do_uv_overlay = do_uv_overlay;
  pd->do_uv_shadow_overlay = is_image_type &&
                             ((is_paint_mode && ((draw_ctx->object_mode &
                                                  (OB_MODE_TEXTURE_PAINT | OB_MODE_EDIT)) != 0)) ||
                              (is_view_mode &&
                               ((draw_ctx->object_mode & (OB_MODE_TEXTURE_PAINT)) != 0)) ||
                              (do_uv_overlay && (show_modified_uvs)));
  pd->do_uv_stretching_overlay = is_image_type && is_uv_editor && is_edit_mode &&
                                 ((sima->flag & SI_DRAW_STRETCH) != 0);
  pd->uv_opacity = sima->uv_opacity;
  pd->do_tiled_image_overlay = is_image_type && is_tiled_image;

  pd->wireframe.line_style = overlay2d_line_style_from_space_image(sima);
  pd->wireframe.do_smooth_wire = (sima->flag & SI_SMOOTH_UV) != 0;
  // // Dummy
  // pd->do_uv_overlay = false;
  OVERLAY2D_background_engine_init(vedata);
  OVERLAY2D_uv_engine_init(vedata);

  if (pd->do_uv_stretching_overlay) {
    OVERLAY2D_uv_stretching_engine_init(vedata);
  }
}

/* -------------------------------------------------------------------- */
/** \name Engine Callbacks
 * \{ */
static void OVERLAY2D_engine_init(void *vedata)
{
  OVERLAY2D_Data *od = (OVERLAY2D_Data *)vedata;
  OVERLAY2D_StorageList *stl = od->stl;

  if (!stl->pd) {
    /* Alloc transient pointers */
    stl->pd = MEM_callocN(sizeof(*stl->pd), __func__);
  }

  OVERLAY2D_PrivateData *pd = stl->pd;
  unit_m4(pd->unit_mat);
  pd->wireframe.line_style = OVERLAY2D_LINE_STYLE_OUTLINE;
  pd->wireframe.dash_length = 4.0f * UI_DPI_FAC;
  pd->wireframe.do_smooth_wire = true;

  const DRWContextState *drw_ctx = DRW_context_state_get();
  SpaceImage *sima = (SpaceImage *)drw_ctx->space_data;
  OVERLAY2D_engine_init_space_image(od, sima);
}

static void OVERLAY2D_cache_init(void *vedata)
{
  OVERLAY2D_Data *od = (OVERLAY2D_Data *)vedata;
  OVERLAY2D_StorageList *stl = od->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

  memset(od->psl, 0, sizeof(OVERLAY2D_PassList));

  GPU_framebuffer_bind(dfbl->overlay_only_fb);
  static float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  GPU_framebuffer_clear_color(dfbl->overlay_only_fb, clear_col);

  if (pd->do_uv_overlay) {
    OVERLAY2D_uv_cache_init(od);
  }
  if (pd->do_uv_shadow_overlay) {
    OVERLAY2D_uv_shadow_cache_init(od);
  }
  if (pd->do_uv_stretching_overlay) {
    OVERLAY2D_uv_stretching_cache_init(od);
  }
  if (pd->do_tiled_image_overlay) {
    OVERLAY2D_tiled_image_cache_init(od);
  }

  OVERLAY2D_background_cache_init(od);
}

static void OVERLAY2D_cache_populate(void *vedata, Object *ob)
{
  OVERLAY2D_Data *od = (OVERLAY2D_Data *)vedata;
  OVERLAY2D_StorageList *stl = od->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;
  const DRWContextState *draw_ctx = DRW_context_state_get();

  if (ob->type != OB_MESH) {
    return;
  }

  const bool is_edit_object = (ob == draw_ctx->object_edit) || BKE_object_is_in_editmode(ob);

  if (is_edit_object) {
    if (pd->do_uv_overlay) {
      OVERLAY2D_uv_cache_populate(od, ob);
    }
    if (pd->do_uv_stretching_overlay) {
      OVERLAY2D_uv_stretching_cache_populate(od, ob);
    }
  }
  if (pd->do_uv_shadow_overlay) {
    OVERLAY2D_uv_shadow_cache_populate(od, ob);
  }
}

static void OVERLAY2D_draw_scene(void *vedata)
{
  OVERLAY2D_Data *od = (OVERLAY2D_Data *)vedata;
  OVERLAY2D_StorageList *stl = od->stl;
  OVERLAY2D_PrivateData *pd = stl->pd;
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();

  GPU_framebuffer_bind(dfbl->overlay_fb);
  OVERLAY2D_background_draw_scene(od);

  /* Image Tiling */
  if (pd->do_tiled_image_overlay) {
    OVERLAY2D_tiled_image_draw_scene(od);
  }

  /* Draw faces */
  if (pd->do_uv_overlay) {
    OVERLAY2D_uv_draw_scene_faces(od);
  }
  if (pd->do_uv_stretching_overlay) {
    OVERLAY2D_uv_stretching_draw_scene_faces(od);
  }

  /* Draw_edges and verts */
  if (pd->do_uv_shadow_overlay) {
    OVERLAY2D_uv_shadow_draw_scene(od);
  }

  if (pd->do_uv_overlay) {
    OVERLAY2D_uv_draw_scene_edges_and_verts(od);
  }

  GPU_framebuffer_bind(dfbl->default_fb);
}

static void OVERLAY2D_engine_free(void)
{
  OVERLAY2D_shaders_free();
}

/* \} */
static const DrawEngineDataSize overlay2d_data_size = DRW_VIEWPORT_DATA_SIZE(OVERLAY2D_Data);

DrawEngineType draw_engine_overlay2d_type = {
    NULL,                      /* next */
    NULL,                      /* prev */
    N_("Overlay 2D"),          /* idname */
    &overlay2d_data_size,      /* vedata_size */
    &OVERLAY2D_engine_init,    /* engine_init */
    &OVERLAY2D_engine_free,    /* engine_free */
    &OVERLAY2D_cache_init,     /* cache_init */
    &OVERLAY2D_cache_populate, /* cache_populate */
    NULL,                      /* cache_finish */
    &OVERLAY2D_draw_scene,     /* draw_scene */
    NULL,                      /* view_update */
    NULL,                      /* id_update */
    NULL,                      /* render_to_image */
};