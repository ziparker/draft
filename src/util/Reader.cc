/**
 * @file Reader.cc
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

#include <chrono>

#include <spdlog/spdlog.h>

#include <draft/util/Reader.hh>
#include <draft/util/Stats.hh>

namespace draft::util {

Reader::Reader(const std::shared_ptr<ScopedFd> &fd, unsigned fileId, Segment segment, const BufferPoolPtr &pool, BufQueue *queue):
    fd_{fd},
    segment_{segment},
    pool_{pool},
    queue_{queue},
    fileId_{fileId}
{
}

int Reader::operator()(std::stop_token stopToken)
{
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;

    while (!stopToken.stop_requested())
    {
        auto buf = std::make_shared<Buffer>(pool_->get(Clock::now() + 20ms));

        if (!buf)
            continue;

        auto len = read(*buf);

        if (!len)
            return 0;

        stats().diskByteCount += len;

        if (auto s = stats(fileId_))
            s->diskByteCount += len;

        // keep trying to push this buffer onto the queue.
        //
        // if the queue is pushing back, we don't want to stack-up more
        // work.
        while (queue_ &&
            !stopToken.stop_requested() &&
            !queue_->put({buf, fileId_, segment_.offset, len}, 100ms))
        {
        }

        if (hashQueue_ && !hashQueue_->put({buf, fileId_, segment_.offset, len}, 1ms))
        {
            spdlog::warn("reader: unable to enqueue file {} offset {} len {} for hashing (queue full)."
                , fileId_, segment_.offset, len);
        }

        ++stats().queuedBlockCount;

        if (auto s = stats(fileId_))
            ++s->queuedBlockCount;

        segment_.offset += len;
    }

    return 0;
}

size_t Reader::read(Buffer &buf)
{
    spdlog::debug("reader segment progress: {}/{} ({:.1f})"
        , segment_.offset
        , segment_.len
        , static_cast<double>(segment_.offset) / segment_.len * 100);

    auto len = roundBlockSize(segment_.len - segment_.offset);
    len = std::min(len, buf.size());

    return readChunk(fd_->get(), buf.data(), len, segment_.offset);
}

}
