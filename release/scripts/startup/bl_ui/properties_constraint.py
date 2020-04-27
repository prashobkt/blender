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


class OBJECT_PT_constraints(Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_label = "Object Constraints"
    bl_context = "constraint"
    bl_options = {'LIST_START', 'HIDE_HEADER'}

    @classmethod
    def poll(cls, context):
        return (context.object)

    def draw(self, context):
        layout = self.layout

        layout.operator_menu_enum("object.constraint_add", "type", text="Add Object Constraint")

        layout.template_constraints()


class BONE_PT_constraints(Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_label = "Bone Constraints"
    bl_context = "bone_constraint"
    bl_options = {'LIST_START', 'HIDE_HEADER'}

    @classmethod
    def poll(cls, context):
        return (context.pose_bone)

    def draw(self, context):
        layout = self.layout

        layout.operator_menu_enum("pose.constraint_add", "type", text="Add Bone Constraint")

        layout.template_constraints()


class ConstraintButtonsPanel(Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_label = ""
    bl_context = "constraint"
    bl_options = {'LIST', 'HEADER_LAYOUT_EXPAND'}

    @staticmethod
    def draw_influence(layout, con):
        layout.separator()
        if con.type in {'IK', 'SPLINE_IK'}:
            # constraint.disable_keep_transform doesn't work well
            # for these constraints.
            layout.prop(con, "influence")
        else:
            row = layout.row(align=True)
            row.prop(con, "influence")
            row.operator("constraint.disable_keep_transform", text="", icon='CANCEL')


    @staticmethod
    def space_template(layout, con, target=True, owner=True):
        if target or owner:
            layout.separator()
            if target:
                layout.prop(con, "target_space", text="Target")
            if owner:
                layout.prop(con, "owner_space", text="Owner")

    @staticmethod
    def target_template(layout, con, subtargets=True):
        layout.prop(con, "target")  # XXX limiting settings for only 'curves' or some type of object

        if con.target and subtargets:
            if con.target.type == 'ARMATURE':
                layout.prop_search(con, "subtarget", con.target.data, "bones", text="Bone")

                if hasattr(con, "head_tail"):
                    row = layout.row(align=True)
                    row.label(text="Head/Tail:")
                    row.prop(con, "head_tail", text="")
                    # XXX icon, and only when bone has segments?
                    row.prop(con, "use_bbone_shape", text="", icon='IPO_BEZIER')
            elif con.target.type in {'MESH', 'LATTICE'}:
                layout.prop_search(con, "subtarget", con.target, "vertex_groups", text="Vertex Group")

    @staticmethod
    def ik_template(layout, con):
        # only used for iTaSC
        layout.prop(con, "pole_target")

        if con.pole_target and con.pole_target.type == 'ARMATURE':
            layout.prop_search(con, "pole_subtarget", con.pole_target.data, "bones", text="Bone")

        if con.pole_target:
            row = layout.row()
            row.label()
            row.prop(con, "pole_angle")

        split = layout.split(factor=0.33)
        col = split.column()
        col.prop(con, "use_tail")
        col.prop(con, "use_stretch")

        col = split.column()
        col.prop(con, "chain_count")

    def get_constraint(self, context):
        con = None
        if context.pose_bone:
            con = context.bose_bone.constraints[self.list_panel_index]
        else:
            con = context.active_object.constraints[self.list_panel_index]
        self.layout.context_pointer_set("constraint", con)
        return con

    def draw_header(self, context):
        layout = self.layout
        con = self.get_constraint(context)

        layout.template_constraint_header(con)


class ConstraintButtonsSubPanel(Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_label = ""
    bl_context = "constraint"
    bl_options = {'LIST_SUBPANEL'}

    def get_constraint(self, context):
        con = None
        if context.pose_bone:
            con = context.bose_bone.constraints[self.list_panel_index]
        else:
            con = context.active_object.constraints[self.list_panel_index]
        self.layout.context_pointer_set("constraint", con)
        return con


class OBJECT_PT_bChildOfConstraint(ConstraintButtonsPanel):
    bl_context = "constraint"

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True
        
        self.target_template(layout, con)

        row = layout.row(heading="Location")
        row.prop(con, "use_location_x", text="X", toggle=True)
        row.prop(con, "use_location_y", text="Y", toggle=True)
        row.prop(con, "use_location_z", text="Z", toggle=True)
        row = layout.row(heading="Rotation")
        row.prop(con, "use_rotation_x", text="X", toggle=True)
        row.prop(con, "use_rotation_y", text="Y", toggle=True)
        row.prop(con, "use_rotation_z", text="Z", toggle=True)
        row = layout.row(heading="Scale")
        row.prop(con, "use_scale_x", text="X", toggle=True)
        row.prop(con, "use_scale_y", text="Y", toggle=True)
        row.prop(con, "use_scale_z", text="Z", toggle=True)

        row = layout.row()
        row.operator("constraint.childof_set_inverse")
        row.operator("constraint.childof_clear_inverse")

        self.draw_influence(layout, con)


class OBJECT_PT_bTrackToConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        layout.prop(con, "track_axis", expand=True)
        layout.prop(con, "up_axis", text="Up", expand=True)
        layout.prop(con, "use_target_z")

        self.space_template(layout, con)

        self.draw_influence(layout, con)

# No option to add this in the UI ?
class OBJECT_PT_bKinematicConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)

        if context.object.pose.ik_solver == 'ITASC':
            layout.prop(con, "ik_type")
            getattr(self, 'IK_' + con.ik_type)(context, layout, con)
        else:
            # Standard IK constraint
            self.target_template(layout, con)
            layout.prop(con, "pole_target")

            if con.pole_target and con.pole_target.type == 'ARMATURE':
                layout.prop_search(con, "pole_subtarget", con.pole_target.data, "bones", text="Bone")

            if con.pole_target:
                row = layout.row()
                row.prop(con, "pole_angle")
                row.label()

            split = layout.split()
            col = split.column()
            col.prop(con, "iterations")
            col.prop(con, "chain_count")

            col = split.column()
            col.prop(con, "use_tail")
            col.prop(con, "use_stretch")

            layout.label(text="Weight:")

            split = layout.split()
            col = split.column()
            row = col.row(align=True)
            row.prop(con, "use_location", text="")
            sub = row.row(align=True)
            sub.active = con.use_location
            sub.prop(con, "weight", text="Position", slider=True)

            col = split.column()
            row = col.row(align=True)
            row.prop(con, "use_rotation", text="")
            sub = row.row(align=True)
            sub.active = con.use_rotation
            sub.prop(con, "orient_weight", text="Rotation", slider=True)

            self.draw_influence(layout, con)


# Unreachable in UI
class OBJECT_PT_constraints_ik_copy_pose(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        
        self.target_template(layout, con)
        self.ik_template(layout, con)

        row = layout.row()
        row.label(text="Axis Ref:")
        row.prop(con, "reference_axis", expand=True)
        split = layout.split(factor=0.33)
        split.row().prop(con, "use_location")
        row = split.row()
        row.prop(con, "weight", text="Weight", slider=True)
        row.active = con.use_location
        split = layout.split(factor=0.33)
        row = split.row()
        row.label(text="Lock:")
        row = split.row()
        row.prop(con, "lock_location_x", text="X")
        row.prop(con, "lock_location_y", text="Y")
        row.prop(con, "lock_location_z", text="Z")
        split.active = con.use_location

        split = layout.split(factor=0.33)
        split.row().prop(con, "use_rotation")
        row = split.row()
        row.prop(con, "orient_weight", text="Weight", slider=True)
        row.active = con.use_rotation
        split = layout.split(factor=0.33)
        row = split.row()
        row.label(text="Lock:")
        row = split.row()
        row.prop(con, "lock_rotation_x", text="X")
        row.prop(con, "lock_rotation_y", text="Y")
        row.prop(con, "lock_rotation_z", text="Z")
        split.active = con.use_rotation

        self.draw_influence(layout, con)


# Not currently accessible in UI
class OBJECT_PT_constraints_ik_distance(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)

        self.target_template(layout, con)
        self.ik_template(layout, con)

        layout.prop(con, "limit_mode")

        row = layout.row()
        row.prop(con, "weight", text="Weight", slider=True)
        row.prop(con, "distance", text="Distance", slider=True)

        self.draw_influence(layout, con)


class OBJECT_PT_bFollowPathConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        layout.prop(con, "use_curve_follow")
        layout.prop(con, "use_curve_radius")

        layout.prop(con, "use_fixed_location")
        if con.use_fixed_location:
            layout.prop(con, "offset_factor", text="Offset")
        else:
            layout.prop(con, "offset")

        layout.prop(con, "forward_axis", expand=True)
        layout.prop(con, "up_axis", expand=True, text="Up")

        self.target_template(layout, con)
        layout.operator("constraint.followpath_path_animate", text="Animate Path", icon='ANIM_DATA')

        self.draw_influence(layout, con)


class OBJECT_PT_bRotLimitConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        row = layout.row(heading="Limit X", align=True)
        row.prop(con, "use_limit_x", text="")
        sub = row.column(align=True)
        sub.active = con.use_limit_x
        sub.prop(con, "min_x", text="Min")
        sub.prop(con, "max_x", text="Max")

        row = layout.row(heading="Y", align=True)
        row.prop(con, "use_limit_y", text="")
        sub = row.column(align=True)
        sub.active = con.use_limit_y
        sub.prop(con, "min_y", text="Min")
        sub.prop(con, "max_y", text="Max")

        row = layout.row(heading="Z", align=True)
        row.prop(con, "use_limit_z", text="")
        sub = row.column(align=True)
        sub.active = con.use_limit_z
        sub.prop(con, "min_z", text="Min")
        sub.prop(con, "max_z", text="Max")

        layout.prop(con, "use_transform_limit")
        layout.prop(con, "owner_space")

        self.draw_influence(layout, con)


class OBJECT_PT_bLocLimitConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        row = layout.row(heading="Minimum X", align=True)
        row.prop(con, "use_min_x", text="")
        sub = row.row()
        sub.active = con.use_min_x
        sub.prop(con, "min_x", text="")

        row = layout.row(heading="Y", align=True)
        row.prop(con, "use_min_y", text="")
        sub = row.row()
        sub.active = con.use_min_y
        sub.prop(con, "min_y", text="")

        row = layout.row(heading="Z", align=True)
        row.prop(con, "use_min_z", text="")
        sub = row.row()
        sub.active = con.use_min_z
        sub.prop(con, "min_z", text="")

        layout.separator()

        row = layout.row(heading="Maximum X", align=True)
        row.prop(con, "use_max_x", text="")
        sub = row.row()
        sub.active = con.use_max_x
        sub.prop(con, "max_x", text="")

        row = layout.row(heading="Y", align=True)
        row.prop(con, "use_max_y", text="")
        sub = row.row()
        sub.active = con.use_max_y
        sub.prop(con, "max_y", text="")

        row = layout.row(heading="Z", align=True)
        row.prop(con, "use_max_z", text="")
        sub = row.row()
        sub.active = con.use_max_z
        sub.prop(con, "max_z", text="")

        layout.prop(con, "use_transform_limit")
        layout.prop(con, "owner_space")

        self.draw_influence(layout, con)


class OBJECT_PT_bSizeLimitConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        row = layout.row(heading="Minimum X", align=True)
        row.prop(con, "use_min_x", text="")
        sub = row.row()
        sub.active = con.use_min_x
        sub.prop(con, "min_x", text="")

        row = layout.row(heading="Y", align=True)
        row.prop(con, "use_min_y", text="")
        sub = row.row()
        sub.active = con.use_min_y
        sub.prop(con, "min_y", text="")

        row = layout.row(heading="Z", align=True)
        row.prop(con, "use_min_z", text="")
        sub = row.row()
        sub.active = con.use_min_z
        sub.prop(con, "min_z", text="")

        layout.separator()

        row = layout.row(heading="Maximum X", align=True)
        row.prop(con, "use_max_x", text="")
        sub = row.row()
        sub.active = con.use_max_x
        sub.prop(con, "max_x", text="")

        row = layout.row(heading="Y", align=True)
        row.prop(con, "use_max_y", text="")
        sub = row.row()
        sub.active = con.use_max_y
        sub.prop(con, "max_y", text="")

        row = layout.row(heading="Z", align=True)
        row.prop(con, "use_max_z", text="")
        sub = row.row()
        sub.active = con.use_max_z
        sub.prop(con, "max_z", text="")

        layout.prop(con, "use_transform_limit")
        layout.prop(con, "owner_space")

        self.draw_influence(layout, con)


class OBJECT_PT_bRotateLikeConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        layout.prop(con, "euler_order", text="Order")

        row = layout.row(heading="Axis")
        row.prop(con, "use_x", text="X", toggle=True)
        row.prop(con, "use_y", text="Y", toggle=True)
        row.prop(con, "use_z", text="Z", toggle=True)

        row = layout.row(heading="Invert")
        row.prop(con, "invert_x", text="X", toggle=True)
        row.prop(con, "invert_y", text="Y", toggle=True)
        row.prop(con, "invert_z", text="Z", toggle=True)

        layout.prop(con, "mix_mode", text="Mix")

        self.space_template(layout, con)

        self.draw_influence(layout, con)


class OBJECT_PT_bLocateLikeConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        row = layout.row(heading="Axis")
        row.prop(con, "use_x", text="X", toggle=True)
        row.prop(con, "use_y", text="Y", toggle=True)
        row.prop(con, "use_z", text="Z", toggle=True)

        row = layout.row(heading="Invert")
        row.prop(con, "invert_x", text="X", toggle=True)
        row.prop(con, "invert_y", text="Y", toggle=True)
        row.prop(con, "invert_z", text="Z", toggle=True)

        layout.prop(con, "use_offset")

        self.space_template(layout, con)

        self.draw_influence(layout, con)


class OBJECT_PT_bSizeLikeConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        row = layout.row(heading="Axis")
        row.prop(con, "use_x", text="X", toggle=True)
        row.prop(con, "use_y", text="Y", toggle=True)
        row.prop(con, "use_z", text="Z", toggle=True)

        layout.prop(con, "power")
        layout.prop(con, "use_make_uniform")

        layout.prop(con, "use_offset")
        row = layout.row()
        row.active = con.use_offset
        row.prop(con, "use_add")

        self.space_template(layout, con)

        self.draw_influence(layout, con)


class OBJECT_PT_bSameVolumeConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        layout.prop(con, "mode")

        row = layout.row(heading="Free Axis")
        row.prop(con, "free_axis", expand=True)

        layout.prop(con, "volume")

        layout.prop(con, "owner_space")

        self.draw_influence(layout, con)


class OBJECT_PT_bTransLikeConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        layout.prop(con, "mix_mode", text="Mix")

        self.space_template(layout, con)

        self.draw_influence(layout, con)


class OBJECT_PT_bActionConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True
        
        self.target_template(layout, con)

        layout.label(text="From Target:")
        layout.prop(con, "transform_channel", text="Channel")
        layout.prop(con, "target_space")

        col = layout.column(align=True)
        col.prop(con, "min", text="Range Min")
        col.prop(con, "max", text="Max")

        layout.label(text="To Action:")
        layout.prop(con, "action")
        layout.prop(con, "use_bone_object_action")

        col = layout.column(align=True)
        col.prop(con, "frame_start", text="Frame Start")
        col.prop(con, "frame_end", text="End")

        layout.prop(con, "mix_mode", text="Mix")

        self.draw_influence(layout, con)


class OBJECT_PT_bLockTrackConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        layout.prop(con, "track_axis", expand=True)
        layout.prop(con, "lock_axis", expand=True)

        self.draw_influence(layout, con)


class OBJECT_PT_bDistLimitConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        col = layout.column(align=True)
        col.prop(con, "distance")
        col.operator("constraint.limitdistance_reset")

        layout.prop(con, "limit_mode", text="Clamp Region")

        layout.prop(con, "use_transform_limit")

        self.space_template(layout, con)

        self.draw_influence(layout, con)


class OBJECT_PT_bStretchToConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        col = layout.column()
        col.prop(con, "rest_length")
        col.operator("constraint.stretchto_reset", text="Reset Length")

        layout.prop(con, "bulge", text="Volume Variation")

        row = layout.row(heading="Volume Min", align=True)
        row.prop(con, "use_bulge_min", text="")
        sub = row.row(align=True)
        sub.active = con.use_bulge_min
        sub.prop(con, "bulge_min", text="")

        row = layout.row(heading="Max", align=True)
        row.prop(con, "use_bulge_max", text="")
        sub = row.row(align=True)
        sub.active = con.use_bulge_max
        sub.prop(con, "bulge_max", text="")
        
        row = layout.row()
        row.active = con.use_bulge_min or con.use_bulge_max
        row.prop(con, "bulge_smooth", text="Smooth")

        layout.prop(con, "volume", expand=True)
        layout.prop(con, "keep_axis", text="Rotation", expand=True)

        self.draw_influence(layout, con)


class OBJECT_PT_bMinMaxConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        layout.prop(con, "use_rotation")
        layout.prop(con, "offset")
        layout.prop(con, "floor_location", expand=True, text="Min/Max")

        self.space_template(layout, con)

        self.draw_influence(layout, con)


class OBJECT_PT_constraints_rigid_body_join(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)

        self.target_template(layout, con, subtargets=False)

        layout.prop(con, "pivot_type")
        layout.prop(con, "child")

        row = layout.row()
        row.prop(con, "use_linked_collision", text="Linked Collision")
        row.prop(con, "show_pivot", text="Display Pivot")

        split = layout.split()

        col = split.column(align=True)
        col.label(text="Pivot:")
        col.prop(con, "pivot_x", text="X")
        col.prop(con, "pivot_y", text="Y")
        col.prop(con, "pivot_z", text="Z")

        col = split.column(align=True)
        col.label(text="Axis:")
        col.prop(con, "axis_x", text="X")
        col.prop(con, "axis_y", text="Y")
        col.prop(con, "axis_z", text="Z")

        if con.pivot_type == 'CONE_TWIST':
            layout.label(text="Limits:")
            split = layout.split()

            col = split.column()
            col.prop(con, "use_angular_limit_x", text="Angle X")
            sub = col.column()
            sub.active = con.use_angular_limit_x
            sub.prop(con, "limit_angle_max_x", text="")

            col = split.column()
            col.prop(con, "use_angular_limit_y", text="Angle Y")
            sub = col.column()
            sub.active = con.use_angular_limit_y
            sub.prop(con, "limit_angle_max_y", text="")

            col = split.column()
            col.prop(con, "use_angular_limit_z", text="Angle Z")
            sub = col.column()
            sub.active = con.use_angular_limit_z
            sub.prop(con, "limit_angle_max_z", text="")

        elif con.pivot_type == 'GENERIC_6_DOF':
            layout.label(text="Limits:")
            split = layout.split()

            col = split.column(align=True)
            col.prop(con, "use_limit_x", text="X")
            sub = col.column(align=True)
            sub.active = con.use_limit_x
            sub.prop(con, "limit_min_x", text="Min")
            sub.prop(con, "limit_max_x", text="Max")

            col = split.column(align=True)
            col.prop(con, "use_limit_y", text="Y")
            sub = col.column(align=True)
            sub.active = con.use_limit_y
            sub.prop(con, "limit_min_y", text="Min")
            sub.prop(con, "limit_max_y", text="Max")

            col = split.column(align=True)
            col.prop(con, "use_limit_z", text="Z")
            sub = col.column(align=True)
            sub.active = con.use_limit_z
            sub.prop(con, "limit_min_z", text="Min")
            sub.prop(con, "limit_max_z", text="Max")

            split = layout.split()

            col = split.column(align=True)
            col.prop(con, "use_angular_limit_x", text="Angle X")
            sub = col.column(align=True)
            sub.active = con.use_angular_limit_x
            sub.prop(con, "limit_angle_min_x", text="Min")
            sub.prop(con, "limit_angle_max_x", text="Max")

            col = split.column(align=True)
            col.prop(con, "use_angular_limit_y", text="Angle Y")
            sub = col.column(align=True)
            sub.active = con.use_angular_limit_y
            sub.prop(con, "limit_angle_min_y", text="Min")
            sub.prop(con, "limit_angle_max_y", text="Max")

            col = split.column(align=True)
            col.prop(con, "use_angular_limit_z", text="Angle Z")
            sub = col.column(align=True)
            sub.active = con.use_angular_limit_z
            sub.prop(con, "limit_angle_min_z", text="Min")
            sub.prop(con, "limit_angle_max_z", text="Max")

        elif con.pivot_type == 'HINGE':
            layout.label(text="Limits:")
            split = layout.split()

            row = split.row(align=True)
            col = row.column()
            col.prop(con, "use_angular_limit_x", text="Angle X")

            col = row.column()
            col.active = con.use_angular_limit_x
            col.prop(con, "limit_angle_min_x", text="Min")
            col = row.column()
            col.active = con.use_angular_limit_x
            col.prop(con, "limit_angle_max_x", text="Max")

        self.draw_influence(layout, con)


class OBJECT_PT_bClampToConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        layout.prop(con, "main_axis", expand=True)

        layout.prop(con, "use_cyclic")

        self.draw_influence(layout, con)


class OBJECT_PT_bTransformConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        layout.prop(con, "use_motion_extrapolate", text="Extrapolate")

        self.space_template(layout, con)

        self.draw_influence(layout, con)

class OBJECT_PT_bTransformConstraint_source(ConstraintButtonsSubPanel):
    bl_parent_id = "OBJECT_PT_bTransformConstraint"
    bl_label = "Source"
    bl_order = 1

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)

        layout.prop(con, "map_from", expand=True)

        layout.use_property_split = True
        layout.use_property_decorate = True

        if con.map_from == 'ROTATION':
            layout.prop(con, "from_rotation_mode", text="Mode")

        ext = "" if con.map_from == 'LOCATION' else "_rot" if con.map_from == 'ROTATION' else "_scale"

        col = layout.column(align=True)
        col.prop(con, "from_min_x" + ext, text="X Min")
        col.prop(con, "from_max_x" + ext, text="Max")

        col = layout.column(align=True)
        col.prop(con, "from_min_y" + ext, text="Y Min")
        col.prop(con, "from_max_y" + ext, text="Max")

        col = layout.column(align=True)
        col.prop(con, "from_min_z" + ext, text="Z Min")
        col.prop(con, "from_max_z" + ext, text="Max")

class OBJECT_PT_bTransformConstraint_mapping(ConstraintButtonsSubPanel):
    bl_parent_id = "OBJECT_PT_bTransformConstraint"
    bl_label = "Mapping"
    bl_order = 2

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True
        
        layout.prop(con, "map_to_x_from", expand=False, text="Source Axis X")

        layout.prop(con, "map_to_y_from", expand=False, text="Y")

        layout.prop(con, "map_to_z_from", expand=False, text="Z")
        
class OBJECT_PT_bTransformConstraint_destination(ConstraintButtonsSubPanel):
    bl_parent_id = "OBJECT_PT_bTransformConstraint"
    bl_label = "Destination"
    bl_order = 3

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)

        layout.prop(con, "map_to", expand=True)

        layout.use_property_split = True
        layout.use_property_decorate = True

        if con.map_to == 'ROTATION':
            layout.prop(con, "to_euler_order", text="Order")

        ext = "" if con.map_to == 'LOCATION' else "_rot" if con.map_to == 'ROTATION' else "_scale"

        col = layout.column(align=True)
        col.prop(con, "to_min_x" + ext, text="X Min")
        col.prop(con, "to_max_x" + ext, text="Max")

        col = layout.column(align=True)
        col.prop(con, "to_min_y" + ext, text="Y Min")
        col.prop(con, "to_max_y" + ext, text="Max")

        col = layout.column(align=True)
        col.prop(con, "to_min_z" + ext, text="Z Min")
        col.prop(con, "to_max_z" + ext, text="Max")      

        layout.prop(con, "mix_mode" + ext, text="Mix")


class OBJECT_PT_bShrinkwrapConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con, False)

        layout.prop(con, "distance")
        layout.prop(con, "shrinkwrap_type", text="Mode")

        layout.separator()

        if con.shrinkwrap_type in {'PROJECT', 'NEAREST_SURFACE', 'TARGET_PROJECT'}:
            layout.prop(con, "wrap_mode", text="Snap Mode")

        if con.shrinkwrap_type == 'PROJECT':
            layout.prop(con, "project_axis")
            layout.prop(con, "project_axis_space", text="Axis Space")
            layout.prop(con, "use_project_opposite")
            layout.prop(con, "project_limit")

            layout.separator()

            layout.prop(con, "cull_face", expand=True)
            row = layout.row()
            row.active = con.use_project_opposite and con.cull_face != 'OFF'
            row.prop(con, "use_invert_cull")

            layout.separator()

        if con.shrinkwrap_type in {'PROJECT', 'NEAREST_SURFACE', 'TARGET_PROJECT'}:
            row = layout.row(heading = "Align to Normal")
            row.prop(con, "use_track_normal", text = "")

            sub = row.row()
            sub.active = con.use_track_normal
            sub.prop(con, "track_axis", text = "")

        self.draw_influence(layout, con)


class OBJECT_PT_bDampTrackConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        layout.prop(con, "track_axis", expand=True)

        self.draw_influence(layout, con)


class OBJECT_PT_bSplineIKConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)

        self.target_template(layout, con)

        col = layout.column()
        col.label(text="Spline Fitting:")
        col.prop(con, "chain_count")
        col.prop(con, "use_even_divisions")
        col.prop(con, "use_chain_offset")

        col = layout.column()
        col.label(text="Chain Scaling:")
        col.prop(con, "use_curve_radius")

        layout.prop(con, "y_scale_mode")
        layout.prop(con, "xz_scale_mode")

        if con.xz_scale_mode in {'INVERSE_PRESERVE', 'VOLUME_PRESERVE'}:
            layout.prop(con, "use_original_scale")

        if con.xz_scale_mode == 'VOLUME_PRESERVE':
            layout.prop(con, "bulge", text="Volume Variation")
            split = layout.split()
            col = split.column(align=True)
            col.prop(con, "use_bulge_min", text="Volume Min")
            sub = col.column()
            sub.active = con.use_bulge_min
            sub.prop(con, "bulge_min", text="")
            col = split.column(align=True)
            col.prop(con, "use_bulge_max", text="Volume Max")
            sub = col.column()
            sub.active = con.use_bulge_max
            sub.prop(con, "bulge_max", text="")
            col = layout.column()
            col.active = con.use_bulge_min or con.use_bulge_max
            col.prop(con, "bulge_smooth", text="Smooth")

        self.draw_influence(layout, con)


