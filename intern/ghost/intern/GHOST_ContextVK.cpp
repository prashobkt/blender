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
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup GHOST
 *
 * Definition of GHOST_ContextVK class.
 */

#include "GHOST_ContextVK.h"
#include "GHOST_SystemX11.h"

#include <vulkan/vulkan.h>

#include <vector>

#include <cassert>
#include <cstdio>
#include <cstring>

GHOST_ContextVK::GHOST_ContextVK(bool stereoVisual) : GHOST_Context(stereoVisual)
{
  assert(m_display != NULL);
}

GHOST_ContextVK::~GHOST_ContextVK()
{
}

GHOST_TSuccess GHOST_ContextVK::swapBuffers()
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextVK::activateDrawingContext()
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextVK::releaseDrawingContext()
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextVK::initializeDrawingContext()
{
  uint32_t extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

  printf("Vulkan extensions supported : %u\n", extensionCount);

  return GHOST_kFailure;
}

GHOST_TSuccess GHOST_ContextVK::releaseNativeHandles()
{
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextVK::setSwapInterval(int interval)
{
  (void)interval;
  return GHOST_kSuccess;
}

GHOST_TSuccess GHOST_ContextVK::getSwapInterval(int &intervalOut)
{
  (void)intervalOut;
  return GHOST_kSuccess;
}
