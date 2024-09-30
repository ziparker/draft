#
# @file FindCityhash.cmake
#
# Licensed under the MIT License <https://opensource.org/licenses/MIT>.
# SPDX-License-Identifier: MIT
# Copyright (c) 2023 Zachary Parker
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of
# this software and associated documentation files (the "Software"), to deal in
# the Software without restriction, including without limitation the rights to
# use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is furnished to do
# so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

unset(CITYHASH_FOUND)
unset(CITYHASH_INCLUDE_DIRS)
unset(CITYHASH_LIBRARY_DIRS)
unset(CITYHASH_LIBRARIES)

find_path(CITYHASH_INCLUDE_DIRS city.h)
message(STATUS "cityhash inc: ${CITYHASH_INCLUDE_DIRS}")

find_library(CITYHASH_LIB libcityhash.a)
get_filename_component(CITYHASH_PARENT_DIR ${CITYHASH_LIB} DIRECTORY)
set(CITYHASH_LIBRARY_DIRS "${CITYHASH_PARENT_DIR}")
set(CITYHASH_LIBRARIES "${CITYHASH_LIB}")

if (CITYHASH_INCLUDE_DIRS AND CITYHASH_LIBRARIES)
    message(STATUS "found libcityhash: ${CITYHASH_LIBRARIES}")
    set(CITYHASH_FOUND 1)
else ()
    message(STATUS "libcityhash not found.")
endif ()

set(CITYHASH_INCLUDE_DIRS ${CITYHASH_INCLUDE_DIRS})
set(CITYHASH_LIBRARY_DIRS ${CITYHASH_LIBRARY_DIRS})
set(CITYHASH_LIBRARIES ${CITYHASH_LIBRARIES})
