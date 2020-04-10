
# Always run tests from install path, so all required scripts and libraries
# are available and we are testing the actual installation layout.
#
# Getting the install path of the executable is somewhat involved, as there are
# no direct CMake generator expressions to get the install paths of executables.
get_property(GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(GENERATOR_IS_MULTI_CONFIG)
  string(REPLACE "\${BUILD_TYPE}" "$<CONFIG>" TEST_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
else()
  string(REPLACE "\${BUILD_TYPE}" "" TEST_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
endif()

# Path to Blender and Python executables for all platforms.
if(MSVC)
  set(TEST_BLENDER_EXE ${TEST_INSTALL_DIR}/blender.exe)
  set(TEST_PYTHON_EXE "${TEST_INSTALL_DIR}/${BLENDER_VERSION_MAJOR}.${BLENDER_VERSION_MINOR}/python/bin/python$<$<CONFIG:Debug>:_d>")
elseif(APPLE)
  set(TEST_BLENDER_EXE ${TEST_INSTALL_DIR}/Blender.app/Contents/MacOS/Blender)
  set(TEST_PYTHON_EXE)
else()
  set(TEST_BLENDER_EXE ${TEST_INSTALL_DIR}/blender)

  # Search for the Python executable to use in the tests.
  # The installation directory's Python is the best one to use. However, it can only be there after the install step, which means
  # that Python will never be here on a fresh system. This is why the search continues to the pre-built libraries, and if that
  # doesn't exist either, default to whatever Python CMake found.
  set(_python_exe "${TEST_INSTALL_DIR}/${BLENDER_VERSION_MAJOR}.${BLENDER_VERSION_MINOR}/python/bin/python${PYTHON_VERSION}m")
  if(EXISTS ${_python_exe})
    message(STATUS "Tests: Using Python from installation directory: ${_python_exe}")
    set(TEST_PYTHON_EXE ${_python_exe})
  else()
    set(_python_exe "${LIBDIR}/python/bin/python${PYTHON_VERSION}m")
    if(EXISTS ${_python_exe})
      message(STATUS "Tests: Using Python from pre-compiled LIBDIR: ${_python_exe}")
      set(TEST_PYTHON_EXE ${_python_exe})
    elseif(EXISTS ${PYTHON_EXECUTABLE})
      message(STATUS "Tests: Using Python from PYTHON_EXECUTABLE: ${PYTHON_EXECUTABLE}")
      set(TEST_PYTHON_EXE ${PYTHON_EXECUTABLE})
    else()
      message(FATAL_ERROR "Tests: unable to find a Python executable to run the tests.")
    endif()
  endif()
  unset(_python_exe)
endif()

# For testing with Valgrind
# set(TEST_BLENDER_EXE valgrind --track-origins=yes --error-limit=no ${TEST_BLENDER_EXE})

# Standard Blender arguments for running tests.
set(TEST_BLENDER_EXE_PARAMS --background -noaudio --factory-startup)

# Python CTests
if(WITH_BLENDER AND WITH_PYTHON)
  add_subdirectory(python)
endif()

# GTest
add_subdirectory(gtests)
