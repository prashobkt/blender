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

from bpy.types import Menu


# Panel mix-in class (don't register).
class PresetPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'HEADER'
    bl_label = "Presets"
    path_menu = Menu.path_menu

    @classmethod
    def draw_panel_header(cls, layout):
        layout.emboss = 'NONE'
        layout.popover(
            panel=cls.__name__,
            icon='PRESET',
            text="",
        )

    @classmethod
    def draw_menu(cls, layout, text=None):
        if text is None:
            text = cls.bl_label

        layout.popover(
            panel=cls.__name__,
            icon='PRESET',
            text=text,
        )

    def draw(self, context):
        layout = self.layout
        layout.emboss = 'PULLDOWN_MENU'
        layout.operator_context = 'EXEC_DEFAULT'

        Menu.draw_preset(self, context)


class View3DShadingLayout:
    bl_label = "Shading"

    @staticmethod
    def draw(layout):
        layout.label(text="Viewport Shading")


class View3DShadingLightingLayout:
    bl_label = "Lighting"

    @staticmethod
    def poll(context, shading):
        engine = context.scene.render.engine
        return shading.type in {'SOLID', 'MATERIAL'} or engine == 'BLENDER_EEVEE' and shading.type == 'RENDERED'

    @staticmethod
    def draw(context, shading, layout, add_world_space_lighting_prop=True):
        col = layout.column()
        split = col.split(factor=0.9)

        if shading.type == 'SOLID':
            split.row().prop(shading, "light", expand=True)
            col = split.column()

            split = layout.split(factor=0.9)
            col = split.column()
            sub = col.row()

            if shading.light == 'STUDIO':
                prefs = context.preferences
                system = prefs.system

                if not system.use_studio_light_edit:
                    sub.scale_y = 0.6  # smaller studiolight preview
                    sub.template_icon_view(
                        shading, "studio_light", scale_popup=3.0)
                else:
                    sub.prop(
                        system,
                        "use_studio_light_edit",
                        text="Disable Studio Light Edit",
                        icon='NONE',
                        toggle=True,
                    )

                col = split.column()
                col.operator("preferences.studiolight_show",
                             emboss=False, text="", icon='PREFERENCES')

                split = layout.split(factor=0.9)
                col = split.column()

                row = col.row()
                if add_world_space_lighting_prop:
                    row.prop(shading, "use_world_space_lighting",
                             text="", icon='WORLD', toggle=True)
                row = row.row()
                row.active = shading.use_world_space_lighting
                row.prop(shading, "studiolight_rotate_z", text="Rotation")
                col = split.column()  # to align properly with above

            elif shading.light == 'MATCAP':
                sub.scale_y = 0.6  # smaller matcap preview
                sub.template_icon_view(
                    shading, "studio_light", scale_popup=3.0)

                col = split.column()
                col.operator("preferences.studiolight_show",
                             emboss=False, text="", icon='PREFERENCES')
                col.operator("view3d.toggle_matcap_flip",
                             emboss=False, text="", icon='ARROW_LEFTRIGHT')

        elif shading.type == 'MATERIAL':
            col.prop(shading, "use_scene_lights")
            col.prop(shading, "use_scene_world")
            col = layout.column()
            split = col.split(factor=0.9)

            if not shading.use_scene_world:
                col = split.column()
                sub = col.row()
                sub.scale_y = 0.6
                sub.template_icon_view(shading, "studio_light", scale_popup=3)

                col = split.column()
                col.operator("preferences.studiolight_show",
                             emboss=False, text="", icon='PREFERENCES')

                split = layout.split(factor=0.9)
                col = split.column()
                col.prop(shading, "studiolight_rotate_z", text="Rotation")
                col.prop(shading, "studiolight_intensity")
                col.prop(shading, "studiolight_background_alpha")
                col.prop(shading, "studiolight_background_blur")
                col = split.column()  # to align properly with above

        elif shading.type == 'RENDERED':
            col.prop(shading, "use_scene_lights_render")
            col.prop(shading, "use_scene_world_render")

            if not shading.use_scene_world_render:
                col = layout.column()
                split = col.split(factor=0.9)

                col = split.column()
                sub = col.row()
                sub.scale_y = 0.6
                sub.template_icon_view(shading, "studio_light", scale_popup=3)

                col = split.column()
                col.operator("preferences.studiolight_show",
                             emboss=False, text="", icon='PREFERENCES')

                split = layout.split(factor=0.9)
                col = split.column()
                col.prop(shading, "studiolight_rotate_z", text="Rotation")
                col.prop(shading, "studiolight_intensity")
                col.prop(shading, "studiolight_background_alpha")
                col.prop(shading, "studiolight_background_blur")
                col = split.column()  # to align properly with above


