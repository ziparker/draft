/**
 * @file BufferPool.cc
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

#include <draft/util/BufferPool.hh>
#include <draft/util/Util.hh>

namespace draft::util {

////////////////////////////////////////////////////////////////////////////////
// FreeList

FreeList::FreeList(size_t size)
{
    list_.resize(size);
    std::iota(begin(list_), end(list_), 1u);
    list_.back() = End;
}

size_t FreeList::get()
{
    auto idx = free_;

    if (free_ == End)
        return ~0u;

    free_ = list_[free_];

    return idx;
}

void FreeList::put(size_t idx)
{
    if (idx < list_.size())
        list_[idx] = free_;

    free_ = idx;
}

////////////////////////////////////////////////////////////////////////////////
// BufferPool::Buffer

BufferPool::Buffer &BufferPool::Buffer::operator=(Buffer &&o) noexcept
{
    if (pool_)
        pool_->put(freeIdx_);

    data_ = o.data_;
    o.data_ = nullptr;

    size_ = o.size_;
    o.size_ = 0;

    freeIdx_ = o.freeIdx_;
    o.freeIdx_ = 0;

    pool_ = o.pool_;
    o.pool_ = nullptr;

    return *this;
}

BufferPool::Buffer::Buffer(const std::shared_ptr<BufferPool> &pool, size_t index, void *data, size_t size):
    data_(data),
    size_(size),
    freeIdx_(index),
    pool_(pool)
{
}

////////////////////////////////////////////////////////////////////////////////
// BufferPool

std::shared_ptr<BufferPool> BufferPool::make(size_t chunkSize, size_t count)
{
    auto p = std::shared_ptr<BufferPool>(new BufferPool);
    p->init(chunkSize, count);
    return p;
}

BufferPool::~BufferPool() noexcept
{
    done_ = true;
    cond_.notify_all();
}

BufferPool::Buffer BufferPool::get()
{
    Lock lk(mtx_);

    size_t idx{ };

    cond_.wait(lk, [this, &idx] {
        if (done_)
            return true;
        idx = freeList_.get();
        return idx != ~0u;
    });

    if (done_ || idx == ~0u)
        return { };

    return {
        shared_from_this(),
        idx,
        mmap_.uint8Data(idx * chunkSize_),
        chunkSize_
    };
}

BufferPool::BufferPool(size_t chunkSize, size_t chunkCount):
    chunkSize_(chunkSize),
    chunkCount_(chunkCount)
{
    init();
}

void BufferPool::init(size_t chunkSize, size_t chunkCount)
{
    chunkSize_ = chunkSize;
    chunkCount_ = chunkCount;

    init();
}

void BufferPool::init()
{
    mmap_ = ScopedMMap::map(
        nullptr,
        roundBlockSize(chunkSize_ * chunkCount_),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
        -1,
        0);

    freeList_ = FreeList{chunkCount_};
}

void BufferPool::put(size_t index)
{
    if (done_)
        return;

    {
        Lock lk(mtx_);
        freeList_.put(index);
    }

    cond_.notify_one();
}

}
