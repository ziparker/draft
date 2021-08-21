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
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <liburing.h>
#include <spdlog/spdlog.h>

#include "Protocol.hh"

namespace draft::util {

////////////////////////////////////////////////////////////////////////////////
// Buffer

using Buffer = std::vector<uint8_t>;

////////////////////////////////////////////////////////////////////////////////
// IOVec

/**
 * An iovec wrapper which allocates a fixed amount of space in-line for iovecs,
 * and uses heap memory for longer vectors.
 */
template <size_t N>
struct IOVec_
{
public:
    /**
     * Allocate an iovec array of the specified length.
     *
     * @param len The length of iovec array to create.
     */
    explicit IOVec_(size_t len)
    {
        struct iovec *dst = nullptr;

        if (len <= N)
        {
            dst = iovs_.stack;
        }
        else
        {
            iovs_.heap = std::make_unique<struct iovec[]>(len);
            dst = iovs_.heap.get();
        }

        std::memset(dst, 0, len * sizeof(struct iovec));
        count_ = len;
    }

    /**
     * Make a copy of the specified iovec array.
     *
     * @param iov The iovec array to copy.
     * @param len The length of the specified iovec array.
     */
    IOVec_(const struct iovec *iov, size_t len):
        IOVec_(len)
    {
        std::copy_n(iov, len, len <= N ? iovs_.stack : iovs_.heap.get());
        count_ = len;
    }

    /**
     * Destroy the underlying iovec array, if necessary.
     *
     * This does not free any memory pointed to by the elements of the iovec
     * array.
     */
    ~IOVec_() noexcept
    {
        using std::unique_ptr;

        if (count_ > N)
            iovs_.heap.~unique_ptr<struct iovec[]>();
    }

    /**
     * Get a pointer to the managed iovec array.
     *
     * @return A pointer to the sart of the underlying iovec array.
     */
    struct iovec *get()
    {
        if (count_ <= N)
            return iovs_.stack;

        return iovs_.heap.get();
    }

    /**
     * Get a pointer to the managed iovec array.
     *
     * @return A pointer to the sart of the underlying iovec array.
     */
    const struct iovec *get() const
    {
        if (count_ <= N)
            return iovs_.stack;

        return iovs_.heap;
    }

    size_t count() const noexcept
    {
        return count_;
    }

private:
    union Data {
        struct iovec stack[N];
        std::unique_ptr<struct iovec[]> heap;

        ~Data() noexcept { }
    };

    Data iovs_ { };
    size_t count_ { };
};

/**
 * Name the default IOVec_ type, with default length.
 */
using IOVec = IOVec_<10>;

////////////////////////////////////////////////////////////////////////////////
// ScopedFd

class ScopedFd
{
public:
    ScopedFd() = default;

    explicit ScopedFd(int fd) noexcept:
        fd_{fd}
    {
    }

    ~ScopedFd() noexcept
    {
        close();
    }

    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;

    ScopedFd(ScopedFd &&o) noexcept
    {
        *this = std::move(o);
    }

    ScopedFd &operator=(ScopedFd &&o) noexcept
    {
        close();
        std::swap(o.fd_, fd_);
        return *this;
    }

    int close() noexcept
    {
        auto stat = 0;

        if (fd_ >= 0)
            stat = ::close(fd_);

        fd_ = -1;

        return stat;
    }

    int get() const noexcept
    {
        return fd_;
    }

    int release() noexcept
    {
        auto fd = fd_;

        fd_ = -1;

        return fd;
    }

    explicit operator int() const noexcept
    {
        return fd_;
    }

private:
    int fd_{-1};
};

////////////////////////////////////////////////////////////////////////////////
// ScopedPipe

class ScopedPipe
{
public:
    ScopedPipe()
    {
        if (::pipe(fds_))
            throw std::system_error(errno, std::system_category(), "draft::ScopedPipe");
    }

    ~ScopedPipe() noexcept
    {
        close();
    }

    ScopedPipe(const ScopedPipe &) = delete;
    ScopedPipe &operator=(const ScopedPipe &) = delete;

    ScopedPipe(ScopedPipe &&o) noexcept
    {
        *this = std::move(o);
    }

