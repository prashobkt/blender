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
from bpy import data
from mathutils import Vector


class CollectionButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "collection"
    # COMPAT_ENGINES must be defined in each subclass, external engines can add themselves here

    @classmethod
    def poll(cls, context):
        return (context.engine in cls.COMPAT_ENGINES)

class COLLECTION_PT_collection_flags(CollectionButtonsPanel, Panel):
    bl_label = "Collection Flags"

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

def is_unit_transformation(ob):
    if ob.scale.xyz==Vector((1,1,1)) and ob.location.xyz==Vector((0,0,0)) and \
        ob.rotation_euler.x == 0.0 and ob.rotation_euler.y == 0.0 and ob.rotation_euler.z == 0.0:
        return True
    return False


class COLLECTION_PT_lanpr_collection(CollectionButtonsPanel, Panel):
    bl_label = "Collection LANPR"

    @classmethod
    def poll(cls, context):
        return context.scene.render.engine == 'BLENDER_LANPR' or context.scene.lanpr.enabled

    def draw(self,context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False
        collection = context.collection
        lanpr = collection.lanpr
        row = layout.row()
        row.prop(lanpr,"usage")
        if lanpr.usage!='INCLUDE':
            layout.prop(lanpr,"force")
        else:
            layout.prop(lanpr,"target")
            
            if lanpr.target:

                if not is_unit_transformation(lanpr.target):
                    layout.label(text = "Target GP has self transformations.")
                    layout.operator("lanpr.reset_object_transfromations").obj=lanpr.target.name

                layout.prop(lanpr,'use_multiple_levels', text="Multiple Levels")
                
                if lanpr.use_multiple_levels:
                    col = layout.column(align=True)
                    col.prop(lanpr,'level_start',text="Level Begin")
                    col.prop(lanpr,'level_end',text="End")
                else:
                    layout.prop(lanpr,'level_start',text="Level")

                layout.prop(lanpr,'use_contour')
                layout.prop(lanpr,'use_crease')
                layout.prop(lanpr,'enable_mark')
                layout.prop(lanpr,'use_material')
                layout.prop(lanpr,'use_intersection')
                
                layout.prop(lanpr,'layer')
                layout.prop(lanpr,'material')


classes = (
    COLLECTION_PT_collection_flags,
    COLLECTION_PT_lanpr_collection,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
