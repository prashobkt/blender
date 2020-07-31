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

#include "GPU_framebuffer.h"

#include "gpu_context_private.hh"

#include "gl_context.hh"

// TODO(fclem) this requires too much refactor for now.
// using namespace blender::gpu;

GLContext::GLContext() : GPUContext()
{
  glGenVertexArrays(1, &default_vao_);
  /* TODO(fclem) call ghost here. */
  // GHOST_GetDefaultOpenGLFramebuffer(g_WS.ghost_window);
  default_framebuffer_ = 0;
}

GLContext::~GLContext()
{
  BLI_assert(ctx->orphaned_vertarray_ids.empty());
  /* For now don't allow GPUFrameBuffers to be reuse in another ctx. */
  BLI_assert(ctx->framebuffers.empty());

  /* delete remaining vaos */
  while (!ctx->batches.empty()) {
    /* this removes the array entry */
    GPU_batch_vao_cache_clear(*ctx->batches.begin());
  }
  GPU_matrix_state_discard(ctx->matrix_state);
  glDeleteVertexArrays(1, &ctx->default_vao);
}

void GLContext::activate(void)
{
#ifdef DEBUG
  /* Make sure no other context is already bound to this thread. */
  BLI_assert(pthread_equal(pthread_self(), thread_));
  /* Make sure no other thread has locked it. */
  BLI_assert(thread_is_used_ == false);
  thread_ = pthread_self();
  thread_is_used_ = true;
#endif
  orphans_clear();
};

void GLContext::deactivate(void)
{
#ifdef DEBUG
  thread_is_used_ = false;
#endif
};

void GLContext::orphans_clear(void) override
{
  /* Check if context has been activated by another thread! */
  BLI_assert(pthread_equal(pthread_self(), ctx->thread));

  orphans_mutex_.lock();
  if (!orphaned_vertarrays_.empty()) {
    glDeleteVertexArrays((uint)orphaned_vertarrays_.size(), orphaned_vertarrays_.data());
    orphaned_vertarrays_.clear();
  }
  if (!orphaned_framebuffers_.empty()) {
    glDeleteFramebuffers((uint)orphaned_framebuffers_.size(), orphaned_framebuffers_.data());
    orphaned_framebuffers_.clear();
  }
  if (!orphaned_buffers_.empty()) {
    glDeleteBuffers((uint)orphaned_buffers_.size(), orphaned_buffers_.data());
    orphaned_buffers_.clear();
  }
  if (!orphaned_textures_.empty()) {
    glDeleteTextures((uint)orphaned_textures_.size(), orphaned_textures_.data());
    orphaned_textures_.clear();
  }
  orphans_mutex_.unlock();
};

void GLContext::orphans_add(std::vector<GLuint> *orphan_list, GLuint id)
{
  orphans_mutex_->lock();
  orphan_list->emplace_back(id);
  orphans_mutex_->unlock();
}

void GLContext::vao_free(GLuint vao_id)
{
  if (ctx == GPU_context_active_get()) {
    glDeleteVertexArrays(1, &vao_id);
  }
  else {
    orphans_add(ctx, &ctx->orphaned_vertarray_ids, vao_id);
  }
}

void GLContext::fbo_free(GLuint fbo_id)
{
  if (ctx == GPU_context_active_get()) {
    glDeleteFramebuffers(1, &fbo_id);
  }
  else {
    orphans_add(ctx, &ctx->orphaned_framebuffer_ids, fbo_id);
  }
}

void GLContext::buf_free(GLuint buf_id)
{
  if (GPU_context_active_get()) {
    glDeleteBuffers(1, &buf_id);
  }
  else {
    orphans_add(&orphaned_buffer_ids, buf_id);
  }
}

void GLContext::tex_free(GLuint tex_id)
{
  if (GPU_context_active_get()) {
    glDeleteTextures(1, &tex_id);
  }
  else {
    orphans_add(&orphaned_texture_ids, tex_id);
  }
}

GLuint GLContext::vao_alloc(void)
{
  GLuint new_vao_id = 0;
  orphans_clear();
  glGenVertexArrays(1, &new_vao_id);
  return new_vao_id;
}

GLuint GLContext::fbo_alloc(void)
{
  GLuint new_fbo_id = 0;
  orphans_clear();
  glGenFramebuffers(1, &new_fbo_id);
  return new_fbo_id;
}

GLuint GLContext::buf_alloc(void)
{
  GLuint new_buffer_id = 0;
  orphans_clear();
  glGenBuffers(1, &new_buffer_id);
  return new_buffer_id;
}

GLuint GLContext::tex_alloc(void)
{
  GLuint new_texture_id = 0;
  orphans_clear();
  glGenTextures(1, &new_texture_id);
  return new_texture_id;
}
