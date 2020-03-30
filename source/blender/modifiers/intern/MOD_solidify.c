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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2005 by the Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup modifiers
 */

#include <string.h>

#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "DNA_mesh_types.h"

#include "BKE_context.h"
#include "BKE_particle.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RNA_access.h"

#include "MOD_modifiertypes.h"

#include "MOD_solidify_util.h"

#ifdef __GNUC__
#  pragma GCC diagnostic error "-Wsign-conversion"
#endif

static bool dependsOnNormals(ModifierData *md)
{
  const SolidifyModifierData *smd = (SolidifyModifierData *)md;
  /* even when we calculate our own normals,
   * the vertex normals are used as a fallback
   * if manifold is enabled vertex normals are not used */
  return smd->mode == MOD_SOLIDIFY_MODE_EXTRUDE;
}

static void initData(ModifierData *md)
{
  SolidifyModifierData *smd = (SolidifyModifierData *)md;
  smd->offset = 0.01f;
  smd->offset_fac = -1.0f;
  smd->flag = MOD_SOLIDIFY_RIM;
  smd->mode = MOD_SOLIDIFY_MODE_EXTRUDE;
  smd->nonmanifold_offset_mode = MOD_SOLIDIFY_NONMANIFOLD_OFFSET_MODE_CONSTRAINTS;
  smd->nonmanifold_boundary_mode = MOD_SOLIDIFY_NONMANIFOLD_BOUNDARY_MODE_NONE;
}

static void requiredDataMask(Object *UNUSED(ob),
                             ModifierData *md,
                             CustomData_MeshMasks *r_cddata_masks)
{
  SolidifyModifierData *smd = (SolidifyModifierData *)md;

  /* ask for vertexgroups if we need them */
  if (smd->defgrp_name[0] != '\0' || smd->shell_defgrp_name[0] != '\0' ||
      smd->rim_defgrp_name[0] != '\0') {
    r_cddata_masks->vmask |= CD_MASK_MDEFORMVERT;
  }
}

static Mesh *applyModifier(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const SolidifyModifierData *smd = (SolidifyModifierData *)md;
  switch (smd->mode) {
    case MOD_SOLIDIFY_MODE_EXTRUDE:
      return MOD_solidify_extrude_applyModifier(md, ctx, mesh);
    case MOD_SOLIDIFY_MODE_NONMANIFOLD:
      return MOD_solidify_nonmanifold_applyModifier(md, ctx, mesh);
    default:
      BLI_assert(0);
  }
  return mesh;
}

// uiLayout *sub, *row, *col, *split;

// int solidify_mode = RNA_enum_get(ptr, "solidify_mode");

// bool has_vertex_group = RNA_string_length(ptr, "vertex_group") != 0;
// row = uiLayoutRow(layout, false);
// uiItemR(row, ptr, "solidify_mode", 0, NULL, ICON_NONE);

// if (solidify_mode == MOD_SOLIDIFY_MODE_NONMANIFOLD) {
//   uiItemR(layout, ptr, "nonmanifold_thickness_mode", 0, NULL, ICON_NONE);
//   uiItemR(layout, ptr, "nonmanifold_boundary_mode", 0, NULL, ICON_NONE);
// }

// split = uiLayoutSplit(layout, 0.5f, false);
// col = uiLayoutColumn(split, false);
// uiItemR(col, ptr, "thickness", 0, NULL, ICON_NONE);
// uiItemR(col, ptr, "thickness_clamp", 0, NULL, ICON_NONE);
// row = uiLayoutRow(col, false);
// uiLayoutSetActive(row, RNA_float_get(ptr, "thickness_clamp") > 0.0f);
// uiItemR(row, ptr, "use_thickness_angle_clamp", 0, NULL, ICON_NONE);

// uiItemS(col);

// row = uiLayoutRow(col, true);
// uiItemPointerR(row, ptr, "vertex_group", ob_ptr, "vertex_groups", "", ICON_NONE);
// sub = uiLayoutRow(row, true);
// uiLayoutSetActive(sub, has_vertex_group);
// uiItemR(sub, ptr, "invert_vertex_group", 0, "", ICON_ARROW_LEFTRIGHT);

// row = uiLayoutRow(col, false);
// uiLayoutSetActive(row, has_vertex_group);
// uiItemR(row, ptr, "thickness_vertex_group", 0, IFACE_("Factor"), ICON_NONE);

// if (solidify_mode == MOD_SOLIDIFY_MODE_EXTRUDE) {
//   uiItemL(col, IFACE_("Crease:"), ICON_NONE);
//   uiItemR(col, ptr, "edge_crease_inner", 0, IFACE_("Inner"), ICON_NONE);
//   uiItemR(col, ptr, "edge_crease_outer", 0, IFACE_("Outer"), ICON_NONE);
//   uiItemR(col, ptr, "edge_crease_rim", 0, IFACE_("Rim"), ICON_NONE);
// }

// col = uiLayoutColumn(split, false);
// uiItemR(col, ptr, "offset", 0, NULL, ICON_NONE);
// uiItemR(col, ptr, "use_flip_normals", 0, NULL, ICON_NONE);
// if (solidify_mode == MOD_SOLIDIFY_MODE_EXTRUDE) {
//   uiItemR(col, ptr, "use_even_offset", 0, NULL, ICON_NONE);
//   uiItemR(col, ptr, "use_quality_normals", 0, NULL, ICON_NONE);
// }
// uiItemR(col, ptr, "use_rim", 0, NULL, ICON_NONE);
// sub = uiLayoutColumn(col, false);
// uiLayoutSetActive(sub, RNA_boolean_get(ptr, "use_rim"));
// uiItemR(sub, ptr, "use_rim_only", 0, NULL, ICON_NONE);

// uiItemS(col);

// uiItemL(col, IFACE_("Material Index Offset:"), ICON_NONE);

// sub = uiLayoutColumn(col, false);
// row = uiLayoutSplit(sub, 0.4f, true);
// uiItemR(row, ptr, "material_offset", 0, "", ICON_NONE);
// row = uiLayoutRow(row, true);
// uiLayoutSetActive(row, RNA_boolean_get(ptr, "use_rim"));
// uiItemR(row, ptr, "material_offset_rim", 0, IFACE_("Rim"), ICON_NONE);

ModifierTypeInfo modifierType_Solidify = {
    /* name */ "Solidify",
    /* structName */ "SolidifyModifierData",
    /* structSize */ sizeof(SolidifyModifierData),
    /* type */ eModifierTypeType_Constructive,

    /* flags */ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_AcceptsCVs |
        eModifierTypeFlag_SupportsMapping | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,

    /* copyData */ modifier_copyData_generic,

    /* deformVerts */ NULL,
    /* deformMatrices */ NULL,
    /* deformVertsEM */ NULL,
    /* deformMatricesEM */ NULL,
    /* applyModifier */ applyModifier,

    /* initData */ initData,
    /* requiredDataMask */ requiredDataMask,
    /* freeData */ NULL,
    /* isDisabled */ NULL,
    /* updateDepsgraph */ NULL,
    /* dependsOnTime */ NULL,
    /* dependsOnNormals */ dependsOnNormals,
    /* foreachObjectLink */ NULL,
    /* foreachIDLink */ NULL,
    /* foreachTexLink */ NULL,
    /* freeRuntimeData */ NULL,
    /* panelRegister */ NULL,
};
