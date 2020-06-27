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

class CollectionButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "collection"
    COMPAT_ENGINES = { 'BLENDER_EEVEE', 'BLENDER_WORKBENCH', 'CYCLES' }

    @classmethod
    def poll(cls, context):
        return (context.engine in cls.COMPAT_ENGINES)


def lineart_make_line_type_entry(col, line_type, text_disp, expand, search_from):
    col.prop(line_type, "use", text=text_disp)
    if line_type.use and expand:
        col.prop_search(line_type, "layer", search_from, "layers", icon='GREASEPENCIL')
        col.prop_search(line_type, "material",  search_from, "materials", icon='SHADING_TEXTURE')

class COLLECTION_PT_collection_flags(CollectionButtonsPanel, Panel):
    bl_label = "Collection Flags"
    COMPAT_ENGINES = { 'BLENDER_EEVEE', 'BLENDER_WORKBENCH', 'CYCLES' }

    def draw(self, context):
        layout=self.layout
        collection=context.collection
        vl = context.view_layer
        vlc = vl.active_layer_collection
        if vlc.name == 'Master Collection':
            row = layout.row()
            row.label(text="This is the master collection")
            return
        
        row = layout.row()
        col = row.column(align=True)
        col.prop(vlc,"hide_viewport")
        col.prop(vlc,"holdout")
        col.prop(vlc,"indirect_only")
        row = layout.row()
        col = row.column(align=True)
        col.prop(collection,"hide_select")
        col.prop(collection,"hide_viewport")
        col.prop(collection,"hide_render")

class COLLECTION_PT_lineart_collection(CollectionButtonsPanel, Panel):
    bl_label = "Collection LANPR"
    COMPAT_ENGINES =  { 'BLENDER_EEVEE', 'BLENDER_WORKBENCH', 'CYCLES' }

    @classmethod
    def poll(cls, context):
        return context.scene.lineart.enabled

    def draw_header(self, context):
        layout = self.layout
        collection = context.collection
        layout.prop(collection, "configure_lineart", text="")

    def draw(self,context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False
        collection = context.collection
        if not collection.configure_lineart:
            return
        
        lineart = collection.lineart
        row = layout.row()
        row.prop(lineart,"usage")
        if lineart.usage!='INCLUDE':
            layout.prop(lineart,"force")
        else:
            layout.prop(lineart,"target")
            
            if lineart.target:

                layout.prop(lineart,'use_multiple_levels', text="Multiple Levels")
                
                if lineart.use_multiple_levels:
                    col = layout.column(align=True)
                    col.prop(lineart,'level_start',text="Level Begin")
                    col.prop(lineart,'level_end',text="End")
                else:
                    layout.prop(lineart,'level_start',text="Level")
                
                layout.prop(lineart, "use_same_style")

                if lineart.use_same_style:
                    layout.prop_search(lineart, 'target_layer', lineart.target.data, "layers", icon='GREASEPENCIL')
                    layout.prop_search(lineart, 'target_material', lineart.target.data, "materials", icon='SHADING_TEXTURE')

                expand = not lineart.use_same_style
                lineart_make_line_type_entry(layout, lineart.contour, "Contour", expand, lineart.target.data)
                lineart_make_line_type_entry(layout, lineart.crease, "Crease", expand, lineart.target.data)
                lineart_make_line_type_entry(layout, lineart.material, "Material", expand, lineart.target.data)
                lineart_make_line_type_entry(layout, lineart.edge_mark, "Edge Mark", expand, lineart.target.data)
                lineart_make_line_type_entry(layout, lineart.intersection, "Intersection", expand, lineart.target.data)

classes = (
    COLLECTION_PT_collection_flags,
    COLLECTION_PT_lineart_collection,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
