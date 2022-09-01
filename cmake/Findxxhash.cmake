#
# @file Findxxhash.cmake
#
# Licensed under the MIT License <https://opensource.org/licenses/MIT>.
# SPDX-License-Identifier: MIT
# Copyright (c) 2022 Zachary Parker
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

unset(XXHASH_FOUND)
unset(XXHASH_INCLUDE_DIRS)
unset(XXHASH_LIBRARY_DIRS)
unset(XXHASH_LIBRARIES)

find_path(XXHASH_DIR xxhash.h)
get_filename_component(XXHASH_PARENT_DIR ${XXHASH_DIR} DIRECTORY)
set(XXHASH_INCLUDE_DIRS "${XXHASH_PARENT_DIR}")

message(STATUS "libxxhash inc: ${XXHASH_INCLUDE_DIRS}")

find_library(XXHASH_LIB libxxhash.a)
get_filename_component(XXHASH_PARENT_DIR ${XXHASH_LIB} DIRECTORY)
set(XXHASH_LIBRARY_DIRS "${XXHASH_PARENT_DIR}")
set(XXHASH_LIBRARIES "${XXHASH_LIB}")

if (XXHASH_INCLUDE_DIRS AND XXHASH_LIBRARIES)
    message(STATUS "found libxxhash: ${XXHASH_LIBRARIES}")
    set(XXHASH_FOUND 1)
else ()
    message(STATUS "libxxhash not found.")
endif ()

set(xxhash_INCLUDE_DIRS ${XXHASH_INCLUDE_DIRS})
set(xxhash_LIBRARY_DIRS ${XXHASH_LIBRARY_DIRS})
set(xxhash_LIBRARIES ${XXHASH_LIBRARIES})
