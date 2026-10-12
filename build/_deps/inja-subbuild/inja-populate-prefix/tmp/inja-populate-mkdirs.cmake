# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-src")
  file(MAKE_DIRECTORY "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-src")
endif()
file(MAKE_DIRECTORY
  "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-build"
  "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-subbuild/inja-populate-prefix"
  "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-subbuild/inja-populate-prefix/tmp"
  "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-subbuild/inja-populate-prefix/src/inja-populate-stamp"
  "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-subbuild/inja-populate-prefix/src"
  "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-subbuild/inja-populate-prefix/src/inja-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-subbuild/inja-populate-prefix/src/inja-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Code Project/Vibecode/website/cpp_server/build/_deps/inja-subbuild/inja-populate-prefix/src/inja-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
