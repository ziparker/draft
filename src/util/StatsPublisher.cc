/* @file ProgressDisplay.cc
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

#include <algorithm>
#include <cstring>

#include <draft/util/ProgressDisplay.hh>
#include <spdlog/spdlog.h>

namespace draft::ui {

using util::FileInfo;
using util::Stats;

ProgressDisplay::ProgressDisplay()
{
    setupDisplay();
}

ProgressDisplay::ProgressDisplay(const std::vector<FileInfo> &info):
    ProgressDisplay()
{
    registerFiles(info);
}

ProgressDisplay::~ProgressDisplay() noexcept
{
    teardownDisplay();
}

void ProgressDisplay::runOnce()
{
    renderStats();
}

void ProgressDisplay::handleStatsPrivate(const Stats &, const std::vector<Stats> &)
{
}

void ProgressDisplay::registerFiles(const std::vector<FileInfo> &infos)
{
    files_.resize(infos.size());

    std::transform(begin(infos), end(infos), begin(files_),
        [](const FileInfo &info) {
            return FileEntry{info.path, info.status.size, info.id};
        });
}

void ProgressDisplay::setupDisplay()
{
    constexpr auto ClearScreen = "\x1b[2J";
    spdlog::info(__func__);

    dprintf(1, "%s", ClearScreen);
}

void ProgressDisplay::teardownDisplay()
{
}

void ProgressDisplay::renderStats()
{
}

}
