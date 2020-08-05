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

# A framework to run regression tests on mesh modifiers and operators based on howardt's mesh_ops_test.py
#
# General idea:
# A test is:
#    Object mode
#    Select <test_object>
#    Duplicate the object
#    Select the object
#    Apply operation for each operation in <operations_stack> with given parameters
#    (an operation is either a modifier or an operator)
#    test_mesh = <test_object>.data
#    run test_mesh.unit_test_compare(<expected object>.data)
#    delete the duplicate object
#
# The words in angle brackets are parameters of the test, and are specified in
# the main class MeshTest.
#
# If the environment variable BLENDER_TEST_UPDATE is set to 1, the <expected_object>
# is updated with the new test result.
# Tests are verbose when the environment variable BLENDER_VERBOSE is set.


import bpy
import functools
import inspect
import os


# Output from this module and from blender itself will occur during tests.
# We need to flush python so that the output is properly interleaved, otherwise
# blender's output for one test will end up showing in the middle of another test...
print = functools.partial(print, flush=True)


class ModifierSpec:
    """
    Holds one modifier and its parameters.
    """

    def __init__(self, modifier_name: str, modifier_type: str, modifier_parameters: dict):
        """
        Constructs a modifier spec.
        :param modifier_name: str - name of object modifier, e.g. "myFirstSubsurfModif"
        :param modifier_type: str - type of object modifier, e.g. "SUBSURF"
        :param modifier_parameters: dict - {name : val} dictionary giving modifier parameters, e.g. {"quality" : 4}
        """
        self.modifier_name = modifier_name
        self.modifier_type = modifier_type
        self.modifier_parameters = modifier_parameters

    def __str__(self):
        return "Modifier: " + self.modifier_name + " of type " + self.modifier_type + \
               " with parameters: " + str(self.modifier_parameters)


class PhysicsSpec:
    """
    Holds one Physics modifier and its parameters.
    """

    def __init__(self, modifier_name: str, modifier_type: str, modifier_parameters: dict, frame_end: int):
        """
        Constructs a physics spec.
        :param modifier_name: str - name of object modifier, e.g. "Cloth"
        :param modifier_type: str - type of object modifier, e.g. "CLOTH"
        :param modifier_parameters: dict - {name : val} dictionary giving modifier parameters, e.g. {"quality" : 4}
        :param frame_end:int - the last frame of the simulation at which it is baked
        """
        self.modifier_name = modifier_name
        self.modifier_type = modifier_type
        self.modifier_parameters = modifier_parameters
        self.frame_end = frame_end

    def __str__(self):
        return "Physics Modifier: " + self.modifier_name + " of type " + self.modifier_type + \
               " with parameters: " + str(self.modifier_parameters) + " with frame end: " + str(self.frame_end)


class FluidDyanmicPaintSpec:
    """
    Holds a Fluid modifier and a Dynamic Paint modifier and their parameters.
    """

    def __init__(self, modifier_name: str, modifier_type: str, sub_type: str, modifier_parameters: dict, frame_end: int):
        """
        Constructs a Fluid/Dynamic Paint spec.
        :param modifier_name: str - name of object modifier, e.g. "FLUID"
        :param sub_type: str - type of fluid, e.g. "Domain"
        :param modifier_parameters: dict - {name : val} dictionary giving modifier parameters, e.g. {"use_mesh" : True}
        :param frame_end:int - the frame at which the modifier is "applied"
        """
        self.modifier_name = modifier_name
        self.sub_type = sub_type
        self.modifier_parameters = modifier_parameters
        self.modifier_type = modifier_type
        self.frame_end = frame_end

    def __str__(self):
        return "Modifier: " + self.modifier_name + " of type " + self.modifier_type + \
               " with parameters: " + str(self.modifier_parameters) + " with frame end: " + str(self.frame_end)

class ParticleSystemSpec:
    """
    Holds a Particle System modifier and its parameters.
    """

    def __init__(self, modifier_name: str, modifier_type: str, modifier_parameters: dict, frame_end: int):
        """
        Constructs a particle system spec.
        :param modifier_name: str - name of object modifier, e.g. "Particles"
        :param modifier_type: str - type of object modifier, e.g. "PARTICLE_SYSTEM", can be removed
        :param modifier_parameters: dict - {name : val} dictionary giving modifier parameters, e.g. {"seed" : 1}
        :param frame_end:int - the last frame of the simulation at which the modifier is applied
        """
        self.modifier_name = modifier_name
        self.modifier_type = modifier_type
        self.modifier_parameters = modifier_parameters
        self.frame_end = frame_end

    def __str__(self):
        return "Physics Modifier: " + self.modifier_name + " of type " + self.modifier_type + \
               " with parameters: " + str(self.modifier_parameters) + " with frame end: " + str(self.frame_end)



