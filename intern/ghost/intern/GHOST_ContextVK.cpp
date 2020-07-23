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

#ifdef _WIN32
#  include <vulkan/vulkan_win32.h>
#else
#  include <vulkan/vulkan_xlib.h>
#endif

#include <vector>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>

using namespace std;

static const char *vulkan_error_as_string(VkResult result)
{
#define FORMAT_ERROR(X) \
  case X: { \
    return "" #X; \
  }

  switch (result) {
    FORMAT_ERROR(VK_NOT_READY);
    FORMAT_ERROR(VK_TIMEOUT);
    FORMAT_ERROR(VK_EVENT_SET);
    FORMAT_ERROR(VK_EVENT_RESET);
    FORMAT_ERROR(VK_INCOMPLETE);
    FORMAT_ERROR(VK_ERROR_OUT_OF_HOST_MEMORY);
    FORMAT_ERROR(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    FORMAT_ERROR(VK_ERROR_INITIALIZATION_FAILED);
    FORMAT_ERROR(VK_ERROR_DEVICE_LOST);
    FORMAT_ERROR(VK_ERROR_MEMORY_MAP_FAILED);
    FORMAT_ERROR(VK_ERROR_LAYER_NOT_PRESENT);
    FORMAT_ERROR(VK_ERROR_EXTENSION_NOT_PRESENT);
    FORMAT_ERROR(VK_ERROR_FEATURE_NOT_PRESENT);
    FORMAT_ERROR(VK_ERROR_INCOMPATIBLE_DRIVER);
    FORMAT_ERROR(VK_ERROR_TOO_MANY_OBJECTS);
    FORMAT_ERROR(VK_ERROR_FORMAT_NOT_SUPPORTED);
    FORMAT_ERROR(VK_ERROR_FRAGMENTED_POOL);
    FORMAT_ERROR(VK_ERROR_UNKNOWN);
    FORMAT_ERROR(VK_ERROR_OUT_OF_POOL_MEMORY);
    FORMAT_ERROR(VK_ERROR_INVALID_EXTERNAL_HANDLE);
    FORMAT_ERROR(VK_ERROR_FRAGMENTATION);
    FORMAT_ERROR(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
    FORMAT_ERROR(VK_ERROR_SURFACE_LOST_KHR);
    FORMAT_ERROR(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR);
    FORMAT_ERROR(VK_SUBOPTIMAL_KHR);
    FORMAT_ERROR(VK_ERROR_OUT_OF_DATE_KHR);
    FORMAT_ERROR(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR);
    FORMAT_ERROR(VK_ERROR_VALIDATION_FAILED_EXT);
    FORMAT_ERROR(VK_ERROR_INVALID_SHADER_NV);
    FORMAT_ERROR(VK_ERROR_INCOMPATIBLE_VERSION_KHR);
    FORMAT_ERROR(VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
    FORMAT_ERROR(VK_ERROR_NOT_PERMITTED_EXT);
    FORMAT_ERROR(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
    FORMAT_ERROR(VK_THREAD_IDLE_KHR);
    FORMAT_ERROR(VK_THREAD_DONE_KHR);
    FORMAT_ERROR(VK_OPERATION_DEFERRED_KHR);
    FORMAT_ERROR(VK_OPERATION_NOT_DEFERRED_KHR);
    FORMAT_ERROR(VK_PIPELINE_COMPILE_REQUIRED_EXT);
    default:
      return "Unknown Error";
  }
}

#define __STR(A) "" #A
#define VK_CHECK(__expression) \
  do { \
    VkResult r = (__expression); \
    if (r != VK_SUCCESS) { \
      fprintf(stderr, \
              "Vulkan Error : %s:%d : %s failled with %s\n", \
              __FILE__, \
              __LINE__, \
              __STR(__expression), \
              vulkan_error_as_string(r)); \
      return GHOST_kFailure; \
    } \
  } while (0)

GHOST_ContextVK::GHOST_ContextVK(bool stereoVisual,
#ifdef _WIN32
                                 HWND hwnd,
                                 HINSTANCE hinstance,
#else
                                 Window window,
                                 Display *display,
#endif
                                 int contextMajorVersion,
                                 int contextMinorVersion,
                                 int useValidationLayers)
    : GHOST_Context(stereoVisual),
#ifdef _WIN32
      m_hinstance(hinstance),
      m_hwnd(hwnd),
#else
      m_display(display),
      m_window(window),
#endif
      m_contextMajorVersion(contextMajorVersion),
      m_contextMinorVersion(contextMinorVersion),
      m_useValidationLayers(useValidationLayers),
      m_instance(VK_NULL_HANDLE),
      m_surface(VK_NULL_HANDLE),
      m_physical_device(VK_NULL_HANDLE),
      m_device(VK_NULL_HANDLE)
{
}

GHOST_ContextVK::~GHOST_ContextVK()
{
  if (m_device != VK_NULL_HANDLE) {
    vkDestroyDevice(m_device, NULL);
  }
  if (m_surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_instance, m_surface, NULL);
  }
  if (m_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(m_instance, NULL);
  }
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

static vector<VkExtensionProperties> getExtensionsAvailable()
{
  uint32_t extension_count = 0;
  vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL);

  vector<VkExtensionProperties> extensions(extension_count);
  vkEnumerateInstanceExtensionProperties(NULL, &extension_count, extensions.data());

  return extensions;
}

static bool checkExtensionSupport(vector<VkExtensionProperties> &extensions_available,
                                  const char *extension_name)
{
  for (const auto &extension : extensions_available) {
    if (strcmp(extension_name, extension.extensionName) == 0) {
      return true;
    }
  }
  return false;
}

static void requireExtension(vector<VkExtensionProperties> &extensions_available,
                             vector<const char *> &extensions_enabled,
                             const char *extension_name)
{
  if (checkExtensionSupport(extensions_available, extension_name)) {
    extensions_enabled.push_back(extension_name);
  }
  else {
    cout << "Error : " << extension_name << " not found\n";
  }
}

static vector<VkLayerProperties> getLayersAvailable()
{
  uint32_t layer_count = 0;
  vkEnumerateInstanceLayerProperties(&layer_count, NULL);

  vector<VkLayerProperties> layers(layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, layers.data());

  return layers;
}

static bool checkLayerSupport(vector<VkLayerProperties> &layers_available, const char *layer_name)
{
  for (const auto &layer : layers_available) {
    if (strcmp(layer_name, layer.layerName) == 0) {
      return true;
    }
  }
  return false;
}

static void enableLayer(vector<VkLayerProperties> &layers_available,
                        vector<const char *> &layers_enabled,
                        const char *layer_name)
{
  if (checkLayerSupport(layers_available, layer_name)) {
    layers_enabled.push_back(layer_name);
  }
  else {
    cout << "Info : " << layer_name << " not supported.\n";
  }
}

static VkPhysicalDevice pickPhysicalDevice(VkInstance instance)
{
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(instance, &device_count, NULL);

  if (device_count == 0) {
    return VK_NULL_HANDLE;
  }

  vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

  // TODO Pick the best GPU by default OR by name from user settings.
  // For now we just select the first suitable gpu.
  VkPhysicalDevice best_device = VK_NULL_HANDLE;
  int best_device_score = -1;

  for (const auto &device : devices) {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device, &device_properties);

    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);

    // List of REQUIRED features.
    if (device_features.geometryShader &&  // Needed for wide lines
        device_features.dualSrcBlend &&    // Needed by EEVEE
        device_features.logicOp            // Needed by UI
    ) {
      int device_score = 0;
      switch (device_properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
          device_score = 400;
          break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
          device_score = 300;
          break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
          device_score = 200;
          break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
          device_score = 100;
          break;
        default:
          break;
      }
      cout << "Found Vulkan Device : " << device_properties.deviceName << "\n";
      if (device_score > best_device_score) {
        best_device = device;
        best_device_score = device_score;
      }
    }
  }

  if (best_device == VK_NULL_HANDLE) {
    // TODO debug output of devices and features.
    cout << "Error: No suitable Vulkan Device found!\n";
  }
  else {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(best_device, &device_properties);
    cout << "Selected Vulkan Device : " << device_properties.deviceName << "\n";
  }

  return best_device;
}

static GHOST_TSuccess getGraphicQueueFamily(VkPhysicalDevice device, uint32_t *r_queue_index)
{
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, NULL);

  vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

  *r_queue_index = 0;
  for (const auto &queue_family : queue_families) {
    if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return GHOST_kSuccess;
    }
    (*r_queue_index)++;
  }

  cout << "Couldn't find any Graphic queue familly on selected device\n";

  return GHOST_kFailure;
}

