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

#include "BLI_dynstr.h"

#include "workbench_engine.h"
#include "workbench_private.h"

extern char datatoc_common_hair_lib_glsl[];
extern char datatoc_common_view_lib_glsl[];

extern char datatoc_workbench_prepass_vert_glsl[];
extern char datatoc_workbench_prepass_hair_vert_glsl[];
extern char datatoc_workbench_prepass_frag_glsl[];

extern char datatoc_workbench_effect_cavity_frag_glsl[];
extern char datatoc_workbench_effect_outline_frag_glsl[];

extern char datatoc_workbench_composite_frag_glsl[];

extern char datatoc_workbench_transparent_accum_frag_glsl[];
extern char datatoc_workbench_transparent_resolve_frag_glsl[];

extern char datatoc_workbench_merge_infront_frag_glsl[];

extern char datatoc_workbench_shadow_vert_glsl[];
extern char datatoc_workbench_shadow_geom_glsl[];
extern char datatoc_workbench_shadow_caps_geom_glsl[];
extern char datatoc_workbench_shadow_debug_frag_glsl[];

extern char datatoc_workbench_cavity_lib_glsl[];
extern char datatoc_workbench_common_lib_glsl[];
extern char datatoc_workbench_curvature_lib_glsl[];
extern char datatoc_workbench_data_lib_glsl[];
extern char datatoc_workbench_image_lib_glsl[];
extern char datatoc_workbench_matcap_lib_glsl[];
extern char datatoc_workbench_material_lib_glsl[];
extern char datatoc_workbench_object_outline_lib_glsl[];
extern char datatoc_workbench_shader_interface_lib_glsl[];
extern char datatoc_workbench_world_light_lib_glsl[];

extern char datatoc_gpu_shader_depth_only_frag_glsl[];

/* Maximum number of variations. */
#define MAX_LIGHTING 3
#define MAX_COLOR 3
#define MAX_GEOM 2

static struct {
  struct GPUShader *opaque_prepass_sh_cache[GPU_SHADER_CFG_LEN][MAX_GEOM][MAX_COLOR];
  struct GPUShader *transp_prepass_sh_cache[GPU_SHADER_CFG_LEN][MAX_GEOM][MAX_LIGHTING][MAX_COLOR];

  struct GPUShader *opaque_composite_sh[MAX_LIGHTING];
  struct GPUShader *oit_resolve_sh;
  struct GPUShader *outline_sh;
  struct GPUShader *merge_infront_sh;

  struct GPUShader *shadow_depth_pass_sh[2];
  struct GPUShader *shadow_depth_fail_sh[2][2];

  struct GPUShader *cavity_sh[2][2];

  struct DRWShaderLibrary *lib;
} e_data = {{{{NULL}}}};

void workbench_shader_library_ensure(void)
{
  if (e_data.lib == NULL) {
    e_data.lib = DRW_shader_library_create();

    DRW_SHADER_LIB_ADD(e_data.lib, common_hair_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, common_view_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_shader_interface_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_common_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_image_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_material_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_data_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_matcap_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_object_outline_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_cavity_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_curvature_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_world_light_lib);
  }
}

static char *workbench_build_defines(
    WORKBENCH_PrivateData *wpd, bool textured, bool tiled, bool cavity, bool curvature)
{
  char *str = NULL;

  DynStr *ds = BLI_dynstr_new();

  if (wpd && wpd->shading.light == V3D_LIGHTING_STUDIO) {
    BLI_dynstr_append(ds, "#define V3D_LIGHTING_STUDIO\n");
  }
  else if (wpd && wpd->shading.light == V3D_LIGHTING_MATCAP) {
    BLI_dynstr_append(ds, "#define V3D_LIGHTING_MATCAP\n");
  }
  else {
    BLI_dynstr_append(ds, "#define V3D_LIGHTING_FLAT\n");
  }

  if (NORMAL_ENCODING_ENABLED()) {
    BLI_dynstr_append(ds, "#define WORKBENCH_ENCODE_NORMALS\n");
  }

  if (textured) {
    BLI_dynstr_append(ds, "#define V3D_SHADING_TEXTURE_COLOR\n");
  }
  if (tiled) {
    BLI_dynstr_append(ds, "#define TEXTURE_IMAGE_ARRAY\n");
  }
  if (cavity) {
    BLI_dynstr_append(ds, "#define USE_CAVITY\n");
  }
  if (curvature) {
    BLI_dynstr_append(ds, "#define USE_CURVATURE\n");
  }

  str = BLI_dynstr_get_cstring(ds);
  BLI_dynstr_free(ds);
  return str;
}

