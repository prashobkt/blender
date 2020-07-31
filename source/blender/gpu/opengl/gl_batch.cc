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
 *
 * The Original Code is Copyright (C) 2016 by Mike Erwin.
 * All rights reserved.
 */

/** \file
 * \ingroup gpu
 */

#include "BLI_assert.h"
#include "BLI_utildefines.h"

#include "GPU_glew.h"

#include "gl_context.hh"

using namespace blender::gpu;

void GLContext::draw_batch(GPUBatch *batch, int v_first, int v_count, int i_first, int i_count)
{
  /* Verify there is enough data do draw. */
  /* TODO(fclem) Nice to have but this is invalid when using procedural draw-calls.
   * The right assert would be to check if there is an enabled attribute from each VBO
   * and check their length. */
  // BLI_assert(i_first + i_count <= (batch->inst ? batch->inst->vertex_len : INT_MAX));
  // BLI_assert(v_first + v_count <=
  //            (batch->elem ? batch->elem->index_len : batch->verts[0]->vertex_len));

#ifdef __APPLE__
  GLuint vao = 0;
#endif

  if (!GPU_arb_base_instance_is_supported()) {
    if (i_first > 0) {
#ifdef __APPLE__
      /**
       * There seems to be a nasty bug when drawing using the same VAO reconfiguring. (see T71147)
       * We just use a throwaway VAO for that. Note that this is likely to degrade performance.
       **/
      glGenVertexArrays(1, &vao);
      glBindVertexArray(vao);
#else
      /* If using offset drawing with instancing, we must
       * use the default VAO and redo bindings. */
      glBindVertexArray(default_vao_);
#endif
      batch_update_program_bindings(batch, i_first);
    }
    else {
      /* Previous call could have bind the default vao
       * see above. */
      glBindVertexArray(batch->vao_id);
    }
  }

  if (batch->elem) {
    const GPUIndexBuf *el = batch->elem;
    GLenum index_type = INDEX_TYPE(el);
    GLint base_index = BASE_INDEX(el);
    void *v_first_ofs = (GLuint *)0 + v_first + el->index_start;

#if GPU_TRACK_INDEX_RANGE
    if (el->index_type == GPU_INDEX_U16) {
      v_first_ofs = (GLushort *)0 + v_first + el->index_start;
    }
#endif

    if (GPU_arb_base_instance_is_supported()) {
      glDrawElementsInstancedBaseVertexBaseInstance(
          batch->gl_prim_type, v_count, index_type, v_first_ofs, i_count, base_index, i_first);
    }
    else {
      glDrawElementsInstancedBaseVertex(
          batch->gl_prim_type, v_count, index_type, v_first_ofs, i_count, base_index);
    }
  }
  else {
#ifdef __APPLE__
    glDisable(GL_PRIMITIVE_RESTART);
#endif
    if (GPU_arb_base_instance_is_supported()) {
      glDrawArraysInstancedBaseInstance(batch->gl_prim_type, v_first, v_count, i_count, i_first);
    }
    else {
      glDrawArraysInstanced(batch->gl_prim_type, v_first, v_count, i_count);
    }
#ifdef __APPLE__
    glEnable(GL_PRIMITIVE_RESTART);
#endif
  }

#ifdef __APPLE__
  if (vao != 0) {
    glDeleteVertexArrays(1, &vao);
  }
#endif
}

void GLContext::draw_primitive(GPUPrimType prim_type, int v_count)
{
  /* we cannot draw without vao ... annoying ... */
  glBindVertexArray(default_vao_);

  GLenum type = convert_prim_type_to_gl(prim_type);
  glDrawArrays(type, 0, v_count);

  /* Performance hog if you are drawing with the same vao multiple time.
   * Only activate for debugging.*/
  // glBindVertexArray(0);
}