    ScopedPipe &operator=(ScopedPipe &&o) noexcept
    {
        close();
        std::swap(o.fds_[0], fds_[0]);
        std::swap(o.fds_[1], fds_[1]);
        return *this;
    }

    int close() noexcept
    {
        auto stat = ::close(fds_[0]);

        if (auto stat2 = ::close(fds_[1]); stat2 && !stat)
            stat = stat2;

        fds_[0] = fds_[1] = -1;

        return stat;
    }

    int readFd() const noexcept
    {
        return fds_[0];
    }

    int writeFd() const noexcept
    {
        return fds_[1];
    }

private:
    int fds_[2] {-1, -1};
};

////////////////////////////////////////////////////////////////////////////////
// ScopedMMap

class ScopedMMap
{
public:
    static ScopedMMap map(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
    {
        auto p = ::mmap(addr, len, prot, flags, fildes, off);

        if (p == MAP_FAILED)
            throw std::system_error(errno, std::system_category(), "draft::ScopedMMap");

        return {p, len};
    }

    ScopedMMap() = default;

    ~ScopedMMap() noexcept
    {
        unmap();
    }

    ScopedMMap(const ScopedMMap &) = delete;
    ScopedMMap &operator=(const ScopedMMap &) = delete;

    ScopedMMap(ScopedMMap &&o) noexcept
    {
        *this = std::move(o);
    }

    ScopedMMap &operator=(ScopedMMap &&o) noexcept
    {
        using std::swap;

        unmap();

        swap(o.addr_, addr_);
        swap(o.len_, len_);

        return *this;
    }

    int unmap() noexcept
    {
        auto stat = 0;

        if (addr_)
            stat = ::munmap(addr_, len_);

        addr_ = nullptr;
        len_ = 0;

        return stat;
    }

    void *data(size_t offset = 0) const noexcept
    {
        return uint8Data(offset);
    }

    uint8_t *uint8Data(size_t offset = 0) const noexcept
    {
        return reinterpret_cast<uint8_t *>(addr_) + offset;
    }

    size_t size() const noexcept
    {
        return len_;
    }

    bool offsetValid(size_t offset) const noexcept
    {
        return addr_ && offset < len_;
    }

private:
    ScopedMMap(void *addr, size_t len) noexcept:
        addr_(addr),
        len_(len)
    {
    }

    void *addr_{ };
    size_t len_{ };
};

////////////////////////////////////////////////////////////////////////////////
// CqueWrapper

class CqeWrapper final
{
public:
    CqeWrapper() = default;

    CqeWrapper(struct io_uring &ring, struct io_uring_cqe *cqe) noexcept:
        ring_(&ring),
        cqe_(cqe)
    {
    }

    CqeWrapper(const CqeWrapper &) = delete;
    CqeWrapper &operator=(const CqeWrapper &) = delete;

    CqeWrapper(CqeWrapper &&cqe)
    {
        *this = std::move(cqe);
    }

    CqeWrapper &operator=(CqeWrapper &&o)
    {
        clear();

        ring_ = o.ring_;
        cqe_ = o.cqe_;
        o.ring_ = nullptr;
        o.cqe_ = nullptr;

        return *this;
    }

    ~CqeWrapper() noexcept
    {
        clear();
    }

    struct io_uring_cqe *get()
    {
        return cqe_;
    }

    const struct io_uring_cqe *get() const
    {
        return cqe_;
    }

    void clear() noexcept
    {
        if (cqe_)
        {
            io_uring_cqe_seen(ring_, cqe_);
            cqe_ = nullptr;
            ring_ = nullptr;
        }
    }

private:
    struct io_uring *ring_{ };
    struct io_uring_cqe *cqe_{ };
};

////////////////////////////////////////////////////////////////////////////////
// PollSet

class PollSet
{
public:
    using Callback = std::function<bool(unsigned)>;
    using EventCallback = std::function<void(const std::vector<epoll_event> &)>;

    PollSet();

    int add(int fd, unsigned events, Callback &&cb = [](unsigned){ return true; });
    int remove(int fd);
    int waitOnce(int tmoMs);
    int waitOnce(int tmoMs, const EventCallback &cb);

    bool empty() const noexcept
    {
        return members_.empty();
    }

private:
    struct Member
    {
        Callback callback;
    };

