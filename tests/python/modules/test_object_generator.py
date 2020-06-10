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

"""blender -b --python tests/python/modules/test_object_generator.py -- /path/to/blend/file/new_or_existing.blend"""
import bpy
import os
import sys
import random
from pathlib import Path

offset_x = 5
flag = 0


def vertex_selection(mesh, vert_set: set, randomize: bool):
    """Selecting vertices which will be later assigned to vertex groups"""
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='DESELECT')
    bpy.ops.object.mode_set(mode='OBJECT')
    mesh.select_set(1)

    if not randomize:
        for index in vert_set:
            mesh.data.vertices[index].select = True
    else:
        max_vert = len(mesh.data.vertices)
        random_size = random.randint(1, max_vert)
        print(random_size)
        for index in range(random_size):
            random_index = random.randint(0, random_size)
            print(random_index)
            mesh.data.vertices[random_index].select = True


def create_vertex_group(obj_name, vg_name, vg_vert_set, randomize):
    """
    Creates a vertex group. Raises Exception for invalid index
    :param obj_name : blend object: - the object for vertex group
    :param vg_name : str - vertex group name
    :param vg_vert_set : set - set of vertices assgined to the vertex group
    :param randomize : bool - select random vertices"""
    #   Validating user input
    #  -checking for whether the object exists
    #  -index value is within the right range

    if obj_name not in bpy.data.objects:
        global flag
        flag = 1
        raise Exception('Object {} not Found!'.format(obj_name))
    mesh = bpy.data.objects[obj_name]
    max_vert = len(mesh.data.vertices)
    for i in vg_vert_set:
        if not isinstance(i, int) or i < 0 or i > max_vert - 1:
            flag = 1
            raise Exception("Please enter a valid index(integer).")

    mesh.vertex_groups.new(name=vg_name)
    vertex_selection(mesh, vg_vert_set, randomize)

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.object.vertex_group_assign()
    bpy.ops.object.mode_set(mode='OBJECT')


def get_last_location():
    """To find the location of last created object."""
    farthest = 0
    all_y_locs = []
    for obj in bpy.data.objects:
        all_y_locs.append(obj.location.y)

    for y in all_y_locs:
        if y > farthest:
            farthest = y

    return farthest


def create_test_objects(obj_dict):
    """
    Creates test and expected objects
    :param obj_dict: dict (key:value) - dictionary contains test object names and types, e.g. {'myTestCube':'Cube'}
     """
    offset_y = get_last_location() + 5
    test_obj_list = []

    for obj_name, obj_type in obj_dict.items():

        test_obj_name = "testObj" + obj_name

        if test_obj_name not in bpy.data.objects.keys():
            if obj_type == "Circle":
                bpy.ops.mesh.primitive_circle_add(location=(0, offset_y, 0))

            elif obj_type == "Cube":
                bpy.ops.mesh.primitive_cube_add(location=(0, offset_y, 0))

            elif obj_type == "Plane":
                bpy.ops.mesh.primitive_plane_add(location=(0, offset_y, 0))

            elif obj_type == "Sphere":
                bpy.ops.mesh.primitive_uv_sphere_add(location=(0, offset_y, 0))

            elif obj_type == "Cone":
                bpy.ops.mesh.primitive_cone_add(location=(0, offset_y, 0))

            elif obj_type == "Cylinder":
                bpy.ops.mesh.primitive_cylinder_add(location=(0, offset_y, 0))

            elif obj_type == "Icosphere":
                bpy.ops.mesh.primitive_ico_sphere_add(location=(0, offset_y, 0))

            elif obj_type == "Torus":
                bpy.ops.mesh.primitive_torus_add(location=(0, offset_y, 0))

            elif obj_type == "Monkey":
                bpy.ops.mesh.primitive_monkey_add(location=(0, offset_y, 0))

            elif obj_type == "Grid":
                bpy.ops.mesh.primitive_grid_add(location=(0, offset_y, 0))
            else:
                global flag
                flag = 1
                raise Exception("{} Object type not yet supported".format(obj_type))

            test_obj_list.append(test_obj_name)
            bpy.context.active_object.name = test_obj_name
            bpy.ops.object.duplicate_move(TRANSFORM_OT_translate={"value": (offset_x, 0, 0)})
            bpy.context.active_object.name = "expObj" + obj_name

            offset_y += 5
        else:
            print("Object already present.")

    if not flag:
        print("{} were successfully created!".format(test_obj_list))


argv = sys.argv
argv = argv[argv.index("--") + 1:]

# Converting the path to be platform independent and then into string
path_to_file = str(Path(argv[0]))

print(path_to_file)
if os.path.exists(path_to_file):

    bpy.ops.wm.open_mainfile(filepath=path_to_file)


else:
    # Looking at the global flag variable
    if not flag:
        bpy.ops.wm.save_as_mainfile(filepath=path_to_file)

    # Cleaning up a new blend file
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

create_test_objects({'Cube2': 'Cube', 'Cy2': "Cylinder"})

create_vertex_group('expObjCube', "vg_solidify8", {1, 2, 3, 4}, True)

bpy.ops.wm.save_mainfile(filepath=path_to_file)
