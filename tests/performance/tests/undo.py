import api
import os

def _run(args):
    import bpy
    import time

    bpy.context.preferences.view.show_developer_ui = True
    bpy.context.preferences.experimental.use_undo_speedup = True

    bpy.ops.ed.undo_push()
    bpy.ops.mesh.primitive_cube_add()
    bpy.ops.object.modifier_add(type='SUBSURF')
    bpy.context.object.modifiers["Subdivision"].levels = 10
    bpy.ops.ed.undo_push()
    bpy.ops.transform.translate(value=(1.0, 1.0, 1.0))
    bpy.ops.ed.undo_push()
    bpy.context.evaluated_depsgraph_get()

    start_time = time.time()
    bpy.ops.ed.undo()
    bpy.context.evaluated_depsgraph_get()
    elapsed_time = time.time() - start_time

    result = {'time': elapsed_time}
    return result

class UndoTest(api.Test):
    def __init__(self):
        pass

    def name(self):
        return 'undo_translation'

    def run(self, env):
        return env.run_in_blender(_run, {})

def generate(env):
    return [UndoTest()]
