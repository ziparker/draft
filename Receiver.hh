/**
 * @file Receiver.hh
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2021 Zachary Parker
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

#ifndef __DRAFT_RECEIVER_HH__
#define __DRAFT_RECEIVER_HH__

#include <functional>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>

#include "Protocol.hh"
#include "Util.hh"

#include <spdlog/fmt/bin_to_hex.h>

namespace draft::util {

class Receiver
{
public:
    struct MessageBuffer
    {
        ConstRawBuffer view{ };
        const wire::ChunkHeader *header{ };
        RefdPointer<BufferPool::Buffer> buf;
    };

    using ChunkCallback = std::function<void(Receiver &, const MessageBuffer &)>;

    Receiver(ScopedFd fd, ChunkCallback &&cb, size_t rxBufSize = 1u << 16):
        cb_(std::move(cb)),
        fd_(std::move(fd))
    {
        pool_ = BufferPool{rxBufSize, 1u << 12};
        buf_ = make_refd<BufferPool::Buffer>(pool_.get());
        spdlog::info("first buf {}", buf_->data());
    }

    int runOnce()
    {
        auto len = ::recv(fd_.get(), buf_->uint8Data() + offset_, buf_->size() - offset_, 0);

        if (len < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                return -errno;

            throw std::system_error(errno, std::system_category(), "Receiver::recv");
        }

        if (!len)
            return -EOF;

        spdlog::trace("handle {} bytes @ offset {}", len, offset_);
        handleData({buf_->uint8Data(), offset_ + static_cast<size_t>(len)});

        return 0;
    }

    int fd() const
    {
        return fd_.get();
    }

private:
    struct BufferView
    {
        uint8_t *data{ };
        size_t size{ };
    };

    void handleData(BufferView view)
    {
        auto [offset, found] = findChunk(view);

        if (!found)
        {
            auto buf = pool_.get();

            std::memcpy(buf.data(), view.data + offset, view.size - offset);
            buf_ = make_refd<BufferPool::Buffer>(std::move(buf));

            offset_ = view.size - offset;
            spdlog::warn("no header found, offset set to {}", offset_);
            return;
        }

        if (offset)
            spdlog::warn("Receiver: found chunk magic at offset {}", offset);

        view.data += offset;
        view.size -= offset;

        while (view.size > sizeof(wire::ChunkHeader))
        {
            const auto size = view.size;

            spdlog::trace("process offset {} len {}"
                , reinterpret_cast<uintptr_t>(view.data) - reinterpret_cast<uintptr_t>(buf_->data())
                , view.size);

            view = processChunk(view);
            spdlog::trace("  -> {}", view.size);

            if (size == view.size)
            {
                auto header = reinterpret_cast<const wire::ChunkHeader *>(view.data);

                spdlog::trace("incomplete frame for {} - waiting for {}/{}"
                    , header->fileId
                    , sizeof(*header) + header->payloadLength - view.size
                    , sizeof(*header) + header->payloadLength);

                break;
            }
        }

        if (view.size)
        {
            auto buf = pool_.get();

            std::memcpy(buf.data(), view.data, view.size);
            buf_ = make_refd<BufferPool::Buffer>(std::move(buf));
        }
        else
        {
            buf_ = make_refd<BufferPool::Buffer>(pool_.get());
        }

        offset_ = view.size;
    }

    std::pair<size_t, bool> findChunk(const BufferView &view)
    {
        size_t offset = 0;

        for ( ; view.size - offset >= 8; ++offset)
        {
            auto magic = reinterpret_cast<const uint64_t *>(view.data + offset);

            if ((*magic & wire::ChunkHeader::MagicMask) == wire::ChunkHeader::Magic)
                return {offset, true};
        }

        return {offset, false};
    }

    bool chunkValid(const wire::ChunkHeader &header) const noexcept
    {
        return header.magic == wire::ChunkHeader::Magic;
    }

    BufferView processChunk(BufferView view)
    {
        auto chunk = reinterpret_cast<const wire::ChunkHeader *>(view.data);

        if (!chunkValid(*chunk))
        {
            spdlog::warn("invalid chunk at {} buf offset {} ({:#x})"
                , static_cast<const void *>(view.data)
                , reinterpret_cast<uintptr_t>(view.data) - reinterpret_cast<uintptr_t>(buf_->data())
                , reinterpret_cast<uintptr_t>(view.data) - reinterpret_cast<uintptr_t>(buf_->data()));

            #if 1
            *view.data = 0xff;
            spdlog::warn(" -> buf data: {}", spdlog::to_hex(buf_->uint8Data(), buf_->uint8Data() + buf_->size()));

            std::exit(42);
            #else
            *view.data = 0xff;
            spdlog::warn(" -> buf data: {}", spdlog::to_hex(view.data, view.data + view.size));
            #endif

            ++view.data;
            --view.size;
            return view;
        }

        auto chunkLen = sizeof(*chunk) + chunk->payloadLength;

        spdlog::trace("  chunk len: {}", chunkLen);
        spdlog::trace("  view len: {}", view.size);

        if (chunkLen > view.size)
            return view;

        view.data += chunkLen;
        view.size -= chunkLen;

        if (cb_)
        {
            auto buf = MessageBuffer {
                {reinterpret_cast<const uint8_t *>(chunk), chunkLen},
                chunk,
                buf_
            };

            cb_(*this, buf);
        }

        return view;
    }

    ChunkCallback cb_;
    BufferPool pool_;
    RefdPointer<BufferPool::Buffer> buf_;
    ScopedFd fd_;
    size_t offset_{ };
};

class ReceiverManager
{
public:
    using ChunkCallback = std::function<void(const Receiver::MessageBuffer &)>;

    explicit ReceiverManager(std::vector<ScopedFd> fds, size_t rxBufSize):
        rxBufSize_(rxBufSize)
    {
        initService(std::move(fds));
    }

    void runOnce(int tmoMs)
    {
        pollSet_.waitOnce(tmoMs);
    }

    bool haveReceivers() const noexcept
    {
        return !receivers_.empty();
    }

    size_t chunkCount() const noexcept
    {
        return count_;
    }

    void setChunkCallback(ChunkCallback cb)
    {
        cb_ = std::move(cb);
    }

private:
    void initService(std::vector<ScopedFd> serviceFds)
    {
        for (const auto &serviceFd : serviceFds)
        {
            pollSet_.add(serviceFd.get(), EPOLLIN,
                [this, fd = serviceFd.get()](unsigned) {
                    handleService(fd);
                    return true;
                });
        }

        serviceFds_ = std::move(serviceFds);
    }

    void handleService(int fd)
    {
        auto cl = net::accept(fd);

        auto rx = std::make_unique<draft::util::Receiver>(
            std::move(cl),
            [this](auto &rx, const auto &header) {
                handleChunk(rx, header);
            },
            rxBufSize_);

        pollSet_.add(rx->fd(), EPOLLIN,
            [this, rxp = rx.get()](unsigned) {
                return receiveChunk(*rxp);
            });

        auto rxFd = rx->fd();
        receivers_.insert({rxFd, std::move(rx)});
    }

    bool receiveChunk(Receiver &rx)
    {
        auto stat = rx.runOnce();

        switch (stat)
        {
            case 0:
                [[fallthrough]];
            case -EAGAIN:
                [[fallthrough]];
            case -EINTR:
                return true;
            case -EOF:
                spdlog::info("connection {} closed.", rx.fd());
                receivers_.erase(rx.fd());
                return false;
            default:
                break;
        }

        spdlog::error("connection {} rx error: {}"
            , rx.fd(), std::strerror(-stat));

        return true;
    }

    void handleChunk(Receiver &rx, const Receiver::MessageBuffer &buf)
    {
        spdlog::trace("connection {} cb magic: {} {}"
            , rx.fd(), buf.header->fileId, buf.header->magic);

        if (cb_)
            cb_(std::move(buf));

        ++count_;
    }

    ChunkCallback cb_;
    std::vector<ScopedFd> serviceFds_;
    std::unordered_map<int, std::unique_ptr<Receiver>> receivers_;
    PollSet pollSet_;
    size_t rxBufSize_{1u << 16};
    size_t count_{ };
};

}

#endif