class OBJECT_PT_bPivotConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        self.target_template(layout, con)

        if con.target:
            layout.prop(con, "offset", text="Pivot Offset")
        else:
            layout.prop(con, "use_relative_location")
            if con.use_relative_location:
                layout.prop(con, "offset", text="Pivot Point")
            else:
                layout.prop(con, "offset", text="Pivot Point")

        col = layout.column()
        col.prop(con, "rotation_range", text="Rotation Range")

        self.draw_influence(layout, con)


class OBJECT_PT_bFollowTrackConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        clip = None
        if con.use_active_clip:
            clip = context.scene.active_clip
        else:
            clip = con.clip

        layout.prop(con, "use_active_clip")
        layout.prop(con, "use_3d_position")

        row = layout.row()
        row.active = not con.use_3d_position
        row.prop(con, "use_undistorted_position")


        if not con.use_active_clip:
            layout.prop(con, "clip")

        layout.prop(con, "frame_method")

        if clip:
            tracking = clip.tracking

            layout.prop_search(con, "object", tracking, "objects", icon='OBJECT_DATA')

            tracking_object = tracking.objects.get(con.object, tracking.objects[0])

            layout.prop_search(con, "track", tracking_object, "tracks", icon='ANIM_DATA')

        layout.prop(con, "camera")

        row = layout.row()
        row.active = not con.use_3d_position
        row.prop(con, "depth_object")

        layout.operator("clip.constraint_to_fcurve")

        self.draw_influence(layout, con)


