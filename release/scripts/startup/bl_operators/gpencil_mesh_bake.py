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

# <pep8-80 compliant>

import bpy
from bpy.types import Operator
from bpy.props import (
    IntProperty,
    FloatProperty,
    BoolProperty
)

class GPENCIL_OT_mesh_bake(Operator):
    """Bake all mesh animation into grease pencil strokes"""
    bl_idname = "gpencil.mesh_bake"
    bl_label = "Bake Mesh to Grease Pencil"
    bl_options = {'REGISTER', 'UNDO'}

    frame_start: IntProperty(
        name="Start Frame",
        description="Start frame for baking",
        min=0, max=300000,
        default=1,
    )
    frame_end: IntProperty(
        name="End Frame",
        description="End frame for baking",
        min=1, max=300000,
        default=250,
    )
    step: IntProperty(
        name="Frame Step",
        description="Frame Step",
        min=1, max=120,
        default=1,
    )
    thickness: IntProperty(
        name="Thickness",
        description="Thickness of the stroke lines",
        min=1, max=100,
        default=1,
    )
    seams: BoolProperty(
        name="Only Seam Edges",
        description="Convert only seam edges",
        default=False,
    )
    faces: BoolProperty(
        name="Export Faces",
        description="Export faces as filled strokes",
        default=True,
    )
    angle: FloatProperty(
        name="Threshold Angle",
        description="Threshold to determine ends of the strokes",
        min=0,
        max=+3.141592,
        default=+1.22173,  # 70 Degress
        subtype='ANGLE',
    )
    offset: FloatProperty(
        name="Offset",
        description="Offset strokes from fill",
        soft_min=0.0, soft_max=100.0,
        min=0.0, max=100.0,
        default=0.001,
        precision=3,
        step=1,
        subtype='DISTANCE',
        unit='LENGTH',
    )

    def execute(self, context):
        bpy.ops.gpencil.bake_mesh_animation(
            frame_start=self.frame_start,
            frame_end=self.frame_end,
            step=self.step,
            angle=self.angle,
            thickness=self.thickness,
            seams=self.seams,
            faces=self.faces,
            offset=self.offset
        )

        return {'FINISHED'}

    def invoke(self, context, _event):
        scene = context.scene
        self.frame_start = scene.frame_start
        self.frame_end = scene.frame_end

        wm = context.window_manager
        return wm.invoke_props_dialog(self)


classes = (
    GPENCIL_OT_mesh_bake,
)
