/**
 * @file Stats.hh
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

#ifndef __DRAFT_UTIL_STATS_HH__
#define __DRAFT_UTIL_STATS_HH__

#include <atomic>
#include <optional>

namespace draft::util {

struct Stats
{
    std::atomic_uint diskByteCount{ };
    std::atomic_uint queuedBlockCount{ };
    std::atomic_uint dequeuedBlockCount{ };
    std::atomic_uint netByteCount{ };
    std::atomic_uint fileByteCount{ };
};

struct StatsManager
{
    Stats &get()
    {
        return globalStats;
    }

    void reallocate(size_t size)
    {
        fileStats = std::vector<Stats>(size);
    }

    std::optional<std::reference_wrapper<Stats>> get(unsigned id)
    {
        if (id >= fileStats.size())
            return { };

        return fileStats[id];
    }

    Stats globalStats;
    std::vector<Stats> fileStats;
};

inline StatsManager &statsMgr()
{
    static StatsManager mgr{ };
    return mgr;
}

inline decltype(auto) stats()
{
    return statsMgr().get();
}

inline decltype(auto) stats(size_t id)
{
    return statsMgr().get(id);
}

}

#endif
