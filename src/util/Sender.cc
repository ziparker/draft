/**
 * @file Sender.cc
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

#include <draft/util/Journal.hh>
#include <draft/util/Sender.hh>
#include <draft/util/Stats.hh>

#include "xxhash.h"

namespace draft::util {

Sender::Sender(ScopedFd fd, BufQueue &queue):
    queue_(&queue),
    fd_(std::move(fd))
{
}

bool Sender::runOnce(std::stop_token stopToken)
{
    using namespace std::chrono_literals;

    using Clock = std::chrono::steady_clock;

    while (auto desc = queue_->get(Clock::now() + 1ms))
    {
        ++stats().dequeuedBlockCount;

        if (auto s = stats(desc->fileId))
            ++s->dequeuedBlockCount;

        const auto len = write(std::move(*desc)) - sizeof(wire::ChunkHeader);

        stats().netByteCount += len;

        if (auto s = stats(desc->fileId))
            s->netByteCount += len;
    }

    return !stopToken.stop_requested();
}

size_t Sender::write(BDesc desc)
{
    auto header = wire::ChunkHeader{ };
    header.magic = wire::ChunkHeader::Magic;
    header.fileOffset = desc.offset;
    header.payloadLength = desc.len;
    header.fileId = desc.fileId;

    iovec iov[2] = {
        {&header, sizeof(header)},
        {desc.buf->data(), desc.len}
    };

    if (hashLog_)
    {
        const auto digest = XXH3_64bits(desc.buf->data(), desc.len);

        hashLog_->writeHash(
            desc.fileId, desc.offset, desc.len, digest);
    }

    return writeChunk(fd_.get(), iov, 2);
}

}