class OBJECT_PT_bCameraSolverConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        layout.prop(con, "use_active_clip")

        if not con.use_active_clip:
            layout.prop(con, "clip")

        layout.operator("clip.constraint_to_fcurve")

        self.draw_influence(layout, con)


class OBJECT_PT_bObjectSolverConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        clip = None
        if con.use_active_clip:
            clip = context.scene.active_clip
        else:
            clip = con.clip

        layout.prop(con, "use_active_clip")

        if not con.use_active_clip:
            layout.prop(con, "clip")

        if clip:
            layout.prop_search(con, "object", clip.tracking, "objects", icon='OBJECT_DATA')

        layout.prop(con, "camera")

        row = layout.row()
        row.operator("constraint.objectsolver_set_inverse")
        row.operator("constraint.objectsolver_clear_inverse")

        layout.operator("clip.constraint_to_fcurve")

        self.draw_influence(layout, con)


class OBJECT_PT_bTransformCacheConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)

        layout.label(text="Cache File Properties:")
        box = layout.box()
        box.template_cache_file(con, "cache_file")

        cache_file = con.cache_file

        layout.label(text="Constraint Properties:")
        box = layout.box()

        if cache_file is not None:
            box.prop_search(con, "object_path", cache_file, "object_paths")

        self.draw_influence(layout, con)