static GHOST_TSuccess getPresetQueueFamily(VkPhysicalDevice device,
                                           VkSurfaceKHR surface,
                                           uint32_t *r_queue_index)
{
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, NULL);

  vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

  /* TODO(fclem) Prefer using the familly who also has graphic ability.
   * These may not be the same. */
  *r_queue_index = 0;
  for (int i = 0; i < queue_family_count; i++) {
    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, *r_queue_index, surface, &present_support);

    if (present_support) {
      return GHOST_kSuccess;
    }
    (*r_queue_index)++;
  }

  cout << "Couldn't find any Present queue familly on selected device\n";

  return GHOST_kFailure;
}

GHOST_TSuccess GHOST_ContextVK::initializeDrawingContext()
{
#ifdef _WIN32
  const bool use_window_surface = (m_hwnd != NULL);
#else
  const bool use_window_surface = (m_display != NULL);
#endif

  auto layers_available = getLayersAvailable();
  auto extensions_available = getExtensionsAvailable();

  vector<const char *> layers_enabled;
  if (m_useValidationLayers) {
    enableLayer(layers_available, layers_enabled, "VK_LAYER_KHRONOS_validation");
  }

  vector<const char *> extensions_enabled;

  if (use_window_surface) {
#ifdef _WIN32
    const char *native_surface_extension_name = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
#else
    const char *native_surface_extension_name = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif
    requireExtension(extensions_available, extensions_enabled, "VK_KHR_surface");
    requireExtension(extensions_available, extensions_enabled, native_surface_extension_name);
  }

  VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Blender",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "Blender",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_0,
  };

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = static_cast<uint32_t>(layers_enabled.size()),
      .ppEnabledLayerNames = layers_enabled.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions_enabled.size()),
      .ppEnabledExtensionNames = extensions_enabled.data(),
  };

  VK_CHECK(vkCreateInstance(&create_info, NULL, &m_instance));

  m_physical_device = pickPhysicalDevice(m_instance);

  vector<VkDeviceQueueCreateInfo> queue_create_infos;

  if (use_window_surface) {
#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        hinstance = m_hinstance,
        hwnd = m_hwnd,
    };
    VK_CHECK(vkCreateWin32SurfaceKHR(m_instance, &surface_create_info, NULL, &m_surface));
