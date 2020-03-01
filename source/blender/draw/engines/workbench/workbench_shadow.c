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

void workbench_shadow_cache_init(WORKBENCH_Data *data)
{
  WORKBENCH_PassList *psl = data->psl;
  WORKBENCH_PrivateData *wpd = data->stl->wpd;
  struct GPUShader *sh;
  DRWShadingGroup *grp;

  studiolight_update_light(wpd);

  if (SHADOW_ENABLED(wpd)) {
#if DEBUG_SHADOW_VOLUME
    DRWState depth_pass_state = DRW_STATE_DEPTH_LESS;
    DRWState depth_fail_state = DRW_STATE_DEPTH_GREATER_EQUAL;
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD_FULL;
#else
    DRWState depth_pass_state = DRW_STATE_WRITE_STENCIL_SHADOW_PASS;
    DRWState depth_fail_state = DRW_STATE_WRITE_STENCIL_SHADOW_FAIL;
    DRWState state = DRW_STATE_DEPTH_LESS | DRW_STATE_STENCIL_ALWAYS;
#endif

    /* TODO(fclem) Merge into one pass with subpasses. */
    DRW_PASS_CREATE(psl->shadow_pass[0], state | depth_pass_state);
    DRW_PASS_CREATE(psl->shadow_pass[1], state | depth_fail_state);

    /* Stencil Shadow passes. */
    for (int manifold = 0; manifold < 2; manifold++) {
      sh = workbench_shader_shadow_pass_get(manifold);
      wpd->shadow_pass_grp[manifold] = grp = DRW_shgroup_create(sh, psl->shadow_pass[0]);
      DRW_shgroup_stencil_mask(grp, 0xFF); /* Needed once to set the stencil state for the pass. */

      sh = workbench_shader_shadow_fail_get(manifold, false);
      wpd->shadow_fail_grp[manifold] = grp = DRW_shgroup_create(sh, psl->shadow_pass[1]);
      DRW_shgroup_stencil_mask(grp, 0xFF); /* Needed once to set the stencil state for the pass. */

      sh = workbench_shader_shadow_fail_get(manifold, true);
      wpd->shadow_fail_caps_grp[manifold] = grp = DRW_shgroup_create(sh, psl->shadow_pass[1]);
    }
  }
  else {
    psl->shadow_pass[0] = NULL;
    psl->shadow_pass[1] = NULL;
  }
}

static void workbench_init_object_data(DrawData *dd)
{
  WORKBENCH_ObjectData *data = (WORKBENCH_ObjectData *)dd;
  data->shadow_bbox_dirty = true;
}

void workbench_shadow_cache_populate(WORKBENCH_Data *data, Object *ob, const bool has_transp_mat)
{
  WORKBENCH_PrivateData *wpd = data->stl->wpd;

  bool is_manifold;
  struct GPUBatch *geom_shadow = DRW_cache_object_edge_detection_get(ob, &is_manifold);
  if (geom_shadow == NULL) {
    return;
  }

  WORKBENCH_ObjectData *engine_object_data = (WORKBENCH_ObjectData *)DRW_drawdata_ensure(
      &ob->id,
      &draw_engine_workbench,
      sizeof(WORKBENCH_ObjectData),
      &workbench_init_object_data,
      NULL);

  if (studiolight_object_cast_visible_shadow(wpd, ob, engine_object_data)) {
    mul_v3_mat3_m4v3(engine_object_data->shadow_dir, ob->imat, wpd->light_direction_ws);

    DRWShadingGroup *grp;
    bool use_shadow_pass_technique = !studiolight_camera_in_object_shadow(
        wpd, ob, engine_object_data);

    /* Shadow pass technique needs object to be have all its surface opaque. */
    if (has_transp_mat) {
      use_shadow_pass_technique = false;
    }

    if (use_shadow_pass_technique) {
      grp = DRW_shgroup_create_sub(wpd->shadow_pass_grp[is_manifold]);
      DRW_shgroup_uniform_vec3(grp, "lightDirection", engine_object_data->shadow_dir, 1);
      DRW_shgroup_uniform_float_copy(grp, "lightDistance", 1e5f);
      DRW_shgroup_call_no_cull(grp, geom_shadow, ob);
#if DEBUG_SHADOW_VOLUME
      DRW_debug_bbox(&engine_object_data->shadow_bbox, (float[4]){1.0f, 0.0f, 0.0f, 1.0f});
#endif
    }
    else {
      float extrude_distance = studiolight_object_shadow_distance(wpd, ob, engine_object_data);

      /* TODO(fclem): only use caps if they are in the view frustum. */
      const bool need_caps = true;
      if (need_caps) {
        grp = DRW_shgroup_create_sub(wpd->shadow_fail_caps_grp[is_manifold]);
        DRW_shgroup_uniform_vec3(grp, "lightDirection", engine_object_data->shadow_dir, 1);
        DRW_shgroup_uniform_float_copy(grp, "lightDistance", extrude_distance);
        DRW_shgroup_call_no_cull(grp, DRW_cache_object_surface_get(ob), ob);
      }

      grp = DRW_shgroup_create_sub(wpd->shadow_fail_grp[is_manifold]);
      DRW_shgroup_uniform_vec3(grp, "lightDirection", engine_object_data->shadow_dir, 1);
      DRW_shgroup_uniform_float_copy(grp, "lightDistance", extrude_distance);
      DRW_shgroup_call_no_cull(grp, geom_shadow, ob);
#if DEBUG_SHADOW_VOLUME
      DRW_debug_bbox(&engine_object_data->shadow_bbox, (float[4]){0.0f, 1.0f, 0.0f, 1.0f});
#endif
    }
  }
}