class OperatorSpec:
    """
    Holds one operator and its parameters.
    """

    def __init__(self, operator_name: str, operator_parameters: dict, select_mode: str, selection: set):
        """
        Constructs an operatorSpec. Raises ValueError if selec_mode is invalid.
        :param operator_name: str - name of mesh operator from bpy.ops.mesh, e.g. "bevel" or "fill"
        :param operator_parameters: dict - {name : val} dictionary containing operator parameters.
        :param select_mode: str - mesh selection mode, must be either 'VERT', 'EDGE' or 'FACE'
        :param selection: set - set of vertices/edges/faces indices to select, e.g. [0, 9, 10].
        """
        self.operator_name = operator_name
        self.operator_parameters = operator_parameters
        if select_mode not in ['VERT', 'EDGE', 'FACE']:
            raise ValueError("select_mode must be either {}, {} or {}".format('VERT', 'EDGE', 'FACE'))
        self.select_mode = select_mode
        self.selection = selection

    def __str__(self):
        return "Operator: " + self.operator_name + " with parameters: " + str(self.operator_parameters) + \
               " in selection mode: " + self.select_mode + ", selecting " + str(self.selection)


class ObjectOperatorSpec:
    """
    Holds an object operator and its parameters.
    """

    def __init__(self, operator_name: str, operator_parameters: dict):
        """
        Constructs an Object Operator spec
        :param operator_name: str - name of the object operator from bpy.ops.object, e.g. "shade_smooth" or "shape_keys"
        :param operator_parameters: dict - contains operator parameters.
        """
        self.operator_name = operator_name
        self.operator_parameters = operator_parameters

    def __str__(self):
        return "Operator: " + self.operator_name + " with parameters: " + str(self.operator_parameters)


class DeformModifierSpec:
    """
    Holds a modifier and object operator.
    """
    def __init__(self, frame_number: int, modifier_list: list, obj_operator_spec: ObjectOperatorSpec = None):
        """
        Constructs a Deform Modifier spec (for user input)
        :param frame_number: int - the frame at which animated keyframe is inserted
        :param modifier_spec: ModifierSpec - contains modifiers
        :param obj_operator_spec: ObjectOperatorSpec - contains object operators
        """
        self.frame_number = frame_number
        self.modifier_list = modifier_list
        self.obj_operator_spec = obj_operator_spec

    def __str__(self):
        return "Modifier: " + str(self.modifier_list) + " with object operator " + str(self.obj_operator_spec)


