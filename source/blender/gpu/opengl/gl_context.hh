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
 * The Original Code is Copyright (C) 2020 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup gpu
 */

#pragma once

#include "../intern/gpu_context_private.hh"

#include "glew-mx.h"

#include <iostream>
#include <mutex>
#include <unordered_set>
#include <vector>

// TODO(fclem) this requires too much refactor for now.
// namespace blender {
// namespace gpu {

class GLContext : public GPUContext {
 public:
  GLContext();
  ~GLContext(){};

  void activate(void) override;
  void deactivate(void) override;

  void draw_batch(GPUBatch *batch, int v_first, int v_count, int i_first, int i_count) override;
  void draw_primitive(GPUPrimType prim_type, int v_count) override;

  void batch_add(GPUBatch *batch) override;
  void batch_remove(GPUBatch *batch) override;

  /* TODO remove */
  GLuint tex_alloc(void) override;
  GLuint vao_alloc(void) override;
  GLuint buf_alloc(void) override;
  GLuint fbo_alloc(void) override;
  void vao_free(GLuint vao_id) override;
  void fbo_free(GLuint fbo_id) override;
  void buf_free(GLuint buf_id) override;
  void tex_free(GLuint tex_id) override;
  GLuint default_framebuffer_get(void) override
  {
    return default_framebuffer_;
  };

 private:
  void orphans_add(std::vector<GLuint> *orphan_list, GLuint id);
  void orphans_clear(void);

  /**
   * Batches & Framebuffers are not shared accross contexts.
   * For this reason we keep a list of them per GPUBatch & GPUFramebuffer.
   * However this list needs to be updated in the case a GPUContext is destroyed.
   */
  std::unordered_set<GPUBatch *> batches;
  std::unordered_set<GPUFrameBuffer *> framebuffers;

  std::vector<GLuint> orphaned_vertarrays_;
  std::vector<GLuint> orphaned_framebuffers_;
  std::vector<GLuint> orphaned_buffers_;
  std::vector<GLuint> orphaned_textures_;

  /** Mutex for the above structures. */
  /** todo: try spinlock instead */
  std::mutex orphans_mutex_;

  GLuint default_vao_;
  GLuint default_framebuffer_;
};

// }  // namespace gpu
// }  // namespace blender
