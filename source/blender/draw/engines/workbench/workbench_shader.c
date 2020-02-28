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
extern char datatoc_workbench_prepass_frag_glsl[];
// extern char datatoc_workbench_cavity_frag_glsl[];
// extern char datatoc_workbench_forward_composite_frag_glsl[];
// extern char datatoc_workbench_deferred_composite_frag_glsl[];
// extern char datatoc_workbench_deferred_background_frag_glsl[];
// extern char datatoc_workbench_ghost_resolve_frag_glsl[];

extern char datatoc_workbench_composite_frag_glsl[];

extern char datatoc_workbench_shadow_vert_glsl[];
extern char datatoc_workbench_shadow_geom_glsl[];
extern char datatoc_workbench_shadow_caps_geom_glsl[];
extern char datatoc_workbench_shadow_debug_frag_glsl[];

extern char datatoc_workbench_cavity_lib_glsl[];
extern char datatoc_workbench_common_lib_glsl[];
extern char datatoc_workbench_material_lib_glsl[];
extern char datatoc_workbench_data_lib_glsl[];
extern char datatoc_workbench_object_outline_lib_glsl[];
extern char datatoc_workbench_curvature_lib_glsl[];
extern char datatoc_workbench_world_light_lib_glsl[];

extern char datatoc_gpu_shader_depth_only_frag_glsl[];

static struct {
  struct GPUShader *prepass_sh_cache[GPU_SHADER_CFG_LEN][MAX_PREPASS_SHADERS];

  struct GPUShader *composite_sh[MAX_COMPOSITE_SHADERS];
  struct GPUShader *cavity_sh[MAX_CAVITY_SHADERS];
  struct GPUShader *ghost_resolve_sh;
  struct GPUShader *oit_resolve_sh;

  struct GPUShader *shadow_fail_sh[2];
  struct GPUShader *shadow_pass_sh[2];
  struct GPUShader *shadow_caps_sh[2];

  struct DRWShaderLibrary *lib;
} e_data = {{{NULL}}};

void workbench_shader_library_ensure(void)
{
  if (e_data.lib == NULL) {
    e_data.lib = DRW_shader_library_create();

    DRW_SHADER_LIB_ADD(e_data.lib, common_hair_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, common_view_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_cavity_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_common_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_material_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_data_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_object_outline_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_curvature_lib);
    DRW_SHADER_LIB_ADD(e_data.lib, workbench_world_light_lib);
  }
}

static char *workbench_build_defines(WORKBENCH_PrivateData *wpd)
{
  char *str = NULL;

  DynStr *ds = BLI_dynstr_new();

  if (NORMAL_ENCODING_ENABLED()) {
    BLI_dynstr_append(ds, "#define WORKBENCH_ENCODE_NORMALS\n");
  }
  BLI_dynstr_append(ds, "#define V3D_SHADING_SPECULAR_HIGHLIGHT\n");

  str = BLI_dynstr_get_cstring(ds);
  BLI_dynstr_free(ds);
  return str;
}

GPUShader *workbench_shader_opaque_get(WORKBENCH_PrivateData *wpd)
{
  int index = 0;
  if (e_data.prepass_sh_cache[wpd->sh_cfg][index] == NULL) {
    char *defines = workbench_build_defines(wpd);
    char *sh_src = DRW_shader_library_create_shader_string(e_data.lib,
                                                           datatoc_workbench_prepass_vert_glsl);
    const GPUShaderConfigData *sh_cfg_data = &GPU_shader_cfg_data[wpd->sh_cfg];

    e_data.prepass_sh_cache[wpd->sh_cfg][index] = GPU_shader_create_from_arrays({
        .vert = (const char *[]){sh_cfg_data->lib, sh_src, NULL},
        .frag = (const char *[]){sh_src, NULL},
        .defs = (const char *[]){sh_cfg_data->def, defines, NULL},
    });

    MEM_freeN(defines);
    MEM_freeN(sh_src);
  }
  return e_data.prepass_sh_cache[wpd->sh_cfg][index];
}

GPUShader *workbench_shader_opaque_hair_get(WORKBENCH_PrivateData *wpd)
{
  //   if (e_data.prepass_sh_cache[wpd->sh_cfg][index] == NULL) {
  //     char *vert = DRW_shader_library_create_shader_string(e_data.lib, char *shader_code);
  //     char *frag = DRW_shader_library_create_shader_string(e_data.lib, char *shader_code);
  //     char *defines = ;

  //     e_data.prepass_sh_cache[wpd->sh_cfg][index] = GPU_shader_create_from_arrays({
  //         .vert = (const char *[]){sh_cfg_data->lib, vert, NULL},
  //         .frag = (const char *[]){frag, NULL},
  //         .defs = (const char *[]){sh_cfg_data->def, defines, NULL},
  //     });

  //     MEM_freeN(vert);
  //     MEM_freeN(frag);
  //     MEM_freeN(defines);
  //   }
  //   return e_data.prepass_sh_cache[wpd->sh_cfg][index];
  return NULL;
}

GPUShader *workbench_shader_composite_get(WORKBENCH_PrivateData *wpd)
{
  int index = 0;
  if (e_data.composite_sh[index] == NULL) {
    char *defines = workbench_build_defines(wpd);
    char *frag = DRW_shader_library_create_shader_string(e_data.lib,
                                                         datatoc_workbench_composite_frag_glsl);

    e_data.composite_sh[index] = DRW_shader_create_fullscreen(frag, defines);

    MEM_freeN(defines);
    MEM_freeN(frag);
  }
  return e_data.composite_sh[index];
}

void workbench_shader_free(void)
{
  for (int j = 0; j < ARRAY_SIZE(e_data.prepass_sh_cache); j++) {
    for (int i = 0; i < MAX_PREPASS_SHADERS; i++) {
      DRW_SHADER_FREE_SAFE(e_data.prepass_sh_cache[j][i]);
    }
  }

  for (int i = 0; i < MAX_COMPOSITE_SHADERS; i++) {
    DRW_SHADER_FREE_SAFE(e_data.composite_sh[i]);
  }

  DRW_SHADER_LIB_FREE_SAFE(e_data.lib);
}
