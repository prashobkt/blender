# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

__all__ = (
    "draw_kmi",
    "draw_custom_menu",
)

import bpy
from bpy.app.translations import pgettext_iface as iface_
from bpy.app.translations import contexts as i18n_contexts


def _indented_layout(layout, level):
    indentpx = 16
    if level == 0:
        level = 0.0001   # Tweak so that a percentage of 0 won't split by half
    indent = level * indentpx / bpy.context.region.width

    split = layout.split(factor=indent)
    col = split.column()
    col = split.column()
    return col

# shortcut has to be added. keep this to later use
def draw_kmi(display_keymaps, kc, km, kmi, layout, level):
    map_type = kmi.map_type

    col = _indented_layout(layout, level)

    if kmi.show_expanded:
        col = col.column(align=True)
        box = col.box()
    else:
        box = col.column()

    split = box.split()

    # header bar
    row = split.row(align=True)
    row.prop(kmi, "show_expanded", text="", emboss=False)
    row.prop(kmi, "active", text="", emboss=False)

    if km.is_modal:
        row.separator()
        row.prop(kmi, "propvalue", text="")
    else:
        row.label(text=kmi.name)

    row = split.row()
    row.prop(kmi, "map_type", text="")
    if map_type == 'KEYBOARD':
        row.prop(kmi, "type", text="", full_event=True)
    elif map_type == 'MOUSE':
        row.prop(kmi, "type", text="", full_event=True)
    elif map_type == 'NDOF':
        row.prop(kmi, "type", text="", full_event=True)
    elif map_type == 'TWEAK':
        subrow = row.row()
        subrow.prop(kmi, "type", text="")
        subrow.prop(kmi, "value", text="")
    elif map_type == 'TIMER':
        row.prop(kmi, "type", text="")
    else:
        row.label()

    if (not kmi.is_user_defined) and kmi.is_user_modified:
        row.operator("preferences.keyitem_restore", text="", icon='BACK').item_id = kmi.id
    else:
        row.operator(
            "preferences.keyitem_remove",
            text="",
            # Abusing the tracking icon, but it works pretty well here.
            icon=('TRACKING_CLEAR_BACKWARDS' if kmi.is_user_defined else 'X')
        ).item_id = kmi.id

    # Expanded, additional event settings
    if kmi.show_expanded:
        box = col.box()

        split = box.split(factor=0.4)
        sub = split.row()

        if km.is_modal:
            sub.prop(kmi, "propvalue", text="")
        else:
            # One day...
            # sub.prop_search(kmi, "idname", bpy.context.window_manager, "operators_all", text="")
            sub.prop(kmi, "idname", text="")

        if map_type not in {'TEXTINPUT', 'TIMER'}:
            sub = split.column()
            subrow = sub.row(align=True)

            if map_type == 'KEYBOARD':
                subrow.prop(kmi, "type", text="", event=True)
                subrow.prop(kmi, "value", text="")
                subrow_repeat = subrow.row(align=True)
                subrow_repeat.active = kmi.value in {'ANY', 'PRESS'}
                subrow_repeat.prop(kmi, "repeat", text="Repeat")
            elif map_type in {'MOUSE', 'NDOF'}:
                subrow.prop(kmi, "type", text="")
                subrow.prop(kmi, "value", text="")

            subrow = sub.row()
            subrow.scale_x = 0.75
            subrow.prop(kmi, "any", toggle=True)
            subrow.prop(kmi, "shift", toggle=True)
            subrow.prop(kmi, "ctrl", toggle=True)
            subrow.prop(kmi, "alt", toggle=True)
            subrow.prop(kmi, "oskey", text="Cmd", toggle=True)
            subrow.prop(kmi, "key_modifier", text="", event=True)

        # Operator properties
        box.template_keymap_item_properties(kmi)

        # Modal key maps attached to this operator
        if not km.is_modal:
            kmm = kc.keymaps.find_modal(kmi.idname)
            if kmm:
                draw_km(display_keymaps, kc, kmm, None, layout, level + 1)
                layout.context_pointer_set("keymap", km)

#def draw_shortcut(context, layout):
    #display_keymaps = keyconfig_merge(kc_user, kc_user)

def draw_item_box(context, row):
    prefs = context.preferences
    cm = prefs.custom_menu

    box_line = row.box()
    box_col = box_line.column(align=True)

    item_index = 0
    active = cm.item_name_get(context=cm.cm_context_selected, index=0, spacetype=cm.cm_space_selected)
    if active == "":
        box_col.label(text="none")
    #while active != "":
    #    box_col.label(text=active)
    #    item_index = item_index + 1
    #    active = cm.item_name_get(context=cm.cm_context_selected, index=item_index, spacetype=cm.cm_space_selected)
    box_col.prop(cm, "cm_item_selected", text="", expand=True)
    
    row = row.split(factor=0.9, align=True)
    col = row.column(align=True)

    col.operator("preferences.menuitem_add", text="", icon='ADD')
    col.operator("preferences.menuitem_remove", text="", icon='REMOVE')
    col.operator("wm.keyconfig_preset_add", text="", icon='TRIA_UP')
    col.operator("wm.keyconfig_preset_add", text="", icon='TRIA_DOWN').remove_active = True
    row.separator()

def draw_item_editor(context, row):
    prefs = context.preferences
    cm = prefs.custom_menu

    col = row.column()

    active = cm.item_name_get(context=cm.cm_context_selected, index=0, spacetype=cm.cm_space_selected)
    if (active == ""):
        col.label(text="No item in this list.")
        col.label(text="Add one or choose another list to get started")
    elif (cm.item_selected() >= 0):
        col.prop(cm, "cm_item_name")
    else :
        col.label(text="No item selected.")


def draw_custom_menu(context, layout):

    wm = context.window_manager
    kc_user = wm.keyconfigs.user
    kc_active = wm.keyconfigs.active
    spref = context.space_data

    prefs = context.preferences
    cm = prefs.custom_menu

    menu_name_active = None
    if not menu_name_active:
        menu_name_active = "Quick favourites"

    spacesub_name_active = None
    if not spacesub_name_active:
        spacesub_name_active = "Edit Mode (Mesh)"

    type_name_active = None
    if not type_name_active:
        type_name_active = "List"

    split = layout.split(factor=0.4)

    row = split.row()
    col = layout.column()

    rowsub = row.row(align=True)
    rowsub.menu("USERPREF_MT_menu_select", text=menu_name_active)
    rowsub.operator("wm.keyconfig_preset_add", text="", icon='ADD')
    rowsub.operator("wm.keyconfig_preset_add", text="", icon='REMOVE').remove_active = True

    rowsub = split.row(align=True)
    rowsub.prop(cm, "cm_space_selected", text="")

    rowsub = split.row(align=True)
    rowsub.prop(cm, "cm_context_selected", text="")

    rowsub = split.row(align=True)
    rowsub.operator("preferences.keyconfig_import", text="", icon='IMPORT')
    rowsub.operator("preferences.keyconfig_export", text="", icon='EXPORT')

    row = layout.row()
    col = layout.column()
    rowsub = row.split(factor=0.4, align=True)

    layout.separator()
    #rowsub = row.row(align=True)
    #rowsub.label(text="Display type :")
    #rowsub.menu("USERPREF_MT_keyconfigs", text=type_name_active)
    #rowsub = row.row(align=True)
    #rowsub.label(text="Shortcut :")
    #draw_shortcut(context, layout)

    col = layout.column()
    row = layout.row()
       
    draw_item_box(context=context, row=row)
    draw_item_editor(context=context, row=row)

    layout.separator()
