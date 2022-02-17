#
# @file CMakeLists.txt
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
project(draft VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED ON)

add_definitions(
    -Wall
    -Wcast-align
    -Wcast-qual
    -Wctor-dtor-privacy
    -Wdeprecated
    -Wdisabled-optimization
    -Wextra
    -Wfloat-equal
    -Wformat=2
    -Winit-self
    -Wmissing-declarations
    -Wmissing-include-dirs
    -Wno-vla
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Woverloaded-virtual
    -Wredundant-decls
    -Wreorder
    -Wshadow
    -Wsign-conversion
    -Wsign-promo
    -Wstrict-overflow=5
    -Wswitch
    -Wundef
    -Wunused
    -pedantic)

option(DRAFT_ENABLE_SANITIZERS "enable google sanitizers" ON)
option(DRAFT_ENABLE_TESTS "enable unit tests" ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
endif ()

if (DRAFT_ENABLE_SANITIZERS)
    message(STATUS "draft: enabling sanitizers")
    set(DRAFT_SANITIZER_OPTS -O0 -fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined)
    set(DRAFT_SANITIZER_LIBS asan ubsan)
endif ()

find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

find_library(blosc2_LIBRARY blosc2)
find_path(blosc2_INCLUDE_DIRS blosc2.h)

find_package(CUDAToolkit)
find_library(nvcomp_LIBRARY nvcomp)
find_library(cufile_LIBRARY cufile)
find_path(cufile_INCLUDE_DIRS cufile.h)
find_path(nvcomp_INCLUDE_DIRS nvcomp.hpp)

list(APPEND DRAFTUTIL_SRC
    src/util/Buffer.cc
    src/util/BufferPool.cc
    src/util/InfoReceiver.cc
    src/util/PollSet.cc
    src/util/Reader.cc
    src/util/Receiver.cc
    src/util/RxSession.cc
    src/util/ScopedMMap.cc
    src/util/Sender.cc
    src/util/TaskPool.cc
    src/util/ThreadExecutor.cc
    src/util/TxSession.cc
    src/util/Util.cc
    src/util/UtilJson.cc
    src/util/Writer.cc)

list(APPEND DRAFTCLI_SRC
    src/cli/draft.cc
    src/cli/transfer.cc)

if (blosc2_LIBRARY AND blosc2_INCLUDE_DIRS)
    list(APPEND DRAFTCLI_SRC src/cli/compress.cc)
endif ()

if (nvcomp_LIBRARY AND cufile_LIBRARY AND
    nvcomp_INCLUDE_DIRS AND cufile_INCLUDE_DIRS)
    message(STATUS "draft: including nvidia compression subcommand.")
    list(APPEND DRAFTCLI_SRC src/cli/nvcompress.cc)
endif ()

add_library(draftutil ${DRAFTUTIL_SRC})
target_include_directories(draftutil PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(draftutil
    PRIVATE spdlog::spdlog
    PUBLIC nlohmann_json::nlohmann_json)

if (blosc2_LIBRARY)
    target_compile_definitions(draftutil PUBLIC DRAFT_HAVE_COMPRESS=1)
    message(STATUS "draft: including compression commands.")
    target_link_libraries(draftutil PRIVATE ${blosc2_LIBRARY})
    target_include_directories(draftutil PRIVATE ${blosc2_INCLUDE_DIRS})
endif ()

if (nvcomp_LIBRARY)
    target_compile_definitions(draftutil PUBLIC DRAFT_HAVE_CUDA=1)
    message(STATUS "draft: including nvidia compression command.")
    target_link_libraries(draftutil PRIVATE ${nvcomp_LIBRARY} ${cufile_LIBRARY} CUDA::cudart)
    target_include_directories(draftutil PRIVATE ${nvcomp_INCLUDE_DIRS} ${cufile_INCLUDE_DIRS})
endif ()

add_executable(draft ${DRAFTCLI_SRC})
target_link_libraries(draft PRIVATE draftutil spdlog::spdlog)

if (DRAFT_ENABLE_TESTS)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(gtest_draft test/gtest_draft.cc)
    target_link_libraries(gtest_draft PRIVATE GTest::GTest GTest::Main draftutil)

    gtest_discover_tests(gtest_draft)
    enable_testing()
endif ()
