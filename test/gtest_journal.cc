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
using HashRecord = Journal::HashRecord;

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

    FileJanitor(const FileJanitor &) = delete;
    FileJanitor &operator=(const FileJanitor &) = delete;

    FileJanitor(FileJanitor &&o) = default;
    FileJanitor &operator=(FileJanitor &&o) = default;

private:
    std::string path_;
};

std::string tempFilename(std::string base)
{
    base += ".draft_gtest.XXXXXX";

    ::mktemp(base.data());

    return base;
}

constexpr HashRecord defaultHashRecord(size_t idx = 0)
{
    return {
        .hash = idx,
        .offset = 512u * (idx + 1),
        .size = 512u,
        .fileId = 0
    };
}

std::pair<FileJanitor, Journal> setupJournal(size_t hashCount = 0)
{
    const auto basename = tempFilename("/tmp/journal");

    auto janitor = FileJanitor{basename + ".draft"};
    auto journal = Journal(basename, { });

    fs::exists(basename + ".draft");

    for (size_t i = 0; i < hashCount; ++i)
    {
        auto rec = defaultHashRecord(i);
        journal.writeHash(rec.fileId, rec.offset, rec.size, rec.hash);
    }

    return {std::move(janitor), std::move(journal)};
}

} // namespace

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

TEST(cursor, no_hash)
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

TEST(cursor, seek)
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
    c.seek(0, Cursor::Set);
    EXPECT_TRUE(c.valid());

    // seek to end -1 to remain valid.
    c.seek(-1, Cursor::End);
    EXPECT_TRUE(c.valid());

    // seek to begin -1 to remain invalid.
    c.seek(-1, Cursor::Set);
    EXPECT_FALSE(c.valid());

    // seek to end -1 to become valid.
    c.seek(-1, Cursor::End);
    EXPECT_TRUE(c.valid());

    // seek to current -1 to become invalid.
    c.seek(-1, Cursor::Current);
    EXPECT_FALSE(c.valid());

    // seek to current +1 to remain invalid.
    c.seek(1, Cursor::Current);
    EXPECT_FALSE(c.valid());
}

TEST(cursor, eventual_hash)
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
    c.seek(0, Cursor::Set);
    EXPECT_TRUE(c.valid());

    // seek to end to become invalid.
    c.seek(0, Cursor::End);
    EXPECT_FALSE(c.valid());

    // seek set to become valid.
    c.seek(0, Cursor::Set);
    EXPECT_TRUE(c.valid());
}

TEST(cursor, record)
{
    auto [janitor, j] = setupJournal();

    auto c = j.cursor();
    EXPECT_FALSE(c.valid());

    auto rec = c.hashRecord();
    EXPECT_FALSE(rec.has_value());

    auto hash0 = defaultHashRecord(0);
    auto hash1 = defaultHashRecord(1);

    constexpr auto recordMatches = [](const HashRecord &a, const HashRecord &b) {
            return !bcmp(&a, &b, sizeof(a));
        };

    ASSERT_EQ(0, j.writeHash(hash0.fileId, hash0.offset, hash0.size, hash0.hash));
    ASSERT_TRUE(c.seek(0, Cursor::Set).valid());

    rec = c.hashRecord();
    ASSERT_TRUE(rec.has_value());
    EXPECT_TRUE(recordMatches(*rec, hash0));

    ASSERT_EQ(0, j.writeHash(hash1.fileId, hash1.offset, hash1.size, hash1.hash));
    rec = c.seek(1).hashRecord();
    ASSERT_TRUE(rec.has_value());

    rec = c.hashRecord();
    ASSERT_TRUE(rec.has_value());
    EXPECT_TRUE(recordMatches(*rec, hash1));

    c.seek(0, Cursor::Set);
    rec = c.hashRecord();
    ASSERT_TRUE(rec.has_value());
    EXPECT_TRUE(recordMatches(*rec, hash0));

    c.seek(0, Cursor::End);
    rec = c.hashRecord();
    EXPECT_FALSE(rec.has_value());

    c.seek(-1, Cursor::End);
    rec = c.hashRecord();
    ASSERT_TRUE(rec.has_value());

    rec = c.hashRecord();
    EXPECT_TRUE(recordMatches(*rec, hash1));
}

TEST(iterator, begin_end)
{
    auto [janitor, journal] = setupJournal(1);
    constexpr auto hash0 = defaultHashRecord(0);

    auto first = journal.begin();
    EXPECT_EQ(first->hash, hash0.hash);
    EXPECT_EQ((*first).hash, hash0.hash);

    auto last = journal.end();
    EXPECT_THROW(*last, std::runtime_error);

    first -= 1;
    EXPECT_EQ(first, last);
}

TEST(iterator, inc_dec)
{
    constexpr auto hash0 = defaultHashRecord(0);
    constexpr auto hash1 = defaultHashRecord(1);

    auto [janitor, journal] = setupJournal(2);

    auto iter = journal.begin();
    const auto last = journal.end();

    EXPECT_EQ(iter->hash, hash0.hash);

    ++iter;
    EXPECT_EQ(iter->hash, hash1.hash);

    --iter;
    EXPECT_EQ(iter->hash, hash0.hash);

    ++iter;
    EXPECT_EQ(iter->hash, hash1.hash);

    ++iter;
    EXPECT_TRUE(iter == last);

    auto old = iter--;
    EXPECT_TRUE(old == last);
    EXPECT_FALSE(iter == last);
    EXPECT_EQ(iter->hash, hash1.hash);

    old = iter++;
    EXPECT_FALSE(old == last);
    EXPECT_TRUE(iter == last);
    EXPECT_EQ(old->hash, hash1.hash);
    EXPECT_THROW(iter->hash, std::runtime_error);
}

TEST(iterator, seek_op)
{
    constexpr auto hash0 = defaultHashRecord(0);
    constexpr auto hash1 = defaultHashRecord(1);
    constexpr auto hash2 = defaultHashRecord(2);
    constexpr auto hash5 = defaultHashRecord(5);

    auto [janitor, journal] = setupJournal(6);

    auto iter = journal.begin();
    auto last = journal.end();

    ASSERT_EQ(iter->hash, hash0.hash);

    iter += 5;
    ASSERT_NE(iter, last);
    EXPECT_EQ(iter->hash, hash5.hash);

    iter -= 5;
    ASSERT_NE(iter, last);
    EXPECT_EQ(iter->hash, hash0.hash);

    iter += 2;
    EXPECT_EQ(iter->hash, hash2.hash);

    iter -= 1;
    EXPECT_EQ(iter->hash, hash1.hash);

    // negative offset.
    iter += -1;
    EXPECT_EQ(iter->hash, hash0.hash);

    // negative offset.
    iter -= -1;
    EXPECT_EQ(iter->hash, hash1.hash);
}

TEST(iterator, seek_invalid)
{
    constexpr auto hash0 = defaultHashRecord(0);

    auto [janitor, journal] = setupJournal(6);

    auto iter = journal.begin();
    auto last = journal.end();

    ASSERT_EQ(iter->hash, hash0.hash);

    iter += 100;
    EXPECT_TRUE(iter == last);

//    iter = journal.begin();
//    iter -= 100;
//    EXPECT_EQ(iter, last);
//
//    iter = journal.begin();
//    iter += -100;
//    EXPECT_EQ(iter, last);
//
//    iter = journal.begin();
//    iter -= -100;
//    EXPECT_EQ(iter, last);
}