    ScopedFd epollFd_{ };
    std::unordered_map<int, Member> members_{ };
};

////////////////////////////////////////////////////////////////////////////////
// Agent

constexpr size_t mtuPayload(unsigned mtu) noexcept
{
    return mtu - 48 - sizeof(wire::ChunkHeader);
}

struct NetworkTarget
{
    std::string ip{ };
    uint16_t port{ };
};

struct AgentConfig
{
    std::vector<NetworkTarget> targets;
    unsigned mtu{9000};
};

class MmapAgent
{
public:
    explicit MmapAgent(AgentConfig conf):
        mtu_(conf.mtu)
    {
        initUring(12);
        initNetwork(conf);
    }

    void cancel()
    {
        done_ = true;
    }

    int transferFile(const std::string &path, unsigned fileId)
    {
        namespace fs = std::filesystem;

        auto fd = ScopedFd{openFile(path)};
        const auto fileLen = fs::file_size(path);

        const auto payloadLen = mtuPayload(mtu_);

        // TODO: - map_populate option for smallish files, huge page usage?
        auto mmap = std::make_shared<ScopedMMap>(
            ScopedMMap::map(nullptr, fileLen, PROT_READ, MAP_PRIVATE, fd.get(), 0));

        if (madvise(mmap->data(), fileLen, MADV_SEQUENTIAL))
        {
            spdlog::warn("transferFile: unable to madvise on mapped file '{}': {}"
                , path
                , std::strerror(errno));
        }

        spdlog::info("file len: {} ({} - {})"
            , fileLen
            , mmap->data()
            , mmap->data(fileLen));

        done_ = false;

        size_t chunkCount{ };
        size_t chunkXfer{ };

        for (size_t fileOffset = 0; !done_ && fileOffset < fileLen; )
        {
            auto startCount = sqeCount_;

            while (!done_ && sqeCount_ < ringDepth_ && fileOffset < fileLen)
            {
                const auto xferPayloadLen = std::min(fileLen - fileOffset, payloadLen);

                auto xfer = initXfer(mmap, fileOffset, xferPayloadLen, fileId);

                xfer->fd = selectSocket();
                if (xfer->fd < 0)
                {
                    spdlog::error("no descriptors are selectable for writing - waiting.");
                    continue;
                }

                spdlog::trace("offset: {} {} {} {}"
                    , fileOffset, xferPayloadLen, fileLen, sqeCount_);

                fileOffset += xferPayloadLen;

                writeChunk(std::move(xfer));
                if (useUring_)
                    ++sqeCount_;

                ++chunkCount;
                chunkXfer += sizeof(wire::ChunkHeader) + xferPayloadLen;
            }

            if (!useUring_)
                continue;

            if (startCount != sqeCount_)
            {
                if (io_uring_submit(&ring_) < 0)
                    throw std::system_error(errno, std::system_category(), "io_uring_submit");
            }

            handleCqes();
        }

        auto count = sqeCount_;

        if (useUring_)
            handleCqes(sqeCount_);

        spdlog::info("drain {} -> {}", count, sqeCount_);
        spdlog::info("xferred {} chunk(s), {} bytes", chunkCount, chunkXfer);

        return 0;
    }

private:
    using SharedMMap = std::shared_ptr<ScopedMMap>;

    struct TransferState
    {
        wire::ChunkHeader header{ };

        SharedMMap fileMap{ };
        iovec iovs[2]{ };

        size_t fileOffset{ };
        size_t xferLen{ };
        size_t payloadLen{ };
        unsigned iovIndex{ };
        int fd{-1};
    };

    struct FdInfo
    {
        ScopedFd fd{ };
        unsigned speed{1000};
    };

    void initUring(unsigned lenPwr);

    void initNetwork(const AgentConfig &conf);

    int selectSocket()
    {
        if (useUring_)
        {
            if (++fdIter_ == end(fdMap_))
                fdIter_ = begin(fdMap_);

            return fdIter_->first;
        }

        int selectedFd{-1};

        const auto selector = [this, &selectedFd](const std::vector<epoll_event> &evts) {
                unsigned fastestSpeed = 0;

                for (const auto &evt : evts)
                {
                    auto fd = evt.data.fd;
                    auto iter = fdMap_.find(fd);

                    if (iter == end(fdMap_))
                        continue;

                    const auto &info = iter->second;

                    if (info.speed > fastestSpeed)
                        selectedFd = fd;
                }

                return true;
            };

        // wait for write readiness.
        while (!done_ && !poll_.waitOnce(100, selector))
            ;

        return selectedFd;
    }