class MeshTest:
    """
    A mesh testing class targeted at testing modifiers and operators on a single object.
    It holds a stack of mesh operations, i.e. modifiers or operators. The test is executed using
    the public method run_test().
    """

    def __init__(self, test_name: str, test_object_name: str, expected_object_name: str, operations_stack=None, apply_modifiers=False, threshold=None):
        """
        Constructs a MeshTest object. Raises a KeyError if objects with names expected_object_name
        or test_object_name don't exist.
        :param test_object: str - Name of object of mesh type to run the operations on.
        :param expected_object: str - Name of object of mesh type that has the expected
                                geometry after running the operations.
        :param operations_stack: list - stack holding operations to perform on the test_object.
        :param apply_modifier: bool - True if we want to apply the modifiers right after adding them to the object.
                                    - True if we want to apply the modifier to a list of modifiers, after some operation.
                               This affects operations of type ModifierSpec and DeformModifierSpec.

        :param test_name: str - unique test name identifier.
        """
        if operations_stack is None:
            operations_stack = []
        for operation in operations_stack:
            if not (isinstance(operation, ModifierSpec) or isinstance(operation, OperatorSpec) or isinstance(operation, PhysicsSpec)
                    or isinstance(operation, ObjectOperatorSpec) or isinstance(operation, DeformModifierSpec)
                    or isinstance(operation, FluidDyanmicPaintSpec) or isinstance(operation, ParticleSystemSpec)):
                raise ValueError("Expected operation of type {} or {} or {} or {} or {} or {}. Got {}".
                                 format(type(ModifierSpec), type(OperatorSpec), type(PhysicsSpec),
                                        type(DeformModifierSpec), type(FluidDyanmicPaintSpec), type(ParticleSystemSpec),
                                        type(operation)))
        self.operations_stack = operations_stack
        self.apply_modifier = apply_modifiers
        self.threshold = threshold
        self.test_name = test_name

        self.verbose = os.environ.get("BLENDER_VERBOSE") is not None
        self.update = os.getenv('BLENDER_TEST_UPDATE') is not None

        # Initialize test objects.
        objects = bpy.data.objects
        self.test_object = objects[test_object_name]
        self.expected_object = objects[expected_object_name]
        if self.verbose:
            print("Found test object {}".format(test_object_name))
            print("Found test object {}".format(expected_object_name))

        # Private flag to indicate whether the blend file was updated after the test.
        self._test_updated = False

    def set_test_object(self, test_object_name):
        """
        Set test object for the test. Raises a KeyError if object with given name does not exist.
        :param test_object_name: name of test object to run operations on.
        """
        objects = bpy.data.objects
        self.test_object = objects[test_object_name]

    def set_expected_object(self, expected_object_name):
        """
        Set expected object for the test. Raises a KeyError if object with given name does not exist
        :param expected_object_name: Name of expected object.
        """
        objects = bpy.data.objects
        self.expected_object = objects[expected_object_name]

    def add_modifier(self, modifier_spec: ModifierSpec):
        """
        Add a modifier to the operations stack.
        :param modifier_spec: modifier to add to the operations stack
        """
        self.operations_stack.append(modifier_spec)
        if self.verbose:
            print("Added modifier {}".format(modifier_spec))

    def add_operator(self, operator_spec: OperatorSpec):
        """
        Adds an operator to the operations stack.
        :param operator_spec: OperatorSpec - operator to add to the operations stack.
        """
        self.operations_stack.append(operator_spec)

    def _on_failed_test(self, compare_result, validation_success, evaluated_test_object):
        if self.update and validation_success:
            if self.verbose:
                print("Test failed expectantly. Updating expected mesh...")

            # Replace expected object with object we ran operations on, i.e. evaluated_test_object.
            evaluated_test_object.location = self.expected_object.location
            expected_object_name = self.expected_object.name

            bpy.data.objects.remove(self.expected_object, do_unlink=True)
            evaluated_test_object.name = expected_object_name

            # Save file
            bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath)

            self._test_updated = True

            # Set new expected object.
            self.expected_object = evaluated_test_object
            return True

        else:
            print("Test comparison result: {}".format(compare_result))
            print("Test validation result: {}".format(validation_success))
            print("Resulting object mesh '{}' did not match expected object '{}' from file {}".
                  format(evaluated_test_object.name, self.expected_object.name, bpy.data.filepath))

            return False

    def is_test_updated(self):
        """
        Check whether running the test with BLENDER_TEST_UPDATE actually modified the .blend test file.
        :return: Bool - True if blend file has been updated. False otherwise.
        """
        return self._test_updated

    def _set_parameters_util(self, modifier, modifier_parameters, settings):
        """
        Doing a depth first traversal of the modifier parameters and setting their values.
        """
        if not isinstance(modifier_parameters, dict):
            param_setting = None
            for i, setting in enumerate(settings):
                if i == len(settings)-1:
                    setattr(modifier, setting, modifier_parameters)
                else:
                    try:
                        param_setting = getattr(modifier, setting)
                        modifier = param_setting
                    except AttributeError:
                        # Clean up first
                        bpy.ops.object.delete()
                        raise AttributeError("Modifier '{}' has no parameter named '{}'".
                                             format(modifier.name, setting))

                settings.pop()
                return

        for key in modifier_parameters:
            settings.append(key)
            self._set_parameters_util(modifier, modifier_parameters[key], settings)

        if len(settings) != 0:
            settings.pop()

    def set_parameters(self, modifier, modifier_parameters):
        """
        Outer interface for _set_parameters_util
        """
        settings = []
        modifier_copy = modifier
        self._set_parameters_util(modifier_copy, modifier_parameters, settings)

    def _add_modifier(self, test_object, modifier_spec: ModifierSpec):
        """
        Add modifier to object and apply (if modifier_spec.apply_modifier is True)
        :param test_object: bpy.types.Object - Blender object to apply modifier on.
        :param modifier_spec: ModifierSpec - ModifierSpec object with parameters
        """
        modifier = test_object.modifiers.new(modifier_spec.modifier_name,
                                             modifier_spec.modifier_type)
        if self.verbose:
            print("Created modifier '{}' of type '{}'.".
                  format(modifier_spec.modifier_name, modifier_spec.modifier_type))

        self.set_parameters(test_object.modifiers[modifier_spec.modifier_name],
                                modifier_spec.modifier_parameters)

    def _apply_modifier(self, test_object, modifier_name):
        # Modifier automatically gets applied when converting from Curve to Mesh.
        if test_object.type == 'CURVE':
            bpy.ops.object.convert(target='MESH')

        elif test_object.type == 'MESH':
            bpy.ops.object.modifier_apply(modifier=modifier_name)

        else:
            raise Exception("This object type is not yet supported!")

    def _bake_current_simulation(self, obj, test_mod_type, test_mod_name, frame_end):
        for scene in bpy.data.scenes:
            for modifier in obj.modifiers:
                if modifier.type == test_mod_type:
                    obj.modifiers[test_mod_name].point_cache.frame_end = frame_end
                    override = {'scene': scene, 'active_object': obj, 'point_cache': modifier.point_cache}
                    bpy.ops.ptcache.bake(override, bake=True)
                    break

    def _apply_physics_settings(self, test_object, physics_spec: PhysicsSpec):
        """
        Apply Physics settings to test objects.
        """
        scene = bpy.context.scene
        scene.frame_set(1)
        modifier = test_object.modifiers.new(physics_spec.modifier_name,
                                             physics_spec.modifier_type)
        physics_setting = modifier.settings
        if self.verbose:
            print("Created modifier '{}' of type '{}'.".
                  format(physics_spec.modifier_name, physics_spec.modifier_type))

        for param_name in physics_spec.modifier_parameters:
            try:
                setattr(physics_setting, param_name, physics_spec.modifier_parameters[param_name])
                if self.verbose:
                    print("\t set parameter '{}' with value '{}'".
                          format(param_name, physics_spec.modifier_parameters[param_name]))
            except AttributeError:
                # Clean up first
                bpy.ops.object.delete()
                raise AttributeError("Modifier '{}' has no parameter named '{}'".
                                     format(physics_spec.modifier_type, param_name))

        scene.frame_set(physics_spec.frame_end + 1)

        self._bake_current_simulation(test_object, physics_spec.modifier_type, physics_spec.modifier_name, physics_spec.frame_end)
        if self.apply_modifier:
            self._apply_modifier(test_object, physics_spec.modifier_name)

    def _apply_fluid_settings(self, test_object, fluid_paint_spec: FluidDyanmicPaintSpec):
        """
        Apply Fluid settings to test objects.
        """
        scene = bpy.context.scene
        scene.frame_set(1)
        modifier = test_object.modifiers.new(fluid_paint_spec.modifier_name,
                                             fluid_paint_spec.modifier_type)

        physics_setting = None
        physics_canvas_surface_settings = None

        if fluid_paint_spec.modifier_type == "FLUID":
            modifier.fluid_type = fluid_paint_spec.sub_type
            if fluid_paint_spec.sub_type == "DOMAIN":
                physics_setting = modifier.domain_settings
            elif fluid_paint_spec.sub_type == "FLOW":
                physics_setting = modifier.flow_settings
            elif fluid_paint_spec.sub_type == "EFFECTOR":
                physics_setting = modifier.effector_settings

        elif fluid_paint_spec.modifier_type == "DYNAMIC_PAINT":
            modifier.ui_type = fluid_paint_spec.sub_type

            if fluid_paint_spec.sub_type == "CANVAS":
                bpy.ops.dpaint.type_toggle(type='CANVAS')
                physics_setting = modifier.canvas_settings
            elif fluid_paint_spec.sub_type == "BRUSH":
                bpy.ops.dpaint.type_toggle(type='BRUSH')
                physics_setting = modifier.brush_settings

        if self.verbose:
            print("Created modifier '{}' of type '{}'.".
                  format(fluid_paint_spec.modifier_name, fluid_paint_spec.modifier_type))

        for param_name in fluid_paint_spec.modifier_parameters:
            try:
                setattr(physics_setting, param_name, fluid_paint_spec.modifier_parameters[param_name])
                if self.verbose:
                    print("\t set parameter '{}' with value '{}'".
                          format(param_name, fluid_paint_spec.modifier_parameters[param_name]))
            except AttributeError:
                try:
                    physics_canvas_surface_settings = physics_setting.canvas_surfaces.active
                    setattr(physics_canvas_surface_settings, param_name, fluid_paint_spec.modifier_parameters[param_name])
                    if self.verbose:
                        print("\t set parameter '{}' with value '{}'".
                              format(param_name, fluid_paint_spec.modifier_parameters[param_name]))
                except AttributeError:
                    # Clean up first
                    bpy.ops.object.delete()
                    raise AttributeError("Modifier '{}' has no parameter named '{}'".
                                         format(fluid_paint_spec.modifier_type, param_name))

        if fluid_paint_spec.modifier_type == "FLUID":
            bpy.ops.fluid.bake_all()
        elif fluid_paint_spec.modifier_type == "DYNAMIC_PAINT":
            override = {'scene': scene, 'active_object': test_object, 'point_cache': physics_canvas_surface_settings.point_cache}
            bpy.ops.ptcache.bake(override, bake=True)


        # Jump to the frame specified by user to apply the modifier.
        scene.frame_set(fluid_paint_spec.frame_end)

        if self.apply_modifier:
            self._apply_modifier(test_object, fluid_paint_spec.modifier_name)

    def _apply_particle_system(self, test_object, particle_sys_spec: ParticleSystemSpec):
        """
        Applies Particle System settings to test objects
        """
        bpy.context.scene.frame_set(0)
        bpy.ops.object.select_all(action='DESELECT')

        test_object.modifiers.new(particle_sys_spec.modifier_name, particle_sys_spec.modifier_type)

        settings_name = test_object.particle_systems.active.settings.name
        particle_setting = bpy.data.particles[settings_name]
        if self.verbose:
            print("Created modifier '{}' of type '{}'.".
                  format(particle_sys_spec.modifier_name, particle_sys_spec.modifier_type))

        for param_name in particle_sys_spec.modifier_parameters:
            try:
                if param_name == "seed":
                    system_setting = test_object.particle_systems[particle_sys_spec.modifier_name]
                    setattr(system_setting, param_name, particle_sys_spec.modifier_parameters[param_name])
                else:
                    setattr(particle_setting, param_name, particle_sys_spec.modifier_parameters[param_name])

                if self.verbose:
                    print("\t set parameter '{}' with value '{}'".
                          format(param_name, particle_sys_spec.modifier_parameters[param_name]))
            except AttributeError:
                # Clean up first
                bpy.ops.object.delete()
                raise AttributeError("Modifier '{}' has no parameter named '{}'".
                                     format(particle_sys_spec.modifier_type, param_name))

        bpy.context.scene.frame_set(20)
        test_object.select_set(True)
        bpy.ops.object.duplicates_make_real()
        test_object.select_set(True)
        bpy.ops.object.join()
        if self.apply_modifier:
            self._apply_modifier(test_object, particle_sys_spec.modifier_name)

    def _apply_operator(self, test_object, operator: OperatorSpec):
        """
        Apply operator on test object.
        :param test_object: bpy.types.Object - Blender object to apply operator on.
        :param operator: OperatorSpec - OperatorSpec object with parameters.
        """
        mesh = test_object.data
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='DESELECT')
        bpy.ops.object.mode_set(mode='OBJECT')

        # Do selection.
        bpy.context.tool_settings.mesh_select_mode = (operator.select_mode == 'VERT',
                                                      operator.select_mode == 'EDGE',
                                                      operator.select_mode == 'FACE')
        for index in operator.selection:
            if operator.select_mode == 'VERT':
                mesh.vertices[index].select = True
            elif operator.select_mode == 'EDGE':
                mesh.edges[index].select = True
            elif operator.select_mode == 'FACE':
                mesh.polygons[index].select = True
            else:
                raise ValueError("Invalid selection mode")

        # Apply operator in edit mode.
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_mode(type=operator.select_mode)
        mesh_operator = getattr(bpy.ops.mesh, operator.operator_name)
        if not mesh_operator:
            raise AttributeError("No mesh operator {}".format(operator.operator_name))
        retval = mesh_operator(**operator.operator_parameters)
        if retval != {'FINISHED'}:
            raise RuntimeError("Unexpected operator return value: {}".format(retval))
        if self.verbose:
            print("Applied operator {}".format(operator))

        bpy.ops.object.mode_set(mode='OBJECT')

    def _apply_object_operator(self, operator: ObjectOperatorSpec):
        """
        Applies the object operator.
        """
        bpy.ops.object.mode_set(mode='OBJECT')
        object_operator = getattr(bpy.ops.object, operator.operator_name)
        retval = object_operator(**operator.operator_parameters)
        print(retval)
        if not object_operator:
            raise AttributeError("No object operator {} found!".format(operator.operator_name))

        if retval != {'FINISHED'}:
            raise RuntimeError("Unexpected operator return value: {}".format(retval))
        if self.verbose:
            print("Applied operator {}".format(operator))

    def _apply_modifier_operator(self, test_object, operation):

        scene = bpy.context.scene
        scene.frame_set(1)
        bpy.ops.object.mode_set(mode='OBJECT')
        mod_ops_list = operation.modifier_list
        modifier_names = []
        ops_ops = operation.obj_operator_spec
        for mod_ops in mod_ops_list:
            if isinstance(mod_ops, ModifierSpec):
                self._add_modifier(test_object, mod_ops)
                modifier_names.append(mod_ops.modifier_name)

        if isinstance(ops_ops, ObjectOperatorSpec):
            self._apply_object_operator(ops_ops)

        print("NAME", list(test_object.modifiers))

        scene.frame_set(operation.frame_number)

        if self.apply_modifier:
            for mod_name in modifier_names:
                self._apply_modifier(test_object, mod_name)


    def run_test(self):
        """
        Apply operations in self.operations_stack on self.test_object and compare the
        resulting mesh with self.expected_object.data
        :return: bool - True if the test passed, False otherwise.
        """
        self._test_updated = False
        bpy.context.view_layer.objects.active = self.test_object

        # Duplicate test object.
        bpy.ops.object.mode_set(mode="OBJECT")
        bpy.ops.object.select_all(action="DESELECT")
        bpy.context.view_layer.objects.active = self.test_object

        self.test_object.select_set(True)
        bpy.ops.object.duplicate()
        evaluated_test_object = bpy.context.active_object
        evaluated_test_object.name = "evaluated_object"
        if self.verbose:
            print()
            print(evaluated_test_object.name, "is set to active")

        # Add modifiers and operators.
        for operation in self.operations_stack:
            if isinstance(operation, ModifierSpec):
                self._add_modifier(evaluated_test_object, operation)
                if self.apply_modifier:
                    self._apply_modifier(evaluated_test_object,operation.modifier_name)

            elif isinstance(operation, OperatorSpec):
                self._apply_operator(evaluated_test_object, operation)

            elif isinstance(operation, PhysicsSpec):
                self._apply_physics_settings(evaluated_test_object, operation)

            elif isinstance(operation, ObjectOperatorSpec):
                self._apply_object_operator(operation)

            elif isinstance(operation, DeformModifierSpec):
                self._apply_modifier_operator(evaluated_test_object, operation)

            elif isinstance(operation, FluidDyanmicPaintSpec):
                self._apply_fluid_settings(evaluated_test_object, operation)

            elif isinstance(operation, ParticleSystemSpec):
                self._apply_particle_system(evaluated_test_object, operation)

            else:
                raise ValueError("Expected operation of type {} or {} or {} or {} or {} or {}. Got {}".
                                 format(type(ModifierSpec), type(OperatorSpec), type(PhysicsSpec),
                                        type(ObjectOperatorSpec), type(FluidDyanmicPaintSpec), type(ParticleSystemSpec), type(operation)))

        # Compare resulting mesh with expected one.
        if self.verbose:
            print("Comparing expected mesh with resulting mesh...")
        evaluated_test_mesh = evaluated_test_object.data
        expected_mesh = self.expected_object.data
        if self.threshold:
            compare_result = evaluated_test_mesh.unit_test_compare(mesh=expected_mesh, threshold=self.threshold)
        else:
            compare_result = evaluated_test_mesh.unit_test_compare(mesh=expected_mesh)
        compare_success = (compare_result == 'Same')

        # Also check if invalid geometry (which is never expected) had to be corrected...
        validation_success = evaluated_test_mesh.validate(verbose=True) == False

        if compare_success and validation_success:
            if self.verbose:
                print("Success!")

            # Clean up.
            if self.verbose:
                print("Cleaning up...")
            # Delete evaluated_test_object.
            bpy.ops.object.delete()
            return True

        else:
            return self._on_failed_test(compare_result, validation_success, evaluated_test_object)


