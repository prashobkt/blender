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
 * Copyright 2019, Blender Foundation.
 *
 */

/** \file
 * \ingroup draw
 */

#include "BKE_customdata.h"
#include "BKE_object.h"

#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLI_math.h"

#include "DEG_depsgraph_query.h"

#include "DNA_camera_types.h"
#include "DNA_lanpr_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "DRW_engine.h"
#include "DRW_render.h"

#include "GPU_batch.h"
#include "GPU_draw.h"
#include "GPU_framebuffer.h"
#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_shader.h"
#include "GPU_uniformbuffer.h"
#include "GPU_viewport.h"

#include "lanpr_all.h"
#include "bmesh.h"

#include <math.h>

static float ED_lanpr_compute_chain_length_draw(LANPR_RenderLineChain *rlc,
                                                float *lengths,
                                                int begin_index)
{
  LANPR_RenderLineChainItem *rlci;
  int i = 0;
  float offset_accum = 0;
  float dist;
  float last_point[2];

  rlci = rlc->chain.first;
  copy_v2_v2(last_point, rlci->pos);
  for (rlci = rlc->chain.first; rlci; rlci = rlci->next) {
    dist = len_v2v2(rlci->pos, last_point);
    offset_accum += dist;
    lengths[begin_index + i] = offset_accum;
    copy_v2_v2(last_point, rlci->pos);
    i++;
  }
  return offset_accum;
}

static int lanpr_get_gpu_line_type(LANPR_RenderLineChainItem *rlci)
{
  switch (rlci->line_type) {
    case LANPR_EDGE_FLAG_CONTOUR:
      return 0;
    case LANPR_EDGE_FLAG_CREASE:
      return 1;
    case LANPR_EDGE_FLAG_MATERIAL:
      return 2;
    case LANPR_EDGE_FLAG_EDGE_MARK:
      return 3;
    case LANPR_EDGE_FLAG_INTERSECTION:
      return 4;
    default:
      return 0;
  }
}

void lanpr_chain_generate_draw_command(LANPR_RenderBuffer *rb)
{
  LANPR_RenderLineChain *rlc;
  LANPR_RenderLineChainItem *rlci;
  int vert_count = 0;
  int i = 0;
  int arg;
  float total_length;
  float *lengths;
  float length_target[2];

  static GPUVertFormat format = {0};
  static struct {
    uint pos, uvs, normal, type, level;
  } attr_id;
  if (format.attr_len == 0) {
    attr_id.pos = GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    attr_id.uvs = GPU_vertformat_attr_add(&format, "uvs", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    attr_id.normal = GPU_vertformat_attr_add(&format, "normal", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
    attr_id.type = GPU_vertformat_attr_add(&format, "type", GPU_COMP_I32, 1, GPU_FETCH_INT);
    attr_id.level = GPU_vertformat_attr_add(&format, "level", GPU_COMP_I32, 1, GPU_FETCH_INT);
  }

  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(&format);

  for (rlc = rb->chains.first; rlc; rlc = rlc->next) {
    int count = ED_lanpr_count_chain(rlc);
    /*  printf("seg contains %d verts\n", count); */
    vert_count += count;
  }

  GPU_vertbuf_data_alloc(vbo, vert_count + 1); /*  serve as end point's adj. */

  lengths = MEM_callocN(sizeof(float) * vert_count, "chain lengths");

  GPUIndexBufBuilder elb;
  GPU_indexbuf_init_ex(&elb, GPU_PRIM_LINES_ADJ, vert_count * 4, vert_count);

  for (rlc = rb->chains.first; rlc; rlc = rlc->next) {

    total_length = ED_lanpr_compute_chain_length_draw(rlc, lengths, i);

    for (rlci = rlc->chain.first; rlci; rlci = rlci->next) {

      length_target[0] = lengths[i];
      length_target[1] = total_length - lengths[i];

      GPU_vertbuf_attr_set(vbo, attr_id.pos, i, rlci->pos);
      GPU_vertbuf_attr_set(vbo, attr_id.normal, i, rlci->normal);
      GPU_vertbuf_attr_set(vbo, attr_id.uvs, i, length_target);

      arg = lanpr_get_gpu_line_type(rlci);
      GPU_vertbuf_attr_set(vbo, attr_id.type, i, &arg);

      arg = (int)rlci->occlusion;
      GPU_vertbuf_attr_set(vbo, attr_id.level, i, &arg);

      if (rlci == rlc->chain.last) {
        if (rlci->prev == rlc->chain.first) {
          length_target[1] = total_length;
          GPU_vertbuf_attr_set(vbo, attr_id.uvs, i, length_target);
        }
        i++;
        continue;
      }

      if (rlci == rlc->chain.first) {
        if (rlci->next == rlc->chain.last) {
          GPU_indexbuf_add_line_adj_verts(&elb, vert_count, i, i + 1, vert_count);
        }
        else {
          GPU_indexbuf_add_line_adj_verts(&elb, vert_count, i, i + 1, i + 2);
        }
      }
      else {
        if (rlci->next == rlc->chain.last) {
          GPU_indexbuf_add_line_adj_verts(&elb, i - 1, i, i + 1, vert_count);
        }
        else {
          GPU_indexbuf_add_line_adj_verts(&elb, i - 1, i, i + 1, i + 2);
        }
      }

      i++;
    }
  }
  /*  set end point flag value. */
  length_target[0] = 3e30f;
  length_target[1] = 3e30f;
  GPU_vertbuf_attr_set(vbo, attr_id.pos, vert_count, length_target);

  MEM_freeN(lengths);

  if (rb->chain_draw_batch) {
    GPU_BATCH_DISCARD_SAFE(rb->chain_draw_batch);
  }
  rb->chain_draw_batch = GPU_batch_create_ex(GPU_PRIM_LINES_ADJ,
                                             vbo,
                                             GPU_indexbuf_build(&elb),
                                             GPU_USAGE_DYNAMIC | GPU_BATCH_OWNS_VBO |
                                                 GPU_BATCH_OWNS_INDEX);
}
