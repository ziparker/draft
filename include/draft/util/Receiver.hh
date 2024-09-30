/**
 * @file Receiver.hh
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

#ifndef __DRAFT_UTIL_RECEIVER_HH__
#define __DRAFT_UTIL_RECEIVER_HH__

#include <stop_token>

#include "Journal.hh"
#include "Util.hh"

namespace draft::util {

class Receiver
{
public:
    using Buffer = BufferPool::Buffer;

    Receiver(ScopedFd fd, BufQueue &queue, BufQueue *hashQueue = nullptr);

    void useHashLog(const std::shared_ptr<Journal> &hashLog)
    {
        hashLog_ = hashLog;
    }

    bool runOnce(std::stop_token stopToken);

private:
    int waitConnect();
    bool waitData(std::stop_token stopToken);

    int readHeader();
    int read();

    BufferPoolPtr pool_{ };
    BufQueue *queue_{ };
    BufQueue *hashQueue_{ };
    std::shared_ptr<Journal> hashLog_{ };
    wire::ChunkHeader header_{ };
    Buffer buf_{ };
    size_t offset_{ };
    ScopedFd fd_{ };
    ScopedFd svcFd_{ };
    bool haveHeader_{ };
};

}

#endif