class OperatorTest:
    """
    Helper class that stores and executes operator tests.

    Example usage:

    >>> tests = [
    >>>     ['FACE', {0, 1, 2, 3, 4, 5}, 'Cubecube', 'Cubecube_result_1', 'intersect_boolean', {'operation': 'UNION'}],
    >>>     ['FACE', {0, 1, 2, 3, 4, 5}, 'Cubecube', 'Cubecube_result_2', 'intersect_boolean', {'operation': 'INTERSECT'}],
    >>> ]
    >>> operator_test = OperatorTest(tests)
    >>> operator_test.run_all_tests()
    """

    def __init__(self, operator_tests):
        """
        Constructs an operator test.
        :param operator_tests: list - list of operator test cases. Each element in the list must contain the following
         in the correct order:
             1) select_mode: str - mesh selection mode, must be either 'VERT', 'EDGE' or 'FACE'
             2) selection: set - set of vertices/edges/faces indices to select, e.g. [0, 9, 10].
             3) test_name: str - unique name for each test
             4) test_object_name: bpy.Types.Object - test object
             5) expected_object_name: bpy.Types.Object - expected object
             6) operator_name: str - name of mesh operator from bpy.ops.mesh, e.g. "bevel" or "fill"
             7) operator_parameters: dict - {name : val} dictionary containing operator parameters.
        """
        self.operator_tests = operator_tests
        self._check_for_unique()
        self.verbose = os.environ.get("BLENDER_VERBOSE") is not None
        self._failed_tests_list = []

    def _check_for_unique(self):
        """
        Check if the test name is unique
        """
        all_test_names = []
        for index, _ in enumerate(self.operator_tests):
            test_name = self.operator_tests[index][2]
            all_test_names.append(test_name)
        seen_name = set()
        for ele in all_test_names:
            if ele in seen_name:
                raise ValueError("{} is a duplicate, write a new unique name.".format(ele))
            else:
                seen_name.add(ele)

    def run_test(self, test_name: str):
        """
        Run a single test from operator_tests list
        :param test_name: str - name of test
        :return: bool - True if test is successful. False otherwise.
        """
        case = self.operator_tests[0]
        len_test = len(self.operator_tests)
        count = 0
        for index,_ in enumerate(self.operator_tests):
            if test_name == self.operator_tests[index][2]:
                case = self.operator_tests[index]
                break
            count = count + 1
        if count == len_test:
            raise Exception("No test {} found!".format(test_name))

        if len(case) != 7:
            raise ValueError("Expected exactly 7 parameters for each test case, got {}".format(len(case)))
        select_mode = case[0]
        selection = case[1]
        test_name = case[2]
        test_object_name = case[3]
        expected_object_name = case[4]
        operator_name = case[5]
        operator_parameters = case[6]

        operator_spec = OperatorSpec(operator_name, operator_parameters, select_mode, selection)

        test = MeshTest(test_name, test_object_name, expected_object_name)
        test.add_operator(operator_spec)

        success = test.run_test()
        if test.is_test_updated():
            # Run the test again if the blend file has been updated.
            success = test.run_test()
        return success

    def run_all_tests(self):
        for index, _ in enumerate(self.operator_tests):
            test_name = self.operator_tests[index][2]
            if self.verbose:
                print()
                print("Running test {}...".format(index))
            success = self.run_test(test_name)

            if not success:
                self._failed_tests_list.append(test_name)

        if len(self._failed_tests_list) != 0:
            print("Following tests failed: {}".format(self._failed_tests_list))

            blender_path = bpy.app.binary_path
            blend_path = bpy.data.filepath
            frame = inspect.stack()[1]
            module = inspect.getmodule(frame[0])
            python_path = module.__file__

            print("Run following command to open Blender and run the failing test:")
            print("{} {} --python {} -- {} {}"
                  .format(blender_path, blend_path, python_path, "--run-test", "<test_name>"))

            raise Exception("Tests {} failed".format(self._failed_tests_list))


