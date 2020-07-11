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
 * The Original Code is Copyright (C) 2017 by Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup draw
 *
 * \brief PointCloud API for render engines
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_math_base.h"
#include "BLI_math_vector.h"
#include "BLI_utildefines.h"

#include "DNA_object_types.h"
#include "DNA_pointcloud_types.h"

#include "BKE_pointcloud.h"

#include "GPU_batch.h"

#include "draw_cache_impl.h" /* own include */

static void pointcloud_batch_cache_clear(PointCloud *pointcloud);

/* ---------------------------------------------------------------------- */
/* PointCloud GPUBatch Cache */

typedef struct PointCloudBatchCache {
  GPUVertBuf *pos;  /* Position and radius. */
  GPUVertBuf *geom; /* Instanced geometry for each point in the cloud (small sphere). */
  GPUIndexBuf *geom_indices;

  GPUBatch *dots;
  GPUBatch *surface;

  /* settings to determine if cache is invalid */
  bool is_dirty;
} PointCloudBatchCache;

/* GPUBatch cache management. */

static bool pointcloud_batch_cache_valid(PointCloud *pointcloud)
{
  PointCloudBatchCache *cache = pointcloud->batch_cache;
  return (cache && cache->is_dirty == false);
}

static void pointcloud_batch_cache_init(PointCloud *pointcloud)
{
  PointCloudBatchCache *cache = pointcloud->batch_cache;

  if (!cache) {
    cache = pointcloud->batch_cache = MEM_callocN(sizeof(*cache), __func__);
  }
  else {
    memset(cache, 0, sizeof(*cache));
  }

  cache->is_dirty = false;
}

void DRW_pointcloud_batch_cache_validate(PointCloud *pointcloud)
{
  if (!pointcloud_batch_cache_valid(pointcloud)) {
    pointcloud_batch_cache_clear(pointcloud);
    pointcloud_batch_cache_init(pointcloud);
  }
}

static PointCloudBatchCache *pointcloud_batch_cache_get(PointCloud *pointcloud)
{
  return pointcloud->batch_cache;
}

void DRW_pointcloud_batch_cache_dirty_tag(PointCloud *pointcloud, int mode)
{
  PointCloudBatchCache *cache = pointcloud->batch_cache;
  if (cache == NULL) {
    return;
  }
  switch (mode) {
    case BKE_POINTCLOUD_BATCH_DIRTY_ALL:
      cache->is_dirty = true;
      break;
    default:
      BLI_assert(0);
  }
}

static void pointcloud_batch_cache_clear(PointCloud *pointcloud)
{
  PointCloudBatchCache *cache = pointcloud->batch_cache;
  if (!cache) {
    return;
  }

  GPU_BATCH_DISCARD_SAFE(cache->dots);
  GPU_BATCH_DISCARD_SAFE(cache->surface);
  GPU_VERTBUF_DISCARD_SAFE(cache->pos);
  GPU_VERTBUF_DISCARD_SAFE(cache->geom);
  GPU_INDEXBUF_DISCARD_SAFE(cache->geom_indices);
}

void DRW_pointcloud_batch_cache_free(PointCloud *pointcloud)
{
  pointcloud_batch_cache_clear(pointcloud);
  MEM_SAFE_FREE(pointcloud->batch_cache);
}

static void pointcloud_batch_cache_ensure_pos(Object *ob, PointCloudBatchCache *cache)
{
  if (cache->pos != NULL) {
    return;
  }

  PointCloud *pointcloud = ob->data;

  static GPUVertFormat format = {0};
  if (format.attr_len == 0) {
    /* initialize vertex format */
    GPU_vertformat_attr_add(&format, "ptcloud", GPU_COMP_F32, 4, GPU_FETCH_FLOAT);
  }

  cache->pos = GPU_vertbuf_create_with_format(&format);
  GPU_vertbuf_data_alloc(cache->pos, pointcloud->totpoint);

  float(*vbo_data)[4] = (float(*)[4])cache->pos->data;

  for (int i = 0; i < pointcloud->totpoint; i++) {
    copy_v3_v3(vbo_data[i], pointcloud->co[i]);
    if (pointcloud->radius) {
      vbo_data[i][3] = pointcloud->radius[i];
    }
    else {
      vbo_data[i][3] = 0.01f;
    }
  }
}

