import bpy
import re
import os
import sys

offset_x = 5


def validating_user_input(mesh, vert_set):
    for i in vert_set:
        if not isinstance(i, int) or i <= 0:
            raise Exception("Please enter a valid index(integer).")

    max_vert = len(mesh.data.vertices)

    for i in vert_set:
        if i > max_vert-1:
            raise Exception("Index is greater than maximum number of vertices.")


def do_selection(mesh, vert_set: set):

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='DESELECT')
    bpy.ops.object.mode_set(mode='OBJECT')

    for index in vert_set:
        mesh.data.vertices[index].select = True

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.object.vertex_group_assign()
    bpy.ops.object.mode_set(mode='OBJECT')


def create_vertex_group(obj_name, vg_name, vg_vert_set):

    mesh = bpy.data.objects[obj_name]
    mesh.vertex_groups.new(name=vg_name)
    vert_set = vg_vert_set
    validating_user_input(mesh, vert_set)
    do_selection(mesh, vert_set)


def get_last_location():
    farthest = 0
    all_y_locs = []
    for obj in bpy.data.objects:
        all_y_locs.append(obj.location.y)

    for y in all_y_locs:
        if y > farthest:
            farthest = y

    return farthest


def name_test_object(obj_name):
    bpy.context.active_object.name = "testObj" + obj_name


def name_exp_object(obj_name):
    bpy.context.active_object.name = "expObj" + obj_name


def create_test_objects(obj_dict):
    offset_y = get_last_location() + 5

    for obj_name, obj_type in obj_dict.items():
        print("Object type:", obj_type, "\nObject name:", obj_name)

        test_obj_name = "testObj" + obj_name
        exp_obj_name = "expObj" + obj_name

        if test_obj_name not in bpy.data.objects.keys():
            if obj_type == "Circle":
                bpy.ops.mesh.primitive_circle_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_circle_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Cube":
                bpy.ops.mesh.primitive_cube_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_cube_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Plane":
                bpy.ops.mesh.primitive_plane_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_plane_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Sphere":
                bpy.ops.mesh.primitive_uv_sphere_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_uv_sphere_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Cone":
                bpy.ops.mesh.primitive_cone_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_cone_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Cylinder":
                bpy.ops.mesh.primitive_cylinder_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_cylinder_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Icosphere":
                bpy.ops.mesh.primitive_ico_sphere_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_ico_sphere_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Torus":
                bpy.ops.mesh.primitive_torus_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_torus_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Monkey":
                bpy.ops.mesh.primitive_monkey_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_monkey_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            elif obj_type == "Grid":
                bpy.ops.mesh.primitive_grid_add(location=(0, offset_y, 0))
                name_test_object(obj_name)
                bpy.ops.mesh.primitive_grid_add(location=(offset_x, offset_y, 0))
                name_exp_object(obj_name)

            offset_y += 5
        else:
            print("Object already present.")


pattern = re.compile(r"[A-Z]:?[\\\/].*\.blend|[\\\/].*\.blend")

args = list(sys.argv)
print(args)
for cmd in args:
    mo = pattern.search(cmd)
    if mo is not None:
        path_to_file = mo.group()

print(path_to_file)
if os.path.exists(path_to_file):

    bpy.ops.wm.open_mainfile(filepath=path_to_file)


else:

    bpy.ops.wm.save_as_mainfile(filepath=path_to_file)
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

create_test_objects({'Cube': 'Cube', 'Cy1': "Cylinder"})

create_vertex_group('Cube', "vg_solidify7", {1, 2, 3, 4})

bpy.ops.wm.save_mainfile(filepath=path_to_file)
