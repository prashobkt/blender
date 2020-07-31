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
 * Manage GL vertex array IDs in a thread-safe way
 * Use these instead of glGenBuffers & its friends
 * - alloc must be called from a thread that is bound
 *   to the context that will be used for drawing with
 *   this vao.
 * - free can be called from any thread
 */

#include "BLI_assert.h"
#include "BLI_utildefines.h"

#include "GPU_context.h"
#include "GPU_framebuffer.h"

#include "gpu_batch_private.h"
#include "gpu_context_private.hh"
#include "gpu_matrix_private.h"

#include "gl_context.hh"

#include <mutex>
#include <pthread.h>
#include <string.h>
#include <unordered_set>
#include <vector>

// TODO
// using namespace blender::gpu;

static thread_local GPUContext *active_ctx = NULL;

GPUContext::GPUContext()
{
  thread_ = pthread_self();
  matrix_state = GPU_matrix_state_create();
}

GPUContext *GPU_context_create(GLuint)
{
  GPUContext *ctx = new GLContext();
  GPU_context_active_set(ctx);
  return ctx;
}

bool GPUContext::is_active_on_thread(void)
{
  return (this == active_ctx) && pthread_equal(pthread_self(), thread_);
}

/* to be called after GPU_context_active_set(ctx_to_destroy) */
void GPU_context_discard(GPUContext *ctx)
{
  /* Make sure no other thread has locked it. */
  BLI_assert(ctx->is_active_on_thread());

  delete ctx;
  active_ctx = NULL;
}

/* ctx can be NULL */
void GPU_context_active_set(GPUContext *ctx)
{
  if (active_ctx) {
    active_ctx->deactivate();
  }
  if (ctx) {
    ctx->activate();
  }
  active_ctx = ctx;
}

GPUContext *GPU_ctx(void)
{
  /* Context has been activated by another thread! */
  BLI_assert(active_ctx->is_active_on_thread());
  return active_ctx;
}

GPUContext *GPU_context_active_get(void)
{
  return active_ctx;
}

GLuint GPU_vao_alloc(void)
{
  return active_ctx->vao_alloc();
}

GLuint GPU_fbo_alloc(void)
{
  return active_ctx->fbo_alloc();
}

GLuint GPU_buf_alloc(void)
{
  return active_ctx->buf_alloc();
}

GLuint GPU_tex_alloc(void)
{
  return active_ctx->tex_alloc();
}

void GPU_vao_free(GLuint vao_id, GPUContext *ctx)
{
  BLI_assert(ctx);
  ctx->vao_free(vao_id);
}

void GPU_fbo_free(GLuint fbo_id, GPUContext *ctx)
{
  BLI_assert(ctx);
  ctx->fbo_free(fbo_id);
}

void GPU_buf_free(GLuint buf_id)
{
  /* FIXME active_ctx can be NULL */
  BLI_assert(active_ctx);
  active_ctx->buf_free(buf_id);
}

void GPU_tex_free(GLuint tex_id)
{
  /* FIXME active_ctx can be NULL */
  BLI_assert(active_ctx);
  active_ctx->tex_free(tex_id);
}
