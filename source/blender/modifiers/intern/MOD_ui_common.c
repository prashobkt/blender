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

#include <string.h>

#include "BLI_listbase.h"

#include "MEM_guardedalloc.h"

#include "BKE_context.h"
#include "BKE_modifier.h"
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
 * HANS-TODO: Implement poll function. Should depend on the show_ui modifier property.
 *            But the modifier pointer is in the Panel, not the PanelType...
 */
static bool modifier_ui_poll(const bContext *UNUSED(C), PanelType *UNUSED(pt))
{
  return true;
}

/**
 * Move a modifier to the index it's moved to after a drag and drop.
 */
static void modifier_re_order(bContext *C, Panel *panel, int new_index)
{
  Object *ob = CTX_data_active_object(C);

  ModifierData *md = BLI_findlink(&ob->modifiers, panel->runtime.list_index);
  PointerRNA props_ptr;
  wmOperatorType *ot = WM_operatortype_find("OBJECT_OT_modifier_move_to_index", false);
  WM_operator_properties_create_ptr(&props_ptr, ot);
  RNA_string_set(&props_ptr, "modifier", md->name);
  RNA_int_set(&props_ptr, "index", new_index);
  WM_operator_name_call_ptr(C, ot, WM_OP_INVOKE_DEFAULT, &props_ptr);
  WM_operator_properties_free(&props_ptr);
}

static void panel_set_expand_from_flag_recursive(Panel *panel, short flag, short flag_index)
{
  bool open = (flag & (1 << flag_index));
  if (open) {
    panel->flag &= ~PNL_CLOSEDY;
  }
  else {
    panel->flag |= PNL_CLOSEDY;
  }
  LISTBASE_FOREACH (Panel *, child, &panel->children) {
    flag_index++;
    panel_set_expand_from_flag_recursive(child, flag, flag_index);
  }
}

static void panel_set_expand_from_flag(const bContext *C, Panel *panel)
{
  BLI_assert(panel->type != NULL);
  BLI_assert(panel->type->flag & PANELTYPE_RECREATE);

  Object *ob = CTX_data_active_object(C);
  ModifierData *md = BLI_findlink(&ob->modifiers, panel->runtime.list_index);
  short expand_flag = md->ui_expand_flag;

  panel_set_expand_from_flag_recursive(panel, expand_flag, 0);
}

static void modifier_expand_flag_set_recursive(Panel *panel, short *flag, short flag_index)
{
  bool open = !(panel->flag & PNL_CLOSEDY);
  if (open) {
    *flag |= (1 << flag_index);
  }
  else {
    *flag &= ~(1 << flag_index);
  }
  LISTBASE_FOREACH (Panel *, child, &panel->children) {
    flag_index++;
    modifier_expand_flag_set_recursive(child, flag, flag_index);
  }
}

static void modifier_expand_flag_set_from_panel(const bContext *C, Panel *panel)
{
  BLI_assert(panel->type != NULL);
  BLI_assert(panel->type->flag & PANELTYPE_RECREATE);

  Object *ob = CTX_data_active_object(C);
  ModifierData *md = BLI_findlink(&ob->modifiers, panel->runtime.list_index);
  short *expand_flag = &md->ui_expand_flag;

  modifier_expand_flag_set_recursive(panel, expand_flag, 0);
}

/**
 * Draw modifier error message.
 */
void modifier_panel_end(uiLayout *layout, PointerRNA *ptr)
{
  ModifierData *md = ptr->data;
  if (md->error) {
    uiLayout *row = uiLayoutRow(layout, false);
    uiItemL(row, IFACE_(md->error), ICON_ERROR);
  }
}

/**
 * Gets RNA pointers for the active object and the panel's modifier data.
 */
void modifier_panel_get_property_pointers(const bContext *C,
                                          Panel *panel,
                                          PointerRNA *r_ob_ptr,
                                          PointerRNA *r_md_ptr)
{
  Object *ob = CTX_data_active_object(C);
  ModifierData *md = BLI_findlink(&ob->modifiers, panel->runtime.list_index);

  RNA_pointer_create(&ob->id, &RNA_Modifier, md, r_md_ptr);

  if (r_ob_ptr != NULL) {
    RNA_pointer_create(&ob->id, &RNA_Object, ob, r_ob_ptr);
  }

  uiLayoutSetContextPointer(panel->layout, "modifier", r_md_ptr);
}

