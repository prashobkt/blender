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
 *
 * This interface allow GPU to manage GL objects for multiple context and threads.
 */

#pragma once

#include "GPU_context.h"

#include <pthread.h>

struct GPUBatch;
struct GPUFrameBuffer;
struct GPUMatrixState;

// TODO(fclem) this requires too much refactor for now.
// namespace blender {
// namespace gpu {

class GPUContext {
 public:
  GPUContext();
  virtual ~GPUContext(){};

  virtual void activate(void) = 0;
  virtual void deactivate(void) = 0;

  virtual void draw_batch(GPUBatch *batch, int v_first, int v_count, int i_first, int i_count) = 0;
  virtual void draw_primitive(GPUPrimType prim_type, int v_count) = 0;

  virtual void batch_add(GPUBatch *){};
  virtual void batch_remove(GPUBatch *){};

  virtual void framebuffer_add(struct GPUFrameBuffer *){};
  virtual void framebuffer_remove(struct GPUFrameBuffer *){};

  /* TODO(fclem) These are gl specifics. To be hidden inside the gl backend. */
  virtual GLuint default_framebuffer_get(void) = 0;
  virtual GLuint buf_alloc(void) = 0;
  virtual GLuint tex_alloc(void) = 0;
  virtual GLuint vao_alloc(void) = 0;
  virtual GLuint fbo_alloc(void) = 0;
  virtual void vao_free(GLuint vao_id) = 0;
  virtual void fbo_free(GLuint fbo_id) = 0;
  virtual void buf_free(GLuint buf_id) = 0;
  virtual void tex_free(GLuint tex_id) = 0;

  /** State managment */
  GPUFrameBuffer *current_fbo = NULL;
  GPUMatrixState *matrix_state = NULL;

  bool is_active_on_thread(void);

 protected:
  /** Thread on which this context is active. */
  pthread_t thread_;
  bool thread_is_used_;
};

/* Return context currently bound to the caller's thread.
 * Note: this assume a context is active! */
GPUContext *GPU_ctx(void);

/* These require a gl ctx bound. */
GLuint GPU_buf_alloc(void);
GLuint GPU_tex_alloc(void);
GLuint GPU_vao_alloc(void);
GLuint GPU_fbo_alloc(void);

/* These can be called any threads even without gl ctx. */
void GPU_buf_free(GLuint buf_id);
void GPU_tex_free(GLuint tex_id);
/* These two need the ctx the id was created with. */
void GPU_vao_free(GLuint vao_id, GPUContext *ctx);
void GPU_fbo_free(GLuint fbo_id, GPUContext *ctx);

// }  // namespace gpu
// }  // namespace blender
