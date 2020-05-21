/* This program is free software; you can redistribute it and/or
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
 */

/** \file
 * \ingroup modifiers
 */

#include <string.h>

#include "BLI_listbase.h"

#include "MEM_guardedalloc.h"

#include "BKE_context.h"
#include "BKE_gpencil_modifier.h"
#include "BKE_object.h"
#include "BKE_screen.h"

#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "ED_object.h"

#include "BLT_translation.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

#include "MOD_ui_common.h" /* Self include */

/**
 * Poll function so these modifier panels only show for grease pencil objects.
 */
static bool gpencil_modifier_ui_poll(const bContext *C, PanelType *UNUSED(pt))
{
  Object *ob = CTX_data_active_object(C);

  return (ob != NULL) && (ob->type == OB_GPENCIL);
}

/* -------------------------------------------------------------------- */
/** \name Panel Drag and Drop, Expansion Saving
 * \{ */

/**
 * Move a modifier to the index it's moved to after a drag and drop.
 */
static void gpencil_modifier_reorder(bContext *C, Panel *panel, int new_index)
{
  Object *ob = CTX_data_active_object(C);

  GpencilModifierData *md = BLI_findlink(&ob->greasepencil_modifiers, panel->runtime.list_index);
  PointerRNA props_ptr;
  wmOperatorType *ot = WM_operatortype_find("OBJECT_OT_gpencil_modifier_move_to_index", false);
  WM_operator_properties_create_ptr(&props_ptr, ot);
  RNA_string_set(&props_ptr, "modifier", md->name);
  RNA_int_set(&props_ptr, "index", new_index);
  WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &props_ptr);
  WM_operator_properties_free(&props_ptr);
}

static short get_gpencil_modifier_expand_flag(const bContext *C, Panel *panel)
{
  Object *ob = CTX_data_active_object(C);
  GpencilModifierData *md = BLI_findlink(&ob->greasepencil_modifiers, panel->runtime.list_index);
  return md->ui_expand_flag;
  return 0;
}