#define ERROR_LIBDATA_MESSAGE TIP_("Can't edit external library data")
void modifier_panel_buttons(const bContext *C, Panel *panel)
{
  uiLayout *row;
  uiLayout *layout = panel->layout;

  row = uiLayoutRow(layout, false);

  Object *ob = CTX_data_active_object(C);
  ModifierData *md = BLI_findlink(&ob->modifiers, panel->runtime.list_index);

  uiBlock *block = uiLayoutGetBlock(row);
  UI_block_lock_set(
      block, BKE_object_obdata_is_libdata(ob) || ID_IS_LINKED(ob), ERROR_LIBDATA_MESSAGE);

  if (md->type == eModifierType_ParticleSystem) {
    ParticleSystem *psys = ((ParticleSystemModifierData *)md)->psys;

    if (!(ob->mode & OB_MODE_PARTICLE_EDIT)) {
      if (ELEM(psys->part->ren_as, PART_DRAW_GR, PART_DRAW_OB)) {
        uiItemO(row,
                CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, "Convert"),
                ICON_NONE,
                "OBJECT_OT_duplicates_make_real");
      }
      else if (psys->part->ren_as == PART_DRAW_PATH) {
        uiItemO(row,
                CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, "Convert"),
                ICON_NONE,
                "OBJECT_OT_modifier_convert");
      }
    }
  }
  else {
    uiLayoutSetOperatorContext(row, WM_OP_INVOKE_DEFAULT);
    uiItemEnumO(row,
                "OBJECT_OT_modifier_apply",
                CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, "Apply"),
                0,
                "apply_as",
                MODIFIER_APPLY_DATA);

    if (modifier_isSameTopology(md) && !modifier_isNonGeometrical(md)) {
      uiItemEnumO(row,
                  "OBJECT_OT_modifier_apply",
                  CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, "Apply as Shape Key"),
                  0,
                  "apply_as",
                  MODIFIER_APPLY_SHAPE);
    }
  }

  if (!ELEM(md->type,
            eModifierType_Fluidsim,
            eModifierType_Softbody,
            eModifierType_ParticleSystem,
            eModifierType_Cloth,
            eModifierType_Fluid)) {
    uiItemO(row,
            CTX_IFACE_(BLT_I18NCONTEXT_OPERATOR_DEFAULT, ""),
            ICON_DUPLICATE,
            "OBJECT_OT_modifier_copy");
  }
}

/* Check whether Modifier is a simulation or not,
 * this is used for switching to the physics/particles context tab */
static int modifier_is_simulation(ModifierData *md)
{
  /* Physic Tab */
  if (ELEM(md->type,
           eModifierType_Cloth,
           eModifierType_Collision,
           eModifierType_Fluidsim,
           eModifierType_Fluid,
           eModifierType_Softbody,
           eModifierType_Surface,
           eModifierType_DynamicPaint)) {
    return 1;
  }
  /* Particle Tab */
  else if (md->type == eModifierType_ParticleSystem) {
    return 2;
  }
  else {
    return 0;
  }
}

static void modifier_panel_header(const bContext *C, Panel *panel)
{
  uiLayout *row;
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  ModifierData *md = ptr.data;
  const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
  Scene *scene = CTX_data_scene(C);

  /* Modifier Icon. */
  row = uiLayoutRow(layout, false);
  if (mti->isDisabled && mti->isDisabled(scene, md, 0)) {
    uiLayoutSetRedAlert(row, true);
  }
  uiItemL(row, "", RNA_struct_ui_icon(ptr.type));

  /* Modifier Name. */
  uiItemR(layout, &ptr, "name", 0, "", ICON_NONE);
}

