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
 * \ingroup modifiers
 */

#ifndef __MOD_UI_COMMON_H__
#define __MOD_UI_COMMON_H__

/* so modifier types match their defines */
#include "MOD_modifiertypes.h"

#include "DEG_depsgraph_build.h"

struct ARegionType;
struct bContext;
struct PanelType;
struct uiLayout;

/**
 * Template for modifier UI code:
 *
static void panel_draw(const bContext *C, Panel *panel)
{
  uiLayout *layout = panel->layout;
  PointerRNA ptr;
  PointerRNA ob_ptr;
  modifier_panel_get_property_pointers(C, panel, &ob_ptr, &ptr);

  // Draw modifier layout

  modifier_panel_end(layout, &ptr);
}

static void subpanel_draw(const bContext *C, Panel *panel)
{
  PointerRNA ptr;
  modifier_panel_get_property_pointers(C, panel, NULL, &ptr);
  uiLayout *layout = panel->layout;

  // Draw subpanel layout
}

static void subpanel_draw_header(const bContext *C, Panel *panel)
{
  PointerRNA ptr;
  modifier_panel_get_property_pointers(C, panel, NULL, &ptr);
  uiLayout *layout = panel->layout;

  // Draw header layout
}

static void panel(ARegionType *region_type)
{
  PanelType *panel_type = modifier_panel_register(region_type, "MODIFIER_TYPE", panel_draw);
  modifier_subpanel_register(
    region_type, "IDNAME", "", subpanel_draw_header, subpanel_draw, true, panel_type);
}
 */

#ifdef __cplusplus
extern "C" {
#endif

void modifier_panel_buttons(const struct bContext *C, struct Panel *panel);

void modifier_panel_end(struct uiLayout *layout, PointerRNA *ptr);

void modifier_panel_get_property_pointers(const bContext *C,
                                          struct Panel *panel,
                                          struct PointerRNA *r_ob_ptr,
                                          struct PointerRNA *r_ptr);

PanelType *modifier_panel_register(struct ARegionType *region_type,
                                   const char *modifier_type,
                                   void *draw);

void modifier_subpanel_register(struct ARegionType *region_type,
                                const char *name,
                                const char *label,
                                void *draw_header,
                                void *draw,
                                bool open,
                                PanelType *parent);

#ifdef __cplusplus
}
#endif

#endif /* __MOD_UI_COMMON_H__ */
