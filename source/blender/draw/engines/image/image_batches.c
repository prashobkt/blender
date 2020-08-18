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

static GPUVertFormat *image_batches_instance_format(void)
{
  static GPUVertFormat format = {0};
  if (format.attr_len == 0) {
    GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  }
  return &format;
}

/* Creates a GPU batch for drawing images in the image editor. */
GPUBatch *IMAGE_batches_image_create(rcti *rect)
{
  GPUVertFormat *format = image_batches_instance_format();

  const int32_t num_patches_x = (rect->xmax - rect->xmin) + 1;
  const int32_t num_patches_y = (rect->ymax - rect->ymin) + 1;
  const int32_t num_verts_x = num_patches_x + 1;
  const int32_t num_verts_y = num_patches_y + 1;
  const int32_t num_verts = num_verts_x * num_verts_y;

  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(format);
  GPU_vertbuf_data_alloc(vbo, num_verts);
  int v = 0;
  for (int iy = 0; iy < num_verts_y; iy++) {
    float yf = (float)(rect->ymin + iy);
    for (int ix = 0; ix < num_verts_x; ix++) {
      float xf = (float)(rect->xmin + ix);
      float local_pos[3] = {xf, yf, 0.0f};
      GPU_vertbuf_vert_set(vbo, v++, &local_pos);
    }
  }

  const int32_t num_tris = num_patches_x * num_patches_y * 2;
  GPUIndexBufBuilder elb;
  GPU_indexbuf_init(&elb, GPU_PRIM_TRIS, num_tris * 3, num_verts);
  int32_t index_offset = 0;
  for (int y = 0; y < num_patches_y; y++) {
    index_offset = y * num_verts_x;
    for (int x = 0; x < num_patches_x; x++) {
      GPU_indexbuf_add_tri_verts(&elb, index_offset, index_offset + 1, index_offset + num_verts_x);
      GPU_indexbuf_add_tri_verts(
          &elb, index_offset + 1, index_offset + num_verts_x + 1, index_offset + num_verts_x);
      index_offset += 1;
    }
  }

  GPUBatch *batch = GPU_batch_create_ex(
      GPU_PRIM_TRIS, vbo, GPU_indexbuf_build(&elb), GPU_BATCH_OWNS_VBO | GPU_BATCH_OWNS_INDEX);

  return batch;
}

GPUBatch *IMAGE_batches_image_tiled_create(Image *image)
{
  BLI_assert(image);
  BLI_assert(image->source == IMA_SRC_TILED);

  static GPUVertFormat format = {0};
  if (format.attr_len == 0) {
    GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
  }

  GPUVertBuf *vbo = GPU_vertbuf_create_with_format(&format);

  const int32_t num_tiles = BLI_listbase_count(&image->tiles);
  const int32_t num_verts = num_tiles * 4;
  const int32_t num_patches = num_tiles * 2;

  GPU_vertbuf_data_alloc(vbo, num_verts);

  float local_pos[3] = {0.0f, 0.0f, 0.0f};
  int vbo_index = 0;

  const int32_t num_tris = num_patches * 2;
  GPUIndexBufBuilder elb;
  GPU_indexbuf_init(&elb, GPU_PRIM_TRIS, num_tris * 3, num_verts);

  LISTBASE_FOREACH (ImageTile *, tile, &image->tiles) {
    const int min_x = ((tile->tile_number - 1001) % 10);
    const int min_y = ((tile->tile_number - 1001) / 10);
    const int max_x = min_x + 1;
    const int max_y = min_y + 1;
    local_pos[0] = min_x;
    local_pos[1] = min_y;
    GPU_vertbuf_vert_set(vbo, vbo_index, &local_pos);
    local_pos[0] = max_x;
    local_pos[1] = min_y;
    GPU_vertbuf_vert_set(vbo, vbo_index + 1, &local_pos);
    local_pos[0] = max_x;
    local_pos[1] = max_y;
    GPU_vertbuf_vert_set(vbo, vbo_index + 2, &local_pos);
    local_pos[0] = min_x;
    local_pos[1] = max_y;
    GPU_vertbuf_vert_set(vbo, vbo_index + 3, &local_pos);

    GPU_indexbuf_add_tri_verts(&elb, vbo_index, vbo_index + 1, vbo_index + 2);
    GPU_indexbuf_add_tri_verts(&elb, vbo_index + 2, vbo_index + 3, vbo_index);

    vbo_index += 4;
  }

  GPUBatch *batch = GPU_batch_create_ex(
      GPU_PRIM_TRIS, vbo, GPU_indexbuf_build(&elb), GPU_BATCH_OWNS_VBO | GPU_BATCH_OWNS_INDEX);
  return batch;
}