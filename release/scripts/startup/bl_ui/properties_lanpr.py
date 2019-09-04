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


class LanprButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "lanpr"
    # COMPAT_ENGINES must be defined in each subclass, external engines can add themselves here

    @classmethod
    def poll(cls, context):
        return True

def find_feature_line_modifier(ob):
    for md in ob.modifiers:
        if md.type=='FEATURE_LINE':
            return md
    return None

def is_unit_transformation(ob):
    if ob.scale.xyz==Vector((1,1,1)) and ob.location.xyz==Vector((0,0,0)) and \
        ob.rotation_euler.x == 0.0 and ob.rotation_euler.y == 0.0 and ob.rotation_euler.z == 0.0:
        return True
    return False

class OBJECT_PT_lanpr_settings(LanprButtonsPanel, Panel):
    bl_label = "Feature Line Modifier"

    @classmethod
    def poll(cls, context):
        return context.scene.render.engine == 'BLENDER_LANPR' or context.scene.lanpr.enabled

    def draw(self,context):
        collection = context.collection
        lanpr = collection.lanpr
        ob = context.object
        md = find_feature_line_modifier(ob)

        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        if not md:
            layout.label(text="No feature line modifier for this object.")
            return
        
        layout.prop(md,'use_multiple_levels', text="Multiple Levels")
        if md.use_multiple_levels:
            col = layout.column(align=True)
            col.prop(md,'level_start')
            col.prop(md,'level_end', text="End")
        else:
            layout.prop(md,'level_start', text="Level")

        layout.prop(md,'use_contour')
        layout.prop(md,'use_crease')
        layout.prop(md,'enable_mark')
        layout.prop(md,'use_material')
        layout.prop(md,'use_intersection')
        layout.prop(md,'enable_modifier_mark')

class OBJECT_PT_lanpr_modifier_target(LanprButtonsPanel, Panel):
    bl_label = "Grease Pencil"
    bl_parent_id = "OBJECT_PT_lanpr_settings"
    COMPAT_ENGINES = {'BLENDER_RENDER', 'BLENDER_LANPR', 'BLENDER_OPENGL', 'BLENDER_EEVEE'}

    @classmethod
    def poll(cls, context):
        scene = context.scene
        lanpr = scene.lanpr
        ob = context.object
        return (scene.render.engine=="BLENDER_LANPR" or lanpr.enabled) and find_feature_line_modifier(ob)

    def draw(self, context):
        lanpr = context.scene.lanpr
        ob = context.object
        md = find_feature_line_modifier(ob)

        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        layout.prop(md, "target")
        
        if md.target:
            if not is_unit_transformation(md.target):
                layout.label(text = "Target GP has self transformations.")
                layout.operator("lanpr.reset_object_transfromations").obj=md.target.name
            layout.prop(md,'layer')
            layout.prop(md,'material')

class OBJECT_PT_lanpr(LanprButtonsPanel, Panel):
    bl_label = "Usage"

    @classmethod
    def poll(cls, context):
        return context.scene.render.engine == 'BLENDER_LANPR' or context.scene.lanpr.enabled

    def draw(self, context):
        layout=self.layout
        lanpr = context.object.lanpr
        if context.object.type == 'MESH':
            layout.prop(lanpr,'usage')


classes = (
    OBJECT_PT_lanpr,
    OBJECT_PT_lanpr_settings,
    OBJECT_PT_lanpr_modifier_target,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