static void modifier_panel_header_modes(const bContext *C, Panel *panel)
{
  uiLayout *sub, *row;
  uiLayout *layout = panel->layout;

  PointerRNA ptr;
  modifier_panel_get_property_pointers(C, panel, NULL, &ptr);

  ModifierData *md = ptr.data;
  const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
  int index = panel->runtime.list_index;
  Object *ob = CTX_data_active_object(C);
  Scene *scene = CTX_data_scene(C);

  /* Switch context buttons. */
  if (modifier_is_simulation(md) == 1) {
    uiItemStringO(
        layout, "", ICON_PROPERTIES, "WM_OT_properties_context_change", "context", "PHYSICS");
  }
  else if (modifier_is_simulation(md) == 2) {
    uiItemStringO(
        layout, "", ICON_PROPERTIES, "WM_OT_properties_context_change", "context", "PARTICLES");
  }

  row = uiLayoutRow(layout, true);
  if (ob->type == OB_MESH) {
    int last_cage_index;
    int cage_index = modifiers_getCageIndex(scene, ob, &last_cage_index, 0);
    if (modifier_supportsCage(scene, md) && (index <= last_cage_index)) {
      sub = uiLayoutRow(row, true);
      if (index < cage_index || !modifier_couldBeCage(scene, md)) {
        uiLayoutSetActive(sub, false);
      }
      uiItemR(sub, &ptr, "show_on_cage", 0, "", ICON_NONE);
    }
  } /* Tessellation point for curve-typed objects. */
  else if (ELEM(ob->type, OB_CURVE, OB_SURF, OB_FONT)) {
    if (mti->type != eModifierTypeType_Constructive) {
      /* Constructive modifiers tessellates curve before applying. */
      uiItemR(layout, &ptr, "use_apply_on_spline", 0, "", ICON_NONE);
    }
  }
  /* Collision and Surface are always enabled, hide buttons. */
  if (((md->type != eModifierType_Collision) || !(ob->pd && ob->pd->deflect)) &&
      (md->type != eModifierType_Surface)) {

    if (mti->flags & eModifierTypeFlag_SupportsEditmode) {
      sub = uiLayoutRow(row, true);
      uiLayoutSetActive(sub, (md->mode & eModifierMode_Realtime));
      uiItemR(sub, &ptr, "show_in_editmode", 0, "", ICON_NONE);
    }
    uiItemR(row, &ptr, "show_viewport", 0, "", ICON_NONE);
    uiItemR(row, &ptr, "show_render", 0, "", ICON_NONE);
  }

  uiItemS(layout);
}

/**
 * Create a panel in the context's region
 */
PanelType *modifier_panel_register(ARegionType *region_type, const char *name, void *draw)
{

  /* Get the name for the modifier's panel. */
  char panel_idname[BKE_ST_MAXNAME];
  strcpy(panel_idname, MODIFIER_TYPE_PANEL_PREFIX);
  strcat(panel_idname, name);

  PanelType *panel_type = MEM_callocN(sizeof(PanelType), panel_idname);

  strcpy(panel_type->idname, panel_idname);
  strcpy(panel_type->label, "");
  strcpy(panel_type->context, "modifier");
  strcpy(panel_type->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);

  panel_type->draw_header = modifier_panel_header;
  panel_type->draw_header_preset = modifier_panel_header_modes;
  panel_type->draw = draw;
  panel_type->poll = modifier_ui_poll;

  /* Give the panel the special flag that says it was built here and corresponds to a
   * modifer rather than a PanelType. */
  panel_type->flag = PANELTYPE_RECREATE;
  panel_type->re_order = modifier_re_order;
  panel_type->set_expand_from_flag = panel_set_expand_from_flag;
  panel_type->set_expand_flag_from_panel = modifier_expand_flag_set_from_panel;

  BLI_addtail(&region_type->paneltypes, panel_type);

  return panel_type;
}

void modifier_subpanel_register(ARegionType *region_type,
                                const char *name,
                                const char *label,
                                void *draw_header,
                                void *draw,
                                PanelType *parent)
{
  /* Create the subpanel's ID name. */
  char panel_idname[BKE_ST_MAXNAME];
  strcpy(panel_idname, MODIFIER_TYPE_PANEL_PREFIX);
  strcat(panel_idname, name);

  PanelType *panel_type = MEM_callocN(sizeof(PanelType), panel_idname);

  strcpy(panel_type->idname, panel_idname);
  strcpy(panel_type->label, label);
  strcpy(panel_type->context, "modifier");
  strcpy(panel_type->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);

  panel_type->draw_header = draw_header;
  panel_type->draw = draw;
  panel_type->poll = modifier_ui_poll;
  panel_type->flag = PNL_DEFAULT_CLOSED;

  BLI_assert(parent != NULL);
  strcpy(panel_type->parent_id, parent->idname);
  panel_type->parent = parent;
  BLI_addtail(&parent->children, BLI_genericNodeN(panel_type));
  BLI_addtail(&region_type->paneltypes, panel_type);
}
