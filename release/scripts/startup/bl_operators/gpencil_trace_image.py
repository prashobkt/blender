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
    BoolProperty,
    EnumProperty,
)

gp_object_items = []


def my_objlist_callback(scene, context):
    gp_object_items.clear()
    gp_object_items.append(('*NEW', "New Object", ""))
    for o in context.scene.objects:
        if o.type == 'GPENCIL':
            gp_object_items.append((o.name, o.name, ""))

    return gp_object_items


class GPENCIL_OT_trace(Operator):
    bl_idname = "gpencil.trace"
    bl_label = "Trace Image to Grease Pencil"
    bl_options = {'REGISTER', 'UNDO'}

    target: EnumProperty(
        name="Target Object",
        description="Grease Pencil Object",
        items=my_objlist_callback
        )
    frame_target: IntProperty(
        name="Target Frame",
        description="Destination frame for the baked animation",
        min=1, max=300000,
        default=1,
    )
    thickness: IntProperty(
        name="Thickness",
        description="Thickness of the stroke lines",
        min=1, max=100,
        default=10,
    )
    resolution: IntProperty(
        name="Resolution",
        description="Resolution of the generated curves",
        min=1, max=20,
        default=5,
    )
    scale: FloatProperty(
        name="Scale",
        description="Scale of the final output",
        min=0.001,
        max=100.0,
        default=1.0,
    )
    sample: FloatProperty(
        name="Sample Distance",
        description="Determine distance between points",
        soft_min=0.001, soft_max=100.0,
        min=0.001, max=100.0,
        default=0.05,
        precision=3,
        step=1,
        subtype='DISTANCE',
        unit='LENGTH',
    )
    threshold: FloatProperty(
        name="Color Threshold",
        description="Determine what is considered white and what black",
        soft_min=0.0, soft_max=1.0,
        min=0.0, max=1.0,
        default=0.5,
        precision=3,
        step=1,
    )
    turnpolicy: EnumProperty(
        name="Turn Policy",
        description="Determines how to resolve ambiguities during decomposition of bitmaps into paths",
        items=(
            ("BLACK", "Black",  "prefers to connect black (foreground) components"),
            ("WHITE", "White", "Prefers to connect white (background) components"),
            ("LEFT", "Left", "Always take a left turn"),
            ("RIGHT", "Right", "Always take a right turn"),
            ("MINORITY", "Minority", "Prefers to connect the color (black or white) that occurs least frequently"),
            ("MAJORITY", "Majority", "Prefers to connect the color (black or white) that occurs most frequently"),
            ("RANDOM", "Random", "Choose pseudo-randomly.")
        ),
        default="MINORITY"
    )

    @classmethod
    def poll(self, context):
        return context.space_data.type == 'IMAGE_EDITOR'

    def execute(self, context):
        bpy.ops.gpencil.trace_image(
            target=self.target,
            frame_target=self.frame_target,
            thickness=self.thickness,
            resolution=self.resolution,
            scale=self.scale,
            sample=self.sample,
            threshold=self.threshold,
            turnpolicy=self.turnpolicy
        )

        return {'FINISHED'}

    def invoke(self, context, _event):
        scene = context.scene
        self.frame_target = scene.frame_current

        wm = context.window_manager
        return wm.invoke_props_dialog(self)


classes = (
    GPENCIL_OT_trace,
)
