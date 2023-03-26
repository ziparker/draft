/**
 * @file ThreadExecutor.hh
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

#ifndef __DRAFT_UTIL_THREAD_EXECUTOR_HH__
#define __DRAFT_UTIL_THREAD_EXECUTOR_HH__

#include <spdlog/spdlog.h>

#include <memory>
#include <ranges>
#include <thread>

namespace draft::util {

class ThreadExecutor
{
public:
    enum Options
    {
        None = 0,
        DoFinalize = 1
    };

    template <typename ...Args>
    ThreadExecutor(Args &&...args)
    {
        (add(std::forward<Args>(args)), ...);
    }

    template <typename T>
    ThreadExecutor &add(T &&runnable, unsigned opts = 0)
    {
        runq_.push_back(
            std::make_unique<Runnable_<T>>(std::forward<T>(runnable), opts));

        return *this;
    }

    template <typename T>
    ThreadExecutor &add(std::vector<T> runnables, unsigned opts = 0)
    {
        auto runnablesView = std::views::transform(
            runnables, [opts](auto &&r) {
                return std::make_unique<Runnable_<T>>(
                    std::forward<T>(r), opts);
            });

        runq_.insert(
            end(runq_),
            std::make_move_iterator(begin(runnablesView)),
            std::make_move_iterator(end(runnablesView)));

        return *this;
    }

    bool runOnce();

    bool empty() const noexcept;
    bool finished() const;
    void cancel() noexcept;
    void waitFinished() noexcept;

private:
    class Runnable
    {
    public:
        virtual ~Runnable() = default;
        virtual bool runOnce() const = 0;
        virtual void cancel() noexcept = 0;
        virtual bool finished() const = 0;
    };

    template <typename T>
    class Runnable_: public Runnable
    {
    public:
        Runnable_(T t, unsigned options = 0):
            options_(options)
        {
            thd_ = std::jthread(
                [this](std::stop_token token, T t_) mutable {
                    while (!token.stop_requested())
                    {
                        try
                        {
                            if (!t_.runOnce(token))
                                break;
                        } catch (const std::exception &e) {
                            const auto id = std::this_thread::get_id();
                            spdlog::warn("thd {:#x} exception: {}"
                                , std::hash<std::thread::id>{ }(id)
                                , e.what());
                            break;
                        }
                    }

                    if (token.stop_requested() && (options_ & Options::DoFinalize))
                    {
                        spdlog::debug("thd runnable finalizing.");
                        t_.runOnce(token);
                    }

                    finished_ = true;
                    spdlog::debug("thd runnable exiting.");
                }, std::move(t));
        }

        ~Runnable_() noexcept
        {
            thd_.request_stop();
        }

    private:
        bool runOnce() const override
        {
            return !finished_;
        }

        void cancel() noexcept override
        {
            thd_.request_stop();
        }

        bool finished() const override
        {
            return finished_;
        }

        std::atomic_bool finished_{ };
        unsigned options_{ };
        std::jthread thd_{ };
    };

    std::vector<std::unique_ptr<Runnable>> runq_;
};

}

#endif