class ModifierTest:
    """
    Helper class that stores and executes modifier tests.

    Example usage:

    >>> modifier_list = [
    >>>     ModifierSpec("firstSUBSURF", "SUBSURF", {"quality": 5}),
    >>>     ModifierSpec("firstSOLIDIFY", "SOLIDIFY", {"thickness_clamp": 0.9, "thickness": 1})
    >>> ]
    >>> tests = [
    >>>     ["Test1","testCube", "expectedCube", modifier_list],
    >>>     ["Test2","testCube_2", "expectedCube_2", modifier_list]
    >>> ]
    >>> modifiers_test = ModifierTest(tests)
    >>> modifiers_test.run_all_tests()
    """

    def __init__(self, modifier_tests: list, apply_modifiers=False, threshold=None):
        """
        Construct a modifier test.
        :param modifier_tests: list - list of modifier test cases. Each element in the list must contain the following
         in the correct order:
             0) test_name: str - unique test name
             1) test_object_name: bpy.Types.Object - test object
             2) expected_object_name: bpy.Types.Object - expected object
             3) modifiers: list - list of mesh_test.ModifierSpec objects.
        """

        self.modifier_tests = modifier_tests
        self._check_for_unique()
        self.apply_modifiers = apply_modifiers
        self.threshold = threshold
        self.verbose = os.environ.get("BLENDER_VERBOSE") is not None
        self._failed_tests_list = []

    def _check_for_unique(self):
        """
        Check if the test name is unique
        """
        all_test_names = []
        for index, _ in enumerate(self.modifier_tests):
            test_name = self.modifier_tests[index][0]
            all_test_names.append(test_name)
        seen_name = set()
        for ele in all_test_names:
            if ele in seen_name:
                raise ValueError("{} is a duplicate, write a new unique name.".format(ele))
            else:
                seen_name.add(ele)

    def run_test(self, test_name: str):
        """
        Run a single test from self.modifier_tests list
        :param test_name: str - name of test
        :return: bool - True if test passed, False otherwise.
        """
        case = self.modifier_tests[0]
        len_test = len(self.modifier_tests)
        count = 0
        for index, _ in enumerate(self.modifier_tests):
            if test_name == self.modifier_tests[index][0]:
                case = self.modifier_tests[index]
                break
            count = count+1
        if count == len_test:
            raise Exception("No test {} found!".format(test_name))

        if len(case) != 4:
            print(len(case))
            raise ValueError("Expected exactly 4 parameters for each test case, got {}".format(len(case)))
        test_name = case[0]
        test_object_name = case[1]
        expected_object_name = case[2]
        spec_list = case[3]

        test = MeshTest(test_name, test_object_name, expected_object_name, threshold=self.threshold)
        if self.apply_modifiers:
            test.apply_modifier = True

        for modifier_spec in spec_list:
            test.add_modifier(modifier_spec)

        success = test.run_test()
        if test.is_test_updated():
            # Run the test again if the blend file has been updated.
            success = test.run_test()

        return success

    def run_all_tests(self):
        """
        Run all tests in self.modifiers_tests list. Raises an exception if one the tests fails.
        """
        for index, _ in enumerate(self.modifier_tests):
            test_name = self.modifier_tests[index][0]
            if self.verbose:
                print()
                print("Running test {}...\n".format(index))
            success = self.run_test(test_name)

            if not success:
                self._failed_tests_list.append(test_name)

        if len(self._failed_tests_list) != 0:
            print("Following tests failed: {}".format(self._failed_tests_list))

            blender_path = bpy.app.binary_path
            blend_path = bpy.data.filepath
            frame = inspect.stack()[1]
            module = inspect.getmodule(frame[0])
            python_path = module.__file__

            print("Run following command to open Blender and run the failing test:")
            print("{} {} --python {} -- {} {}"
                  .format(blender_path, blend_path, python_path, "--run-test", "<test_name>"))

            raise Exception("Tests {} failed".format(self._failed_tests_list))


