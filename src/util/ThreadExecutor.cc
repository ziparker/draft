/**
 * @file ThreadExecutor.cc
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

#include <draft/util/ThreadExecutor.hh>

namespace draft::util {

bool ThreadExecutor::runOnce()
{
    std::erase_if(runq_,
        [](const auto &r) {
            return !r->runOnce();
        });

    return !empty();
}

bool ThreadExecutor::empty() const noexcept
{
    return runq_.empty();
}

bool ThreadExecutor::finished() const
{
    return std::all_of(
        begin(runq_),
        end(runq_),
        [](const auto &r) { return !r || r->finished(); });
}

void ThreadExecutor::cancel() noexcept
{
    for (auto &r : runq_)
        r->cancel();
}

void ThreadExecutor::waitFinished() noexcept
{
    try {
        runq_.clear();
    } catch (...) {
    }
}

}
