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
from bpy.types import Header, Menu, Panel


class INFO_HT_header(Header):
    bl_space_type = 'INFO'

    def draw(self, context):
        layout = self.layout
        layout.template_header()
        sinfo = context.space_data

        INFO_MT_editor_menus.draw_collapsible(context, layout)
        row = layout.row(align=True)
        row.prop(sinfo.search_filter, "search_string", text="")
        row.prop(sinfo.search_filter, "use_match_reverse", text="")
        row.prop(sinfo.search_filter, "use_match_case", text="")
        row.prop(sinfo.search_filter, "use_glob", text="")

        layout.separator_spacer()

        layout.separator_spacer()

        row = layout.row()
        if sinfo.view == 'CLOGS':
            row.popover(panel="INFO_PT_log_formatting", text="", icon="SYNTAX_ON")
        row.popover(panel="INFO_PT_report_type_visibility", text="", icon="FILTER")


class INFO_MT_editor_menus(Menu):
    bl_idname = "INFO_MT_editor_menus"
    bl_label = ""

    def draw(self, context):
        layout = self.layout
        layout.menu("INFO_MT_view")
        sinfo = context.space_data
        if sinfo.view == 'REPORTS':
            layout.menu("INFO_MT_report_info")
        else:
            layout.menu("INFO_MT_clog_info")

        row = layout.row()
        row.prop(sinfo, "view", expand=True)


class INFO_MT_view(Menu):
    bl_label = "View"

    def draw(self, _context):
        layout = self.layout

        layout.menu("INFO_MT_area")


class INFO_MT_clog_info(Menu):
    bl_label = "Log"

    def draw(self, _context):
        layout = self.layout

        layout.operator("info.clog_select_all", text="Select All").action = 'SELECT'
        layout.operator("info.clog_select_all", text="Deselect All").action = 'DESELECT'
        layout.operator("info.clog_select_all", text="Invert Selection").action = 'INVERT'
        layout.operator("info.clog_select_all", text="Toggle Selection").action = 'TOGGLE'

        layout.separator()

        layout.operator("info.clog_select_box")

        layout.separator()

        layout.operator("info.clog_delete", text="Delete")
        layout.operator("info.clog_copy", text="Copy")


class INFO_MT_report_info(Menu):
    bl_label = "Report"

    def draw(self, _context):
        layout = self.layout

        layout.operator("info.report_select_all", text="Select All").action = 'SELECT'
        layout.operator("info.report_select_all", text="Deselect All").action = 'DESELECT'
        layout.operator("info.report_select_all", text="Invert Selection").action = 'INVERT'
        layout.operator("info.report_select_all", text="Toggle Selection").action = 'TOGGLE'

        layout.separator()

        layout.operator("info.report_select_box")

        layout.separator()

        # Disabled because users will likely try this and find
        # it doesn't work all that well in practice.
        # Mainly because operators needs to run in the right context.

        # layout.operator("info.report_replay")
        # layout.separator()

        layout.operator("info.report_delete", text="Delete")
        layout.operator("info.report_copy", text="Copy")


class INFO_MT_area(Menu):
    bl_label = "Area"

    def draw(self, context):
        layout = self.layout

        if context.space_data.type == 'VIEW_3D':
            layout.operator("screen.region_quadview")
            layout.separator()

        layout.operator("screen.area_split", text="Horizontal Split").direction = 'HORIZONTAL'
        layout.operator("screen.area_split", text="Vertical Split").direction = 'VERTICAL'

        layout.separator()

        layout.operator("screen.area_dupli", icon='WINDOW')

        layout.separator()

        layout.operator("screen.screen_full_area")
        layout.operator(
            "screen.screen_full_area",
            text="Toggle Fullscreen Area",
            icon='FULLSCREEN_ENTER',
        ).use_hide_panels = True


class INFO_MT_context_menu(Menu):
    bl_label = "Info Context Menu"

    def draw(self, context):
        layout = self.layout

        sinfo = context.space_data

        if sinfo.view == 'REPORTS':
            layout.operator("info.report_copy", text="Copy")
            layout.operator("info.report_delete", text="Delete")
        else:
            layout.operator("info.clog_copy", text="Copy Message")
            layout.operator("info.clog_delete", text="Delete (mockup)")


