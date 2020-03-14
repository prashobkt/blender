
import abc
from . import TestEnvironment
from typing import Dict

class Test:
    @abc.abstractmethod
    def name(self) -> str:
        """
        Name of the test.
        """

    def use_device(self) -> bool:
        """
        Test uses a specific CPU or GPU device.
        """
        return False

    @abc.abstractmethod
    def run(self, env: TestEnvironment) -> Dict:
        """
        Execute the test and report results.
        """

class TestCollection:
    def __init__(self, env: TestEnvironment):
        import importlib
        import pkgutil
        import tests

        self.tests = []

        for _, modname, _ in pkgutil.iter_modules(tests.__path__, 'tests.'):
            module = importlib.import_module(modname)
            self.tests += module.generate(env)

    def find(self, test_name: str):
        for test in self.tests:
            if test.name() == test_name:
                return test

        return None
