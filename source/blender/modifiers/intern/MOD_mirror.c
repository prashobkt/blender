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

#include "BLI_math.h"

#include "BLT_translation.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_context.h"
#include "BKE_deform.h"
#include "BKE_lib_id.h"
#include "BKE_lib_query.h"
#include "BKE_mesh.h"
#include "BKE_mesh_mirror.h"
#include "BKE_modifier.h"
#include "BKE_screen.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RNA_access.h"

#include "bmesh.h"
#include "bmesh_tools.h"

#include "MEM_guardedalloc.h"

#include "DEG_depsgraph_build.h"
#include "DEG_depsgraph_query.h"

#include "MOD_modifiertypes.h"
#include "MOD_ui_common.h"

static void initData(ModifierData *md)
{
  MirrorModifierData *mmd = (MirrorModifierData *)md;

  mmd->flag |= (MOD_MIR_AXIS_X | MOD_MIR_VGROUP);
  mmd->tolerance = 0.001;
  mmd->mirror_ob = NULL;
}

static void foreachObjectLink(ModifierData *md, Object *ob, ObjectWalkFunc walk, void *userData)
{
  MirrorModifierData *mmd = (MirrorModifierData *)md;

  walk(userData, ob, &mmd->mirror_ob, IDWALK_CB_NOP);
}

static void updateDepsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  MirrorModifierData *mmd = (MirrorModifierData *)md;
  if (mmd->mirror_ob != NULL) {
    DEG_add_object_relation(ctx->node, mmd->mirror_ob, DEG_OB_COMP_TRANSFORM, "Mirror Modifier");
    DEG_add_modifier_to_transform_relation(ctx->node, "Mirror Modifier");
  }
}

static Mesh *mirrorModifier__doMirror(MirrorModifierData *mmd,
                                      const ModifierEvalContext *ctx,
                                      Object *ob,
                                      Mesh *mesh)
{
  Mesh *result = mesh;

  /* check which axes have been toggled and mirror accordingly */
  if (mmd->flag & MOD_MIR_AXIS_X) {
    result = BKE_mesh_mirror_apply_mirror_on_axis(mmd, ctx, ob, result, 0);
  }
  if (mmd->flag & MOD_MIR_AXIS_Y) {
    Mesh *tmp = result;
    result = BKE_mesh_mirror_apply_mirror_on_axis(mmd, ctx, ob, result, 1);
    if (tmp != mesh) {
      /* free intermediate results */
      BKE_id_free(NULL, tmp);
    }
  }
  if (mmd->flag & MOD_MIR_AXIS_Z) {
    Mesh *tmp = result;
    result = BKE_mesh_mirror_apply_mirror_on_axis(mmd, ctx, ob, result, 2);
    if (tmp != mesh) {
      /* free intermediate results */
      BKE_id_free(NULL, tmp);
    }
  }

  return result;
}

static Mesh *applyModifier(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  Mesh *result;
  MirrorModifierData *mmd = (MirrorModifierData *)md;

  result = mirrorModifier__doMirror(mmd, ctx, ctx->object, mesh);

  if (result != mesh) {
    result->runtime.cd_dirty_vert |= CD_MASK_NORMAL;
  }
  return result;
}

