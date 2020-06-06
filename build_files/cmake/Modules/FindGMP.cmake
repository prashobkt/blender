# - Find GMP library
# Find the native GMP includes and library
# This module defines
#  GMP_INCLUDE_DIRS, where to find gmp.h and gmpxx.h, Set when
#                    GMP is found.
#  GMP_LIBRARIES, libraries to link against to use GMP.
#  GMP_ROOT_DIR, The base directory to search for GMP.
#                This can also be an environment variable.
#  GMP_FOUND, If false, do not try to use GMP.
#
# also defined, but not for general use are
#  GMP_LIBRARY, where to find the GMP library.

#=============================================================================
# Copyright 2020 Blender Foundation.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

# If GMP_ROOT_DIR was defined in the environment, use it.
IF(NOT GMP_ROOT_DIR AND NOT $ENV{GMP_ROOT_DIR} STREQUAL "")
  SET(GMP_ROOT_DIR $ENV{GMP_ROOT_DIR})
ENDIF()

SET(_gmp_SEARCH_DIRS
  ${LIBDIR}
  ${GMP_ROOT_DIR}
)

FIND_PATH(GMP_INCLUDE_DIR
  NAMES
    gmp.h
  HINTS
    ${_gmp_SEARCH_DIRS}
  PATH_SUFFIXES
    include
)

FIND_LIBRARY(GMP_LIBRARY
  NAMES
    gmp
  HINTS
    ${_gmp_SEARCH_DIRS}
  PATH_SUFFIXES
    lib
  )

FIND_LIBRARY(GMPXX_LIBRARY
  NAMES
    gmpxx
  HINTS
    ${_gmp_SEARCH_DIRS}
  PATH_SUFFIXES
    lib
  )

# HWT: I can't make the above work, so do this for now:
SET(GMP_LIBRARY "${LIBDIR}/gmp/lib/libgmp.a")
SET(GMPXX_LIBRARY "${LIBDIR}/gmp/lib/libgmpxx.a")
SET(GMP_INCLUDE_DIR "${LIBDIR}/gmp/include")

# handle the QUIETLY and REQUIRED arguments and set GMP_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GMP DEFAULT_MSG
    GMP_LIBRARY GMPXX_LIBRARY GMP_INCLUDE_DIR)

IF(GMP_FOUND)
  SET(GMP_LIBRARIES ${GMPXX_LIBRARY} ${GMP_LIBRARY})
  SET(GMP_INCLUDE_DIRS ${GMP_INCLUDE_DIR})
ENDIF()

MARK_AS_ADVANCED(
  GMP_INCLUDE_DIR
  GMP_LIBRARY
)

UNSET(_gmp_SEARCH_DIRS)