class DeformModifierTest:
    """
    Helper class that stores and executes deform modifier tests.

    Example usage:

    >>> deform_modifier_list = [
    >>>         MeshTest("WarpPlane", "testObjPlaneWarp", "expObjPlaneWarp",
    >>>        [DeformModifierSpec(10, [ModifierSpec('warp', 'WARP',
    >>>                                               {'object_from': bpy.data.objects["From"],
    >>>                                                'object_to': bpy.data.objects["To"],})])]),
    >>> ]
    >>> deform_test = DeformModifierTest(deform_modifier_list)
    >>> deform_test.run_all_tests()
    """

    def __init__(self, deform_tests: list, apply_modifiers=False, threshold=None):
        """
        Construct a deform modifier test.
        Each test is made up of a MeshTest Class with its parameters
        """
        self.deform_tests = deform_tests
        self._check_for_unique()
        self.apply_modifiers = apply_modifiers
        self.threshold = threshold
        self.verbose = os.environ.get("BLENDER_VERBOSE") is not None
        self._failed_tests_list = []

    def _check_for_unique(self):
        """
        Check if the test name is unique
        """
        all_test_names = []
        for index, _ in enumerate(self.deform_tests):
            test_name = self.deform_tests[index].test_name
            all_test_names.append(test_name)
        seen_name = set()
        for ele in all_test_names:
            if ele in seen_name:
                raise ValueError("{} is a duplicate, write a new unique name.".format(ele))
            else:
                seen_name.add(ele)

    def run_test(self, test_name: str):
        """
        Run a single test from self.deform_tests list
        :param test_name: int - name of test
        :return: bool - True if test passed, False otherwise.
        """
        case = self.deform_tests[0]
        len_test = len(self.deform_tests)
        count = 0
        for index, _ in enumerate(self.deform_tests):
            if test_name == self.deform_tests[index].test_name:
                case = self.deform_tests[index]
                break
            count = count+1

        if count == len_test:
            raise Exception('No test called {} found!'.format(test_name))

        test = case
        if self.apply_modifiers:
            test.apply_modifier = True

        success = test.run_test()
        if test.is_test_updated():
            # Run the test again if the blend file has been updated.
            success = test.run_test()

        return success

    def run_all_tests(self):
        """
        Run all tests in self.modifiers_tests list. Raises an exception if one the tests fails.
        """
        for index, _ in enumerate(self.deform_tests):
            test_name = self.deform_tests[index].test_name
            if self.verbose:
                print()
                print("Running test {}...\n".format(index))
            success = self.run_test(test_name)

            if not success:
                self._failed_tests_list.append(test_name)

        if len(self._failed_tests_list) != 0:
            print("Following tests failed: {}".format(self._failed_tests_list))

            blender_path = bpy.app.binary_path
            blend_path = bpy.data.filepath
            frame = inspect.stack()[1]
            module = inspect.getmodule(frame[0])
            python_path = module.__file__

            print("Run following command to open Blender and run the failing test:")
            print("{} {} --python {} -- {} {}"
                  .format(blender_path, blend_path, python_path, "--run-test", "<test_name>"))

            raise Exception("Tests {} failed".format(self._failed_tests_list))
