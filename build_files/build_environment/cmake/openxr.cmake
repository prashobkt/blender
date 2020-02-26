# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ***** END GPL LICENSE BLOCK *****


# Keep flags in sync with install_deps.sh ones in compile_OpenXR_SDK()
set(OPENXR_SDK_EXTRA_ARGS
  -DBUILD_FORCE_GENERATION=OFF
  -DBUILD_LOADER=ON
  -DDYNAMIC_LOADER=OFF
)

if(UNIX AND NOT APPLE)
  list(APPEND OPENXR_SDK_EXTRA_ARGS
    -DBUILD_WITH_WAYLAND_HEADERS=OFF
    -DBUILD_WITH_XCB_HEADERS=OFF
    -DBUILD_WITH_XLIB_HEADERS=ON
  )
endif()

ExternalProject_Add(external_openxr_sdk
  URL ${OPENXR_SDK_URI}
  DOWNLOAD_DIR ${DOWNLOAD_DIR}
  URL_HASH MD5=${OPENXR_SDK_HASH}
  PREFIX ${BUILD_DIR}/openxr_sdk
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${LIBDIR}/openxr_sdk ${DEFAULT_CMAKE_FLAGS} ${OPENXR_SDK_EXTRA_ARGS}
  INSTALL_DIR ${LIBDIR}/openxr_sdk
)

if(WIN32)
  if(BUILD_MODE STREQUAL Release)
    ExternalProject_Add_Step(external_openxr_sdk after_install
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${LIBDIR}/openxr_sdk/include/openxr ${HARVEST_TARGET}/openxr_sdk/include/openxr
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${LIBDIR}/openxr_sdk/lib ${HARVEST_TARGET}/openxr_sdk/lib
      DEPENDEES install
    )
  endif()
  if(BUILD_MODE STREQUAL Debug)
    ExternalProject_Add_Step(external_openxr_sdk after_install
      COMMAND ${CMAKE_COMMAND} -E copy ${LIBDIR}/openxr_sdk/lib/openxr_loader-1_0.lib ${HARVEST_TARGET}/openxr_sdk/lib/openxr_loader-1_0_d.lib
      DEPENDEES install
    )
  endif()
endif()
