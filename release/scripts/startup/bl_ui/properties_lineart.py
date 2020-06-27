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
from bpy.types import Panel

class LanprButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "lineart"

def lineart_make_line_type_entry(col, line_type, text_disp, expand, search_from):
    col.prop(line_type, "use", text=text_disp)
    if line_type.use and expand:
        col.prop_search(line_type, "layer", search_from, "layers", icon='GREASEPENCIL')
        col.prop_search(line_type, "material",  search_from, "materials", icon='SHADING_TEXTURE')

class OBJECT_PT_lineart_settings(LanprButtonsPanel, Panel):
    bl_label = "LRT settings"

    @classmethod
    def poll(cls, context):
        ob = context.object
        obl = ob.lineart
        return context.scene.lineart.enabled and\
            obl.usage == 'INCLUDE' and obl.target

    def draw(self,context):
        collection = context.collection
        lineart = collection.lineart
        ob = context.object
        obl = ob.lineart

        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False
        
        layout.prop(obl,'use_multiple_levels', text="Multiple Levels")
        if obl.use_multiple_levels:
            col = layout.column(align=True)
            col.prop(obl,'level_start')
            col.prop(obl,'level_end', text="End")
        else:
            layout.prop(obl,'level_start', text="Level")
        
        layout.prop(obl,'use_same_style')
        if obl.use_same_style:
            layout.prop_search(obl, 'target_layer', obl.target.data, "layers", icon='GREASEPENCIL')
            layout.prop_search(obl, 'target_material', obl.target.data, "materials", icon='SHADING_TEXTURE')
        
        expand = not obl.use_same_style
        lineart_make_line_type_entry(layout, obl.contour, "Contour", expand, obl.target.data)
        lineart_make_line_type_entry(layout, obl.crease, "Crease", expand, obl.target.data)
        lineart_make_line_type_entry(layout, obl.material, "Material", expand, obl.target.data)
        lineart_make_line_type_entry(layout, obl.edge_mark, "Edge Mark", expand, obl.target.data)


class OBJECT_PT_lineart(LanprButtonsPanel, Panel):
    bl_label = "Usage"

    @classmethod
    def poll(cls, context):
        return context.scene.lineart.enabled

    def draw(self, context):
        layout=self.layout
        lineart = context.object.lineart
        if context.object.type == 'MESH':
            layout.prop(lineart,'usage')
            if lineart.usage == 'INCLUDE':
                layout.prop(lineart, "target")


classes = (
    OBJECT_PT_lineart,
    OBJECT_PT_lineart_settings,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
