/* @file ScopedTimer.hh
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

#ifndef __DRAFT_UTIL_SCOPED_TIMER_HH__
#define __DRAFT_UTIL_SCOPED_TIMER_HH__

#include <chrono>
#include <functional>
#include <string>

namespace draft::util {

class ScopedTimer
{
public:
    using SecCallback = std::function<void(double)>;

    ScopedTimer():
        start_(Clock::now())
    {
    }

    explicit ScopedTimer(SecCallback cb):
        cb_(std::move(cb)),
        start_(Clock::now())
    {
    }

    ScopedTimer(const ScopedTimer &) = default;
    ScopedTimer &operator=(const ScopedTimer &) = default;
    ScopedTimer(ScopedTimer &&) = default;
    ScopedTimer &operator=(ScopedTimer &&) = default;

    ~ScopedTimer() noexcept
    {
        if (cb_)
            cb_(elapsedSec());
    }

    double elapsedSec() const noexcept
    {
        if (start_ == Clock::time_point{ })
            return 0.0;

        return Duration(Clock::now() - start_).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;

    SecCallback cb_;
    Clock::time_point start_;
};

}

#endif
