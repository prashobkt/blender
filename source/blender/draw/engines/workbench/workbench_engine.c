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
 * Copyright 2016, Blender Foundation.
 */

/** \file
 * \ingroup draw_engine
 *
 * Optimized engine to draw the working viewport with solid and transparent geometry.
 */

#include "DRW_render.h"

#include "BLI_alloca.h"

#include "BKE_modifier.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_particle.h"

#include "DNA_image_types.h"
#include "DNA_fluid_types.h"
#include "DNA_mesh_types.h"
#include "DNA_modifier_types.h"
#include "DNA_node_types.h"

#include "workbench_engine.h"
#include "workbench_private.h"

#define WORKBENCH_ENGINE "BLENDER_WORKBENCH"

static void workbench_engine_init(void *ved)
{
  WORKBENCH_Data *vedata = ved;
  WORKBENCH_StorageList *stl = vedata->stl;
  WORKBENCH_TextureList *txl = vedata->txl;

  workbench_shader_library_ensure();

  if (!stl->wpd) {
    stl->wpd = MEM_callocN(sizeof(*stl->wpd), __func__);
  }

  if (!stl->effects) {
    stl->effects = MEM_callocN(sizeof(*stl->effects), __func__);
    workbench_effect_info_init(stl->effects);
  }

  WORKBENCH_PrivateData *wpd = stl->wpd;
  workbench_private_data_init(wpd);

  if (txl->dummy_image_tx == NULL) {
    float fpixel[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    txl->dummy_image_tx = DRW_texture_create_2d(1, 1, GPU_RGBA8, 0, fpixel);
  }
  wpd->dummy_image_tx = txl->dummy_image_tx;

  workbench_opaque_engine_init(vedata);
  workbench_transparent_engine_init(vedata);
  //   workbench_volume_engine_init();
  //   workbench_fxaa_engine_init();
  //   workbench_taa_engine_init(vedata);
  //   workbench_dof_engine_init(vedata, camera);
}

static void workbench_cache_init(void *ved)
{
  WORKBENCH_Data *vedata = ved;

  workbench_opaque_cache_init(vedata);
  workbench_transparent_cache_init(vedata);

  //   workbench_aa_create_pass(vedata);
  //   workbench_dof_create_pass(vedata);
}

/* TODO(fclem) DRW_cache_object_surface_material_get needs a refactor to allow passing NULL
 * instead of gpumat_array. Avoiding all this boilerplate code. */
static struct GPUBatch **workbench_object_surface_material_get(Object *ob)
{
  const int materials_len = DRW_cache_object_material_count_get(ob);
  struct GPUMaterial **gpumat_array = BLI_array_alloca(gpumat_array, materials_len);
  memset(gpumat_array, 0, sizeof(*gpumat_array) * materials_len);

  return DRW_cache_object_surface_material_get(ob, gpumat_array, materials_len);
}

static void workbench_cache_sculpt_populate(WORKBENCH_PrivateData *wpd, Object *ob, int color_type)
{
  const bool use_vcol = ELEM(color_type, V3D_SHADING_VERTEX_COLOR);
  const bool use_single_drawcall = !ELEM(color_type, V3D_SHADING_MATERIAL_COLOR);
  BLI_assert(wpd->shading.color_type != V3D_SHADING_TEXTURE_COLOR);

  if (use_single_drawcall) {
    DRWShadingGroup *grp = workbench_material_setup(wpd, ob, 0, color_type);
    DRW_shgroup_call_sculpt(grp, ob, false, false, use_vcol);
  }
  else {
    const int materials_len = DRW_cache_object_material_count_get(ob);
    struct DRWShadingGroup **shgrps = BLI_array_alloca(shgrps, materials_len);
    for (int i = 0; i < materials_len; i++) {
      shgrps[i] = workbench_material_setup(wpd, ob, i + 1, color_type);
    }
    DRW_shgroup_call_sculpt_with_materials(shgrps, ob, false);
  }
}

static void workbench_cache_texpaint_populate(WORKBENCH_PrivateData *wpd, Object *ob)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  const Scene *scene = draw_ctx->scene;
  const ImagePaintSettings *imapaint = &scene->toolsettings->imapaint;
  const bool use_single_drawcall = imapaint->mode == IMAGEPAINT_MODE_IMAGE;

  if (use_single_drawcall) {
    struct GPUBatch *geom = DRW_cache_mesh_surface_texpaint_single_get(ob);
    if (geom) {
      Image *ima = imapaint->canvas;
      int interp = (imapaint->interp == IMAGEPAINT_INTERP_LINEAR) ? SHD_INTERP_LINEAR :
                                                                    SHD_INTERP_CLOSEST;

      DRWShadingGroup *grp = workbench_image_setup(wpd, ob, 0, ima, NULL, interp);
      DRW_shgroup_call(grp, geom, ob);
    }
  }
  else {
    struct GPUBatch **geoms = DRW_cache_mesh_surface_texpaint_get(ob);
    if (geoms) {
      const int materials_len = DRW_cache_object_material_count_get(ob);
      for (int i = 0; i < materials_len; i++) {
        DRWShadingGroup *grp = workbench_image_setup(wpd, ob, i + 1, NULL, NULL, 0);
        DRW_shgroup_call(grp, geoms[i], ob);
      }
    }
  }
}