    ScopedFd openFile(const std::string &path)
    {
        return ScopedFd{::open(path.c_str(), O_RDONLY)};
    }

    std::unique_ptr<TransferState> initXfer(const std::shared_ptr<ScopedMMap> &mmap, size_t offset, size_t len, uint16_t fileId)
    {
        auto xfer = std::make_unique<TransferState>();
        xfer->fileMap = mmap;
        xfer->fileOffset = offset;
        xfer->xferLen = sizeof(wire::ChunkHeader) + len;
        xfer->payloadLen = len;

        xfer->iovs[0].iov_base = &xfer->header;
        xfer->iovs[0].iov_len = sizeof(xfer->header);

        xfer->iovs[1].iov_base = xfer->fileMap->uint8Data(offset);
        xfer->iovs[1].iov_len = len;

        xfer->header = {
            wire::ChunkHeader::Magic,
            offset,
            len,
            fileId,
            { }
        };

        return xfer;
    }

    int writeChunk(std::unique_ptr<TransferState> xfer)
    {
        if (!useUring_)
        {
            if (auto stat = syncWrite(std::move(xfer)))
                spdlog::error("sync write: {}", std::strerror(-stat));
        }
        else
        {
            if (enqueueNetWrite(std::move(xfer)))
                spdlog::error("enq write: {}", std::strerror(errno));
        }

        return 0;
    }

    int syncWrite(std::unique_ptr<TransferState> xfer)
    {
        int stat{ };
        const auto xferLen = xfer->xferLen;

        for (size_t wlen = 0; wlen < xferLen; )
        {
            auto len = ::writev(
                xfer->fd,
                xfer->iovs + xfer->iovIndex,
                2 - static_cast<int>(xfer->iovIndex));

            if (len < 0)
            {
                stat = -errno;
                spdlog::error("socket write: {}", std::strerror(-stat));
                break;
            }

            if (!len)
            {
                spdlog::error("zero-length write - ending transfer.");
                stat = -EOF;
                break;
            }

            wlen += static_cast<size_t>(len);

            if (wlen < xferLen)
            {
                spdlog::info("advancing for short write: {}/{}"
                    , len
                    , wlen);

                advanceXfer(*xfer, static_cast<size_t>(len));
            }
        }

        return stat;
    }

    void handleCqes(size_t required = 1)
    {
        const auto hadRequired = !!required;

        while (true)
        {
            io_uring_cqe *cqe{ };

            if (required)
            {
                if (auto stat = io_uring_wait_cqe(&ring_, &cqe); stat < 0)
                    throw std::system_error(-stat, std::system_category(), "io_uring_wait_cqe");

                --required;
            }
            else
            {
                auto stat = io_uring_peek_cqe(&ring_, &cqe);

                if (stat == -EAGAIN)
                    break;
            }

            if (!cqe)
            {
                if (required)
                    spdlog::warn("no cqe found, but we still require {}", required);

                return;
            }

            auto wrappedCqe = CqeWrapper{ring_, cqe};

            auto xfer = std::unique_ptr<TransferState>(
                reinterpret_cast<TransferState *>(io_uring_cqe_get_data(cqe)));

            if (!xfer)
                throw std::runtime_error("null data found on reaped cqe.");

            if (cqe->res < 0)
            {
                if (cqe->res == -EAGAIN)
                {
                    if (auto stat = enqueueNetWritePrepped(std::move(xfer)))
                    {
                        throw std::runtime_error(fmt::format(
                            "unable to handle eagain prepped write: {}."
                            , std::strerror(-stat)));
                    }

                    continue;
                }

                if (cqe->res == -ECANCELED)
                {
                    spdlog::info("re-queue canceled write for {} (len: {})."
                        , xfer->header.fileId
                        , xfer->xferLen);

                    if (auto stat = enqueueNetWritePrepped(std::move(xfer)))
                    {
                        throw std::runtime_error(fmt::format(
                            "unable to handle prepped write following canceled write: {}."
                            , std::strerror(-stat)));
                    }

                    if (hadRequired)
                        ++required;

                    if (io_uring_submit(&ring_) < 0)
                        throw std::system_error(errno, std::system_category(), "io_uring_submit");

                    continue;
                }

                throw std::system_error(-cqe->res, std::system_category(),
                    fmt::format("cqe failed for offset {} (addr {}): cqe status {}"
                        , xfer->fileOffset
                        , xfer->iovs[1].iov_base
                        , cqe->res));
            }

            if (cqe->res > 0 &&
                static_cast<size_t>(cqe->res) != xfer->xferLen)
            {
                spdlog::info("re-queue short write for {}: ({}/{})."
                    , xfer->header.fileId
                    , cqe->res
                    , xfer->xferLen);

                advanceXfer(*xfer, static_cast<size_t>(cqe->res));

                spdlog::info(" -> re-queued write for {}"
                    , xfer->xferLen);

                if (auto stat = enqueueNetWritePrepped(std::move(xfer)))
                {
                    throw std::runtime_error(fmt::format(
                        "unable to handle prepped write following short write: {}."
                        , std::strerror(-stat)));
                }

                if (hadRequired)
                    ++required;

                if (io_uring_submit(&ring_) < 0)
                    throw std::system_error(errno, std::system_category(), "io_uring_submit");

                continue;
            }

            --sqeCount_;
        }
    }

