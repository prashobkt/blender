
from . import TestEnvironment

class TestDevice:
    def __init__(self, name: str):
        self.name = name

class TestMachine:
    def __init__(self, env: TestEnvironment):
        # TODO: implement device detection, matching Blender Benchmark.
        self.devices = [TestDevice('CPU')]

    def cpu_device(self) -> str:
        return self.devices[0]

