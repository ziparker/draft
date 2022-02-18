/**
 * @file Writer.cc
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

#include <spdlog/spdlog.h>

#include <draft/util/Stats.hh>
#include <draft/util/Writer.hh>

namespace draft::util {

Writer::Writer(FdMap fdMap, BufQueue &queue):
    queue_(&queue),
    fdMap_(std::move(fdMap))
{
}
    
bool Writer::runOnce(std::stop_token stopToken)
{
    using namespace std::chrono_literals;

    while (auto desc = queue_->get(100ms))
    {
        if (!desc->buf)
            break;

        ++stats().dequeuedBlockCount;

        const auto len = write(std::move(*desc));

        stats().diskByteCount += len;

        if (auto s = stats(desc->fileId))
        {
            ++s->get().dequeuedBlockCount;
            s->get().diskByteCount += len;
        }
    }

    return !stopToken.stop_requested();
}

int Writer::getFd(unsigned id)
{
    auto iter = fdMap_.find(id);
    if (iter == end(fdMap_))
        return -1;

    return iter->second;
}

size_t Writer::write(BDesc desc)
{
    const auto fd = getFd(desc.fileId);

    if (fd < 0)
    {
        spdlog::error("no mapped fd for file id {}"
            , desc.fileId);

        return 0;
    }

    iovec iov{
        desc.buf->data(),
        roundBlockSize(desc.len)
    };

    spdlog::trace("write {} -> id {}"
        , iov.iov_len
        , desc.fileId);

    return writeChunk(fd, &iov, 1, desc.offset);
}

}
