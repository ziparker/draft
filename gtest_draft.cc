/* @file gtest_draft.cc
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2021 Zachary Parker
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <gtest/gtest.h>

#include "Util.hh"

////////////////////////////////////////////////////////////////////////////////
// Util

using namespace draft::util;

////////////////////////////////////////////////////////////////////////////////
// ScopedFd

TEST(scoped_fd, ctor)
{
    auto fd = ScopedFd{ };
    EXPECT_EQ(fd.get(), -1);

    fd = ScopedFd{42};
    EXPECT_EQ(fd.get(), 42);

    fd = ScopedFd(24);
    EXPECT_EQ(fd.get(), 24);
}

TEST(scoped_mmap, ctor)
{
    auto map = ScopedMMap::map(nullptr, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    EXPECT_NE(map.data(), nullptr);
    EXPECT_EQ(map.size(), 4096);
    EXPECT_TRUE(map.offsetValid(0));
    EXPECT_TRUE(map.offsetValid(4096-1));
}

TEST(scoped_mmap, unmap)
{
    auto map = ScopedMMap::map(nullptr, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(map.data(), nullptr);

    ASSERT_EQ(0, map.unmap());

    EXPECT_EQ(nullptr, map.data());
    EXPECT_EQ(0, map.size());
    EXPECT_FALSE(map.offsetValid(0));
    EXPECT_FALSE(map.offsetValid(4096-1));
}

////////////////////////////////////////////////////////////////////////////////
// FreeList

TEST(free_list, get)
{
    auto list = FreeList{10};

    for (size_t i = 0u; i < 10u; ++i)
        EXPECT_EQ(i, list.get());

    EXPECT_EQ(FreeList::End, list.get());
}

////////////////////////////////////////////////////////////////////////////////
// WaitQueue

TEST(wait_q, put_get)
{
    auto q = WaitQueue<int>{ };
    q.put(42);

    auto v = q.get();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 42);
}
