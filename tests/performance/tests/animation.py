import api
import os

def _run(args):
    import bpy
    import time

    scene = bpy.context.scene
    scene.frame_set(scene.frame_start)

    start_time = time.time()
    for frame in range(scene.frame_start + 1, scene.frame_end + 1):
        scene.frame_set(frame)
        bpy.context.evaluated_depsgraph_get()
    elapsed_time = time.time() - start_time

    result = {'time': elapsed_time}
    return result

class AnimationTest(api.Test):
    def __init__(self, filepath):
        self.filepath = filepath

    def name(self):
        return 'animation_' + self.filepath.stem

    def run(self, env):
        return env.run_in_blender(_run, {}, blendfile=self.filepath)

def generate(env):
    filepaths = env.find_blend_files('animation')
    return [AnimationTest(filepath) for filepath in filepaths]