static void set_gpencil_modifier_expand_flag(const bContext *C, Panel *panel, short expand_flag)
{
  Object *ob = CTX_data_active_object(C);
  GpencilModifierData *md = BLI_findlink(&ob->greasepencil_modifiers, panel->runtime.list_index);
  md->ui_expand_flag = expand_flag;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Modifier Panel Layouts
 * \{ */

void gpencil_modifier_masking_panel_draw(const bContext *C,
                                         Panel *panel,
                                         bool use_material,
                                         bool use_vertex)
{
  uiLayout *row, *col, *sub;
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  PointerRNA ob_ptr;
  gpencil_modifier_panel_get_property_pointers(C, panel, &ob_ptr, &ptr);

  PointerRNA obj_data_ptr = RNA_pointer_get(&ob_ptr, "data");
  bool has_layer = RNA_string_length(&ptr, "layer") != 0;

  uiLayoutSetPropSep(layout, true);

  col = uiLayoutColumn(layout, true);
  row = uiLayoutRow(col, true);
  uiItemPointerR(row, &ptr, "layer", &obj_data_ptr, "layers", NULL, ICON_GREASEPENCIL);
  sub = uiLayoutRow(row, true);
  uiLayoutSetActive(sub, has_layer);
  uiLayoutSetPropDecorate(sub, false);
  uiItemR(sub, &ptr, "invert_layers", 0, "", ICON_ARROW_LEFTRIGHT);

  row = uiLayoutRow(col, true);
  uiItemR(row, &ptr, "layer_pass", 0, NULL, ICON_NONE);
  sub = uiLayoutRow(row, true);
  uiLayoutSetActive(sub, RNA_int_get(&ptr, "layer_pass") != 0);
  uiLayoutSetPropDecorate(sub, false);
  uiItemR(sub, &ptr, "invert_layer_pass", 0, "", ICON_ARROW_LEFTRIGHT);

  if (use_material) {
    bool has_material = RNA_string_length(&ptr, "material") != 0;

    col = uiLayoutColumn(layout, true);
    row = uiLayoutRow(col, true);
    uiItemPointerR(row, &ptr, "material", &obj_data_ptr, "materials", NULL, ICON_SHADING_TEXTURE);
    sub = uiLayoutRow(row, true);
    uiLayoutSetActive(sub, has_material);
    uiLayoutSetPropDecorate(sub, false);
    uiItemR(sub, &ptr, "invert_layers", 0, "", ICON_ARROW_LEFTRIGHT);

    row = uiLayoutRow(col, true);
    uiItemR(row, &ptr, "pass_index", 0, NULL, ICON_NONE);
    sub = uiLayoutRow(row, true);
    uiLayoutSetActive(sub, RNA_int_get(&ptr, "pass_index") != 0);
    uiLayoutSetPropDecorate(sub, false);
    uiItemR(sub, &ptr, "invert_material_pass", 0, "", ICON_ARROW_LEFTRIGHT);
  }

  if (use_vertex) {
    bool has_vertex_group = RNA_string_length(&ptr, "vertex_group") != 0;

    row = uiLayoutRow(layout, true);
    uiItemPointerR(row, &ptr, "vertex_group", &ob_ptr, "vertex_groups", NULL, ICON_NONE);
    sub = uiLayoutRow(row, true);
    uiLayoutSetActive(sub, has_vertex_group);
    uiLayoutSetPropDecorate(sub, false);
    uiItemR(sub, &ptr, "invert_vertex", 0, "", ICON_ARROW_LEFTRIGHT);
  }
}

void gpencil_modifier_curve_header_draw(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  gpencil_modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  uiItemR(layout, &ptr, "use_custom_curve", 0, NULL, ICON_NONE);
}

void gpencil_modifier_curve_panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  gpencil_modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  uiTemplateCurveMapping(layout, &ptr, "curve", 0, false, false, false, false);
}

/**
 * Draw modifier error message.
 */
void gpencil_modifier_panel_end(uiLayout *layout, PointerRNA *ptr)
{
  GpencilModifierData *md = ptr->data;
  if (md->error) {
    uiLayout *row = uiLayoutRow(layout, false);
    uiItemL(row, IFACE_(md->error), ICON_ERROR);
  }
}

/**
 * Gets RNA pointers for the active object and the panel's modifier data.
 */
void gpencil_modifier_panel_get_property_pointers(const bContext *C,
                                                  Panel *panel,
                                                  PointerRNA *r_ob_ptr,
                                                  PointerRNA *r_md_ptr)
{
  Object *ob = CTX_data_active_object(C);
  GpencilModifierData *md = BLI_findlink(&ob->greasepencil_modifiers, panel->runtime.list_index);

  RNA_pointer_create(&ob->id, &RNA_GpencilModifier, md, r_md_ptr);

  if (r_ob_ptr != NULL) {
    RNA_pointer_create(&ob->id, &RNA_Object, ob, r_ob_ptr);
  }

  uiLayoutSetContextPointer(panel->layout, "modifier", r_md_ptr);
}

#define ERROR_LIBDATA_MESSAGE TIP_("Can't edit external library data")
void gpencil_modifier_panel_buttons(const bContext *C, Panel *panel)
{
  uiLayout *row;
  uiBlock *block;
  uiLayout *layout = panel->layout;

  row = uiLayoutRow(layout, false);

  uiLayoutSetScaleY(row, 0.8f);

  Object *ob = CTX_data_active_object(C);
  GpencilModifierData *md = BLI_findlink(&ob->greasepencil_modifiers, panel->runtime.list_index);
  const GpencilModifierTypeInfo *mti = BKE_gpencil_modifier_get_info(md->type);

  block = uiLayoutGetBlock(row);
  UI_block_lock_set(
      block, BKE_object_obdata_is_libdata(ob) || ID_IS_LINKED(ob), ERROR_LIBDATA_MESSAGE);

  if (mti->flags & eGpencilModifierTypeFlag_NoApply) {
    uiLayoutSetEnabled(row, false);
  }
  uiItemEnumO(row,
              "OBJECT_OT_gpencil_modifier_apply",
              CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, "Apply"),
              0,
              "apply_as",
              MODIFIER_APPLY_DATA);

  UI_block_lock_clear(block);
  UI_block_lock_set(block, ob && ID_IS_LINKED(ob), ERROR_LIBDATA_MESSAGE);

  uiItemO(row,
          CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, "Copy"),
          ICON_NONE,
          "OBJECT_OT_gpencil_modifier_copy");

  row = uiLayoutRow(layout, false);
  uiLayoutSetScaleY(row, 0.2f);
  uiItemS(row);
}