class INFO_PT_log_formatting(Panel):
    bl_space_type = 'INFO'
    bl_region_type = 'HEADER'
    bl_label = "Log Formatting"

    def draw(self, context):
        layout = self.layout

        sinfo = context.space_data

        layout.label(text="Log Formatting")
        col = layout.column()
        col.prop(sinfo, "log_format")
        col.prop(sinfo, "use_short_file_line")
        col.prop(sinfo, "use_log_message_new_line", text="Message In New Line")


class INFO_PT_report_type_visibility(Panel):
    bl_space_type = 'INFO'
    bl_region_type = 'HEADER'
    bl_label = "Report Types"

    # bl_ui_units_x = 8

    def draw(self, context):
        layout = self.layout

        sinfo = context.space_data

        if sinfo.view == 'REPORTS':
            layout.label(text="Report Types Visibility")
            col = layout.column(align=True)
            col.prop(sinfo, "show_report_debug", text="Debug")
            col.prop(sinfo, "show_report_info", text="Info")
            col.prop(sinfo, "show_report_operator", text="Operator")
            col.prop(sinfo, "show_report_property", text="Property")
            col.prop(sinfo, "show_report_warning", text="Warning")
            col.prop(sinfo, "show_report_error", text="Error")
            col.prop(sinfo, "show_report_error_out_of_memory", text="Error Out of Memory")
            col.prop(sinfo, "show_report_error_invalid_context", text="Error Invalid Context")
            col.prop(sinfo, "show_report_error_invalid_input", text="Error Invalid Input")
            layout.separator()
        else:
            layout.label(text="Filter Log Severity")
            col = layout.column(align=True)
            col.prop(sinfo, "log_severity_mask")
            layout.separator()

            box = layout.box()
            row = box.row(align=True)
            row.prop(sinfo, "use_log_file_line_filter", text="Filter File Line")
            row.operator("info.log_file_line_filter_add", text="", icon='ADD', emboss=False)
            for i, filter in enumerate(sinfo.filter_log_file_line):
                row = box.row(align=True)
                row.active = sinfo.use_log_file_line_filter
                row.prop(filter, "search_string", text="")
                row.prop(filter, "use_match_reverse", text="")
                row.prop(filter, "use_match_case", text="")
                row.prop(filter, "use_glob", text="")
                row.operator("info.log_file_line_filter_remove", text="", icon='X', emboss=False).index = i

            box = layout.box()
            row = box.row(align=True)
            row.prop(sinfo, "use_log_type_filter", text="Filter Log Type")
            row.operator("info.log_type_filter_add", text="", icon='ADD', emboss=False)
            for i, filter in enumerate(sinfo.filter_log_type):
                row = box.row(align=True)
                row.active = sinfo.use_log_type_filter
                row.prop(filter, "search_string", text="")
                row.prop(filter, "use_match_reverse", text="")
                row.prop(filter, "use_match_case", text="")
                row.prop(filter, "use_glob", text="")
                row.operator("info.log_type_filter_remove", text="", icon='X', emboss=False).index = i

            box = layout.box()
            row = box.row(align=True)
            row.prop(sinfo, "use_log_function_filter", text="Filter Log Function")
            row.operator("info.log_function_filter_add", text="", icon='ADD', emboss=False)
            for i, filter in enumerate(sinfo.filter_log_function):
                row = box.row(align=True)
                row.active = sinfo.use_log_function_filter
                row.prop(filter, "search_string", text="")
                row.prop(filter, "use_match_reverse", text="")
                row.prop(filter, "use_match_case", text="")
                row.prop(filter, "use_glob", text="")
                row.operator("info.log_function_filter_remove", text="", icon='X', emboss=False).index = i

            row = layout.row(align=True)
            row.prop(sinfo, "use_log_level_filter", text="")
            row.active = sinfo.use_log_level_filter
            row.prop(sinfo, "filter_log_level", text="Max verbosity")


classes = (
    INFO_HT_header,
    INFO_MT_editor_menus,
    INFO_MT_area,
    INFO_MT_view,
    INFO_MT_clog_info,
    INFO_MT_report_info,
    INFO_MT_context_menu,
    INFO_PT_log_formatting,
    INFO_PT_report_type_visibility
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class

    for cls in classes:
        register_class(cls)
