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

#include "BLI_rect.h"

#include "GPU_batch.h"

#include "image_private.h"

static GPUVertFormat *editors_batches_image_instance_format(void)
{
  static GPUVertFormat format = {0};
  if (format.attr_len == 0) {
    GPU_vertformat_attr_add(&format, "local_pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  }
  return &format;
}

GPUBatch *IMAGE_batches_image_instance_create(rcti *rect)
{
  GPUVertFormat *format = editors_batches_image_instance_format();
  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(format);

  int32_t num_instances_x = (rect->xmax - rect->xmin) + 1;
  int32_t num_instances_y = (rect->ymax - rect->ymin) + 1;
  int32_t num_instances = num_instances_x * num_instances_y;

  GPU_vertbuf_data_alloc(vbo, num_instances);

  int v = 0;
  for (int y = rect->ymin; y <= rect->ymax; y++) {
    float yf = (float)y;
    for (int x = rect->xmin; x <= rect->xmax; x++) {
      float xf = (float)x;
      float local_pos[3] = {xf, yf, 0.0f};
      GPU_vertbuf_vert_set(vbo, v++, &local_pos);
    }
  }

  GPUBatch *batch = GPU_batch_create_ex(GPU_PRIM_POINTS, vbo, NULL, GPU_BATCH_OWNS_VBO);
  return batch;
}