class View3DShadingColorLayout:
    bl_label = "Color"

    @staticmethod
    def poll(context, shading):
        return shading.type in {'WIREFRAME', 'SOLID'}

    @staticmethod
    def _draw_color_type(shading, layout):
        layout.grid_flow(columns=3, align=True).prop(
            shading, "color_type", expand=True)
        if shading.color_type == 'SINGLE':
            layout.row().prop(shading, "single_color", text="")

    @staticmethod
    def _draw_background_color(shading, layout):
        layout.row().label(text="Background")
        layout.row().prop(shading, "background_type", expand=True)
        if shading.background_type == 'VIEWPORT':
            layout.row().prop(shading, "background_color", text="")

    @classmethod
    def draw(cls, context, shading, layout):
        if shading.type == 'WIREFRAME':
            layout.row().prop(shading, "wireframe_color_type", expand=True)
        else:
            cls._draw_color_type(shading, layout)
            layout.separator()
        cls._draw_background_color(shading, layout)


class View3DShadingOptionsLayout:
    bl_label = "Options"

    @staticmethod
    def poll(context, shading):
        return shading.type in {'WIREFRAME', 'SOLID'}

    @staticmethod
    def draw(context, shading, layout):
        col = layout.column()

        if shading.type == 'SOLID':
            col.prop(shading, "show_backface_culling")

        row = col.row(align=True)

        if shading.type == 'WIREFRAME':
            row.prop(shading, "show_xray_wireframe", text="")
            sub = row.row()
            sub.active = shading.show_xray_wireframe
            sub.prop(shading, "xray_alpha_wireframe", text="X-Ray")
        elif shading.type == 'SOLID':
            row.prop(shading, "show_xray", text="")
            sub = row.row()
            sub.active = shading.show_xray
            sub.prop(shading, "xray_alpha", text="X-Ray")
            # X-ray mode is off when alpha is 1.0
            xray_active = shading.show_xray and shading.xray_alpha != 1

            row = col.row(align=True)
            row.prop(shading, "show_shadows", text="")
            row.active = not xray_active
            sub = row.row(align=True)
            sub.active = shading.show_shadows
            sub.prop(shading, "shadow_intensity", text="Shadow")
            sub.popover(
                panel="VIEW3D_PT_shading_options_shadow",
                icon='PREFERENCES',
                text="",
            )

            col = layout.column()

            row = col.row()
            row.active = not xray_active
            row.prop(shading, "show_cavity")

            if shading.show_cavity and not xray_active:
                row.prop(shading, "cavity_type", text="Type")

                if shading.cavity_type in {'WORLD', 'BOTH'}:
                    col.label(text="World Space")
                    sub = col.row(align=True)
                    sub.prop(shading, "cavity_ridge_factor", text="Ridge")
                    sub.prop(shading, "cavity_valley_factor", text="Valley")
                    sub.popover(
                        panel="VIEW3D_PT_shading_options_ssao",
                        icon='PREFERENCES',
                        text="",
                    )

                if shading.cavity_type in {'SCREEN', 'BOTH'}:
                    col.label(text="Screen Space")
                    sub = col.row(align=True)
                    sub.prop(shading, "curvature_ridge_factor", text="Ridge")
                    sub.prop(shading, "curvature_valley_factor", text="Valley")

            row = col.row()
            row.active = not xray_active
            row.prop(shading, "use_dof", text="Depth Of Field")

        if shading.type in {'WIREFRAME', 'SOLID'}:
            row = layout.split()
            row.prop(shading, "show_object_outline")
            sub = row.row()
            sub.active = shading.show_object_outline
            sub.prop(shading, "object_outline_color", text="")

        if shading.type == 'SOLID':
            col = layout.column()
            if shading.light in {'STUDIO', 'MATCAP'}:
                col.active = shading.selected_studio_light.has_specular_highlight_pass
                col.prop(shading, "show_specular_highlight",
                         text="Specular Lighting")


class View3DShadingRenderPassLayout:
    bl_label = "Render Pass"

    @staticmethod
    def poll(context, shading):
        return (
            (shading.type == 'MATERIAL') or
            (context.engine in {'BLENDER_EEVEE'}
             and shading.type == 'RENDERED')
        )

    @staticmethod
    def draw(context, shading, layout):
        layout.prop(shading, "render_pass", text="")
