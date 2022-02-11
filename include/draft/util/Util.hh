/* @file Util.hh
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

#ifndef __DRAFT_UTIL_HH__
#define __DRAFT_UTIL_HH__

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include "Buffer.hh"
#include "BufferPool.hh"
#include "IOVec.hh"
#include "Protocol.hh"
#include "ScopedFd.hh"
#include "ScopedMMap.hh"
#include "WaitQueue.hh"

namespace draft::util {

constexpr size_t BlockSize = 4096u;

constexpr size_t roundBlockSize(size_t len) noexcept
{
    return (len + BlockSize - 1) & ~size_t{BlockSize-1};
}

inline size_t computeSegmentSize(size_t fileLen, size_t linkCount)
{
    return roundBlockSize(fileLen / linkCount);
}

////////////////////////////////////////////////////////////////////////////////
// IO

inline size_t readChunk(int fd, void *data, size_t dlen, size_t fileOffset)
{
    auto buf = reinterpret_cast<uint8_t *>(data);

    for (size_t offset = 0; offset < dlen; )
    {
        auto len = ::pread(fd, buf + offset, dlen - offset, static_cast<off_t>(fileOffset + offset));

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "pread");

        if (!len)
            return offset;

        offset += static_cast<size_t>(len);
    }

    return dlen;
}

inline size_t writeChunk(int fd, iovec *iov, size_t iovCount)
{
    auto written = size_t{ };

    while (iovCount)
    {
        const auto len = ::writev(fd, iov, iovCount);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "write");

        if (!len)
            break;

        auto ulen = static_cast<size_t>(len);

        written += ulen;

        while (ulen && iovCount)
        {
            const auto adv = std::min(iov->iov_len, ulen);
            iov->iov_base = reinterpret_cast<uint8_t *>(iov->iov_base) + adv;
            iov->iov_len -= adv;

            ulen -= adv;

            if (!iov->iov_len)
            {
                ++iov;
                --iovCount;
            }
        }
    }

    return written;
}

inline size_t writeChunk(int fd, iovec *iov, size_t iovCount, size_t offset)
{
    auto written = size_t{ };

    while (iovCount)
    {
        if (offset > static_cast<size_t>(std::numeric_limits<off_t>::max()))
        {
            throw std::range_error("writeChunk offset is out of off_t range.");
        }

        const auto len = ::pwritev(fd, iov, iovCount, static_cast<off_t>(offset));

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "write");

        if (!len)
            break;

        auto ulen = static_cast<size_t>(len);

        offset += ulen;
        written += ulen;

        while (ulen && iovCount)
        {
            const auto adv = std::min(iov->iov_len, ulen);
            iov->iov_base = reinterpret_cast<uint8_t *>(iov->iov_base) + adv;
            iov->iov_len -= adv;

            ulen -= adv;

            if (!iov->iov_len)
            {
                ++iov;
                --iovCount;
            }
        }
    }

    return written;
}

struct MessageBuffer
{
    std::shared_ptr<BufferPool::Buffer> buf;
    size_t fileOffset{ };
    size_t payloadLength{ };
    unsigned fileId{ };
};

////////////////////////////////////////////////////////////////////////////////
// Agent

struct NetworkTarget
{
    std::string ip{ };
    uint16_t port{ };
};

struct FileInfo
{
    std::string path;
    std::string targetSuffix;

    struct Status
    {
        mode_t mode{ };
        uid_t uid{ };
        gid_t gid{ };
        dev_t dev{ };
        blksize_t blkSize{ };
        blkcnt_t blkCount{ };
        size_t size{ };
    } status{ };

    uint16_t id{ };
};

struct FileAgentConfig
{
    std::vector<FileInfo> fileInfo;
    std::string root;
    size_t ringPwr{5};
    bool enableDio{ };
};

struct TransferRequest
{
    draft::util::FileAgentConfig config;
};

// --

struct BDesc
{
    std::shared_ptr<BufferPool::Buffer> buf{ };
    unsigned fileId{ };
    size_t offset{ };
    size_t len{ };
};

struct Segment
{
    size_t offset{ };
    size_t len{ };
};

struct TransferInfo
{
    std::unordered_map<unsigned, ScopedFd> fileMap;
};

struct SessionConfig
{
    std::vector<NetworkTarget> targets;
    NetworkTarget service;
    std::string pathRoot{"."};
};

struct Stats
{
    std::atomic_uint diskByteCount{ };
    std::atomic_uint queuedBlockCount{ };
    std::atomic_uint dequeuedBlockCount{ };
    std::atomic_uint netByteCount{ };
    std::atomic_uint fileByteCount{ };
};

constexpr auto BufSize = size_t{1u << 22};

using BufQueue = WaitQueue<BDesc>;
using BufferPtr = std::shared_ptr<BufferPool::Buffer>;
using FdMap = std::unordered_map<unsigned, int>;

std::vector<FileInfo> getFileInfo(const std::string &path);

draft::util::NetworkTarget parseTarget(const std::string &str);
size_t parseSize(const std::string &str);

void createTargetFiles(const std::string &root, const std::vector<FileInfo> &infos);

std::string dirname(std::string path);

std::filesystem::path rootedPath(std::filesystem::path root, std::string path, std::string suffix);

namespace net {

ScopedFd bindTun(const std::string &name);

ScopedFd bindTcp(const std::string &host, uint16_t port, unsigned backlog = 1);

ScopedFd connectTcp(const std::string &host, uint16_t port, int tmoMs = 0);

ScopedFd bindUdp(const std::string &host, uint16_t port);

ScopedFd connectUdp(const std::string &host, uint16_t port);

ScopedFd accept(int fd);

void setNonBlocking(int fd, bool on);

int udpSendQueueSize(int fd);

void writeAll(int fd, const void *data, size_t len);

void writeAll(int fd, const struct iovec *iovs, size_t iovlen);

void readAll(int fd, const void *data, size_t len);

void readAll(int fd, const struct iovec *iovs, size_t iovlen);

std::string peerName(int fd);

}

std::vector<ScopedFd> connectNetworkTargets(const std::vector<NetworkTarget> &targets);
std::vector<ScopedFd> bindNetworkTargets(const std::vector<NetworkTarget> &targets);

}

#endif