static void panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *row, *col, *decorator_layout;
  uiLayout *layout = panel->layout;
  PropertyRNA *prop;

  PointerRNA ptr;
  PointerRNA ob_ptr;
  modifier_panel_get_property_pointers(C, panel, &ob_ptr, &ptr);
  modifier_panel_buttons(C, panel);

  col = uiLayoutColumn(layout, false);
  uiLayoutSetPropSep(col, true);

  /* No decorators for the first few rows. */
  uiLayoutSetPropDecorate(col, false);

  /* Aligned axis booleans with a single label and no decorators. */
  prop = RNA_struct_find_property(&ptr, "use_axis");
  row = uiLayoutRow(col, true);
  decorator_layout = uiItemL_respect_property_split(row, IFACE_("Axis"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 0, 0, UI_ITEM_R_TOGGLE, IFACE_("X"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 1, 0, UI_ITEM_R_TOGGLE, IFACE_("Y"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 2, 0, UI_ITEM_R_TOGGLE, IFACE_("Z"), ICON_NONE);
  uiItemL(decorator_layout, "", ICON_BLANK1);

  /* Aligned axis booleans with a single label and no decorators. */
  prop = RNA_struct_find_property(&ptr, "use_bisect_axis");
  row = uiLayoutRow(col, true);
  decorator_layout = uiItemL_respect_property_split(row, IFACE_("Bisect"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 0, 0, UI_ITEM_R_TOGGLE, IFACE_("X"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 1, 0, UI_ITEM_R_TOGGLE, IFACE_("Y"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 2, 0, UI_ITEM_R_TOGGLE, IFACE_("Z"), ICON_NONE);
  uiItemL(decorator_layout, "", ICON_BLANK1);

  /* Aligned axis booleans with a single label and no decorators. */
  prop = RNA_struct_find_property(&ptr, "use_bisect_flip_axis");
  row = uiLayoutRow(col, true);
  decorator_layout = uiItemL_respect_property_split(row, IFACE_("Flip"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 0, 0, UI_ITEM_R_TOGGLE, IFACE_("X"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 1, 0, UI_ITEM_R_TOGGLE, IFACE_("Y"), ICON_NONE);
  uiItemFullR(row, &ptr, prop, 2, 0, UI_ITEM_R_TOGGLE, IFACE_("Z"), ICON_NONE);
  uiItemL(decorator_layout, "", ICON_BLANK1);

  uiItemS(col);
  /* Now decorators are fine, we don't insert multiple items in a single row anymore. */
  uiLayoutSetPropDecorate(col, true);

  uiItemR(col, &ptr, "mirror_object", 0, NULL, ICON_NONE);

  uiItemR(col, &ptr, "use_mirror_vertex_groups", 0, IFACE_("Vertex Groups"), ICON_NONE);
}

static void merge_panel_draw_header(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  uiItemR(layout, &ptr, "use_mirror_merge", 0, IFACE_("Merge"), ICON_NONE);
}

static void symmetry_panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *row;
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  uiLayoutSetPropSep(layout, true);

  row = uiLayoutRow(layout, false);
  uiLayoutSetActive(row, RNA_boolean_get(&ptr, "use_mirror_merge"));
  uiItemR(row, &ptr, "merge_threshold", 0, NULL, ICON_NONE);
  uiItemR(layout, &ptr, "use_clip", 0, IFACE_("Clipping"), ICON_NONE);
}

static void uv_panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *col, *row, *split, *sub, *decorator_layout;
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  uiLayoutSetPropSep(layout, true);

  col = uiLayoutColumn(layout, false);
  row = uiLayoutRow(col, true);
  decorator_layout = uiItemL_respect_property_split(row, IFACE_("Mirror U"), ICON_NONE);
  uiItemR(row, &ptr, "use_mirror_u", 0, IFACE_(""), ICON_NONE);
  sub = uiLayoutRow(row, true);
  uiLayoutSetActive(sub, RNA_boolean_get(&ptr, "use_mirror_u"));
  uiItemR(sub, &ptr, "mirror_offset_u", UI_ITEM_R_SLIDER, IFACE_("Offset"), ICON_NONE);

  col = uiLayoutColumn(layout, false);
  row = uiLayoutRow(col, true);
  decorator_layout = uiItemL_respect_property_split(row, IFACE_("V"), ICON_NONE);
  uiItemR(row, &ptr, "use_mirror_v", 0, IFACE_(""), ICON_NONE);
  sub = uiLayoutRow(row, true);
  uiLayoutSetActive(sub, RNA_boolean_get(&ptr, "use_mirror_v"));
  uiItemR(sub, &ptr, "mirror_offset_v", UI_ITEM_R_SLIDER, IFACE_("Offset"), ICON_NONE);

  modifier_panel_end(layout, &ptr);
}

static void panelRegister(ARegionType *region_type)
{
  PanelType *panel_type = modifier_panel_register(region_type, "Mirror", panel_draw);
  modifier_subpanel_register(
      region_type, "mirror_merge", "", merge_panel_draw_header, symmetry_panel_draw, panel_type);
  modifier_subpanel_register(
      region_type, "mirror_textures", "UVs", NULL, uv_panel_draw, panel_type);
}

ModifierTypeInfo modifierType_Mirror = {
    /* name */ "Mirror",
    /* structName */ "MirrorModifierData",
    /* structSize */ sizeof(MirrorModifierData),
    /* type */ eModifierTypeType_Constructive,
    /* flags */ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsMapping |
        eModifierTypeFlag_SupportsEditmode | eModifierTypeFlag_EnableInEditmode |
        eModifierTypeFlag_AcceptsCVs |
        /* this is only the case when 'MOD_MIR_VGROUP' is used */
        eModifierTypeFlag_UsesPreview,

    /* copyData */ modifier_copyData_generic,

    /* deformVerts */ NULL,
    /* deformMatrices */ NULL,
    /* deformVertsEM */ NULL,
    /* deformMatricesEM */ NULL,
    /* applyModifier */ applyModifier,

    /* initData */ initData,
    /* requiredDataMask */ NULL,
    /* freeData */ NULL,
    /* isDisabled */ NULL,
    /* updateDepsgraph */ updateDepsgraph,
    /* dependsOnTime */ NULL,
    /* dependsOnNormals */ NULL,
    /* foreachObjectLink */ foreachObjectLink,
    /* foreachIDLink */ NULL,
    /* foreachTexLink */ NULL,
    /* freeRuntimeData */ NULL,
    /* panelRegister */ panelRegister,
};
