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

# To run all tests, use
# BLENDER_VERBOSE=1 blender path/to/bevel_regression.blend --python path/to/bevel_operator.py -- --run-all-tests
# To run one test, use
# BLENDER_VERBOSE=1 blender path/to/bevel_regression.blend --python path/to/bevel_operator.py -- --run-test <index>
# where <index> is the index of the test specified in the list tests.

import bpy
import os
import sys

sys.path.append(os.path.dirname(os.path.realpath(__file__)))
from modules.mesh_test import OperatorTest


def main():
    tests = [
        # 0
        ['EDGE', {10}, "test 1",  'Cube_test', 'Cube_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {10, 7}, "test 2",  'Cube_test', 'Cube_result_2', 'bevel', {'offset': 0.2, 'offset_type': 'WIDTH'}],
        ['EDGE', {8, 10, 7}, "test 3",  'Cube_test', 'Cube_result_3', 'bevel', {'offset': 0.2, 'offset_type': 'DEPTH'}],
        ['EDGE', {10}, "test 4",  'Cube_test', 'Cube_result_4', 'bevel', {'offset': 0.4, 'segments': 2}],
        ['EDGE', {10, 7}, "test 5",  'Cube_test', 'Cube_result_5', 'bevel', {'offset': 0.4, 'segments': 3}],
        # 5
        ['EDGE', {8, 10, 7}, "test 6",  'Cube_test', 'Cube_result_6', 'bevel', {'offset': 0.4, 'segments': 4}],
        ['EDGE', {0, 10, 4, 7}, "test 7",  'Cube_test', 'Cube_result_7', 'bevel', {'offset': 0.4, 'segments': 5, 'profile': 0.2}],
        ['EDGE', {8, 10, 7}, "test 8",  'Cube_test', 'Cube_result_8', 'bevel', {'offset': 0.4, 'segments': 5, 'profile': 0.25}],
        ['EDGE', {8, 10, 7}, "test 9",  'Cube_test', 'Cube_result_9', 'bevel', {'offset': 0.4, 'segments': 6, 'profile': 0.9}],
        ['EDGE', {10, 7}, "test 10",  'Cube_test', 'Cube_result_10', 'bevel', {'offset': 0.4, 'segments': 4, 'profile': 1.0}],
        # 10
        ['EDGE', {8, 10, 7}, "test 11",  'Cube_test', 'Cube_result_11', 'bevel', {'offset': 0.4, 'segments': 5, 'profile': 1.0}],
        ['EDGE', {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, "test 12",  'Cube_test', 'Cube_result_12', 'bevel',
         {'offset': 0.4, 'segments': 8}],
        ['EDGE', {5}, "test 13",  'Pyr4_test', 'Pyr4_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {2, 5}, "test 14",  'Pyr4_test', 'Pyr4_result_2', 'bevel', {'offset': 0.2}],
        ['EDGE', {2, 3, 5}, "test 15",  'Pyr4_test', 'Pyr4_result_3', 'bevel', {'offset': 0.2}],
        # 15
        ['EDGE', {1, 2, 3, 5}, "test 16",  'Pyr4_test', 'Pyr4_result_4', 'bevel', {'offset': 0.2}],
        ['EDGE', {1, 2, 3, 5}, "test 17",  'Pyr4_test', 'Pyr4_result_5', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['EDGE', {2, 3}, "test 18",  'Pyr4_test', 'Pyr4_result_6', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['EDGE', {1, 2, 3, 5}, "test 19",  'Pyr4_test', 'Pyr4_result_7', 'bevel', {'offset': 0.2, 'segments': 4, 'profile': 0.15}],
        ['VERT', {1}, "test 20",  'Pyr4_test', 'Pyr4_result_8', 'bevel', {'offset': 0.75, 'segments': 4, 'affect': 'VERTICES'}],
        # 20
        ['VERT', {1}, "test 21",  'Pyr4_test', 'Pyr4_result_9', 'bevel',
         {'offset': 0.75, 'segments': 3, 'affect': 'VERTICES', 'profile': 0.25}],
        ['EDGE', {2, 3}, "test 22",  'Pyr6_test', 'Pyr6_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {8, 2, 3}, "test 23",  'Pyr6_test', 'Pyr6_result_2', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['EDGE', {0, 2, 3, 4, 6, 7, 9, 10, 11}, "test 24",  'Pyr6_test', 'Pyr6_result_3', 'bevel',
         {'offset': 0.2, 'segments': 4, 'profile': 0.8}],
        ['EDGE', {8, 9, 3, 11}, "test 25",  'Sept_test', 'Sept_result_1', 'bevel', {'offset': 0.1}],
        # 25
        ['EDGE', {8, 9, 11}, "test 26",  'Sept_test', 'Sept_result_2', 'bevel', {'offset': 0.1, 'offset_type': 'WIDTH'}],
        ['EDGE', {2, 8, 9, 12, 13, 14}, "test 27",  'Saddle_test', 'Saddle_result_1', 'bevel', {'offset': 0.3, 'segments': 5}],
        ['VERT', {4}, "test 28",  'Saddle_test', 'Saddle_result_2', 'bevel', {'offset': 0.6, 'segments': 6, 'affect': 'VERTICES'}],
        ['EDGE', {2, 5, 8, 11, 14, 18, 21, 24, 27, 30, 34, 37, 40, 43, 46, 50, 53, 56, 59, 62, 112, 113, 114, 115}, "test 29",

         'Bent_test', 'Bent_result_1', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['EDGE', {1, 8, 9, 10, 11}, "test 31",  'Bentlines_test', 'Bentlines_result_1', 'bevel', {'offset': 0.2, 'segments': 3}],
        # 30
        ['EDGE', {26, 12, 20}, "test 32",  'Flaretop_test', 'Flaretop_result_1', 'bevel', {'offset': 0.4, 'segments': 2}],
        ['EDGE', {26, 12, 20}, "test 33",  'Flaretop_test', 'Flaretop_result_2', 'bevel',
         {'offset': 0.4, 'segments': 2, 'profile': 1.0}],
        ['FACE', {1, 6, 7, 8, 9, 10, 11, 12}, "test 34",  'Flaretop_test', 'Flaretop_result_3', 'bevel',
         {'offset': 0.4, 'segments': 4}],
        ['EDGE', {4, 8, 10, 18, 24}, "test 35",  'BentL_test', 'BentL_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {0, 1, 2, 10}, "test 36",  'Wires_test', 'Wires_test_result_1', 'bevel', {'offset': 0.3}],
        # 35
        ['VERT', {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, "test 37",  'Wires_test', 'Wires_test_result_2', 'bevel',
         {'offset': 0.3, 'affect': 'VERTICES'}],
        ['EDGE', {3, 4, 5}, "test 38",  'tri', 'tri_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {3, 4, 5}, "test 39",  'tri', 'tri_result_2', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['EDGE', {3, 4, 5}, "test 40",  'tri', 'tri_result_3', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['EDGE', {3, 4}, "test 41",  'tri', 'tri_result_4', 'bevel', {'offset': 0.2}],
        # 40
        ['EDGE', {3, 4}, "test 42",  'tri', 'tri_result_5', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['VERT', {3}, "test 43",  'tri', 'tri_result_6', 'bevel', {'offset': 0.2, 'affect': 'VERTICES'}],
        ['VERT', {3}, "test 44",  'tri', 'tri_result_7', 'bevel', {'offset': 0.2, 'segments': 2, 'affect': 'VERTICES'}],
        ['VERT', {3}, "test 45",  'tri', 'tri_result_8', 'bevel', {'offset': 0.2, 'segments': 3, 'affect': 'VERTICES'}],
        ['VERT', {1}, "test 46",  'tri', 'tri_result_9', 'bevel', {'offset': 0.2, 'affect': 'VERTICES'}],
        # 45
        ['EDGE', {3, 4, 5}, "test 47",  'tri1gap', 'tri1gap_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {3, 4, 5}, "test 48",  'tri1gap', 'tri1gap_result_2', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['EDGE', {3, 4, 5}, "test 49",  'tri1gap', 'tri1gap_result_3', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['EDGE', {3, 4}, "test 50",  'tri1gap', 'tri1gap_result_4', 'bevel', {'offset': 0.2}],
        ['EDGE', {3, 4}, "test 51",  'tri1gap', 'tri1gap_result_5', 'bevel', {'offset': 0.2, 'segments': 2}],
        # 50
        ['EDGE', {3, 4}, "test 52",  'tri1gap', 'tri1gap_result_6', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['EDGE', {3, 5}, "test 53",  'tri1gap', 'tri1gap_result_7', 'bevel', {'offset': 0.2}],
        ['EDGE', {3, 5}, "test 54",  'tri1gap', 'tri1gap_result_8', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['EDGE', {3, 5}, "test 55",  'tri1gap', 'tri1gap_result_9', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['VERT', {3}, "test 56",  'tri1gap', 'tri1gap_result_10', 'bevel', {'offset': 0.2, 'affect': 'VERTICES'}],
        # 55
        ['EDGE', {3, 4, 5}, "test 57",  'tri2gaps', 'tri2gaps_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {3, 4, 5}, "test 58",  'tri2gaps', 'tri2gaps_result_2', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['EDGE', {3, 4, 5}, "test 59",  'tri2gaps', 'tri2gaps_result_3', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['EDGE', {3, 4}, "test 60",  'tri2gaps', 'tri2gaps_result_4', 'bevel', {'offset': 0.2}],
        ['EDGE', {3, 4}, "test 61",  'tri2gaps', 'tri2gaps_result_5', 'bevel', {'offset': 0.2, 'segments': 2}],
        # 60
        ['EDGE', {3, 4}, "test 62",  'tri2gaps', 'tri2gaps_result_6', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['EDGE', {3, 4, 5}, "test 63",  'tri3gaps', 'tri3gaps_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {3, 4, 5}, "test 64",  'tri3gaps', 'tri3gaps_result_2', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['EDGE', {3, 4, 5}, "test 65",  'tri3gaps', 'tri3gaps_result_3', 'bevel', {'offset': 0.2, 'segments': 3}],
        ['EDGE', {32, 33, 34, 35, 24, 25, 26, 27, 28, 29, 30, 31}, "test 66",  'cube3', 'cube3_result_1', 'bevel', {'offset': 0.2}],
        # 65
        ['EDGE', {32, 33, 34, 35, 24, 25, 26, 27, 28, 29, 30, 31}, "test 67",  'cube3', 'cube3_result_2', 'bevel',
         {'offset': 0.2, 'segments': 2}],
        ['EDGE', {32, 35}, "test 68",  'cube3', 'cube3_result_3', 'bevel', {'offset': 0.2}],
        ['EDGE', {24, 35}, "test 69",  'cube3', 'cube3_result_4', 'bevel', {'offset': 0.2}],
        ['EDGE', {24, 32, 35}, "test 70",  'cube3', 'cube3_result_5', 'bevel', {'offset': 0.2, 'segments': 2}],
        ['EDGE', {24, 32, 35}, "test 71",  'cube3', 'cube3_result_6', 'bevel', {'offset': 0.2, 'segments': 3}],
        # 70
        ['EDGE', {0, 1, 6, 7, 12, 14, 16, 17}, "test 72",  'Tray', 'Tray_result_1', 'bevel', {'offset': 0.01, 'segments': 2}],
        ['EDGE', {33, 4, 38, 8, 41, 10, 42, 12, 14, 17, 24, 31}, "test 73",  'Bumptop', 'Bumptop_result_1', 'bevel',
         {'offset': 0.1, 'segments': 4}],
        ['EDGE', {16, 14, 15}, "test 74",  'Multisegment_test', 'Multisegment_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {16, 14, 15}, "test 75",  'Multisegment_test', 'Multisegment_result_1', 'bevel', {'offset': 0.2}],
        ['EDGE', {19, 20, 23, 15}, "test 76",  'Window_test', 'Window_result_1', 'bevel', {'offset': 0.05, 'segments': 2}],
        # 75
        ['EDGE', {8}, "test 77",  'Cube_hn_test', 'Cube_hn_result_1', 'bevel', {'offset': 0.2, 'harden_normals': True}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 78",  'Blocksteps_test', 'Blocksteps_result_1', 'bevel',
         {'offset': 0.2, 'miter_outer': 'PATCH'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 79",  'Blocksteps_test', 'Blocksteps_result_2', 'bevel',
         {'offset': 0.2, 'segments': 2, 'miter_outer': 'PATCH'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 80",  'Blocksteps_test', 'Blocksteps_result_3', 'bevel',
         {'offset': 0.2, 'segments': 3, 'miter_outer': 'PATCH'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 81",  'Blocksteps_test', 'Blocksteps_result_4', 'bevel',
         {'offset': 0.2, 'miter_outer': 'ARC'}],
        # 80
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 82",  'Blocksteps_test', 'Blocksteps_result_5', 'bevel',
         {'offset': 0.2, 'segments': 2, 'miter_outer': 'ARC'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 83",  'Blocksteps_test', 'Blocksteps_result_6', 'bevel',
         {'offset': 0.2, 'segments': 3, 'miter_outer': 'ARC'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 84",  'Blocksteps_test', 'Blocksteps_result_7', 'bevel',
         {'offset': 0.2, 'miter_outer': 'PATCH', 'miter_inner': 'ARC'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 85",  'Blocksteps_test', 'Blocksteps_result_8', 'bevel',
         {'offset': 0.2, 'segments': 2, 'miter_outer': 'PATCH', 'miter_inner': 'ARC'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 86",  'Blocksteps2_test', 'Blocksteps2_result_9', 'bevel',
         {'offset': 0.2, 'segments': 2, 'miter_outer': 'ARC'}],
        # 85
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 87",  'Blocksteps3_test', 'Blocksteps3_result_10', 'bevel',
         {'offset': 0.2, 'segments': 2, 'miter_outer': 'ARC'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 88",  'Blocksteps4_test', 'Blocksteps4_result_11', 'bevel',
         {'offset': 0.2, 'segments': 2, 'miter_outer': 'ARC'}],
        ['EDGE', {4, 7, 39, 27, 30, 31}, "test 89",  'Blocksteps4_test', 'Blocksteps4_result_12', 'bevel',
         {'offset': 0.2, 'segments': 3, 'miter_outer': 'ARC'}],
        ['EDGE', {1, 7}, "test 90",  'Spike_test', 'Spike_result_1', 'bevel', {'offset': 0.2, 'segments': 3}]
    ]
    operator_test = OperatorTest(tests)

    command = list(sys.argv)
    for i, cmd in enumerate(command):
        if cmd == "--run-all-tests":
            operator_test.run_all_tests()
            break
        elif cmd == "--run-test":
            name = str(command[i + 1])
            operator_test.run_test(name)
            break


if __name__ == "__main__":
    main()
