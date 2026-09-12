# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/vite/click-hitbox-trail/build-win/_deps/arc-src")
  file(MAKE_DIRECTORY "/home/vite/click-hitbox-trail/build-win/_deps/arc-src")
endif()
file(MAKE_DIRECTORY
  "/home/vite/click-hitbox-trail/build-win/_deps/arc-build"
  "/home/vite/click-hitbox-trail/build-win/_deps/arc-subbuild/arc-populate-prefix"
  "/home/vite/click-hitbox-trail/build-win/_deps/arc-subbuild/arc-populate-prefix/tmp"
  "/home/vite/click-hitbox-trail/build-win/_deps/arc-subbuild/arc-populate-prefix/src/arc-populate-stamp"
  "/home/vite/click-hitbox-trail/build-win/_deps/arc-subbuild/arc-populate-prefix/src"
  "/home/vite/click-hitbox-trail/build-win/_deps/arc-subbuild/arc-populate-prefix/src/arc-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/vite/click-hitbox-trail/build-win/_deps/arc-subbuild/arc-populate-prefix/src/arc-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/vite/click-hitbox-trail/build-win/_deps/arc-subbuild/arc-populate-prefix/src/arc-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
