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
 */

/** \file
 * \ingroup bke
 *
 * User defined menu API.
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_string_utils.h"

#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "RNA_access.h"

#include "BKE_blender_user_menu.h"
#include "BKE_idprop.h"

/* -------------------------------------------------------------------- */
/** \name Menu group
 * \{ */

void BKE_blender_user_menu_free_list(ListBase *lb)
{
  for (bUserMenu *um = lb->first, *um_next; um; um = um_next) {
    um_next = um->next;
    BKE_blender_user_menu_item_free_list(&um->items);
    MEM_freeN(um);
  }
  BLI_listbase_clear(lb);
}

bUserMenusGroup *BKE_blender_user_menus_group_find(ListBase *lb, char *idname)
{
  LISTBASE_FOREACH (bUserMenusGroup *, umg, lb) {
    if ((STREQ(idname, umg->idname))) {
      return umg;
    }
  }
  return NULL;
}

void BKE_blender_user_menus_group_idname_update(bUserMenusGroup *umg)
{
  char name[64];

  STRNCPY(umg->idname, "USER_MT_");
  STRNCPY(name, umg->name);
  for (int i = 0; name[i]; i++) {
    if (name[i] == ' ')
      name[i] = '_';
    if (name[i] >= 'a' && name[i] <= 'z')
      name[i] += 'A' - 'a';
  }
  strcat(umg->idname, name);
  BLI_uniquename(&U.user_menus,
                 umg,
                 umg->idname,
                 '_',
                 offsetof(bUserMenusGroup, idname),
                 sizeof(umg->idname));
}

void BKE_blender_user_menus_group_idname_update_keymap(wmWindowManager *wm, char *old, char *new)
{
  wmKeyConfig *kc;
  wmKeyMap *km;

  for (kc = wm->keyconfigs.first; kc; kc = kc->next) {
    for (km = kc->keymaps.first; km; km = km->next) {
      wmKeyMapItem *kmi;
      for (kmi = km->items.first; kmi; kmi = kmi->next) {
        if (STREQ(kmi->idname, "WM_OT_call_user_menu")) {
          IDProperty *idp = IDP_GetPropertyFromGroup(kmi->properties, "name");
          char *index = IDP_String(idp);
          printf("%s\n", index);
        }
      }
    }
  }
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Menu Type
 * \{ */

bUserMenu *BKE_blender_user_menu_find(ListBase *lb, char space_type, const char *context)
{
  LISTBASE_FOREACH (bUserMenu *, um, lb) {
    if ((space_type == um->space_type) && (STREQ(context, um->context))) {
      return um;
    }
  }
  return NULL;
}

bUserMenu *BKE_blender_user_menu_ensure(ListBase *lb, char space_type, const char *context)
{
  bUserMenu *um = BKE_blender_user_menu_find(lb, space_type, context);
  if (um == NULL) {

    um = MEM_callocN(sizeof(bUserMenu), __func__);
    um->space_type = space_type;
    BLI_listbase_clear(&um->items);
    STRNCPY(um->context, context);
    BLI_addhead(lb, um);
  }
  return um;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Menu Item
 * \{ */

bUserMenuItem *BKE_blender_user_menu_item_add(ListBase *lb, int type)
{
  uint size;

  if (type == USER_MENU_TYPE_SEP) {
    size = sizeof(bUserMenuItem);
  }
  else if (type == USER_MENU_TYPE_OPERATOR) {
    size = sizeof(bUserMenuItem_Op);
  }
  else if (type == USER_MENU_TYPE_MENU) {
    size = sizeof(bUserMenuItem_Menu);
  }
  else if (type == USER_MENU_TYPE_PROP) {
    size = sizeof(bUserMenuItem_Prop);
  }
  else if (type == USER_MENU_TYPE_SUBMENU) {
    size = sizeof(bUserMenuItem_SubMenu);
  }
  else {
    size = sizeof(bUserMenuItem);
    BLI_assert(0);
  }

  bUserMenuItem *umi = MEM_callocN(size, __func__);
  umi->type = type;
  umi->icon = 0;
  if (lb)
    BLI_addtail(lb, umi);
  return umi;
}

void BKE_blender_user_menu_item_free(bUserMenuItem *umi)
{
  if (umi->type == USER_MENU_TYPE_OPERATOR) {
    bUserMenuItem_Op *umi_op = (bUserMenuItem_Op *)umi;
    if (umi_op->prop) {
      IDP_FreeProperty(umi_op->prop);
    }
  }
  if (umi->type == USER_MENU_TYPE_SUBMENU) {
    bUserMenuItem_SubMenu *umi_sm = (bUserMenuItem_SubMenu *)umi;
    BKE_blender_user_menu_item_free_list(&umi_sm->items);
  }
  MEM_freeN(umi);
}

void BKE_blender_user_menu_item_free_list(ListBase *lb)
{
  for (bUserMenuItem *umi = lb->first, *umi_next; umi; umi = umi_next) {
    umi_next = umi->next;
    BKE_blender_user_menu_item_free(umi);
  }
  BLI_listbase_clear(lb);
}
