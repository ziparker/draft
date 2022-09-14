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
#include <chrono>
#include <optional>
#include <vector>

#include <spdlog/spdlog.h>

namespace draft::util {

struct Stats
{
    std::atomic_uint64_t diskByteCount{ };
    std::atomic_uint64_t queuedBlockCount{ };
    std::atomic_uint64_t dequeuedBlockCount{ };
    std::atomic_uint64_t netByteCount{ };
    std::atomic_uint64_t fileByteCount{ };
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

    Stats *get(unsigned id)
    {
        if (id >= fileStats.size())
            return { };

        return &fileStats[id];
    }

    Stats globalStats;
    std::vector<Stats> fileStats;
};

struct BandwidthMonitor
{
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;

    struct Sample
    {
        Clock::time_point time{ };
        size_t value{ };
    };

    double update(size_t value)
    {
        const auto now = Clock::now();
        const auto sample = Sample{now, value};

        const auto dt = Duration(now - prevSample_.time).count();
        const auto dv = value - prevSample_.value;

        avg_ = .95 * avg_ + .05 * static_cast<double>(dv) / dt;

        prevSample_ = sample;

        return avg_;
    }

    double dataRate() const noexcept
    {
        return avg_;
    }

    double etaSec(size_t totalLen) const noexcept
    {
        if (avg_ <= 0.0 || totalLen < prevSample_.value)
            return 0.0;

        return (totalLen - prevSample_.value) / avg_;
    }

    Sample prevSample_{ };
    double avg_{1e9};
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
