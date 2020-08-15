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
 * \ingroup draw_editors
 *
 * Draw engine to draw the Image/UV editor
 */

#include "DRW_render.h"

#include "BKE_object.h"

#include "editors_engine.h"
#include "editors_private.h"

/* Shaders */

/* Default image width and height when image is not available */

/* -------------------------------------------------------------------- */
/** \name Engine Callbacks
 * \{ */
static void EDITORS_engine_init(void *vedata)
{
  EDITORS_Data *ed = (EDITORS_Data *)vedata;

  EDITORS_shader_library_ensure();
  EDITORS_image_init(ed);
}

static void EDITORS_cache_init(void *vedata)
{
  EDITORS_Data *ed = (EDITORS_Data *)vedata;
  EDITORS_image_cache_init(ed);
}

static void EDITORS_cache_populate(void *UNUSED(vedata), Object *UNUSED(ob))
{
}

static void EDITORS_draw_scene(void *vedata)
{
  EDITORS_Data *ed = (EDITORS_Data *)vedata;
  EDITORS_image_draw_scene(ed);
}

static void EDITORS_engine_free(void)
{
  EDITORS_shaders_free();
}

/* \} */
static const DrawEngineDataSize EDITORS_data_size = DRW_VIEWPORT_DATA_SIZE(EDITORS_Data);

DrawEngineType draw_engine_editors_type = {
    NULL,                    /* next */
    NULL,                    /* prev */
    N_("Editor"),            /* idname */
    &EDITORS_data_size,      /*vedata_size */
    &EDITORS_engine_init,    /* engine_init */
    &EDITORS_engine_free,    /* engine_free */
    &EDITORS_cache_init,     /* cache_init */
    &EDITORS_cache_populate, /* cache_populate */
    NULL,                    /* cache_finish */
    &EDITORS_draw_scene,     /* draw_scene */
    NULL,                    /* view_update */
    NULL,                    /* id_update */
    NULL,                    /* render_to_image */
};
