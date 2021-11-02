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

#include <deque>
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
    using ChunkCallback = std::function<void(Receiver &, const MessageBuffer &)>;
    using SegmentCallback = std::function<void(Receiver &, const wire::ChunkHeader &)>;

    Receiver(ScopedFd fd, ChunkCallback &&cb, size_t rxBufSize = 1u << 16, size_t rxRingPwr = 5, size_t chunkAlignment = 0):
        cb_(std::move(cb)),
        fd_(std::move(fd)),
        chunkAlignment_(chunkAlignment)
    {
        pool_ = BufferPool::make(rxBufSize, 1u << rxRingPwr);
        buf_ = std::make_shared<BufferPool::Buffer>(pool_->get());
        spdlog::info("opened rx on fd {}", fd_.get());
    }

    int runOnce()
    {
        if (!chunk_)
        {
            if (auto stat = receiveSegmentHeader(); stat <= 0)
                return stat;
        }

        return receiveSegmentData();
    }

    int fd() const
    {
        return fd_.get();
    }

    void setSegmentCallback(SegmentCallback cb)
    {
        segmentCb_ = std::move(cb);
    }

private:
    struct BufferView
    {
        uint8_t *data{ };
        size_t size{ };
    };

    int receiveSegmentHeader()
    {
        auto len = ::recv(fd_.get(), buf_->uint8Data() + offset_, sizeof(wire::ChunkHeader) - offset_, 0);

        if (len < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                return -errno;

            throw std::system_error(errno, std::system_category(), "Receiver::recv");
        }

        if (!len)
            return EOF;

        if (offset_ + static_cast<size_t>(len) < sizeof(wire::ChunkHeader))
        {
            offset_ += static_cast<size_t>(len);
            return 0;
        }

        auto chunk = reinterpret_cast<const wire::ChunkHeader *>(buf_->data());

        if (!chunkValid(*chunk))
            throw std::runtime_error("rx'd invalid segment header.");

        chunk_ = *chunk;
        fileOffset_ = chunk_->fileOffset;
        offset_ = 0;

        spdlog::info("start chunk {}-{} range [{}, {})"
            , fd_.get(), chunk_->fileId
            , chunk_->fileOffset, chunk_->fileOffset + chunk_->payloadLength);

        return 1;
    }

    int receiveSegmentData()
    {
        const auto maxRx = std::min(
            buf_->size() - offset_,
            chunk_->fileOffset + chunk_->payloadLength - fileOffset_ - offset_);

        spdlog::trace("{}-{} max rx {}", fd_.get(), chunk_->fileId, maxRx);

        auto len = ::recv(fd_.get(), buf_->uint8Data() + offset_, maxRx, 0);

        if (len < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
                return -errno;

            throw std::system_error(errno, std::system_category(), "Receiver::recv");
        }

        if (!len)
        {
            finishSegment();
            return 0;
        }

        size_t alignedLen = offset_ + static_cast<size_t>(len);
        size_t diffLen{ };

        if (chunkAlignment_)
        {
            spdlog::debug("{}-{}: offset {} payload len {} - current offset {}"
                    , fd_.get()
                    , chunk_->fileId
                    , chunk_->fileOffset
                    , chunk_->payloadLength
                    , fileOffset_);

            if (alignedLen >= chunkAlignment_)
            {
                if (((offset_ + static_cast<size_t>(len)) & (chunkAlignment_ - 1)))
                {
                    const auto newOffset = offset_ + static_cast<size_t>(len);
                    alignedLen = newOffset & ~(chunkAlignment_ - 1);
                    diffLen = newOffset - alignedLen;
                }
            }
            else if (chunk_->fileOffset + chunk_->payloadLength - fileOffset_ > chunkAlignment_)
            {
                spdlog::debug("{}-{}: skipping write cycle for {} bytes, with {} remaining."
                    , fd_.get()
                    , chunk_->fileId
                    , alignedLen
                    , chunk_->fileOffset + chunk_->payloadLength - fileOffset_);

                diffLen = alignedLen;
                alignedLen = 0;
            }
        }

        spdlog::trace("{}-{}: {} bytes -> offset {}"
            , fd_.get(), chunk_->fileId, alignedLen, fileOffset_);

        if (alignedLen)
            processChunk(buf_, alignedLen);

        auto buf = std::make_shared<BufferPool::Buffer>(pool_->get());

        if (diffLen)
        {
            memcpy(buf->data(), buf_->uint8Data() + alignedLen, diffLen);
            spdlog::trace("{}-{}: have {} bytes remaining for disk."
                , fd_.get(), chunk_->fileId, diffLen);
        }

        buf_ = std::move(buf);
        offset_ = diffLen;

        return 0;
    }

    int flushSegmentData()
    {
        if (offset_)
        {
            spdlog::debug("{}-{}: flushing {} bytes bound for disk."
                , fd_.get()
                , chunk_->fileId
                , offset_);

            processChunk(buf_, util::roundBlockSize(offset_));
            offset_ = 0;
        }

        return 0;
    }

    void finishSegment()
    {
        flushSegmentData();

        fileOffset_ = 0;
        chunk_ = { };

        buf_ = std::make_shared<BufferPool::Buffer>(pool_->get());
        offset_ = 0;
    }

    bool chunkValid(const wire::ChunkHeader &header) const noexcept
    {
        return header.magic == wire::ChunkHeader::Magic;
    }

    void processChunk(const std::shared_ptr<BufferPool::Buffer> &buf, size_t len)
    {
        if (cb_)
        {
            auto msg = MessageBuffer {
                std::move(buf),
                fileOffset_,
                len,
                chunk_->fileId
            };

            cb_(*this, std::move(msg));
        }

        fileOffset_ += len;

        if (fileOffset_ >= chunk_->fileOffset + chunk_->payloadLength)
        {
            spdlog::info("{} finished receiving segment for file id {}: {}/{}"
                , fd_.get()
                , chunk_->fileId
                , fileOffset_
                , chunk_->fileOffset + chunk_->payloadLength);

            if (segmentCb_)
                segmentCb_(*this, *chunk_);

            fileOffset_ = 0;
            chunk_ = { };
        }
    }

    ChunkCallback cb_;
    std::optional<wire::ChunkHeader> chunk_;
    std::shared_ptr<BufferPool> pool_;
    std::shared_ptr<BufferPool::Buffer> buf_;
    ScopedFd fd_;
    size_t offset_{ };
    size_t fileOffset_{ };
    size_t chunkAlignment_{ };
    SegmentCallback segmentCb_;
};

