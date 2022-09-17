/**
 * @file JournalOperations.cc
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

#include <map>

#include <spdlog/spdlog.h>

#include <draft/util/JournalOperations.hh>

using draft::util::Journal;

namespace draft::cli {

namespace {

struct Key
{
    uint16_t file{ };
    uint64_t offset{ };
};

constexpr auto operator<=>(Key a, Key b)
{
    if (auto cmp = a.file <=> b.file; cmp != 0)
        return cmp;

    return a.offset <=> b.offset;
}

struct Value
{
    uint64_t hash{ };
};

}

JournalFileDiff diffJournals(const Journal &journalA, const Journal &journalB)
{
    std::map<Key, Value> map;
    auto diffs = std::vector<JournalFileDiff::Difference>{ };

    auto iterA = journalA.begin();
    const auto endA = journalA.begin();

    auto iterB = journalB.begin();
    const auto endB = journalB.end();

    auto diff = [&map, &diffs](const auto &record) {
            if (auto b = map.find(Key{record.fileId, record.offset}); b != end(map))
            {
                if (record.hash != b->second.hash)
                {
                    diffs.push_back({
                        .offset = record.offset,
                        .size = record.size,
                        .hashA = record.hash,
                        .hashB = b->second.hash,
                        .fileId = record.fileId
                    });
                }

                map.erase(b);

                return;
            }

            map.insert({{record.fileId, record.offset}, {record.hash}});
            spdlog::debug("diff map size: {}", map.size());
        };

    while (iterA != endA || iterB != endB)
    {
        if (iterA != endA)
        {
            diff(*iterA);
            ++iterA;
        }

        if (iterA != endA)
        {
            diff(*iterB);
            ++iterB;
        }
    }

    return {std::move(diffs)};
}

}
