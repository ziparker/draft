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

namespace draft::util {

namespace {

enum class Which { A, B };

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
    uint64_t size{ };
    uint64_t hash{ };
    Which which{ };
};

}

JournalFileDiff diffJournals(const Journal &journalA, const Journal &journalB)
{
    std::map<Key, Value> map;
    auto diffs = std::vector<JournalFileDiff::Difference>{ };

    auto iterA = journalA.begin();
    const auto endA = journalA.end();

    auto iterB = journalB.begin();
    const auto endB = journalB.end();

    auto diff = [&map, &diffs](const auto &record, Which which) {
            if (auto other = map.find(Key{record.fileId, record.offset}); other != end(map))
            {
                if (record.hash != other->second.hash)
                {
                    diffs.push_back({
                        .offset = record.offset,
                        .size = record.size,
                        .hashA = which == Which::A ? record.hash : other->second.hash,
                        .hashB = which == Which::B ? record.hash : other->second.hash,
                        .fileId = record.fileId
                    });
                }

                map.erase(other);

                return;
            }

            map.insert({
                {record.fileId, record.offset},
                {record.size, record.hash, which}});

            spdlog::debug("diff map size: {}", map.size());
        };

    while (iterA != endA || iterB != endB)
    {
        if (iterA != endA)
        {
            diff(*iterA, Which::A);
            ++iterA;
        }

        if (iterB != endB)
        {
            diff(*iterB, Which::B);
            ++iterB;
        }
    }

    std::transform(
        begin(map), end(map),
        std::back_inserter(diffs),
        [](const auto &kv) {
            const auto &[k, v] = kv;
            return JournalFileDiff::Difference {
                .offset = k.offset,
                .size = v.size,
                .hashA = v.which == Which::A ?
                    v.hash : 0,
                .hashB = v.which == Which::B ?
                    v.hash : 0,
                .fileId = k.file
            };
        });

    return {std::move(diffs)};
}

JournalFileDiff verifyJournal(const Journal &journal, const std::string &pathRoot)
{
    const auto fileMap = [](const Journal &j, const std::filesystem::path &root) {
            namespace fs = std::filesystem;

            const auto &info = j.fileInfo();

            auto pathMap = std::unordered_map<uint16_t, std::string>{ };
            pathMap.reserve(info.size());

            for (const auto &item : info)
                pathMap[item.id] = root.empty() ? item.path : (root / item.path).string();

            return pathMap;
        }(journal, pathRoot);

    // enqueue readers for files in journal.
    // pass all reader chunks to hashers.
    // on chunk hash complete, verify hash in journal.
    for (const auto &record : journal)
    {
        auto iter = fileMap.find(record.fileId);

        if (iter == end(fileMap))
        {
            spdlog::warn("skipping record: {} @ {}", record.fileId, record.offset);
            continue;
        }

        // enqueue read + hash info for this chunk.
    }

    return { };
}

}
