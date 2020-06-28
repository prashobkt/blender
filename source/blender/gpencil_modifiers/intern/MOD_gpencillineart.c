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
 * The Original Code is Copyright (C) 2017, Blender Foundation
 * This is a new part of Blender
 */

/** \file
 * \ingroup modifiers
 */

#include <stdio.h>

#include "BLI_utildefines.h"

#include "BLI_blenlib.h"
#include "BLI_math_vector.h"

#include "BLT_translation.h"

#include "DNA_gpencil_modifier_types.h"
#include "DNA_gpencil_types.h"
#include "DNA_lineart_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "ED_lineart.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_lib_query.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_screen.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "BKE_modifier.h"
#include "RNA_access.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "MOD_gpencil_modifiertypes.h"
#include "MOD_gpencil_ui_common.h"
#include "MOD_gpencil_util.h"

#include "ED_lineart.h"

static void initData(GpencilModifierData *md)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)md;
}

static void copyData(const GpencilModifierData *md, GpencilModifierData *target)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)md;
  LineartGpencilModifierData *tgmd = (LineartGpencilModifierData *)target;

  BKE_gpencil_modifier_copydata_generic(md, target);
}

static void generate_strokes_actual(
    GpencilModifierData *md, Depsgraph *depsgraph, Object *ob, bGPDlayer *gpl, bGPDframe *gpf)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)md;
  bGPdata *gpd = (bGPdata *)ob->data;

  if (!gpl) {
    return;
  }

  if (lmd->source_type == LRT_SOURCE_OBJECT) {
    Object *source_object = lmd->source_object;
    if (!source_object) {
      return;
    }
    ED_lineart_generate_gpencil_from_chain(
        depsgraph,
        source_object,
        gpl,
        gpf,
        lmd->level_start,
        lmd->use_multiple_levels ? lmd->level_end : lmd->level_start,
        lmd->target_gp_material ?
            BKE_gpencil_object_material_index_get(ob, lmd->target_gp_material) :
            0,
        NULL,
        lmd->line_types & (~LRT_EDGE_FLAG_INTERSECTION));
    /* Note that intersection lines will only be in collection */
  }
  else if (lmd->source_type == LRT_SOURCE_COLLECTION) {
    Collection *source_collection = lmd->source_collection;
    if (!source_collection) {
      return;
    }
    ED_lineart_generate_gpencil_from_chain(
        depsgraph,
        NULL,
        gpl,
        gpf,
        lmd->level_start,
        lmd->use_multiple_levels ? lmd->level_end : lmd->level_start,
        lmd->target_gp_material ?
            BKE_gpencil_object_material_index_get(ob, lmd->target_gp_material) :
            0,
        source_collection,
        lmd->line_types);
  }
  else {
    /* no other type selection for now. */
    return;
  }
}

static void generateStrokes(GpencilModifierData *md, Depsgraph *depsgraph, Object *ob)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)md;
  bGPdata *gpd = ob->data;
  Scene *scene = DEG_get_evaluated_scene(depsgraph);

  bGPDlayer *gpl = BKE_gpencil_layer_get_by_name(gpd, lmd->target_gp_layer, 1);
  bGPDframe *gpf = BKE_gpencil_layer_frame_get(gpl, scene->r.cfra, GP_GETFRAME_ADD_NEW);

  generate_strokes_actual(md, depsgraph, ob, gpl, gpf);
}

static void bakeModifier(Main *UNUSED(bmain),
                         Depsgraph *depsgraph,
                         GpencilModifierData *md,
                         Object *ob)
{
  bGPdata *gpd = ob->data;
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)md;

  LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd->layers) {
    LISTBASE_FOREACH (bGPDframe *, gpf, &gpl->frames) {
      bGPDlayer *gpl = BKE_gpencil_layer_get_by_name(gpd, lmd->target_gp_layer, 1);
      generate_strokes_actual(md, depsgraph, ob, gpl, gpf);
    }
  }
}

static void freeData(GpencilModifierData *md)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)md;
}

