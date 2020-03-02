# - Find OpenXR-SDK library
# Find the native OpenXR-SDK includes and library
# This module defines
#  OPENXR_SDK_INCLUDE_DIRS, where to find OpenXR-SDK headers, Set when
#                           OPENXR_SDK_INCLUDE_DIR is found.
#  OPENXR_SDK_LIBRARIES, libraries to link against to use OpenXR-SDK.
#  OPENXR_SDK_ROOT_DIR, the base directory to search for OpenXR-SDK.
#                        This can also be an environment variable.
#  OPENXR_SDK_FOUND, if false, do not try to use OpenXR-SDK.
#
# also defined, but not for general use are
#  OPENXR_LOADER_LIBRARY, where to find the OpenXR-SDK library.

#=============================================================================
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

# If OPENXR_SDK_ROOT_DIR was defined in the environment, use it.
IF(NOT OPENXR_SDK_ROOT_DIR AND NOT $ENV{OPENXR_SDK_ROOT_DIR} STREQUAL "")
  SET(OPENXR_SDK_ROOT_DIR $ENV{OPENXR_SDK_ROOT_DIR})
ENDIF()

SET(_openxr_sdk_SEARCH_DIRS
  ${OPENXR_SDK_ROOT_DIR}
  /usr/local
  /opt/lib/openxr-sdk
)

FIND_PATH(OPENXR_SDK_INCLUDE_DIR
  NAMES
    openxr/openxr.h
  HINTS
    ${_openxr_sdk_SEARCH_DIRS}
  PATH_SUFFIXES
    include
)

FIND_LIBRARY(OPENXR_LOADER_LIBRARY
  NAMES
    openxr_loader
  HINTS
    ${_openxr_sdk_SEARCH_DIRS}
  PATH_SUFFIXES
    lib64 lib
)

# handle the QUIETLY and REQUIRED arguments and set OPENXR_SDK_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(OPENXR_SDK DEFAULT_MSG
    OPENXR_LOADER_LIBRARY OPENXR_SDK_INCLUDE_DIR)

IF(OPENXR_SDK_FOUND)
  SET(OPENXR_SDK_LIBRARIES ${OPENXR_LOADER_LIBRARY})
  SET(OPENXR_SDK_INCLUDE_DIRS ${OPENXR_SDK_INCLUDE_DIR})
ENDIF(OPENXR_SDK_FOUND)

MARK_AS_ADVANCED(
  OPENXR_SDK_INCLUDE_DIR
  OPENXR_LOADER_LIBRARY
)
