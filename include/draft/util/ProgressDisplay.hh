/* @file ProgressDisplay.hh
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

#ifndef __DRAFT_UTIL_PROGRESS_DISPLAY_HH__
#define __DRAFT_UTIL_PROGRESS_DISPLAY_HH__

#include <memory>

#include "Stats.hh"
#include "Util.hh"

namespace draft::ui {

class ProgressDisplay: public util::StatsHandler
{
public:
    struct FileEntry
    {
        std::string path;
        size_t size{ };
        unsigned id{ };
    };

    ProgressDisplay();
    explicit ProgressDisplay(const std::vector<util::FileInfo> &info);

    ProgressDisplay(const ProgressDisplay &) = delete;
    ProgressDisplay &operator=(const ProgressDisplay &) = delete;

    ~ProgressDisplay() noexcept;

    void runOnce();

private:
    struct Data;

    void handleStatsPrivate(const util::Stats &globalStats, const std::vector<util::Stats> &fileStats) override;

    void registerFiles(const std::vector<util::FileInfo> &info);

    void setupDisplay();
    void teardownDisplay();

    void renderStats();

    std::unique_ptr<Data> d_;
};

}

#endif
