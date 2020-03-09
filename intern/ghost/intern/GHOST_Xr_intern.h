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

/** \file
 * \ingroup GHOST
 */

#ifndef __GHOST_XR_INTERN_H__
#define __GHOST_XR_INTERN_H__

#include <memory>
#include <vector>

#include "GHOST_Xr_openxr_includes.h"

#define CHECK_XR(call, error_msg) \
  { \
    XrResult _res = call; \
    if (XR_FAILED(_res)) { \
      throw GHOST_XrException(error_msg, __FILE__, __LINE__, _res); \
    } \
  } \
  (void)0

#define CHECK_XR_ASSERT(call) \
  { \
    XrResult _res = call; \
    assert(_res == XR_SUCCESS); \
    (void)_res; \
  } \
  (void)0

#define THROW_XR(error_msg) throw GHOST_XrException(error_msg, __FILE__, __LINE__);

#define XR_DEBUG_ONLY_BEGIN(ctx) \
  if ((ctx)->isDebugMode()) { \
    (void)0
#define XR_DEBUG_ONLY_END \
  } \
  (void)0

#define XR_DEBUG_PRINTF(ctx, ...) \
  if ((ctx)->isDebugMode()) { \
    printf(__VA_ARGS__); \
  } \
  (void)0

#define XR_DEBUG_ONLY_CALL(ctx, call) \
  if ((ctx)->isDebugMode()) { \
    call; \
  } \
  (void)0

/**
 * Helper for RAII usage of OpenXR handles (defined with XR_DEFINE_HANDLE). This is based on
 * `std::unique_ptr`, to give similar behavior and usage (e.g. functions like #get() and #release()
 * are supported).
 */
template<typename _OXR_HANDLE> class unique_oxr_ptr {
 public:
  using xr_destroy_func = XrResult (*)(_OXR_HANDLE);

  unique_oxr_ptr(xr_destroy_func destroy_fn) : m_destroy_fn(destroy_fn)
  {
  }

  unique_oxr_ptr(unique_oxr_ptr &&other) : m_ptr(other.release()), m_destroy_fn(other.m_destroy_fn)
  {
  }

  /**
   * Construct the pointer from a xrCreate function passed as \a create_fn, with additional
   * arguments forwarded from \a args.
   * For example, this:
   * \code
   * some_unique_oxr_ptr.contruct(xrCreateFoo, some_arg, some_other_arg);
   * \endcode
   * effectively results in this call:
   * \code
   * xrCreateFoo(some_arg, some_other_arg, &some_unique_oxr_ptr.m_ptr);
   * \endcode
   */
  template<typename _create_func, typename... _Args>
  XrResult construct(_create_func create_fn, _Args &&... args)
  {
    assert(m_ptr == XR_NULL_HANDLE);
    return create_fn(std::forward<_Args>(args)..., &m_ptr);
  }

  ~unique_oxr_ptr()
  {
    if (m_ptr != XR_NULL_HANDLE) {
      m_destroy_fn(m_ptr);
    }
  }

  _OXR_HANDLE get()
  {
    return m_ptr;
  }

  _OXR_HANDLE release()
  {
    _OXR_HANDLE ptr = get();
    m_ptr = XR_NULL_HANDLE;
    return ptr;
  }

  /* operator= defines not needed for now. */

  unique_oxr_ptr(const unique_oxr_ptr &) = delete;
  unique_oxr_ptr &operator=(const unique_oxr_ptr &) = delete;

 private:
  _OXR_HANDLE m_ptr = XR_NULL_HANDLE;
  xr_destroy_func m_destroy_fn;
};

#endif /* __GHOST_XR_INTERN_H__ */