class ReceiverManager
{
public:
    using ChunkCallback = std::function<void(int, const MessageBuffer &)>;

    explicit ReceiverManager(std::vector<ScopedFd> fds, size_t rxBufSize, size_t rxRingPwr):
        rxBufSize_(rxBufSize),
        rxRingPwr_(rxRingPwr)
    {
        initService(std::move(fds));
    }

    ~ReceiverManager() noexcept
    {
        cancel();
    }

    void runOnce(int tmoMs)
    {
        using namespace std::chrono_literals;

        pollSet_.waitOnce(tmoMs);

        auto rmList = std::vector<int>{ };
        for (auto &fdInfo : receivers_)
        {
            if (!fdInfo.second.result.valid())
                continue;

            if (fdInfo.second.result.wait_for(1ms) == std::future_status::ready)
            {
                rmList.push_back(fdInfo.first);
                spdlog::info("reaping rx for fd {}", fdInfo.first);
                rxReaped_ = true;
            }
        }

        for (auto fd : rmList)
            receivers_.erase(fd);
    }

    void cancel() noexcept
    {
        done_ = true;
    }

    bool haveReceivers() const noexcept
    {
        return !receivers_.empty();
    }

    bool finished() const noexcept
    {
        return rxReaped_ && !haveReceivers();
    }

    size_t chunkCount() const noexcept
    {
        return count_;
    }

    void setChunkCallback(ChunkCallback cb)
    {
        cb_ = std::move(cb);
    }

    void setSegmentCallback(Receiver::SegmentCallback cb)
    {
        segmentCb_ = std::move(cb);
    }

private:
    struct RxInfo
    {
        std::future<void> result{ };
    };

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
            rxBufSize_,
            rxRingPwr_,
            BlockSize);

        rx->setSegmentCallback(segmentCb_);

        auto rxFd = rx->fd();
        receivers_.insert({rxFd,
            RxInfo{
                std::async(
                    std::launch::async,
                    [this, rx = std::move(rx)] {
                        runReceiver(*rx);
                    })
            }});
    }

    void runReceiver(draft::util::Receiver &rx)
    {
        while (!done_ && receiveChunk(rx))
            ;

        spdlog::info("finished rx for fd {}", rx.fd());
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
            case EOF:
                spdlog::info("connection {} closed.", rx.fd());
                return false;
            default:
                break;
        }

        spdlog::error("connection {} rx error: {}"
            , rx.fd(), std::strerror(-stat));

        return true;
    }

    void handleChunk(Receiver &rx, const MessageBuffer &buf)
    {
        if (cb_)
            cb_(rx.fd(), std::move(buf));

        ++count_;
    }

    ChunkCallback cb_;
    std::vector<ScopedFd> serviceFds_;
    std::unordered_map<int, RxInfo> receivers_;
    PollSet pollSet_;
    Receiver::SegmentCallback segmentCb_;
    size_t rxBufSize_{1u << 16};
    size_t rxRingPwr_{5};
    size_t count_{ };
    bool done_{ };
    bool rxReaped_{ };
};

}

#endif