class OBJECT_PT_bPythonConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        layout.label(text="Blender 2.6 doesn't support python constraints yet")


class OBJECT_PT_bArmatureConstraint(ConstraintButtonsPanel):

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        layout.prop(con, "use_deform_preserve_volume")
        layout.prop(con, "use_bone_envelopes")

        if context.pose_bone:
            layout.prop(con, "use_current_location")

        layout.operator("constraint.add_target", text="Add Target Bone")

        layout.operator("constraint.normalize_target_weights")

        self.draw_influence(layout, con)

        if not con.targets:
            layout.label(text="No target bones added", icon='ERROR')

class OBJECT_PT_bArmatureConstraint_bones(ConstraintButtonsSubPanel):
    bl_parent_id = "OBJECT_PT_bArmatureConstraint"
    bl_label = "Bones"

    def draw(self, context):
        layout = self.layout
        con = self.get_constraint(context)
        layout.use_property_split = True
        layout.use_property_decorate = True

        for i, tgt in enumerate(con.targets):
            has_target = tgt.target is not None

            box = layout.box()
            header = box.row()
            header.use_property_split = False

            split = header.split(factor=0.45, align=True)
            split.prop(tgt, "target", text="")

            row = split.row(align=True)
            row.active = has_target
            if has_target:
                row.prop_search(tgt, "subtarget", tgt.target.data, "bones", text="")
            else:
                row.prop(tgt, "subtarget", text="", icon='BONE_DATA')

            header.operator("constraint.remove_target", text="", icon='X').index = i

            row = box.row()
            row.active = has_target and tgt.subtarget != ""
            row.prop(tgt, "weight", slider=True, text="Weight")


