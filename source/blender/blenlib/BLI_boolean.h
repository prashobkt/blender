/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __BLI_BOOLEAN_H__
#define __BLI_BOOLEAN_H__

/** \file
 * \ingroup bli
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bool_optype {
  BOOLEAN_NONE = -1,
  /* Aligned with BooleanModifierOp. */
  BOOLEAN_ISECT = 0,
  BOOLEAN_UNION = 1,
  BOOLEAN_DIFFERENCE = 2,
} bool_optype;

typedef struct Boolean_trimesh_input {
  int vert_len;
  int tri_len;
  float (*vert_coord)[3];
  int (*tri)[3];
} Boolean_trimesh_input;

typedef struct Boolean_trimesh_output {
  int vert_len;
  int tri_len;
  float (*vert_coord)[3];
  int (*tri)[3];
} Boolean_trimesh_output;

Boolean_trimesh_output *BLI_boolean_trimesh(const Boolean_trimesh_input *in0,
                                            const Boolean_trimesh_input *in1,
                                            int bool_optype);

void BLI_boolean_trimesh_free(Boolean_trimesh_output *output);

#ifdef __cplusplus
}
#endif

#endif /* __BLI_BOOLEAN_H__ */