    struct io_uring_sqe *getSqe()
    {
        auto sqe = io_uring_get_sqe(&ring_);

        if (!sqe)
            throw std::runtime_error("unable to get sqe");

        return sqe;
    }

    int enqueueNetWrite(std::unique_ptr<TransferState> xfer)
    {
        if (xfer->fileOffset > std::numeric_limits<off_t>::max())
        {
            throw std::out_of_range(fmt::format(
                "transfer file offset: {}", xfer->fileOffset));
        }

        auto sqe = getSqe();

        if (!sqe)
        {
            spdlog::info("unable to fetch sqe.");
            return -EBUSY;
        }

        io_uring_prep_writev(
            sqe,
            xfer->fd,
            xfer->iovs,
            2,
            0);

        spdlog::debug("enqueue net write: {} {}",
            xfer->fileOffset, xfer->xferLen);

        io_uring_sqe_set_data(sqe, xfer.release());
        io_uring_sqe_set_flags(sqe, IOSQE_IO_LINK);

        return 0;
    }

    int enqueueNetWritePrepped(std::unique_ptr<TransferState> xfer)
    {
        auto sqe = getSqe();

        if (!sqe)
        {
            spdlog::info("unable to fetch sqe.");
            return -EBUSY;
        }

        io_uring_prep_writev(
            sqe,
            xfer->fd,
            xfer->iovs + xfer->iovIndex,
            2 - xfer->iovIndex,
            0);

        spdlog::debug("enqueue prepped net write: {} {}/{}",
            xfer->fileOffset, xfer->xferLen, sizeof(wire::ChunkHeader) + xfer->payloadLen);

        io_uring_sqe_set_data(sqe, xfer.release());
        io_uring_sqe_set_flags(sqe, IOSQE_IO_LINK);

        return 0;
    }

    void advanceXfer(TransferState &xfer, size_t len)
    {
        if (len > xfer.xferLen)
        {
            throw std::runtime_error(fmt::format(
                "advancing xfer > xfer len ({} > {})"
                , len
                , xfer.xferLen));
        }

        xfer.xferLen -= len;

        for (size_t i = xfer.iovIndex; i < 2; ++i)
        {
            xfer.iovs[i].iov_base = reinterpret_cast<uint8_t *>(xfer.iovs[i].iov_base) + len;
            xfer.iovs[i].iov_len -= std::min(len, xfer.iovs[i].iov_len);

            if (!xfer.iovs[i].iov_len)
            {
                xfer.iovs[i].iov_base = nullptr;
                xfer.iovIndex = i + 1;
            }
        }
    }