static const float bone_box_smooth_normals[8][3] = {
    {M_SQRT3, -M_SQRT3, M_SQRT3},
    {M_SQRT3, -M_SQRT3, -M_SQRT3},
    {-M_SQRT3, -M_SQRT3, -M_SQRT3},
    {-M_SQRT3, -M_SQRT3, M_SQRT3},
    {M_SQRT3, M_SQRT3, M_SQRT3},
    {M_SQRT3, M_SQRT3, -M_SQRT3},
    {-M_SQRT3, M_SQRT3, -M_SQRT3},
    {-M_SQRT3, M_SQRT3, M_SQRT3},
};

static const uint bone_box_solid_tris[12][3] = {
    {0, 2, 1}, /* bottom */
    {0, 3, 2},

    {0, 1, 5}, /* sides */
    {0, 5, 4},

    {1, 2, 6},
    {1, 6, 5},

    {2, 3, 7},
    {2, 7, 6},

    {3, 0, 4},
    {3, 4, 7},

    {4, 5, 6}, /* top */
    {4, 6, 7},
};

static void pointcloud_batch_cache_ensure_geom(Object *UNUSED(ob), PointCloudBatchCache *cache)
{
  if (cache->geom != NULL) {
    return;
  }

  static GPUVertFormat format = {0};
  static uint pos;
  if (format.attr_len == 0) {
    /* initialize vertex format */
    pos = GPU_vertformat_attr_add(&format, "pos", GPU_COMP_F32, 3, GPU_FETCH_FLOAT);
    GPU_vertformat_alias_add(&format, "nor");
  }

  cache->geom = GPU_vertbuf_create_with_format(&format);
  GPU_vertbuf_data_alloc(cache->geom, ARRAY_SIZE(bone_box_smooth_normals));

  GPU_vertbuf_attr_fill(cache->geom, pos, bone_box_smooth_normals);

  GPUIndexBufBuilder builder;
  GPU_indexbuf_init(&builder,
                    GPU_PRIM_TRIS,
                    ARRAY_SIZE(bone_box_solid_tris),
                    ARRAY_SIZE(bone_box_smooth_normals));

  for (int i = 0; i < ARRAY_SIZE(bone_box_solid_tris); i++) {
    GPU_indexbuf_add_tri_verts(&builder, UNPACK3(bone_box_solid_tris[i]));
  }

  cache->geom_indices = GPU_indexbuf_build(&builder);
}

GPUBatch *DRW_pointcloud_batch_cache_get_dots(Object *ob)
{
  PointCloud *pointcloud = ob->data;
  PointCloudBatchCache *cache = pointcloud_batch_cache_get(pointcloud);

  if (cache->dots == NULL) {
    pointcloud_batch_cache_ensure_pos(ob, cache);
    cache->dots = GPU_batch_create(GPU_PRIM_POINTS, cache->pos, NULL);
  }

  return cache->dots;
}

GPUBatch *DRW_pointcloud_batch_cache_get_surface(Object *ob)
{
  PointCloud *pointcloud = ob->data;
  PointCloudBatchCache *cache = pointcloud_batch_cache_get(pointcloud);

  if (cache->surface == NULL) {
    pointcloud_batch_cache_ensure_pos(ob, cache);
    pointcloud_batch_cache_ensure_geom(ob, cache);

    cache->surface = GPU_batch_create(GPU_PRIM_TRIS, cache->geom, cache->geom_indices);
    GPU_batch_instbuf_add_ex(cache->surface, cache->pos, false);
  }

  return cache->surface;
}

int DRW_pointcloud_material_count_get(PointCloud *pointcloud)
{
  return max_ii(1, pointcloud->totcol);
}