static void gpencil_modifier_panel_header(const bContext *C, Panel *panel)
{
  uiLayout *row, *sub;
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  gpencil_modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  GpencilModifierData *md = ptr.data;
  const GpencilModifierTypeInfo *mti = BKE_gpencil_modifier_get_info(md->type);
  bool narrow_panel = (panel->sizex < UI_UNIT_X * 8 && panel->sizex != 0);

  /* Modifier Icon. */
  row = uiLayoutRow(layout, false);
  if (mti->isDisabled && mti->isDisabled(md, 0)) {
    uiLayoutSetRedAlert(row, true);
  }
  uiItemL(row, "", RNA_struct_ui_icon(ptr.type));

  /* Modifier Name. */
  if (!narrow_panel) {
    uiItemR(layout, &ptr, "name", 0, "", ICON_NONE);
  }

  row = uiLayoutRow(layout, true);
  if (mti->flags & eGpencilModifierTypeFlag_SupportsEditmode) {
    sub = uiLayoutRow(row, true);
    uiItemR(sub, &ptr, "show_in_editmode", 0, "", ICON_NONE);
  }
  uiItemR(row, &ptr, "show_viewport", 0, "", ICON_NONE);
  uiItemR(row, &ptr, "show_render", 0, "", ICON_NONE);

  row = uiLayoutRow(layout, false);
  uiLayoutSetEmboss(row, UI_EMBOSS_NONE);
  uiItemO(row, "", ICON_X, "OBJECT_OT_gpencil_modifier_remove");

  /* Some extra padding at the end, so 'x' icon isn't too close to drag button. */
  uiItemS(layout);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Modifier Registration Helpers
 * \{ */

/**
 * Create a panel in the context's region
 */
PanelType *gpencil_modifier_panel_register(ARegionType *region_type,
                                           GpencilModifierType type,
                                           PanelDrawFn draw)
{

  /* Get the name for the modifier's panel. */
  char panel_idname[BKE_ST_MAXNAME];
  BKE_gpencil_modifierType_panel_id(type, panel_idname);

  PanelType *panel_type = MEM_callocN(sizeof(PanelType), panel_idname);

  strcpy(panel_type->idname, panel_idname);
  strcpy(panel_type->label, "");
  strcpy(panel_type->context, "modifier");
  strcpy(panel_type->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);

  panel_type->draw_header = gpencil_modifier_panel_header;
  panel_type->draw = draw;
  panel_type->poll = gpencil_modifier_ui_poll;

  /* Give the panel the special flag that says it was built here and corresponds to a
   * modifer rather than a PanelType. */
  panel_type->flag = PNL_LAYOUT_HEADER_EXPAND | PNL_DRAW_BOX | PNL_INSTANCED;
  panel_type->reorder = gpencil_modifier_reorder;
  panel_type->get_list_data_expand_flag = get_gpencil_modifier_expand_flag;
  panel_type->set_list_data_expand_flag = set_gpencil_modifier_expand_flag;

  BLI_addtail(&region_type->paneltypes, panel_type);

  return panel_type;
}

/**
 * Add a child panel to the parent.
 *
 * \note To create the panel type's idname, it appends the \a name argument to the \a parent's
 * idname.
 */
PanelType *gpencil_modifier_subpanel_register(ARegionType *region_type,
                                              const char *name,
                                              const char *label,
                                              PanelDrawFn draw_header,
                                              PanelDrawFn draw,
                                              PanelType *parent)
{
  /* Create the subpanel's ID name. */
  char panel_idname[BKE_ST_MAXNAME];
  strcpy(panel_idname, parent->idname);
  strcat(panel_idname, "_");
  strcat(panel_idname, name);

  PanelType *panel_type = MEM_callocN(sizeof(PanelType), panel_idname);

  strcpy(panel_type->idname, panel_idname);
  strcpy(panel_type->label, label);
  strcpy(panel_type->context, "modifier");
  strcpy(panel_type->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);

  panel_type->draw_header = draw_header;
  panel_type->draw = draw;
  panel_type->poll = gpencil_modifier_ui_poll;
  panel_type->flag = (PNL_DEFAULT_CLOSED | PNL_DRAW_BOX);

  BLI_assert(parent != NULL);
  strcpy(panel_type->parent_id, parent->idname);
  panel_type->parent = parent;
  BLI_addtail(&parent->children, BLI_genericNodeN(panel_type));
  BLI_addtail(&region_type->paneltypes, panel_type);

  return panel_type;
}

/** \} */