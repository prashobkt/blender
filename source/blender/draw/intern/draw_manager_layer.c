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
 * \ingroup draw
 */

#include <stdio.h>

#include "DNA_screen_types.h"
#include "DNA_space_types.h"

#include "ED_gpencil.h"
#include "ED_space_api.h"

#include "DEG_depsgraph_query.h"

#include "draw_manager.h"

typedef struct DRWLayer {
  struct DRWLayer *next, *prev;

  const DRWLayerType *type;

  GPUFrameBuffer *framebuffer;
} DRWLayer;

static ListBase DRW_layers = {NULL}; /* DRWLayer */

static DRWLayer *drw_layer_create(const DRWLayerType *type)
{
  DRWLayer *layer = MEM_callocN(sizeof(*layer), __func__);
  layer->type = type;
  return layer;
}

static void drw_layer_free(DRWLayer *layer)
{
  MEM_SAFE_FREE(layer);
}

static DRWLayer *drw_layer_for_type_ensure(const DRWLayerType *type)
{
  DRWLayer *layer = BLI_findptr(&DRW_layers, type, offsetof(DRWLayer, type));

  if (!layer) {
    layer = drw_layer_create(type);
    BLI_addtail(&DRW_layers, layer);
  }

  /* Could reinsert layer at tail here, so that the next layer to be drawn is likely first in the
   * list (or at least close to the top). Iterating isn't that expensive though. */

  return layer;
}

static void drw_layer_bind(DRWLayer *layer)
{
}

static void drw_layer_unbind(const DRWLayer *layer)
{
}

void DRW_layers_free(void)
{
  LISTBASE_FOREACH_MUTABLE (DRWLayer *, layer, &DRW_layers) {
    BLI_remlink(&DRW_layers, layer);
    drw_layer_free(layer);
  }
}

void DRW_layers_draw_combined_cached(void)
{
  for (const DRWLayerType *layer_type = &DRW_layer_types[0]; layer_type->draw_layer;
       layer_type++) {
    if (!layer_type->poll || layer_type->poll()) {
      DRWLayer *layer = drw_layer_for_type_ensure(layer_type);

      drw_layer_bind(layer);

      layer_type->draw_layer();

      drw_layer_unbind(layer);
    }
  }
}
