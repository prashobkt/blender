import api
import os

def _run(args):
    import bpy
    import time

    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 4

    start_time = time.time()
    bpy.ops.render.render()
    elapsed_time = time.time() - start_time
    result = {'time': elapsed_time}
    return result

class CyclesTest(api.Test):
    def __init__(self, filepath):
        self.filepath = filepath

    def name(self):
        return 'cycles_' + self.filepath.stem

    def use_device(self):
        return True

    def run(self, env):
        return env.run_in_blender(_run, {}, blendfile=self.filepath)

def generate(env):
    filepaths = env.find_blend_files('cycles')
    return [CyclesTest(filepath) for filepath in filepaths]