static int workbench_color_index(WORKBENCH_PrivateData *UNUSED(wpd), bool textured, bool tiled)
{
  BLI_assert(2 < MAX_COLOR);
  return (textured) ? (tiled ? 2 : 1) : 0;
}

static GPUShader *workbench_shader_get_ex(
    WORKBENCH_PrivateData *wpd, bool transp, bool hair, bool textured, bool tiled)
{
  int color = workbench_color_index(wpd, textured, tiled);
  int light = wpd->shading.light;
  BLI_assert(light < MAX_LIGHTING);
  struct GPUShader **shader =
      (transp) ? &e_data.transp_prepass_sh_cache[wpd->sh_cfg][hair][light][color] :
                 &e_data.opaque_prepass_sh_cache[wpd->sh_cfg][hair][color];

  if (*shader == NULL) {
    char *defines = workbench_build_defines(wpd, textured, tiled, false, false);

    char *frag_file = transp ? datatoc_workbench_transparent_accum_frag_glsl :
                               datatoc_workbench_prepass_frag_glsl;
    char *frag_src = DRW_shader_library_create_shader_string(e_data.lib, frag_file);

    char *vert_file = hair ? datatoc_workbench_prepass_hair_vert_glsl :
                             datatoc_workbench_prepass_vert_glsl;
    char *vert_src = DRW_shader_library_create_shader_string(e_data.lib, vert_file);

    const GPUShaderConfigData *sh_cfg_data = &GPU_shader_cfg_data[wpd->sh_cfg];

    *shader = GPU_shader_create_from_arrays({
        .vert = (const char *[]){sh_cfg_data->lib, vert_src, NULL},
        .frag = (const char *[]){frag_src, NULL},
        .defs = (const char *[]){sh_cfg_data->def, defines, NULL},
    });

    MEM_freeN(defines);
    MEM_freeN(frag_src);
    MEM_freeN(vert_src);
  }
  return *shader;
}

GPUShader *workbench_shader_opaque_get(WORKBENCH_PrivateData *wpd, bool hair)
{
  return workbench_shader_get_ex(wpd, false, hair, false, false);
}

GPUShader *workbench_shader_opaque_image_get(WORKBENCH_PrivateData *wpd, bool hair, bool tiled)
{
  return workbench_shader_get_ex(wpd, false, hair, true, tiled);
}

GPUShader *workbench_shader_transparent_get(WORKBENCH_PrivateData *wpd, bool hair)
{
  return workbench_shader_get_ex(wpd, true, hair, false, false);
}

GPUShader *workbench_shader_transparent_image_get(WORKBENCH_PrivateData *wpd,
                                                  bool hair,
                                                  bool tiled)
{
  return workbench_shader_get_ex(wpd, true, hair, true, tiled);
}

GPUShader *workbench_shader_composite_get(WORKBENCH_PrivateData *wpd)
{
  int light = wpd->shading.light;
  struct GPUShader **shader = &e_data.opaque_composite_sh[light];
  BLI_assert(light < MAX_LIGHTING);

  if (*shader == NULL) {
    char *defines = workbench_build_defines(wpd, false, false, false, false);
    char *frag = DRW_shader_library_create_shader_string(e_data.lib,
                                                         datatoc_workbench_composite_frag_glsl);

    *shader = DRW_shader_create_fullscreen(frag, defines);

    MEM_freeN(defines);
    MEM_freeN(frag);
  }
  return *shader;
}

GPUShader *workbench_shader_merge_infront_get(WORKBENCH_PrivateData *UNUSED(wpd))
{
  if (e_data.merge_infront_sh == NULL) {
    char *frag = DRW_shader_library_create_shader_string(
        e_data.lib, datatoc_workbench_merge_infront_frag_glsl);

    e_data.merge_infront_sh = DRW_shader_create_fullscreen(frag, NULL);

    MEM_freeN(frag);
  }
  return e_data.merge_infront_sh;
}

