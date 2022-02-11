/**
 * @file WaitQueue.hh
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

#ifndef __DRAFT_UTIL_WAIT_QUEUE_HH__
#define __DRAFT_UTIL_WAIT_QUEUE_HH__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace draft::util {

template <typename T, typename Alloc = std::allocator<T>, template <typename, typename> class Q = std::deque>
class WaitQueue
{
public:
    using Clock = std::chrono::steady_clock;
    using Mutex = std::timed_mutex;
    using Lock = std::unique_lock<Mutex>;
    using Value = T;
    using ReturnType = std::optional<Value>;
    using Queue = Q<Value, Alloc>;

    enum class Status
    {
        OK,
        TimedOut,
        Full,
        Error
    };

    bool put(Value t)
    {
        return doPut(std::move(t), nullptr) == Status::OK;
    }

    bool put(Value t, const Clock::time_point &deadline)
    {
        return doPut(std::move(t), &deadline) == Status::OK;
    }

    template <typename Rep, typename Period>
    bool put(Value t, const std::chrono::duration<Rep, Period> &tmo)
    {
        const auto deadline = Clock::now() + tmo;
        return doPut(std::move(t), &deadline) == Status::OK;
    }

    ReturnType get()
    {
        return doGet(nullptr);
    }

    template <typename Rep, typename Period>
    ReturnType get(const std::chrono::duration<Rep, Period> &tmo)
    {
        auto deadline = Clock::now() + tmo;
        return doGet(&deadline);
    }

    ReturnType get(const Clock::time_point &deadline)
    {
        return doGet(&deadline);
    }

    ReturnType tryGet()
    {
        const auto op = [this]()
            -> std::remove_reference_t<decltype(q_.front())> {
            if (q_.empty())
                return { };

            auto t = std::move(q_.front());
            q_.pop_front();
            return t;
        };

        return tryOp(op);
    }

    void cancel() noexcept
    {
        done_ = true;
        cond_.notify_all();
    }

    void resume() noexcept
    {
        done_ = false;
    }

    void setSizeLimit(size_t limit)
    {
        sizeLimit_ = limit;
    }

    size_t sizeLimit(size_t limit) const noexcept
    {
        return sizeLimit_;
    }

    bool done() const noexcept
    {
        return done_;
    }

private:
    ReturnType doGet(const Clock::time_point *deadline)
    {
        const auto op = [this] {
            auto t = std::move(q_.front());
            q_.pop_front();
            return t;
        };

        return doWithCondition(op, deadline);
    }

    Status doPut(Value t, const Clock::time_point *deadline)
    {
        const auto op = [this, v = std::move(t)]() -> Status {
            if (q_.size() >= sizeLimit_)
                return Status::Full;

            q_.push_back(std::move(v));

            return Status::OK;
        };

        bool timedOut{ };
        const auto status = doWithLock(op, deadline, &timedOut);

        if (status && *status == Status::Full)
            return *status;

        if (!timedOut)
            cond_.notify_one();

        return timedOut ? Status::TimedOut : Status::OK;
    }

    auto doWithLock(const auto &op, const Clock::time_point *deadline, bool *timedOut = nullptr)
        -> std::optional<std::invoke_result_t<decltype(op)>>
    {
        Lock lk(mtx_, std::defer_lock_t{ });

        if (deadline)
        {
            if (!lk.try_lock_until(*deadline))
            {
                if (timedOut)
                    *timedOut = true;

                return { };
            }
        }
        else
        {
            lk.lock();
        }

        return op();
    }

    auto doWithCondition(const auto &op, const Clock::time_point *deadline, bool *timedOut = nullptr)
        -> std::optional<Value>
    {
        Lock lk(mtx_, std::defer_lock_t{ });

        if (deadline)
        {
            if (!lk.try_lock_until(*deadline))
            {
                if (timedOut)
                    *timedOut = true;

                return { };
            }
        }
        else
        {
            lk.lock();
        }

        if (!deadline)
        {
            cond_.wait(lk, [this]{ return done_ || !q_.empty(); });
        }
        else
        {
            if (!cond_.wait_until(lk, *deadline, [this]{ return done_ || !q_.empty(); }))
            {
                if (timedOut)
                    *timedOut = true;

                return { };
            }
        }

        if (timedOut)
            *timedOut = false;

        if (done_)
            return { };

        return op();
    }

    auto tryOp(const auto &op, bool *timedOut = nullptr)
        -> std::optional<Value>
    {
        if (timedOut)
            *timedOut = false;

        Lock lk(mtx_, std::defer_lock_t{ });

        if (!lk.try_lock())
        {
            if (timedOut)
                *timedOut = true;

            return { };
        }

        return op();
    }

    Mutex mtx_;
    std::condition_variable_any cond_;
    Queue q_;
    size_t sizeLimit_{std::numeric_limits<size_t>::max()};
    std::atomic_bool done_{ };
};

}

#endif
