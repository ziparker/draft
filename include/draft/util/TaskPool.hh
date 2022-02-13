/**
 * @file TaskPool.hh
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

#ifndef __DRAFT_UTIL_TASK_POOL_HH__
#define __DRAFT_UTIL_TASK_POOL_HH__

#include <future>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

#include "Util.hh"

namespace draft::util {

class TaskPool
{
public:
    TaskPool() = default;

    explicit TaskPool(size_t size);

    TaskPool(TaskPool &&) = default;
    TaskPool &operator=(TaskPool &&) = default;

    ~TaskPool() noexcept;

    void setQueueSizeLimit(size_t limit);

    void cancel() noexcept;
    bool cancelled() const noexcept;

    size_t size() const noexcept;
    void resize(size_t newSize);

    template <typename Function, typename ...Args>
        requires std::invocable<Function, std::stop_token, Args...>
    [[nodiscard]]
    auto launch(Function &&f, Args &&...args)
    {
        using result_type =
            std::invoke_result_t<
                std::decay_t<Function>,
                std::stop_token,
                std::decay_t<Args>...>;

        auto promise = std::make_shared<std::promise<result_type>>();
        auto future = promise->get_future();

        auto ok = q_.put([
            f = std::move(f),
            ...args = std::forward<Args>(args),
            p = std::move(promise)](std::stop_token stopToken) mutable {
                try {
                    p->set_value(std::invoke(std::move(f), stopToken, std::forward<Args>(args)...));
                } catch (...) {
                    p->set_exception(std::current_exception());
                }
            });

        if (!ok)
            return std::optional<decltype(future)>{ };

        return std::make_optional(std::move(future));
    }

private:
    using Work = std::function<void(std::stop_token)>;

    void stealWork(std::stop_token token);

    WaitQueue<Work> q_;
    std::vector<std::jthread> threads_;
};

}

#endif