static void workbench_cache_common_populate(WORKBENCH_PrivateData *wpd, Object *ob, int color_type)
{
  const bool use_tex = ELEM(color_type, V3D_SHADING_TEXTURE_COLOR);
  const bool use_vcol = ELEM(color_type, V3D_SHADING_VERTEX_COLOR);
  const bool use_single_drawcall = !ELEM(
      color_type, V3D_SHADING_MATERIAL_COLOR, V3D_SHADING_TEXTURE_COLOR);

  if (use_single_drawcall) {
    struct GPUBatch *geom = (use_vcol) ? DRW_cache_mesh_surface_vertpaint_get(ob) :
                                         DRW_cache_object_surface_get(ob);
    if (geom) {
      DRWShadingGroup *grp = workbench_material_setup(wpd, ob, 0, color_type);
      DRW_shgroup_call(grp, geom, ob);
    }
  }
  else {
    struct GPUBatch **geoms = (use_tex) ? DRW_cache_mesh_surface_texpaint_get(ob) :
                                          workbench_object_surface_material_get(ob);
    if (geoms) {
      const int materials_len = DRW_cache_object_material_count_get(ob);
      for (int i = 0; i < materials_len; i++) {
        DRWShadingGroup *grp = workbench_material_setup(wpd, ob, i + 1, color_type);
        DRW_shgroup_call(grp, geoms[i], ob);
      }
    }
  }
}

/* Decide what colortype to draw the object with.
 * In some cases it can be overwritten by workbench_material_setup(). */
static int workbench_color_type_get(WORKBENCH_PrivateData *wpd,
                                    Object *ob,
                                    bool *r_sculpt_pbvh,
                                    bool *r_texpaint_mode)
{
  int color_type = wpd->shading.color_type;
  const Mesh *me = (ob->type == OB_MESH) ? ob->data : NULL;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  const bool is_active = (ob == draw_ctx->obact);
  const bool is_sculpt_pbvh = BKE_sculptsession_use_pbvh_draw(ob, draw_ctx->v3d) &&
                              !DRW_state_is_image_render();
  const bool is_render = DRW_state_is_image_render() && (draw_ctx->v3d == NULL);
  const bool is_texpaint_mode = is_active && (wpd->ctx_mode == CTX_MODE_PAINT_TEXTURE);
  const bool is_vertpaint_mode = is_active && (wpd->ctx_mode == CTX_MODE_PAINT_VERTEX);

  if ((color_type == V3D_SHADING_TEXTURE_COLOR) && (ob->dt < OB_TEXTURE)) {
    color_type = V3D_SHADING_MATERIAL_COLOR;
  }
  /* Disable color mode if data layer is unavailable. */
  if ((color_type == V3D_SHADING_TEXTURE_COLOR) && (me == NULL || me->mloopuv == NULL)) {
    color_type = V3D_SHADING_MATERIAL_COLOR;
  }
  if ((color_type == V3D_SHADING_VERTEX_COLOR) && (me == NULL || me->mloopcol == NULL)) {
    color_type = V3D_SHADING_OBJECT_COLOR;
  }

  *r_sculpt_pbvh = is_sculpt_pbvh;
  *r_texpaint_mode = false;

  if (!is_sculpt_pbvh && !is_render) {
    /* Force texture or vertex mode if object is in paint mode. */
    if (is_texpaint_mode && me && me->mloopuv) {
      color_type = V3D_SHADING_TEXTURE_COLOR;
      *r_texpaint_mode = true;
    }
    else if (is_vertpaint_mode && me && me->mloopcol) {
      color_type = V3D_SHADING_VERTEX_COLOR;
    }
  }

  return color_type;
}