GPUShader *workbench_shader_transparent_resolve_get(WORKBENCH_PrivateData *wpd)
{
  if (e_data.oit_resolve_sh == NULL) {
    char *defines = workbench_build_defines(wpd, false, false, false, false);

    e_data.oit_resolve_sh = DRW_shader_create_fullscreen(
        datatoc_workbench_transparent_resolve_frag_glsl, defines);

    MEM_freeN(defines);
  }
  return e_data.oit_resolve_sh;
}

static GPUShader *workbench_shader_shadow_pass_get_ex(bool depth_pass, bool manifold, bool cap)
{
  struct GPUShader **shader = (depth_pass) ? &e_data.shadow_depth_pass_sh[manifold] :
                                             &e_data.shadow_depth_fail_sh[manifold][cap];

  if (*shader == NULL) {
#if DEBUG_SHADOW_VOLUME
    const char *shadow_frag = datatoc_workbench_shadow_debug_frag_glsl;
#else
    const char *shadow_frag = datatoc_gpu_shader_depth_only_frag_glsl;
#endif

    *shader = GPU_shader_create_from_arrays({
        .vert = (const char *[]){datatoc_common_view_lib_glsl,
                                 datatoc_workbench_shadow_vert_glsl,
                                 NULL},
        .geom = (const char *[]){(cap) ? datatoc_workbench_shadow_caps_geom_glsl :
                                         datatoc_workbench_shadow_geom_glsl,
                                 NULL},
        .frag = (const char *[]){shadow_frag, NULL},
        .defs = (const char *[]){(depth_pass) ? "#define SHADOW_PASS\n" : "#define SHADOW_FAIL\n",
                                 (manifold) ? "" : "#define DOUBLE_MANIFOLD\n",
                                 NULL},
    });
  }
  return *shader;
}

GPUShader *workbench_shader_shadow_pass_get(bool manifold)
{
  return workbench_shader_shadow_pass_get_ex(true, manifold, false);
}

GPUShader *workbench_shader_shadow_fail_get(bool manifold, bool cap)
{
  return workbench_shader_shadow_pass_get_ex(false, manifold, cap);
}

GPUShader *workbench_shader_cavity_get(bool cavity, bool curvature)
{
  BLI_assert(cavity || curvature);
  struct GPUShader **shader = &e_data.cavity_sh[cavity][curvature];

  if (*shader == NULL) {
    char *defines = workbench_build_defines(NULL, false, false, cavity, curvature);
    char *frag = DRW_shader_library_create_shader_string(
        e_data.lib, datatoc_workbench_effect_cavity_frag_glsl);

    *shader = DRW_shader_create_fullscreen(frag, defines);

    MEM_freeN(defines);
    MEM_freeN(frag);
  }
  return *shader;
}

GPUShader *workbench_shader_outline_get(void)
{
  if (e_data.outline_sh == NULL) {
    char *frag = DRW_shader_library_create_shader_string(
        e_data.lib, datatoc_workbench_effect_outline_frag_glsl);

    e_data.outline_sh = DRW_shader_create_fullscreen(frag, NULL);

    MEM_freeN(frag);
  }
  return e_data.outline_sh;
}

void workbench_shader_free(void)
{
  for (int j = 0; j < sizeof(e_data.opaque_prepass_sh_cache) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.opaque_prepass_sh_cache[0][0][0];
    DRW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.transp_prepass_sh_cache) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.transp_prepass_sh_cache[0][0][0][0];
    DRW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.opaque_composite_sh) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.opaque_composite_sh[0];
    DRW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.shadow_depth_pass_sh) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.shadow_depth_pass_sh[0];
    DRW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.shadow_depth_fail_sh) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.shadow_depth_fail_sh[0][0];
    DRW_SHADER_FREE_SAFE(sh_array[j]);
  }
  for (int j = 0; j < sizeof(e_data.cavity_sh) / sizeof(void *); j++) {
    struct GPUShader **sh_array = &e_data.cavity_sh[0][0];
    DRW_SHADER_FREE_SAFE(sh_array[j]);
  }
  DRW_SHADER_FREE_SAFE(e_data.oit_resolve_sh);
  DRW_SHADER_FREE_SAFE(e_data.outline_sh);
  DRW_SHADER_FREE_SAFE(e_data.merge_infront_sh);

  DRW_SHADER_LIB_FREE_SAFE(e_data.lib);
}
