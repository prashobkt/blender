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

/* Forward declarations */
struct rcti;
struct GPUBatch;

/* *********** LISTS *********** */

/* GPUViewport.storage
 * Is freed everytime the viewport engine changes */
typedef struct EDITORS_PassList {
  DRWPass *image_pass;
} EDITORS_PassList;

typedef struct EDITORS_Data {
  void *engine_type;
  DRWViewportEmptyList *fbl;
  DRWViewportEmptyList *txl;
  EDITORS_PassList *psl;
  DRWViewportEmptyList *stl;
} EDITORS_Data;

/* editors_image.c */
void EDITORS_image_init(EDITORS_Data *vedata);
void EDITORS_image_cache_init(EDITORS_Data *vedata);
void EDITORS_image_draw_scene(EDITORS_Data *vedata);

/* editors_shaders.c */
GPUShader *EDITORS_shaders_image_get(void);
GPUShader *EDITORS_shaders_image_unavailable_get(void);
void EDITORS_shader_library_ensure(void);
void EDITORS_shaders_free(void);

/* editors_batches.c */
struct GPUBatch *EDITORS_batches_image_instance_create(struct rcti *rect);