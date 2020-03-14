
import base64
import inspect
import logging
import logging.handlers
import os
import multiprocessing
import pathlib
import pickle
import subprocess
import sys
from typing import Callable, Dict, List

class TestEnvironment:
    def __init__(self):
        # Directory paths.
        self.repo_dir = pathlib.Path(__file__).parent.parent.parent.parent
        self.base_dir = self.repo_dir.parent / 'benchmark'
        self.blender_dir = self.base_dir / 'blender.git'
        self.build_dir = self.base_dir / 'build'
        self.lib_dir = self.base_dir / 'lib'
        self.benchmarks_dir = self.lib_dir / 'benchmarks'

        # Executable paths.
        if sys.platform == 'darwin':
            blender_executable = 'Blender.app/Contents/MacOS/Blender'
        elif sys.platform == 'win32':
            blender_executable = 'blender.exe'
        else:
            blender_executable = 'blender'

        self.blender_executable = self.build_dir / 'bin' / blender_executable
        self.git_executable = 'git'
        self.cmake_executable = 'cmake'

        self.logger = None
        self._init_logger()

    def _init_logger(self):
        # Logging.
        if os.path.isdir(self.base_dir) and not self.logger:
            log = self.base_dir / 'command.log'
            maxbytes = 5 * 1024 * 1024
            self.logger = logging.getLogger('Blender Benchmark')
            self.logger.setLevel(logging.INFO)
            handler = logging.handlers.RotatingFileHandler(log, maxBytes=maxbytes, backupCount=0)
            self.logger.addHandler(handler)

    def validate(self) -> bool:
        benchmarks_dir = self.repo_dir.parent / 'lib' / 'benchmarks'
        if not os.path.isdir(benchmarks_dir):
            return 'Warning: benchmarks not found at ' + str(benchmarks_dir)
        return None

    def initialized(self) -> bool:
        return os.path.isdir(self.base_dir) and \
               os.path.isdir(self.blender_dir) and \
               os.path.isdir(self.build_dir) and \
               os.path.isdir(self.benchmarks_dir)

    def init(self) -> None:
        blender_dir = self.repo_dir
        lib_dir = self.repo_dir.parent / 'lib'

        if not os.path.isdir(self.base_dir):
            print("Creating", self.base_dir)
            os.makedirs(self.base_dir, exist_ok=True)

        self._init_logger()

        if not os.path.isdir(self.lib_dir):
            print("Creating symlink at", self.lib_dir)
            os.symlink(lib_dir, self.lib_dir, target_is_directory=True)
        if not os.path.isdir(self.blender_dir):
            print("Creating git worktree in", self.blender_dir)
            self.call([self.git_executable, 'worktree', 'add', self.blender_dir, 'HEAD'], blender_dir)

        # Setup build directory.
        print("Configuring cmake in", self.build_dir)
        os.makedirs(self.build_dir, exist_ok=True)
        cmakecache = self.build_dir / 'u.txt'
        if os.path.isfile(cmakecache):
            os.remove(cmakecache)
        cmake_options = ['-DWITH_CYCLES_NATIVE_ONLY=ON',
                         '-DWITH_BUILDINFO=OFF',
                         '-DWITH_INTERNATIONAL=OFF']
        self.call([self.cmake_executable, self.blender_dir] + cmake_options, self.build_dir)
        print("Done")

    def current_revision(self) -> str:
        lines = self.call([self.git_executable, 'rev-parse', '--short=7', 'HEAD'], self.repo_dir)
        return lines[0].strip()

    def build_revision(self, revision: str) -> None:
        # Checkout Blender revision
        self.call([self.git_executable, 'clean', '-f', '-d'], self.blender_dir)
        self.call([self.git_executable, 'reset', '--hard', 'HEAD'], self.blender_dir)
        self.call([self.git_executable, 'fetch', 'origin'], self.blender_dir)
        self.call([self.git_executable, 'checkout', '--detach', revision], self.blender_dir)

        # Update submodules not needed for now
        # make_update = self.blender_dir / 'build_files' / 'utils' / 'make_update.py'
        # self.call([sys.executable, make_update, '--no-libraries', '--no-blender'], self.blender_dir)

        # Build
        self.call([self.cmake_executable,
                   '--build', '.',
                   '--parallel', str(multiprocessing.cpu_count()),
                   '--target', 'install',
                   '--config', 'Release'],
                  self.build_dir)

    def info(self, msg):
        if self.logger:
            self.logger.info(msg)
        else:
            print(msg)

    def call(self, args: List[str], cwd: pathlib.Path, silent=False) -> List[str]:
        """Execute command with arguments in specified directory,
           and return combined stdout and stderr output."""
        self.info("$ " + " ".join([str(arg) for arg in args]))
        proc = subprocess.Popen(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        lines = []
        while proc.poll() is None:
            line = proc.stdout.readline()
            if line:
                line = line.decode('utf-8', 'ignore')
                self.info(line.strip())
                lines += [line]

        if proc.returncode != 0 and not silent:
            raise Exception("Error executing command")

        return lines

    def call_blender(self, args: List[str], foreground=False) -> List[str]:
        """Execute Blender command with arguments"""
        common_args = ['--factory-startup', '--enable-autoexec']
        if foreground:
            common_args += ['--no-window-focus', '--window-geometry', '0', '0', '1024', '768']
        else:
            common_args += ['--background']

        return self.call([self.blender_executable] + common_args + args, cwd=self.base_dir)

    def run_in_blender(self, function: Callable[[Dict], Dict], args: Dict, blendfile=None, foreground=False) -> Dict:
        """Run function in a Blender instance. Arguments and return values are
           passed as a dictionary that must be serializable with pickle."""
        function_path = os.path.abspath(inspect.getfile(function))

        # Get information to call this function from Blender.
        package_path = pathlib.Path(__file__).parent.parent
        functionname = function.__name__
        modulename = inspect.getmodule(function).__name__

        # Serialize arguments in base64, to avoid having to escape it.
        args = base64.b64encode(pickle.dumps(args))
        output_prefix = 'TEST_OUTPUT: '

        expression = (f'import sys, pickle, base64\n'
                      f'sys.path.append("{package_path}")\n'
                      f'import {modulename}\n'
                      f'args = pickle.loads(base64.b64decode({args}))\n'
                      f'result = {modulename}.{functionname}(args)\n'
                      f'result = base64.b64encode(pickle.dumps(result))\n'
                      f'print("{output_prefix}" + result.decode())\n')

        blender_args = []
        if blendfile:
            blender_args += [blendfile]
        blender_args += ['--python-expr', expression]
        lines = self.call_blender(blender_args, foreground=foreground)

        # Parse output.
        for line in lines:
            if line.startswith(output_prefix):
                output = line[len(output_prefix):].strip()
                result = pickle.loads(base64.b64decode(output))
                return result

        return {}

    def find_blend_files(self, dirname):
        """
        Search for <name>.blend or <name>/<name>.blend files in the given directory
        under lib/benchmarks.
        """
        dirpath = self.benchmarks_dir / dirname
        filepaths = []
        if os.path.isdir(dirpath):
            for filename in os.listdir(dirpath):
                filepath = dirpath / filename
                if os.path.isfile(filepath) and filename.endswith('.blend'):
                    filepaths += [filepath]
                elif os.path.isdir(filepath):
                    filepath = filepath / (filename + ".blend")
                    if os.path.isfile(filepath):
                        filepaths += [filepath]

        return filepaths
