/**
 * @file BufferPool.hh
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

#ifndef __DRAFT_UTIL_BUFFER_POOL_HH__
#define __DRAFT_UTIL_BUFFER_POOL_HH__

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "ScopedMMap.hh"

namespace draft::util {

////////////////////////////////////////////////////////////////////////////////
// FreeList

class FreeList
{
public:
    static constexpr auto End = ~size_t{0};

    FreeList() = default;

    explicit FreeList(size_t size);

    size_t get();
    void put(size_t idx);

private:
    std::vector<size_t> list_{ };
    size_t free_{ };
};

////////////////////////////////////////////////////////////////////////////////
// BufferPool

class BufferPool: public std::enable_shared_from_this<BufferPool>
{
public:
    using Lock = std::unique_lock<std::mutex>;

    struct Buffer
    {
    public:
        Buffer() = default;

        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;

        Buffer(Buffer &&o) noexcept
        {
            *this = std::move(o);
        }

        Buffer &operator=(Buffer &&o) noexcept;

        ~Buffer() noexcept
        {
            if (pool_)
                pool_->put(freeIdx_);
        }

        void *data() noexcept { return data_; };
        const void *data() const noexcept { return data_; };

        uint8_t *uint8Data() noexcept { return reinterpret_cast<uint8_t *>(data_); };
        const uint8_t *uint8Data() const noexcept { return reinterpret_cast<const uint8_t *>(data_); };

        size_t size() const noexcept { return size_; };

        size_t freeIndex() const noexcept { return freeIdx_; }

        explicit operator bool() const noexcept { return data_; }

    private:
        friend class BufferPool;

        Buffer(const std::shared_ptr<BufferPool> &pool, size_t index, void *data, size_t size);

        void *data_{ };
        size_t size_{ };
        size_t freeIdx_{ };
        std::shared_ptr<BufferPool> pool_{ };
    };

    static std::shared_ptr<BufferPool> make(size_t chunkSize, size_t count);

    ~BufferPool() noexcept;

    Buffer get();

private:
    BufferPool() = default;

    BufferPool(size_t chunkSize, size_t chunkCount);

    void init(size_t chunkSize, size_t chunkCount);
    void init();

    void put(size_t index);

    std::mutex mtx_{ };
    std::condition_variable cond_{ };
    FreeList freeList_{ };
    ScopedMMap mmap_{ };
    size_t chunkSize_{ };
    size_t chunkCount_{ };
    bool done_{ };
};

using BufferPoolPtr = std::shared_ptr<BufferPool>;

}

#endif
