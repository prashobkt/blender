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

import os
import sys

import bpy

sys.path.append(os.path.dirname(os.path.realpath(__file__)))
from modules.mesh_test import ModifierTest, ModifierSpec


def main():
    test = [
        #
        # ["FluidLiquid", "test", "exp",
        #  [FluidSpec('fluid_liquid', 'FLUID', 'DOMAIN', {'domain_type': 'LIQUID', 'use_mesh': True, 'cache_type': 'ALL',
        #                                        'cache_frame_end': 20}, 20)]],

        ["FluidLiquid", "test", "exp",
         [ModifierSpec('fluid_liquid', 'FLUID', {'fluid_type': 'DOMAIN', 'domain_settings': {'domain_type': 'LIQUID', 'use_mesh': True, 'cache_type': 'ALL',
                                                                                           'cache_frame_end': 20}})]],

    ]
    fluid_test = ModifierTest(test)

    command = list(sys.argv)
    for i, cmd in enumerate(command):
        if cmd == "--run-all-tests":
            fluid_test.apply_modifiers = True
            fluid_test.run_all_tests()
            break
        elif cmd == "--run-test":
            fluid_test.apply_modifiers = False
            name = str(command[i + 1])
            fluid_test.run_test(name)
            break


if __name__ == "__main__":
    main()
