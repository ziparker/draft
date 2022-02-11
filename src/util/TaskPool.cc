/**
 * @file TaskPool.cc
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

#include <draft/util/TaskPool.hh>

namespace draft::util {

TaskPool::TaskPool(size_t size)
{
    resize(size);
}

TaskPool::~TaskPool() noexcept
{
    cancel();
}

void TaskPool::setQueueSizeLimit(size_t limit)
{
    q_.setSizeLimit(limit);
}

void TaskPool::cancel() noexcept
{
    q_.cancel();
}

bool TaskPool::cancelled() const noexcept
{
    return q_.done();
}

size_t TaskPool::size() const noexcept
{
    return threads_.size();
}

void TaskPool::resize(size_t newSize)
{
    const auto prevSize = threads_.size();
    threads_.resize(newSize);

    for (auto i = prevSize; i < newSize; ++i)
        threads_[i] = std::jthread([this](std::stop_token token){ stealWork(token); });
}

void TaskPool::stealWork(std::stop_token token)
{
    while (!token.stop_requested() && !q_.done())
    {
        if (auto work = q_.get(); work && *work)
            (*work)(token);
    }
}

}