    PollSet poll_{ };
    struct io_uring ring_{ };
    std::unordered_map<int, FdInfo> fdMap_{ };
    std::unordered_map<int, FdInfo>::iterator fdIter_{ };
    size_t sqeCount_{ };
    size_t ringDepth_{ };
    unsigned mtu_{9000};
    bool done_{ };
    bool useUring_{ };
};

////////////////////////////////////////////////////////////////////////////////
// FileAgent

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
};

struct TransferRequest
{
    draft::util::FileAgentConfig config;
};

class FileAgent
{
public:
    explicit FileAgent(FileAgentConfig conf)
    {
        initUring(12);
        initFileState(std::move(conf));
    }

    void cancel()
    {
        done_ = true;
    }

    int updateFile(unsigned fileId, size_t offset, Buffer buf)
    {
        auto xfer = initWrite(fileId, offset, std::move(buf));

        if (!xfer)
        {
            spdlog::error("unable to find file by id: {}", fileId);
            return -EEXIST;
        }

        writeChunk(std::move(xfer));

        return 0;
    }

    void drain()
    {
        auto count = sqeCount_;

        handleCqes(sqeCount_);

        spdlog::info("drain {} -> {}", count, sqeCount_);
    }

private:
    struct IOState
    {
        Buffer buf{ };
        iovec iov{ };

        size_t fileOffset{ };
        size_t len{ };
        unsigned fileId{ };
        int fd{-1};
    };

    struct FileState
    {
        FileInfo info;
        ScopedFd fd;
    };

    using FileStateMap = std::unordered_map<unsigned, FileState>;

    void initUring(unsigned lenPwr);

    void initFileState(FileAgentConfig conf);

    std::unique_ptr<IOState> initWrite(unsigned fileId, size_t offset, Buffer buf)
    {
        auto iter = fileMap_.find(fileId);
        if (iter == end(fileMap_))
            return nullptr;

        auto xfer = std::make_unique<IOState>();
        xfer->buf = std::move(buf);
        xfer->fileOffset = offset;
        xfer->len = xfer->buf.size();
        xfer->fileId = fileId;
        xfer->fd = iter->second.fd.get();

        xfer->iov.iov_base = xfer->buf.data();
        xfer->iov.iov_len = xfer->buf.size();

        return xfer;
    }

    int writeChunk(std::unique_ptr<IOState> xfer)
    {
        if (0)
        {
            if (auto stat = syncWrite(std::move(xfer)))
                spdlog::error("sync write: {}", std::strerror(-stat));
        }
        else
        {
            if (xferStarted_)
                handleCqes();

            xferStarted_ = true;

            if (enqueueWrite(std::move(xfer)))
                spdlog::error("enq write: {}", std::strerror(errno));

            ++sqeCount_;

            if (io_uring_submit(&ring_) < 0)
                throw std::system_error(errno, std::system_category(), "io_uring_submit");
        }

        return 0;
    }

    int syncWrite(std::unique_ptr<IOState> xfer)
    {
        int stat{ };
        const auto xferLen = xfer->len;

        for (size_t wlen = 0; wlen < xferLen; )
        {
            auto len = ::pwritev(
                xfer->fd,
                &xfer->iov,
                1,
                static_cast<off_t>(xfer->fileOffset));

            if (len < 0)
            {
                stat = -errno;
                spdlog::error("file id {} write: {}", xfer->fileId, std::strerror(-stat));
                break;
            }

            if (!len)
            {
                spdlog::error("zero-length write - ending transfer.");
                stat = -EOF;
                break;
            }

            wlen += static_cast<size_t>(len);

            if (wlen < xferLen)
            {
                spdlog::info("advancing for short write: {}/{}"
                    , wlen
                    , xferLen);

                advanceXfer(*xfer, static_cast<size_t>(len));
            }
        }

        return stat;
    }

