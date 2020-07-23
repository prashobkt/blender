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
    "draw_user_menus",
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

def get_keymap(context, index):
    prefs = context.preferences
    um = prefs.user_menus

    if index < 0:
        index = menu_id(context, um.active_group)

    for km in context.window_manager.keyconfigs.user.keymaps:
        for kmi in km.keymap_items:
            if kmi.idname == "wm.call_user_menu":
                if kmi.properties.index == index:
                    return kmi
    km = context.window_manager.keyconfigs.user.keymaps['Window']
    kmi = km.keymap_items.new("wm.call_user_menu",'RIGHTMOUSE', 'PRESS', shift=True, ctrl=True, alt=True)
    kmi.properties.index = index
    kmi.active = True
    return kmi

def draw_button(context, box, item):
    prefs = context.preferences
    um = prefs.user_menus

    name = item.name
    if name == "":
        name = " "
    item.is_selected = item == um.active_item
    col = box.column(align=True)
    row = col.row(align=True)
    if item.type == "SEPARATOR":
        name = "___________"
    row.prop(item, "is_selected", text=name, toggle=1)
    if item.type == "SUBMENU":
        sm = item.get_submenu()
        if um.active_group.is_pie:
            row.operator("preferences.menuitem_add", text="", icon='ADD')
            row.operator("preferences.menuitem_remove", text="", icon='REMOVE')
            row.operator("preferences.menuitem_up", text="", icon='TRIA_UP')
            row.operator("preferences.menuitem_down", text="", icon='TRIA_DOWN')
        sub_box = col.box()
        sub_box = sub_box.column(align=True)
        draw_item(context, sub_box, sm.items_list)

def draw_item(context, box, items):
    for umi in items:
        draw_button(context, box, umi)

def draw_item_box(context, row):
    prefs = context.preferences
    um = prefs.user_menus

    box_line = row.box()
    box_col = box_line.column(align=True)

    has_item = um.has_item()
    if not has_item:
        box_col.label(text="none")
    else:
        draw_item(context, box_col, um.get_current_menu().menu_items)

    row = row.split(factor=0.9, align=True)
    col = row.column(align=True)

    col.operator("preferences.menuitem_add", text="", icon='ADD')
    col.operator("preferences.menuitem_remove", text="", icon='REMOVE')
    col.operator("preferences.menuitem_up", text="", icon='TRIA_UP')
    col.operator("preferences.menuitem_down", text="", icon='TRIA_DOWN')
    row.separator()

def draw_pie_item(context, col, items, label):
    prefs = context.preferences
    um = prefs.user_menus

    row = col.row()
    #subrow = row.split(factor=0.1)
    row.label(text=label)
    draw_button(context, row, items)

def draw_pie(context, row):
    prefs = context.preferences
    um = prefs.user_menus

    cm = um.get_current_menu()
    if not cm:
        return
    col = row.column()
    draw_pie_item(context, col, cm.menu_items[0], "Left : ")
    draw_pie_item(context, col, cm.menu_items[1], "Right : ")
    draw_pie_item(context, col, cm.menu_items[2], "Down : ")
    draw_pie_item(context, col, cm.menu_items[3], "Up : ")
    draw_pie_item(context, col, cm.menu_items[4], "Upper left : ")
    draw_pie_item(context, col, cm.menu_items[5], "Upper right : ")
    draw_pie_item(context, col, cm.menu_items[6], "Lower left : ")
    draw_pie_item(context, col, cm.menu_items[7], "Lower right : ")
    row.separator()
        

def draw_item_editor(context, row):
    prefs = context.preferences
    um = prefs.user_menus

    col = row.column()

    has_item = um.has_item()
    current = um.active_item
    if not has_item:
        col.label(text="No item in this list.")
        col.label(text="Add one or choose another list to get started")
    elif current:
        col.prop(current, "type")
        if (current.type != "SEPARATOR"):
            col.prop(current, "name")
        if (current.type == "OPERATOR"):
            umi_op = current.get_operator()
            col.prop(umi_op, "operator")
        if (current.type == "MENU"):
            umi_pm = current.get_menu()
            col.prop(umi_pm, "id_name", text="ID name")
    else:
        col.label(text="No item selected.")

def draw_user_menu_preference_expanded(context, layout):
    prefs = context.preferences
    um = prefs.user_menus
    umg = um.active_group
    kmi = umg.keymap

    layout.prop(kmi, "idname", text="")
    layout.prop(kmi.properties, "index", text="")


def draw_user_menu_preference(context, layout):
    prefs = context.preferences
    um = prefs.user_menus
    umg = um.active_group
    kmi = get_keymap(context, -1)

    col = _indented_layout(layout, 0)
    row = col.row()

    row.prop(um, "expanded", text="", emboss=False)

    row.prop(umg, "name")
    pie_text = "List"
    if umg.is_pie:
        pie_text = "Pie"
    row.prop(umg, "is_pie", text=pie_text, toggle=True)

    row.prop(kmi, "map_type", text="")
    map_type = kmi.map_type
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

    if um.expanded:
        box = col.box()
        draw_user_menu_preference_expanded(context=context, layout=box)


def menu_id(context, umg):
    prefs = context.preferences
    um = prefs.user_menus

    i = 0
    for item in um.menus:
        if item == umg:
            return i
        i = i + 1
    return -1

    
def ensure_keymap(context):
    prefs = context.preferences
    um = prefs.user_menus
    umg = um.active_group
    index = menu_id(context, umg)

    for km in context.window_manager.keyconfigs.user.keymaps:
        for kmi in km.keymap_items:
            if kmi.idname == "wm.call_user_menu":
                if kmi.properties.index == index:
                    umg.set_keymap(kmi=kmi)
    if not umg.keymap:
        km = context.window_manager.keyconfigs.user.keymaps['Window']
        kmi = km.keymap_items.new("wm.call_user_menu",'RIGHTMOUSE', 'PRESS', shift=True, ctrl=True, alt=True)
        kmi.properties.index = index
        kmi.active = True
        umg.set_keymap(kmi=kmi)


def draw_user_menus(context, layout):
    prefs = context.preferences
    um = prefs.user_menus

    if not um.active_group:
        um.active_group = um.menus[0]
    if not um.active_group.keymap:
        ensure_keymap(context)

    split = layout.split(factor=0.4)

    row = split.row()
    col = layout.column()

    rowsub = row.row(align=True)
    rowsub.menu("USERPREF_MT_menu_select", text=um.active_group.name)
    rowsub.operator("preferences.usermenus_add", text="", icon='ADD')
    rowsub.operator("preferences.usermenus_remove", text="", icon='REMOVE')

    rowsub = split.row(align=True)
    rowsub.prop(um, "space_selected", text="")

    rowsub = split.row(align=True)
    rowsub.prop(um, "context_selected", text="")

    #rowsub = split.row(align=True)
    #rowsub.operator("preferences.keyconfig_import", text="", icon='IMPORT')
    #rowsub.operator("preferences.keyconfig_export", text="", icon='EXPORT')

    row = layout.row()
    col = layout.column()

    layout.separator()
    draw_user_menu_preference(context=context, layout=row)

    col = layout.column()
    row = layout.row()

    if um.active_group.is_pie:
        draw_pie(context=context, row=row)
    else:
        draw_item_box(context=context, row=row)
    draw_item_editor(context=context, row=row)

    layout.separator()