static void foreachIDLink(GpencilModifierData *md, Object *ob, IDWalkFunc walk, void *userData)
{
  LineartGpencilModifierData *lmd = (LineartGpencilModifierData *)md;

  walk(userData, ob, (ID **)&lmd->target_gp_material, IDWALK_CB_USER);
  walk(userData, ob, (ID **)&lmd->source_object, IDWALK_CB_USER);
  walk(userData, ob, (ID **)&lmd->source_collection, IDWALK_CB_USER);
}

static void panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;

  PointerRNA ptr, ob_ptr;
  gpencil_modifier_panel_get_property_pointers(C, panel, &ob_ptr, &ptr);

  PointerRNA obj_data_ptr = RNA_pointer_get(&ob_ptr, "data");

  int source_type = RNA_enum_get(&ptr, "source_type");

  uiLayoutSetPropSep(layout, true);

  uiItemR(layout, &ptr, "source_type", 0, NULL, ICON_NONE);

  if (source_type == LRT_SOURCE_OBJECT) {
    uiItemR(layout, &ptr, "source_object", 0, NULL, ICON_CUBE);
  }
  else if (source_type == LRT_SOURCE_COLLECTION) {
    uiItemR(layout, &ptr, "source_collection", 0, NULL, ICON_GROUP);
  }

  uiItemR(layout, &ptr, "use_contour", 0, NULL, ICON_NONE);
  uiItemR(layout, &ptr, "use_crease", 0, NULL, ICON_NONE);
  uiItemR(layout, &ptr, "use_material", 0, NULL, ICON_NONE);
  uiItemR(layout, &ptr, "use_edge_mark", 0, NULL, ICON_NONE);
  uiItemR(layout, &ptr, "use_intersection", 0, NULL, ICON_NONE);

  uiItemPointerR(
      layout, &ptr, "target_gp_layer", &obj_data_ptr, "layers", NULL, ICON_GREASEPENCIL);
  uiItemPointerR(
      layout, &ptr, "target_gp_material", &obj_data_ptr, "materials", NULL, ICON_SHADING_TEXTURE);

  gpencil_modifier_panel_end(layout, &ptr);
}

static void occlusion_panel_draw(const bContext *C, Panel *panel)
{
  PointerRNA ptr;
  gpencil_modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  uiLayout *layout = panel->layout;

  bool use_multiple_levels = RNA_boolean_get(&ptr, "use_multiple_levels");

  uiItemR(layout, &ptr, "use_multiple_levels", 0, "Multiple Levels", ICON_NONE);

  if (use_multiple_levels) {
    uiLayout *col = uiLayoutColumn(layout, true);
    uiItemR(col, &ptr, "level_start", 0, NULL, ICON_NONE);
    uiItemR(col, &ptr, "level_end", 0, NULL, ICON_NONE);
  }
  else {
    uiItemR(layout, &ptr, "level_start", 0, "Level", ICON_NONE);
  }
}

static void panelRegister(ARegionType *region_type)
{
  PanelType *panel_type = gpencil_modifier_panel_register(
      region_type, eGpencilModifierType_Lineart, panel_draw);

  PanelType *occlusion_panel_type = gpencil_modifier_subpanel_register(
      region_type, "occlusion", "Occlusion", NULL, occlusion_panel_draw, panel_type);
}

GpencilModifierTypeInfo modifierType_Gpencil_Lineart = {
    /* name */ "Line Art",
    /* structName */ "LineartGpencilModifierData",
    /* structSize */ sizeof(LineartGpencilModifierData),
    /* type */ eGpencilModifierTypeType_Gpencil,
    /* flags */ eGpencilModifierTypeFlag_SupportsEditmode,

    /* copyData */ copyData,

    /* deformStroke */ NULL,
    /* generateStrokes */ generateStrokes,
    /* bakeModifier */ bakeModifier,
    /* remapTime */ NULL,

    /* initData */ initData,
    /* freeData */ freeData,
    /* isDisabled */ NULL,
    /* updateDepsgraph */ NULL,
    /* dependsOnTime */ NULL,
    /* foreachObjectLink */ NULL,
    /* foreachIDLink */ foreachIDLink,
    /* foreachTexLink */ NULL,
    /* panelRegister */ panelRegister,
};
