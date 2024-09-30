#
# @file ExternalCMakeLists.txt
#
# Licensed under the MIT License <https://opensource.org/licenses/MIT>.
# SPDX-License-Identifier: MIT
# Copyright (c) 2021 Zachary Parker
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

cmake_minimum_required(VERSION 3.10)
project(draft_external VERSION 0.0.1)

include(ExternalProject)

if (DRAFT_ENABLE_CBLOSC)
    ExternalProject_Add(
        c-blosc2
        GIT_REPOSITORY https://github.com/Blosc/c-blosc2.git
        GIT_TAG v2.0.3
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DBUILD_SHARED=${BUILD_SHARED_LIBS}
            -DBUILD_TESTS=OFF
            -DBUILD_FUZZERS=OFF
            -DBUILD_BENCHMARKS=OFF
            -DBUILD_EXAMPLES=OFF
            -DBUILD_PLUGINS=OFF)
endif ()

if (DRAFT_ENABLE_CITYHASH)
    ExternalProject_Add(
        cityhash
        GIT_REPOSITORY https://github.com/google/cityhash.git
        GIT_TAG master
        CONFIGURE_COMMAND
            cd <SOURCE_DIR> && ./configure --prefix=${CMAKE_INSTALL_PREFIX}
        BUILD_COMMAND
            make all check
        BUILD_IN_SOURCE ON
        INSTALL_COMMAND
            make -C <SOURCE_DIR> prefix=${CMAKE_INSTALL_PREFIX} install)
endif ()

ExternalProject_Add(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.9.1
    CMAKE_ARGS
        -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
        -DCMAKE_BUILD_SHARED_LIBS=${CMAKE_BUILD_TYPE}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
        -DJSON_BuildTests=OFF
        -DJSON_Install=ON)

if (DRAFT_ENABLE_CUDA)
    ExternalProject_Add(
        nvcomp
        GIT_REPOSITORY https://github.com/NVIDIA/nvcomp.git
        GIT_TAG v2.2.0
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX})
endif ()

ExternalProject_Add(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.8.5
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
        -DSPDLOG_BUILD_SHARED=${BUILD_SHARED_LIBS}
        -DSPDLOG_INSTALL=ON)

if (DRAFT_ENABLE_USTAT)
    ExternalProject_Add(
        ustat
        GIT_REPOSITORY git@github.com:ziparker/ustat.git
        GIT_TAG master
        CMAKE_ARGS
            -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX})
    add_dependencies(ustat spdlog)
endif ()

ExternalProject_Add(
    xxhash
    GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
    GIT_TAG v0.8.1
    CONFIGURE_COMMAND
        ""
    BUILD_COMMAND
        make -C <SOURCE_DIR>
    INSTALL_COMMAND
        make -C <SOURCE_DIR> prefix=${CMAKE_INSTALL_PREFIX} install)
