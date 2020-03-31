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
#include "BKE_screen.h"

#include "DNA_object_force_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"

#include "BLT_translation.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "RNA_access.h"

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
  ModifierData *md = ob->modifiers.first;

  for (int i = 0; i < panel->list_index; i++) {
    md = md->next;
  }
  RNA_pointer_create(&ob->id, &RNA_Modifier, md, r_md_ptr);

  if (r_ob_ptr != NULL) {
    RNA_pointer_create(&ob->id, &RNA_Object, ob, r_ob_ptr);
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
  PointerRNA ptr;
  modifier_panel_get_property_pointers(C, panel, NULL, &ptr);
  ModifierData *md = ptr.data;
  const ModifierTypeInfo *mti = modifierType_getInfo(md->type);
  int index = panel->list_index;
  Object *ob = CTX_data_active_object(C);
  Scene *scene = CTX_data_scene(C);

  uiLayout *sub, *row;
  uiLayout *layout = panel->layout;

  /* Modifier Icon. */
  sub = uiLayoutRow(layout, false);
  if (mti->isDisabled && mti->isDisabled(scene, md, 0)) {
    uiLayoutSetRedAlert(sub, true);
  }
  uiItemL(sub, "", RNA_struct_ui_icon(ptr.type));

  /* Modifier Name. */
  sub = uiLayoutRow(layout, false);
  uiLayoutSetEmboss(sub, true);
  uiItemR(sub, &ptr, "name", 0, "", ICON_NONE);

  /* Mode enabling buttons. */
  row = uiLayoutRow(layout, true);
  if (((md->type != eModifierType_Collision) || !(ob->pd && ob->pd->deflect)) &&
      (md->type != eModifierType_Surface)) {
    /* Collision and Surface are always enabled, hide buttons. */
    uiItemR(row, &ptr, "show_render", 0, "", ICON_NONE);
    uiItemR(row, &ptr, "show_viewport", 0, "", ICON_NONE);

    if (mti->flags & eModifierTypeFlag_SupportsEditmode) {
      sub = uiLayoutRow(row, true);
      uiLayoutSetActive(sub, (md->mode & eModifierMode_Realtime));
      uiItemR(sub, &ptr, "show_in_editmode", 0, "", ICON_NONE);
    }
  }
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

  /* Switch context buttons. */
  if (modifier_is_simulation(md) == 1) {
    uiItemStringO(
        layout, "", ICON_PROPERTIES, "WM_OT_properties_context_change", "context", "PHYSICS");
  }
  else if (modifier_is_simulation(md) == 2) {
    uiItemStringO(
        layout, "", ICON_PROPERTIES, "WM_OT_properties_context_change", "context", "PARTICLES");
  }
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

  PanelType *pt = MEM_callocN(sizeof(PanelType), panel_idname);

  strcpy(pt->idname, panel_idname);
  strcpy(pt->label, "");
  strcpy(pt->context, "modifier");
  strcpy(pt->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);

  pt->draw_header = modifier_panel_header;
  pt->draw = draw;
  pt->poll = modifier_ui_poll;

  /* Give the panel the special flag that says it was built here and corresponds to a
   * modifer rather than a PanelType. */
  pt->flag = PANELTYPE_RECREATE;

  BLI_addtail(&region_type->paneltypes, pt);

  return pt;
}

void modifier_subpanel_register(ARegionType *region_type,
                                const char *name,
                                const char *label,
                                void *draw_header,
                                void *draw,
                                bool open,
                                PanelType *parent)
{
  /* Get the subpanel's ID name. */
  char panel_idname[BKE_ST_MAXNAME];
  strcpy(panel_idname, MODIFIER_TYPE_PANEL_PREFIX);
  strcat(panel_idname, name);

  PanelType *pt = MEM_callocN(sizeof(PanelType), panel_idname);

  strcpy(pt->idname, panel_idname);
  strcpy(pt->label, label);
  strcpy(pt->context, "modifier");
  strcpy(pt->translation_context, BLT_I18NCONTEXT_DEFAULT_BPYRNA);

  pt->draw_header = draw_header;
  pt->draw = draw;
  pt->poll = modifier_ui_poll;
  pt->flag = (open) ? 0 : PNL_DEFAULT_CLOSED;

  BLI_assert(parent != NULL);
  strcpy(pt->parent_id, parent->idname);
  pt->parent = parent;
  BLI_addtail(&parent->children, BLI_genericNodeN(pt));
  BLI_addtail(&region_type->paneltypes, pt);
}