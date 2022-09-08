/**
 * @file Journal.hh
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

#ifndef __DRAFT_UTIL_JOURNAL_HH__
#define __DRAFT_UTIL_JOURNAL_HH__

#include <optional>
#include <span>
#include <system_error>
#include <vector>

#include "ScopedFd.hh"
#include "Util.hh"

namespace draft::util {

class Cursor;
class CursorIter;

class Journal
{
public:
    struct HashRecord
    {
        uint64_t hash{ };
        uint64_t offset{ };
        uint64_t size{ };
        uint16_t fileId{ };
        uint8_t pad0_[6]{ };
    };

    static_assert(sizeof(HashRecord) == 4 * 8);

    using const_iterator = CursorIter;

    explicit Journal(std::string basename, const std::vector<FileInfo> &info);

    void sync();

    int writeHash(uint16_t fileId, size_t offset, size_t size, uint64_t hash);

    const_iterator begin() const;
    const_iterator end() const;

private:

    void writeHeader(const std::vector<FileInfo> &info);
    void writeFileData(const void *data, size_t size);
    void writeHashRecord(const HashRecord &record);

    ScopedFd fd_;
};

class Cursor
{
public:
    enum Whence
    {
        Start,
        Current,
        End
    };

    Cursor();
    ~Cursor() noexcept;

    Cursor &seek(off_t count, Whence whence = Whence::Current);
    bool valid() const;

    std::optional<Journal::HashRecord> hashRecord() const;

private:
    friend class Journal;

    explicit Cursor(ScopedFd fd);

    size_t journalRecordCount() const;
    size_t journalHashOffset() const;

    struct Data;
    std::shared_ptr<Data> d_;
};

class CursorIter
{
public:
    using HashRecord = Journal::HashRecord;

    const HashRecord *operator->() const
    {
        return &record_;
    }

    const HashRecord &operator*() const
    {
        return record_;
    }

    CursorIter &operator++()
    {
        cursor_.seek(1);
        return *this;
    }

    CursorIter operator++(int)
    {
        auto cursor = cursor_.seek(1);
        return *this;
    }

    CursorIter operator--()
    {
        auto cursor = cursor_.seek(-1);
        return *this;
    }

    CursorIter operator--(int)
    {
        auto cursor = cursor_.seek(-1);
        return *this;
    }

private:
    friend class Journal;

    explicit CursorIter(Cursor c):
        cursor_(c)
    {
    }

    Cursor cursor_;
    HashRecord record_;
};

}

#endif