    void handleCqes(size_t required = 1)
    {
        const auto hadRequired = !!required;

        while (true)
        {
            io_uring_cqe *cqe{ };

            if (required)
            {
                if (auto stat = io_uring_wait_cqe(&ring_, &cqe); stat < 0)
                    throw std::system_error(-stat, std::system_category(), "io_uring_wait_cqe");

                --required;
            }
            else
            {
                auto stat = io_uring_peek_cqe(&ring_, &cqe);

                if (stat == -EAGAIN)
                    break;
            }

            if (!cqe)
            {
                if (required)
                    spdlog::warn("no cqe found, but we still require {}", required);

                return;
            }

            auto wrappedCqe = CqeWrapper{ring_, cqe};

            auto xfer = std::unique_ptr<IOState>(
                reinterpret_cast<IOState *>(io_uring_cqe_get_data(cqe)));

            if (!xfer)
                throw std::runtime_error("null data found on reaped cqe.");

            if (cqe->res < 0)
            {
                if (cqe->res == -EAGAIN)
                {
                    if (auto stat = enqueueWritePrepped(std::move(xfer)))
                    {
                        throw std::runtime_error(fmt::format(
                            "unable to handle eagain prepped write: {}."
                            , std::strerror(-stat)));
                    }

                    continue;
                }

                throw std::system_error(-cqe->res, std::system_category(),
                    fmt::format("cqe failed for offset {} (addr {})"
                        , xfer->fileOffset
                        , xfer->iov.iov_base));
            }

            if (cqe->res > 0 &&
                static_cast<size_t>(cqe->res) != xfer->len)
            {
                spdlog::info("re-queue short write for {}: ({}/{})."
                    , xfer->fileId
                    , cqe->res
                    , xfer->len);

                advanceXfer(*xfer, static_cast<size_t>(cqe->res));

                spdlog::info(" -> re-queued write for {}"
                    , xfer->len);

                if (auto stat = enqueueWritePrepped(std::move(xfer)))
                {
                    throw std::runtime_error(fmt::format(
                        "unable to handle prepped write following short write: {}."
                        , std::strerror(-stat)));
                }

                if (hadRequired)
                    ++required;

                if (io_uring_submit(&ring_) < 0)
                    throw std::system_error(errno, std::system_category(), "io_uring_submit");

                continue;
            }

            --sqeCount_;
        }
    }

    struct io_uring_sqe *getSqe()
    {
        auto sqe = io_uring_get_sqe(&ring_);

        if (!sqe)
            throw std::runtime_error("unable to get sqe");

        return sqe;
    }

    int enqueueWrite(std::unique_ptr<IOState> xfer)
    {
        if (xfer->fileOffset > std::numeric_limits<off_t>::max())
        {
            throw std::out_of_range(fmt::format(
                "transfer file offset: {}", xfer->fileOffset));
        }

        auto sqe = getSqe();

        if (!sqe)
        {
            spdlog::info("unable to fetch sqe.");
            return -EBUSY;
        }

        io_uring_prep_writev(
            sqe,
            xfer->fd,
            &xfer->iov,
            1,
            static_cast<off_t>(xfer->fileOffset));

        spdlog::debug("enqueue file write: {} {}",
            xfer->fileOffset, xfer->len);

        io_uring_sqe_set_data(sqe, xfer.release());

        return 0;
    }

    int enqueueWritePrepped(std::unique_ptr<IOState> xfer)
    {
        auto sqe = getSqe();

        if (!sqe)
        {
            spdlog::info("unable to fetch sqe.");
            return -EBUSY;
        }

        io_uring_prep_writev(
            sqe,
            xfer->fd,
            &xfer->iov,
            1,
            static_cast<off_t>(xfer->fileOffset));

        spdlog::debug("enqueue prepped file write: {} {}/{}",
            xfer->fileOffset, xfer->len, xfer->buf.size());

        io_uring_sqe_set_data(sqe, xfer.release());

        return 0;
    }

    void advanceXfer(IOState &xfer, size_t len)
    {
        if (len > xfer.len)
        {
            throw std::runtime_error(fmt::format(
                "advancing xfer > xfer len ({} > {})"
                , len
                , xfer.len));
        }

        xfer.len -= len;
        xfer.fileOffset += len;

        xfer.iov.iov_base = reinterpret_cast<uint8_t *>(xfer.iov.iov_base) + len;
        xfer.iov.iov_len -= std::min(len, xfer.iov.iov_len);
    }

    struct io_uring ring_{ };
    FileStateMap fileMap_{ };
    size_t sqeCount_{ };
    size_t ringDepth_{ };
    bool done_{ };
    bool xferStarted_{ };
};

std::vector<FileInfo> getFileInfo(const std::string &path);

draft::util::NetworkTarget parseTarget(const std::string &str);

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

}

}

#endif
