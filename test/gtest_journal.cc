/**
 * @file gtest_journal.cc
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Zachary Parker
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

#include <cstdio>
#include <filesystem>

#include <gtest/gtest.h>

#include <draft/util/Journal.hh>

namespace fs = std::filesystem;

using draft::util::Journal;

namespace {

class FileJanitor
{
public:
    explicit FileJanitor(std::string path):
        path_(std::move(path))
    {
    }

    ~FileJanitor() noexcept
    {
        ::remove(path_.c_str());
    }

private:
    std::string path_;
};

std::string tempFilename(std::string base)
{
    base += ".draft_gtest.XXXXXX";

    ::mktemp(base.data());

    return base;
}

}

TEST(journal, ctor_empty_info)
{
    const auto basename = tempFilename("/tmp/journal");
    const auto filename = basename + ".draft";
    auto janitor = FileJanitor{filename};

    auto j = Journal(basename, { });

    ASSERT_TRUE(fs::exists(basename + ".draft"));

    const auto sz = fs::file_size(filename);

    // expect file size is a multiple of 512.
    EXPECT_GT(sz, 0);
    EXPECT_EQ(0, sz % 512);
}