static void workbench_cache_populate(void *ved, Object *ob)
{
  WORKBENCH_Data *vedata = ved;
  WORKBENCH_StorageList *stl = vedata->stl;
  WORKBENCH_PrivateData *wpd = stl->wpd;

  if (!DRW_object_is_renderable(ob)) {
    return;
  }

  //   if (ob->type == OB_MESH) {
  //     workbench_cache_populate_particles(vedata, ob);
  //   }

  /* TODO volume */

  //   if (!(DRW_object_visibility_in_active_context(ob) & OB_VISIBLE_SELF)) {
  //     return;
  //   }

  if ((ob->dt < OB_SOLID) && !DRW_state_is_scene_render()) {
    return;
  }

  if (ELEM(ob->type, OB_MESH, OB_CURVE, OB_SURF, OB_FONT, OB_MBALL)) {
    bool use_sculpt_pbvh, use_texpaint_mode;
    int color_type = workbench_color_type_get(wpd, ob, &use_sculpt_pbvh, &use_texpaint_mode);

    if (use_sculpt_pbvh) {
      workbench_cache_sculpt_populate(wpd, ob, color_type);
    }
    else if (use_texpaint_mode) {
      workbench_cache_texpaint_populate(wpd, ob);
    }
    else {
      workbench_cache_common_populate(wpd, ob, color_type);
    }
  }
}

static void workbench_cache_finish(void *ved)
{
  WORKBENCH_Data *vedata = ved;
  WORKBENCH_StorageList *stl = vedata->stl;
  WORKBENCH_PrivateData *wpd = stl->wpd;

  workbench_update_material_ubos(wpd);

  if (wpd->material_hash) {
    BLI_ghash_free(wpd->material_hash, NULL, NULL);
    wpd->material_hash = NULL;
  }
}

static void workbench_draw_scene(void *ved)
{
  WORKBENCH_Data *vedata = ved;
  WORKBENCH_FramebufferList *fbl = vedata->fbl;
  WORKBENCH_PassList *psl = vedata->psl;
  DefaultFramebufferList *dfbl = DRW_viewport_framebuffer_list_get();
  float clear_col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float clear_col_with_alpha[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  GPU_framebuffer_bind(dfbl->color_only_fb);
  GPU_framebuffer_clear_color(dfbl->color_only_fb, clear_col);

  {
    GPU_framebuffer_bind(fbl->prepass_fb);
    DRW_draw_pass(psl->prepass_pass);

    /* TODO(fclem) shadows */
    // DRW_draw_pass(psl->shadow_pass);

    {
      /* TODO(fclem) infront */
      // GPU_framebuffer_bind(fbl->prepass_infront_fb);
      // DRW_draw_pass(psl->prepass_infront_pass);

      /* TODO(fclem) merge infront depth & stencil. */
      // GPU_framebuffer_bind(fbl->prepass_fb);
      // DRW_draw_pass(psl->merge_infront_pass);
    }

    GPU_framebuffer_bind(dfbl->default_fb);
    DRW_draw_pass(psl->composite_pass);

    /* TODO(fclem) shadows : render shadowed areas */
    // DRW_draw_pass(psl->composite_shadow_pass);

    /* TODO(fclem) ambient occlusion */
    // GPU_framebuffer_bind(dfbl->color_only_fb);
    // DRW_draw_pass(psl->ambient_occlusion_pass);
  }

  {
    GPU_framebuffer_bind(fbl->transp_accum_fb);
    GPU_framebuffer_clear_color(fbl->transp_accum_fb, clear_col_with_alpha);

    DRW_draw_pass(psl->transp_accum_pass);

    {
      /* TODO(fclem) infront */
      // GPU_framebuffer_bind(fbl->tranp_accum_infront_fb);
      // DRW_draw_pass(psl->transp_accum_infront_pass);
    }

    GPU_framebuffer_bind(dfbl->color_only_fb);
    DRW_draw_pass(psl->transp_resolve_pass);
  }

  /* TODO(fclem) outline */
  // DRW_draw_pass(psl->outline_pass);

  /* TODO(fclem) dof */

  /* TODO(fclem) antialias */
}

static void workbench_engine_free(void)
{
  workbench_shader_free();
}

static void workbench_view_update(void *UNUSED(ved))
{
}

static const DrawEngineDataSize workbench_data_size = DRW_VIEWPORT_DATA_SIZE(WORKBENCH_Data);

DrawEngineType draw_engine_workbench = {
    NULL,
    NULL,
    N_("Workbench"),
    &workbench_data_size,
    &workbench_engine_init,
    &workbench_engine_free,
    &workbench_cache_init,
    &workbench_cache_populate,
    &workbench_cache_finish,
    &workbench_draw_scene,
    &workbench_view_update,
    NULL,
    NULL,
};

RenderEngineType DRW_engine_viewport_workbench_type = {
    NULL,
    NULL,
    WORKBENCH_ENGINE,
    N_("Workbench"),
    RE_INTERNAL,
    NULL,
    &DRW_render_to_image,
    NULL,
    NULL,
    NULL,
    NULL,
    &workbench_render_update_passes,
    &draw_engine_workbench,
    {NULL, NULL, NULL},
};

#undef WORKBENCH_ENGINE
