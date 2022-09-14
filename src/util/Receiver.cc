/**
 * @file Receiver.cc
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

#include <poll.h>

#include <spdlog/spdlog.h>

#include <draft/util/Receiver.hh>
#include <draft/util/Stats.hh>

namespace draft::util {

Receiver::Receiver(ScopedFd fd, BufQueue &queue):
    queue_(&queue),
    svcFd_(std::move(fd))
{
    pool_ = BufferPool::make(BufSize, 35);
}

bool Receiver::runOnce(std::stop_token stopToken)
{
    if (fd_.get() < 0)
    {
        switch (waitConnect())
        {
            case -1:
                return false;
            case 0:
                return true;
            case 1:
                break;
        }
    }

    return waitData(stopToken);
}

int Receiver::waitConnect()
{
    auto pfd = pollfd{svcFd_.get(), POLLIN, 0};
    auto count = ::poll(&pfd, 1, 50);

    if (!count || !(pfd.revents & POLLIN))
        return 0;

    fd_ = util::net::accept(svcFd_.get());

    if (fd_.get() < 0)
    {
        spdlog::error("accept on fd {}: {}", svcFd_.get(), std::strerror(errno));
        return -1;
    }

    spdlog::info("accepted connection on fd {}", fd_.get());

    return 1;
}

bool Receiver::waitData(std::stop_token stopToken)
{
    using namespace std::chrono_literals;

    if (!haveHeader_)
    {
        if (auto stat = readHeader(); stat <= 0)
            return stat == EOF ? false : true;

        buf_ = pool_->get();

        haveHeader_ = true;
        offset_ = 0;
    }

    const auto readStat = read();

    if (readStat < 0)
        return false;

    if (readStat > 0)
    {
        spdlog::trace("receiver put {} -> id {}"
            , header_.payloadLength
            , header_.fileId);

        auto buf = std::make_shared<Buffer>(std::move(buf_));

        while (!stopToken.stop_requested() &&
            !queue_->put({
                buf,
                header_.fileId,
                header_.fileOffset,
                header_.payloadLength
            }, 100ms))
        {
        }

        ++stats().queuedBlockCount;

        if (auto s = stats(header_.fileId))
            ++s->queuedBlockCount;

        haveHeader_ = false;
        offset_ = 0;
    }

    return true;
}

int Receiver::readHeader()
{
    if (offset_ > sizeof(header_))
    {
        throw std::runtime_error(fmt::format(
            "receiver invalid state: reading header, offset is {}, header size is {}"
            , offset_, sizeof(header_)));
    }

    auto buf = reinterpret_cast<uint8_t *>(&header_) + offset_;
    auto sz = sizeof(header_) - offset_;

    auto len = ::read(fd_.get(), buf, sz);

    if (len < 0)
        throw std::system_error(errno, std::system_category(), "read");

    if (!len)
        return EOF;

    offset_ += static_cast<size_t>(len);

    if (offset_ < sizeof(header_))
        return 0;

    // TODO: sanity check header, trigger resync if required.
    spdlog::trace("header magic: {:x}", header_.magic);

    if (header_.magic != wire::ChunkHeader::Magic)
    {
        spdlog::error(
            "invalid header magic: {:x} - client fd {} - closing connection."
            , header_.magic
            , fd_.get());

        fd_ = ScopedFd{ };
        offset_ = 0;

        return 0;
    }

    return 1;
}

int Receiver::read()
{
    auto len = ::read(fd_.get(), buf_.uint8Data() + offset_, header_.payloadLength - offset_);

    if (len < 0)
        throw std::system_error(errno, std::system_category(), "read");

    if (!len)
        return EOF;

    stats().netByteCount += static_cast<size_t>(len);

    if (auto s = stats(header_.fileId))
        s->netByteCount += static_cast<size_t>(len);

    offset_ += static_cast<size_t>(len);

    if (offset_ >= header_.payloadLength)
        return 1;

    return 0;
}

}