classes = (
    OBJECT_PT_constraints,
    BONE_PT_constraints,
    OBJECT_PT_bChildOfConstraint,
    OBJECT_PT_bTrackToConstraint,
    OBJECT_PT_bKinematicConstraint,
    #OBJECT_PT_constraints_ik_copy_pose, Deprecated unreachable UI. Delete?
    #OBJECT_PT_constraints_ik_distance,
    OBJECT_PT_bFollowPathConstraint,
    OBJECT_PT_bRotLimitConstraint,
    OBJECT_PT_bLocLimitConstraint,
    OBJECT_PT_bSizeLimitConstraint,
    OBJECT_PT_bRotateLikeConstraint,
    OBJECT_PT_bLocateLikeConstraint,
    OBJECT_PT_bSizeLikeConstraint,
    OBJECT_PT_bSameVolumeConstraint,
    OBJECT_PT_bTransLikeConstraint,
    OBJECT_PT_bActionConstraint,
    OBJECT_PT_bLockTrackConstraint,
    OBJECT_PT_bDistLimitConstraint,
    OBJECT_PT_bStretchToConstraint,
    OBJECT_PT_bMinMaxConstraint,
    #OBJECT_PT_constraints_rigid_body_join,
    OBJECT_PT_bClampToConstraint,
    OBJECT_PT_bTransformConstraint,
    OBJECT_PT_bTransformConstraint_mapping,
    OBJECT_PT_bTransformConstraint_source,
    OBJECT_PT_bTransformConstraint_destination,
    OBJECT_PT_bShrinkwrapConstraint,
    OBJECT_PT_bDampTrackConstraint,
    OBJECT_PT_bSplineIKConstraint,
    OBJECT_PT_bPivotConstraint,
    OBJECT_PT_bFollowTrackConstraint,
    OBJECT_PT_bCameraSolverConstraint,
    OBJECT_PT_bObjectSolverConstraint,
    OBJECT_PT_bTransformCacheConstraint,
    OBJECT_PT_bPythonConstraint,
    OBJECT_PT_bArmatureConstraint,
    OBJECT_PT_bArmatureConstraint_bones,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
