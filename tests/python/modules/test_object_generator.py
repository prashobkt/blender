import bpy


def createTestObjects(obj_dict):
    # all_obj_names = [name for _,name in obj_dict.items()]
    offset_y = 0
    offset_x = 5

    for obj_type, obj_name in obj_dict.items():
        print("Object type:", obj_type, "Object name:", obj_name)

        if obj_name not in bpy.data.objects:
            if obj_type == "Circle":
                bpy.ops.mesh.primitive_circle_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_circle_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Cube":
                bpy.ops.mesh.primitive_cube_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_cube_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Plane":
                bpy.ops.mesh.primitive_plane_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_plane_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Sphere":
                bpy.ops.mesh.primitive_uv_sphere_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_uv_sphere_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Cone":
                bpy.ops.mesh.primitive_cone_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_cone_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Cylinder":
                bpy.ops.mesh.primitive_cylinder_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_uv_sphere_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Icosphere":
                bpy.ops.mesh.primitive_ico_sphere_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_ico_sphere_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Torus":
                bpy.ops.mesh.primitive_torus_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_torus_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Suzanne":
                bpy.ops.mesh.primitive_monkey_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_monkey_add(location=(offset_x, offset_y, 0))

            elif obj_type == "Grid":
                bpy.ops.mesh.primitive_grid_add(location=(0, offset_y, 0))
                bpy.ops.mesh.primitive_grid_add(location=(offset_x, offset_y, 0))

            bpy.context.active_object.name = "testObj" + obj_name
            bpy.data.objects[obj_type].name = "expObj" + obj_name
        else:
            print("Object already present.")

        offset_y += 5


# Deleting start up Cube
if bpy.context.active_object.name == 'Cube':
    bpy.ops.object.delete(use_global=False)
createTestObjects({'Cube': "testmonkey", 'Plane': "testPlane2"})
