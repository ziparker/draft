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

using draft::util::Cursor;
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

TEST(journal, single_hash_write)
{
    const auto basename = tempFilename("/tmp/journal");
    auto janitor = FileJanitor{basename + ".draft"};

    auto j = Journal(basename, { });
    ASSERT_TRUE(fs::exists(basename + ".draft"));
    ASSERT_EQ(0, j.writeHash(0, 512, 512, 0x1122334455667788));
}

TEST(journal, no_hash_cursor)
{
    const auto basename = tempFilename("/tmp/journal");
    auto janitor = FileJanitor{basename + ".draft"};

    auto j = Journal(basename, { });
    ASSERT_TRUE(fs::exists(basename + ".draft"));

    auto c = j.cursor();
    EXPECT_FALSE(c.valid());

    // verify that seeks from start/end/current remain invalid.
    c.seek(1);
    EXPECT_FALSE(c.valid());

    c.seek(-2);
    EXPECT_FALSE(c.valid());

    c.seek(0, Cursor::Set);
    EXPECT_FALSE(c.valid());

    c.seek(0, Cursor::End);
    EXPECT_FALSE(c.valid());

    c.seek(1, Cursor::End);
    EXPECT_FALSE(c.valid());

    c.seek(-1, Cursor::Set);
    EXPECT_FALSE(c.valid());
}

TEST(journal, eventual_hash_cursor_start)
{
    const auto basename = tempFilename("/tmp/journal");
    auto janitor = FileJanitor{basename + ".draft"};

    auto j = Journal(basename, { });
    ASSERT_TRUE(fs::exists(basename + ".draft"));

    auto c = j.cursor();
    EXPECT_FALSE(c.valid());

    ASSERT_EQ(0, j.writeHash(0, 512, 512, 0x1122334455667788));

    // cursor remains invalid until position is set to a valid value.
    EXPECT_FALSE(c.valid());

    // seek to become valid.
    c.seek(0);
    EXPECT_TRUE(c.valid());

    // seek to end to become invalid.
    c.seek(0, Cursor::End);
    EXPECT_FALSE(c.valid());

    // seek set to become valid.
    c.seek(0, Cursor::Set);
    EXPECT_TRUE(c.valid());
}

//TEST(journal, cursor)
//{
//    const auto basename = tempFilename("/tmp/journal");
//    auto janitor = FileJanitor{basename + ".draft"};
//
//    auto j = Journal(basename, { });
//    ASSERT_TRUE(fs::exists(basename + ".draft"));
//    ASSERT_EQ(0, j.writeHash(0, 512, 512, 0x1122334455667788));
//
//    auto c = j.
//}
//
//TEST(journal, iter_begin)
//{
//    const auto basename = tempFilename("/tmp/journal");
//    auto janitor = FileJanitor{basename + ".draft"};
//
//    auto j = Journal(basename, { });
//    ASSERT_TRUE(fs::exists(basename + ".draft"));
//    ASSERT_EQ(0, j.writeHash(0, 512, 512, 0x1122334455667788));
//
//    auto c = j.
//}
//
//TEST(journal, iter_end)
//{
//    const auto basename = tempFilename("/tmp/journal");
//    auto janitor = FileJanitor{basename + ".draft"};
//
//    auto j = Journal(basename, { });
//    ASSERT_TRUE(fs::exists(basename + ".draft"));
//    ASSERT_EQ(0, j.writeHash(0, 512, 512, 0x1122334455667788));
//
//    auto c = j.
//}
