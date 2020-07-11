import os
import sys
import bpy

sys.path.append(os.path.dirname(os.path.realpath(__file__)))
from modules.mesh_test import MeshTest, ModifierSpec, ObjectOperatorSpec, DeformModifierSpec, DeformModifierTest

tests = [

    # Surface Deform Test, finally can bind to the Target object.
    # Actual deformation occurs by animating imitating user input.

    MeshTest("SurfaceDeform", "testObjMonkeySurfaceDeform", "expObjMonkeySurfaceDeform",
             [DeformModifierSpec(10, [ModifierSpec('surface_deform', 'SURFACE_DEFORM', {'target': bpy.data.objects["Cube"]})],
              ObjectOperatorSpec('surfacedeform_bind', {'modifier': 'surface_deform'}))]),

    # Mesh Deform Test, finally can bind to the Target object.
    # Actual deformation occurs by animating imitating user input.

    MeshTest("MeshDeform", "testObjMonkeyMeshDeform", "expObjMonkeyMeshDeform",
             [DeformModifierSpec(10, [ModifierSpec('mesh_deform', 'MESH_DEFORM', {'object': bpy.data.objects["MeshCube"],
                                                                                 'precision': 2})],
                                 ObjectOperatorSpec('meshdeform_bind', {'modifier': 'mesh_deform'}))]),


    # Surface Deform Test, finally can bind to the Target object.
    # Actual deformation occurs by animating imitating user input.

    MeshTest("Hook", "testObjHookPlane", "expObjHookPlane",
             [DeformModifierSpec(10, [ModifierSpec('hook', 'HOOK',
                                                  {'object': bpy.data.objects["Empty"], 'falloff_radius': 1,
                                                   'vertex_group': 'Group'})])]),


    # Laplacian Deform Test, first a hook is attached.

    MeshTest("Laplace", "testObjCubeLaplacian", "expObjCubeLaplacian",
             [DeformModifierSpec(10,
                                 [ModifierSpec('hook2', 'HOOK', {'object': bpy.data.objects["Empty.001"],
                                                                'vertex_group': 'hook_vg'}),
                                 ModifierSpec('laplace', 'LAPLACIANDEFORM', {'vertex_group': 'laplace_vg'})],
                                 ObjectOperatorSpec('laplaciandeform_bind', {'modifier':'laplace'}))]),


    MeshTest("WarpPlane", "testObjPlaneWarp", "expObjPlaneWarp",
             [DeformModifierSpec(10, [ModifierSpec('warp', 'WARP',
                                                   {'object_from': bpy.data.objects["From"], 'object_to': bpy.data.objects["To"],
                                                    })])]),



]

deform_tests = DeformModifierTest(tests)
command = list(sys.argv)
for i, cmd in enumerate(command):
    if cmd == "--run-all-tests":
        deform_tests.apply_modifiers = True
        deform_tests.run_all_tests()
        break
    elif cmd == "--run-test":
        deform_tests.apply_modifiers = False
        name = str(command[i + 1])
        deform_tests.run_test(name)
        break
