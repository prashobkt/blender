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
 * The Original Code is Copyright (C) 2008, Blender Foundation
 * This is a new part of Blender
 */

/** \file
 * \ingroup draw
 */

#include "BLI_polyfill_2d.h"
#include "BLI_math_color.h"

#include "DEG_depsgraph_query.h"

#include "DNA_meshdata_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"

#include "BKE_deform.h"
#include "BKE_gpencil.h"

#include "DRW_render.h"

#include "ED_gpencil.h"
#include "ED_view3d.h"

#include "UI_resources.h"

#include "gpencil_engine.h"

/* create batch geometry data for current buffer control point shader */
GPUBatch *gpencil_get_buffer_ctrlpoint_geom(bGPdata *gpd)
{
  bGPDcontrolpoint *cps = gpd->runtime.cp_points;
  int totpoints = gpd->runtime.tot_cp_points;

  const DRWContextState *draw_ctx = DRW_context_state_get();
  Scene *scene = draw_ctx->scene;
  ToolSettings *ts = scene->toolsettings;

  if (ts->gp_sculpt.guide.use_guide) {
    totpoints++;
  }

  static GPUVertFormat format = {0};
  static uint pos_id, color_id, size_id;
  if (format.attr_len == 0) {
    pos_id = GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
    size_id = GPU_vertformat_attr_add(&format, "size", GPU_COMP_F32, 1, GPU_FETCH_FLOAT);
    color_id = GPU_vertformat_attr_add(&format, "color", GPU_COMP_F32, 4, GPU_FETCH_FLOAT);
  }

  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(&format);
  GPU_vertbuf_data_alloc(vbo, totpoints);

  int idx = 0;
  for (int i = 0; i < gpd->runtime.tot_cp_points; i++) {
    bGPDcontrolpoint *cp = &cps[i];

    GPU_vertbuf_attr_set(vbo, color_id, idx, cp->color);

    /* scale size */
    float size = cp->size * 0.8f;
    GPU_vertbuf_attr_set(vbo, size_id, idx, &size);

    GPU_vertbuf_attr_set(vbo, pos_id, idx, &cp->x);
    idx++;
  }

  if (ts->gp_sculpt.guide.use_guide) {
    float size = 10 * 0.8f;
    float color[4];
    float position[3];
    if (ts->gp_sculpt.guide.reference_point == GP_GUIDE_REF_CUSTOM) {
      UI_GetThemeColor4fv(TH_GIZMO_PRIMARY, color);
      copy_v3_v3(position, ts->gp_sculpt.guide.location);
    }
    else if (ts->gp_sculpt.guide.reference_point == GP_GUIDE_REF_OBJECT &&
             ts->gp_sculpt.guide.reference_object != NULL) {
      UI_GetThemeColor4fv(TH_GIZMO_SECONDARY, color);
      copy_v3_v3(position, ts->gp_sculpt.guide.reference_object->loc);
    }
    else {
      UI_GetThemeColor4fv(TH_REDALERT, color);
      copy_v3_v3(position, scene->cursor.location);
    }
    GPU_vertbuf_attr_set(vbo, pos_id, idx, position);
    GPU_vertbuf_attr_set(vbo, size_id, idx, &size);
    GPU_vertbuf_attr_set(vbo, color_id, idx, color);
  }

  return GPU_batch_create_ex(GPU_PRIM_POINTS, vbo, NULL, GPU_BATCH_OWNS_VBO);
}
