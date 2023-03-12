/**
 * @file Hasher.hh
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

#ifndef __DRAFT_UTIL_HASHER_HH_
#define __DRAFT_UTIL_HASHER_HH_

#include <functional>
#include <stop_token>

#include "Util.hh"

namespace draft::util {

class Journal;

class Hasher
{
public:
    struct DigestInfo
    {
        uint64_t digest{ };
        size_t offset{ };
        size_t size{ };
        unsigned fileId{ };
    };

    using Buffer = BufferPool::Buffer;
    using Callback = std::function<void(const DigestInfo &)>;

    Hasher(BufQueue &queue, const std::shared_ptr<Journal> &hashLog);
    Hasher(BufQueue &queue, Callback cb);

    bool runOnce(std::stop_token stopToken);

private:
    uint64_t hash(const BDesc &desc);

    BufQueue *queue_{ };
    const std::shared_ptr<Journal> hashLog_{ };
    Callback cb_{ };
};

}

#endif
