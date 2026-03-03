# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/src/eyrie-hello-eyrie")
  file(MAKE_DIRECTORY "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/src/eyrie-hello-eyrie")
endif()
file(MAKE_DIRECTORY
  "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/src/eyrie-hello-eyrie-build"
  "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime"
  "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/tmp"
  "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/src/eyrie-hello-eyrie-stamp"
  "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/src"
  "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/src/eyrie-hello-eyrie-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/src/eyrie-hello-eyrie-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/data/hykang/RVSS/q-vela-ras/examples/build/hello/runtime/src/eyrie-hello-eyrie-stamp${cfgdir}") # cfgdir has leading slash
endif()
