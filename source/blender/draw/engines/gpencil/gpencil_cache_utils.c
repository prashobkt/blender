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
 * Copyright 2017, Blender Foundation.
 */

/** \file
 * \ingroup draw
 */

#include "DRW_engine.h"
#include "DRW_render.h"

#include "ED_gpencil.h"
#include "ED_view3d.h"

#include "DNA_gpencil_types.h"
#include "DNA_view3d_types.h"

#include "BKE_library.h"
#include "BKE_gpencil.h"
#include "BKE_object.h"

#include "BLI_memblock.h"
#include "BLI_link_utils.h"

#include "gpencil_engine.h"

#include "draw_cache_impl.h"

#include "DEG_depsgraph.h"

GPENCIL_tObject *gpencil_object_cache_add(GPENCIL_PrivateData *pd, Object *ob)
{
  bGPdata *gpd = (bGPdata *)ob->data;
  GPENCIL_tObject *tgp_ob = BLI_memblock_alloc(pd->gp_object_pool);

  tgp_ob->layers.first = tgp_ob->layers.last = NULL;
  tgp_ob->vfx.first = tgp_ob->vfx.last = NULL;
  tgp_ob->camera_z = dot_v3v3(pd->camera_z_axis, ob->obmat[3]);
  tgp_ob->is_drawmode3d = (gpd->draw_mode == GP_DRAWMODE_3D);

  /* Find the normal most likely to represent the gpObject. */
  /* TODO: This does not work quite well if you use
   * strokes not aligned with the object axes. Maybe we could try to
   * compute the minimum axis of all strokes. But this would be more
   * computationaly heavy and should go into the GPData evaluation. */
  BoundBox *bbox = BKE_object_boundbox_get(ob);
  /* Convert bbox to matrix */
  float mat[4][4], size[3], center[3];
  BKE_boundbox_calc_size_aabb(bbox, size);
  BKE_boundbox_calc_center_aabb(bbox, center);
  unit_m4(mat);
  copy_v3_v3(mat[3], center);
  /* Avoid division by 0.0 later. */
  add_v3_fl(size, 1e-8f);
  rescale_m4(mat, size);
  /* BBox space to World. */
  mul_m4_m4m4(mat, ob->obmat, mat);
  if (DRW_view_is_persp_get(NULL)) {
    /* BBox center to camera vector. */
    sub_v3_v3v3(tgp_ob->plane_normal, pd->camera_pos, mat[3]);
  }
  else {
    copy_v3_v3(tgp_ob->plane_normal, pd->camera_z_axis);
  }
  /* World to BBox space. */
  invert_m4(mat);
  /* Normalize the vector in BBox space. */
  mul_mat3_m4_v3(mat, tgp_ob->plane_normal);
  normalize_v3(tgp_ob->plane_normal);

  transpose_m4(mat);
  /* mat is now a "normal" matrix which will transform
   * BBox space normal to world space.  */
  mul_mat3_m4_v3(mat, tgp_ob->plane_normal);
  normalize_v3(tgp_ob->plane_normal);

  /* Define a matrix that will be used to render a triangle to merge the depth of the rendered
   * gpencil object with the rest of the scene. */
  unit_m4(tgp_ob->plane_mat);
  copy_v3_v3(tgp_ob->plane_mat[2], tgp_ob->plane_normal);
  orthogonalize_m4(tgp_ob->plane_mat, 2);
  mul_mat3_m4_v3(ob->obmat, size);
  float radius = len_v3(size);
  mul_m4_v3(ob->obmat, center);
  rescale_m4(tgp_ob->plane_mat, (float[3]){radius, radius, radius});
  copy_v3_v3(tgp_ob->plane_mat[3], center);

  /* Add to corresponding list if is in front. */
  if (ob->dtx & OB_DRAWXRAY) {
    BLI_LINKS_APPEND(&pd->tobjects_infront, tgp_ob);
  }
  else {
    BLI_LINKS_APPEND(&pd->tobjects, tgp_ob);
  }

  return tgp_ob;
}