#else
    VkXlibSurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = m_display,
        .window = m_window,
    };
    VK_CHECK(vkCreateXlibSurfaceKHR(m_instance, &surface_create_info, NULL, &m_surface));
#endif

    /* A present queue is required only if we render to a window. */
    if (!getPresetQueueFamily(m_physical_device, m_surface, &m_queue_family_present)) {
      return GHOST_kFailure;
    }

    float queue_priorities[] = {1.0f};
    VkDeviceQueueCreateInfo present_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_queue_family_present,
        .queueCount = 1,
        .pQueuePriorities = queue_priorities,
    };
    queue_create_infos.push_back(present_queue_create_info);
  }

  {
    /* A graphic queue is required to draw anything. */
    if (!getGraphicQueueFamily(m_physical_device, &m_queue_family_graphic)) {
      return GHOST_kFailure;
    }

    float queue_priorities[] = {1.0f};
    VkDeviceQueueCreateInfo graphic_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_queue_family_graphic,
        .queueCount = 1,
        .pQueuePriorities = queue_priorities,
    };
    /* Eash queue must be unique. */
    if (m_queue_family_graphic != m_queue_family_present) {
      queue_create_infos.push_back(graphic_queue_create_info);
    }
  }

  VkPhysicalDeviceFeatures device_features = {
      .geometryShader = VK_TRUE,  // Needed for wide lines & EEVEE barycentric support
      .dualSrcBlend = VK_TRUE,    // Needed by EEVEE
      .logicOp = VK_TRUE,         // Needed by UI
  };

  VkDeviceCreateInfo device_create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
      .pQueueCreateInfos = queue_create_infos.data(),
      // Same as instance extensions. Only needed for 1.0 implementations.
      .enabledLayerCount = static_cast<uint32_t>(layers_enabled.size()),
      .ppEnabledLayerNames = layers_enabled.data(),
      .pEnabledFeatures = &device_features,
  };

  VK_CHECK(vkCreateDevice(m_physical_device, &device_create_info, nullptr, &m_device));

  vkGetDeviceQueue(m_device, m_queue_family_graphic, 0, &m_graphic_queue);

  if (use_window_surface) {
    vkGetDeviceQueue(m_device, m_queue_family_present, 0, &m_present_queue);
  }

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
