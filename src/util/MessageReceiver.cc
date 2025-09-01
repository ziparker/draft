/**
 * @file MessageReceiver.cc
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

#include <sys/socket.h>

#include <spdlog/spdlog.h>

#include <draft/util/MessageReceiver.hh>
#include <draft/util/UtilJson.hh>

namespace draft::util {

MessageReceiver::MessageReceiver(int fd):
    fd_{fd}
{
}

bool MessageReceiver::runOnce()
{
    if (haveMessage_)
        return true;

    if (buf_.size() - offset_ < 4096)
        buf_.resize(buf_.size() + 4096);

    auto data = buf_.data() + offset_;
    auto sz = buf_.size() - offset_;

    auto len = ::recv(fd_, data, sz, 0);
    spdlog::debug("rx'd {} for info", len);

    if (len < 0)
        throw std::system_error(errno, std::system_category(), "recv");

    offset_ += static_cast<size_t>(len);

    if (!len)
    {
        buf_.resize(offset_);
        haveMessage_ = true;
        return true;
    }

    return false;
}

wire::ChunkHeader *MessageReceiver::get() const
{
    if (!haveMessage_)
        throw std::runtime_error{"MessageReceiver::get: no message received"};

    auto header = reinterpret_cast<const wire::ChunkHeader *>(buf_.data());

    if (header->magic != wire::ChunkHeader::Magic)
        throw std::runtime_error("invalid chunk magic");

    if (sizeof(*header) + header->payloadLength > buf_.size())
    {
        throw std::runtime_error(
            "invalid payload length: " + std::to_string(header->payloadLength));
    }

    return header;
    return deserializeTransferRequest(
        Buffer{buf_.data() + sizeof(*header), header->payloadLength});
}

}
