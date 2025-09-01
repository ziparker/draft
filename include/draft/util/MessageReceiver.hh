/**
 * @file MessageReceiver.hh
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Zachary Parker
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

#ifndef __DRAFT_UTIL_MESSAGE_RECEIVER_HH__
#define __DRAFT_UTIL_MESSAGE_RECEIVER_HH__

#include <vector>

#include "Util.hh"

namespace draft::util {

class MessageReceiver
{
public:
    explicit MessageReceiver(int fd);

    bool runOnce();

    template <typename T>
    T get() const
    {
        auto header = this->get();

        return deserializeMessage(
            Buffer{buf_.data() + sizeof(*header), header->payloadLength});
    }

private:
    const wire::ChunkHeader *get() const;

    std::vector<uint8_t> buf_{ };
    size_t offset_{ };
    int fd_{-1};
    bool haveMessage_{ };
};

}

#endif