GPENCIL_tLayer *gpencil_layer_cache_add(GPENCIL_PrivateData *pd, Object *ob, bGPDlayer *gpl)
{
  const bool is_obact = ((pd->obact) && (pd->obact == ob));
  const bool is_fade = ((pd->fade_layer_opacity > -1.0f) && (is_obact) &&
                        ((gpl->flag & GP_LAYER_ACTIVE) == 0));
  bool mask_invert = true; /* True because we invert the dummy texture red channel. */

  /* Defines layer opacity. For active object depends of layer opacity factor, and
   * for no active object, depends if the fade grease pencil objects option is enabled. */
  float fade_layer_opacity = gpl->opacity;
  if (!pd->is_render) {
    if ((is_obact) && (is_fade)) {
      fade_layer_opacity = pd->fade_layer_opacity;
    }
    else if ((!is_obact) && (pd->fade_gp_object_opacity > -1.0f)) {
      fade_layer_opacity = pd->fade_gp_object_opacity;
    }
  }

  bGPdata *gpd = (bGPdata *)ob->data;
  GPENCIL_tLayer *tgp_layer = BLI_memblock_alloc(pd->gp_layer_pool);
  tgp_layer->layer_id = BLI_findindex(&gpd->layers, gpl);
  tgp_layer->mask_id = -1;

  bGPDlayer *gpl_mask = BKE_gpencil_layer_named_get(gpd, gpl->mask_layer);
  if (gpl_mask && (gpl_mask != gpl) && ((gpl_mask->flag & GP_LAYER_HIDE) == 0)) {
    mask_invert = (gpl->flag & GP_LAYER_MASK_INVERT) != 0;
    tgp_layer->mask_id = BLI_findindex(&gpd->layers, gpl_mask);
    pd->use_mask_fb = true;
  }

  const bool is_masked = tgp_layer->mask_id != -1;

  {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA_PREMUL;
    if (GPENCIL_3D_DRAWMODE(ob, gpd) || pd->draw_depth_only) {
      /* TODO better 3D mode. */
      state |= DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    }
    else {
      /* We render all strokes with uniform depth (increasing with stroke id). */
      state |= DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_GREATER;
    }

    /* Always write stencil. Only used as optimization for blending. */
    state |= DRW_STATE_WRITE_STENCIL | DRW_STATE_STENCIL_ALWAYS;

    tgp_layer->geom_ps = DRW_pass_create("GPencil Layer", state);
  }

  if (is_masked || (gpl->blend_mode != eGplBlendMode_Regular) || (fade_layer_opacity < 1.0f)) {
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_STENCIL_EQUAL;
    switch (gpl->blend_mode) {
      case eGplBlendMode_Regular:
        state |= DRW_STATE_BLEND_ALPHA_PREMUL;
        break;
      case eGplBlendMode_Add:
        state |= DRW_STATE_BLEND_ADD_FULL;
        break;
      case eGplBlendMode_Subtract:
        state |= DRW_STATE_BLEND_SUB;
        break;
      case eGplBlendMode_Multiply:
      case eGplBlendMode_Divide:
      case eGplBlendMode_Overlay:
        state |= DRW_STATE_BLEND_MUL;
        break;
    }

    if (ELEM(gpl->blend_mode, eGplBlendMode_Subtract, eGplBlendMode_Overlay)) {
      /* For these effect to propagate, we need a signed floating point buffer. */
      pd->use_signed_fb = true;
    }

    tgp_layer->blend_ps = DRW_pass_create("GPencil Blend Layer", state);

    GPUShader *sh = GPENCIL_shader_layer_blend_get();
    DRWShadingGroup *grp = DRW_shgroup_create(sh, tgp_layer->blend_ps);
    DRW_shgroup_uniform_int_copy(grp, "blendMode", gpl->blend_mode);
    DRW_shgroup_uniform_float_copy(grp, "blendOpacity", fade_layer_opacity);
    DRW_shgroup_uniform_texture_ref(grp, "colorBuf", &pd->color_layer_tx);
    DRW_shgroup_uniform_texture_ref(grp, "revealBuf", &pd->reveal_layer_tx);
    DRW_shgroup_uniform_texture_ref(grp, "maskBuf", (is_masked) ? &pd->mask_tx : &pd->dummy_tx);
    DRW_shgroup_uniform_bool_copy(grp, "maskInvert", mask_invert);
    DRW_shgroup_stencil_mask(grp, 0xFF);
    DRW_shgroup_call_procedural_triangles(grp, NULL, 1);

    if (gpl->blend_mode == eGplBlendMode_Overlay) {
      /* We cannot do custom blending on MultiTarget framebuffers.
       * Workaround by doing 2 passes. */
      grp = DRW_shgroup_create(sh, tgp_layer->blend_ps);
      DRW_shgroup_state_disable(grp, DRW_STATE_BLEND_MUL);
      DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ADD_FULL);
      DRW_shgroup_uniform_int_copy(grp, "blendMode", 999);
      DRW_shgroup_call_procedural_triangles(grp, NULL, 1);
    }

    pd->use_layer_fb = true;
  }
  else {
    tgp_layer->blend_ps = NULL;
  }

  return tgp_layer;
}

GPENCIL_tLayer *gpencil_layer_cache_get(GPENCIL_tObject *tgp_ob, int number)
{
  if (number >= 0) {
    GPENCIL_tLayer *layer = tgp_ob->layers.first;
    while (layer != NULL) {
      if (layer->layer_id == number) {
        return layer;
      }
      layer = layer->next;
    }
  }
  return NULL;
}