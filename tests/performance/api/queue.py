
import json
import os
from . import TestEnvironment
from typing import Dict

class TestQueue:
    def __init__(self, env: TestEnvironment):
        self.filepath = env.base_dir / 'queue.json'

        if os.path.isfile(self.filepath):
            with open(self.filepath, 'r') as f:
                self.entries = json.load(f)
        else:
            self.entries = []

    def find(self, revision: str, test: str, device: str) -> Dict:
        for entry in self.entries:
            if entry['revision'] == revision and entry['test'] == test and entry['device'] == device:
                return entry

        return None

    def add(self, revision: str, test: str, device: str) -> Dict:
        if self.find(revision, test, device):
            return None

        entry = {'revision': revision,
                 'test': test,
                 'device': device,
                 'status': 'queued',
                 'output': {}}
        self.entries += [entry]
        return entry

    def update(self, entry: Dict) -> None:
        existing = self.find(entry['revision'], entry['test'], entry['device'])
        if existing:
            existing['status'] = entry['status']
            existing['output'] = entry['output']

    def remove(self, entry: Dict) -> Dict:
        self.entries.remove(entry)
        entry['status'] = 'removed'
        return entry

    def write(self) -> None:
        # TODO: lock file to avoid multiple processes overwrting each other.
        with open(self.filepath, 'w') as f:
            json.dump(self.entries, f)
