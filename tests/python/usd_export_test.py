#!/usr/bin/env python3.7
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

import argparse
import collections
import dataclasses
import pathlib
import sys
import unittest
from typing import Set, List

from modules.test_utils import (
    with_tempdir,
    AbstractBlenderRunnerTest,
)

class AbstractUSDTest(AbstractBlenderRunnerTest):
    @classmethod
    def setUpClass(cls):
        cls.blender = args.blender
        cls.testdir = pathlib.Path(args.testdir)

# Defined here at top-level so that the string's contents don't get indented.
# The opening parentheses on some lines are an indication that these "def"s
# are referencing another one, i.e. they're instances.
# The lines are sorted alphabetically to allow stable comparison.
expected_hierarchy = """<root>
def "_materials"
    def Material "Head"
        def Shader "previewShader"
    def Material "Nose"
        def Shader "previewShader"
def Xform "Camera"
    def Camera "Camera"
def Xform "Dupli1"
    def Xform "GEO_Head_0"
        def Mesh "Face" (
        def Xform "GEO_Ear_L_1"
            def Mesh "Ear" (
        def Xform "GEO_Ear_R_2"
            def Mesh "Ear" (
        def Xform "GEO_Nose_3"
            def Mesh "Nose" (
def Xform "Ground_plane"
    def Mesh "Plane"
    def Xform "OutsideDupliGrandParent"
        def Xform "OutsideDupliParent"
            def Xform "GEO_Head"
                def Mesh "Face"
                def Xform "GEO_Ear_L"
                    def Mesh "Ear"
                def Xform "GEO_Ear_R"
                    def Mesh "Ear"
                def Xform "GEO_Nose"
                    def Mesh "Nose"
def Xform "ParentOfDupli2"
    def Mesh "Icosphere"
    def Xform "Dupli2"
        def Xform "GEO_Head_0"
            def Mesh "Face" (
            def Xform "GEO_Ear_L_1"
                def Mesh "Ear" (
            def Xform "GEO_Ear_R_2"
                def Mesh "Ear" (
            def Xform "GEO_Nose_3"
                def Mesh "Nose" (
""".strip()


@dataclasses.dataclass
class Node:
    children: Set['Node']
    line: str
    level: int

    def __eq__(self, other: 'Node') -> bool:
        return (self.level, self.line) == (other.level, other.line)

    def __lt__(self, other: 'Node') -> bool:
        return (self.level, self.line) < (other.level, other.line)

    def __hash__(self) -> int:
        return hash((self.level, self.line))

    def __str__(self) -> str:
        return self.line

    def to_sorted_lines(self) -> str:
        my_indent = self.level * ' '
        lines = [f'{my_indent}{self.line}']

        for child in sorted(self.children):
            lines.append(child.to_sorted_lines())

        return '\n'.join(lines)


def parse_usda_hierarchy(hierarchy_lines: List[str]) -> Node:
    """Build a representation based on indent + 'def' keywords"""

    root = Node(children=set(), line='<root>', level=-1)
    parents: List[Node] = [root]

    for line in hierarchy_lines:
        unindented = line.lstrip()
        if not unindented.startswith('def '):
            continue

        level = len(line) - len(unindented)

        while level <= parents[-1].level:
            parents.pop()
        parent_node: Node = parents[-1]

        node = Node(children=set(), line=unindented, level=level)
        parent_node.children.add(node)
        parents.append(node)

    return root

class USDExportTest(AbstractUSDTest):
    @with_tempdir
    def test_export(self, tempdir: pathlib.Path):
        """Very minimal test for USD exports."""
        usd = tempdir / 'usd_hierarchy_export_test.usda'
        script = f"import bpy; bpy.ops.wm.usd_export(filepath='{usd.as_posix()}'," \
            " use_instancing=True)"
        output = self.run_blender('usd_hierarchy_export_test.blend', script)

        # Do some minimal assertions on the USDA file.
        self.assertTrue(usd.exists(), f"File {usd} should exist: {output}")
        usd_contents = usd.read_text(encoding='utf8')
        self.assertIn('metersPerUnit = 1', usd_contents[:256],
            f"Basic test failed, probably more info in Blender's output:\n\n{output}\n\n---")
        self.assertIn('upAxis = "Z"', usd_contents[:256])

        # Uncomment to get a copy of the file that won't be deleted.
        # import shutil
        # shutil.copy(usd, "/tmp/usd_hierarchy_export_test.usda")

        root = parse_usda_hierarchy(usd_contents.splitlines())
        self.maxDiff = 2 * len(expected_hierarchy)
        self.assertEqual(expected_hierarchy, root.to_sorted_lines())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--blender', required=True)
    parser.add_argument('--testdir', required=True)
    args, remaining = parser.parse_known_args()

    unittest.main(argv=sys.argv[0:1] + remaining)
