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
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <future>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

#include <set>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>

#include "Protocol.hh"

namespace draft::util {

constexpr size_t BlockSize = 4096u;

constexpr size_t roundBlockSize(size_t len) noexcept
{
    return (len + BlockSize - 1) & ~size_t{BlockSize-1};
};

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

        written += ulen;
    }

    return written;
}

////////////////////////////////////////////////////////////////////////////////
// Buffer

class Buffer
{
public:
    Buffer() = default;

    explicit Buffer(size_t size):
        data_(std::malloc(size)),
        size_(size)
    {
        if (!data_)
            throw std::bad_alloc();
    }

    Buffer(const std::vector<uint8_t> &vec):
        Buffer(vec.data(), vec.size())
    {
    }

    explicit Buffer(const void *data, size_t size):
        data_(std::malloc(size)),
        size_(size)
    {
        if (!data_)
            throw std::bad_alloc();

        std::memcpy(data_, data, size_);
    }

    Buffer(const Buffer &o)
    {
        *this = o;
    }

    Buffer &operator=(const Buffer &o)
    {
        if (this == &o)
            return *this;

        resize(o.size_);

        std::memcpy(data_, o.data_, size_);

        return *this;
    }

    Buffer(Buffer &&o)
    {
        *this = o;
    }

    Buffer &operator=(Buffer &&o)
    {
        if (this == &o)
            return *this;

        std::free(data_);

        data_ = o.data_;
        size_ = o.size_;
        o.data_ = nullptr;
        o.size_ = 0u;

        return *this;
    }

    ~Buffer()
    {
        std::free(data_);
        data_ = nullptr;
        size_ = 0;
    }

    void resize(size_t size)
    {
        auto p = std::realloc(data_, size);

        if (!p)
        {
            std::free(data_);
            size_ = 0;
            throw std::runtime_error("Buffer: realloc failed");
        }

        data_ = p;
        size_ = size;
    }

    std::vector<uint8_t> vector() const
    {
        return {uint8Data(), uint8Data() + size_};
    }

    void *data() noexcept { return data_; }
    const void *data() const noexcept { return data_; }
    uint8_t *uint8Data() noexcept { return reinterpret_cast<uint8_t *>(data_); }
    const uint8_t *uint8Data() const noexcept { return reinterpret_cast<uint8_t *>(data_); }

    size_t size() const noexcept { return size_; }

private:
    void *data_{ };
    size_t size_{ };
};

////////////////////////////////////////////////////////////////////////////////
// WaitQueue

template <typename T>
class WaitQueue
{
public:
    using Clock = std::chrono::steady_clock;
    using Mutex = std::timed_mutex;
    using Lock = std::unique_lock<Mutex>;
    using Value = T;
    using ReturnType = std::optional<Value>;
    using Queue = std::deque<Value>;

    void put(Value t)
    {
        doPut(std::move(t), nullptr);
    }

    bool put(Value t, const Clock::time_point &deadline)
    {
        return doPut(std::move(t), &deadline);
    }

    ReturnType get()
    {
        return doGet(nullptr);
    }

    ReturnType get(const Clock::time_point &deadline)
    {
        return doGet(&deadline);
    }

    void cancel() noexcept
    {
        done_ = true;
        cond_.notify_all();
    }

    void resume() noexcept
    {
        done_ = false;
    }

    bool done() const noexcept
    {
        return done_;
    }

private:
    ReturnType doGet(const Clock::time_point *deadline)
    {
        const auto op = [this] {
            auto t = std::move(q_.front());
            q_.pop_front();
            return t;
        };

        return doWithCondition(op, deadline);
    }

    bool doPut(Value t, const Clock::time_point *deadline)
    {
        const auto op = [this, v = std::move(t)]() -> Value {
            q_.push_back(std::move(v));
            return { };
        };

        bool timedOut{ };
        doWithLock(op, deadline, &timedOut);

        if (!timedOut)
            cond_.notify_one();

        return !timedOut;
    }

    auto doWithLock(const std::function<Value()> &op, const Clock::time_point *deadline, bool *timedOut = nullptr)
        -> std::optional<Value>
    {
        Lock lk(mtx_, std::defer_lock_t{ });

        if (deadline)
        {
            if (!lk.try_lock_until(*deadline))
            {
                if (timedOut)
                    *timedOut = true;

                return { };
            }
        }
        else
        {
            lk.lock();
        }

        if (!op)
            return { };

        return op();
    }

    auto doWithCondition(const std::function<Value()> &op, const Clock::time_point *deadline, bool *timedOut = nullptr)
        -> std::optional<Value>
    {
        Lock lk(mtx_, std::defer_lock_t{ });

        if (deadline)
        {
            if (!lk.try_lock_until(*deadline))
            {
                if (timedOut)
                    *timedOut = true;

                return { };
            }
        }
        else
        {
            lk.lock();
        }

        if (!deadline)
        {
            cond_.wait(lk, [this]{ return done_ || !q_.empty(); });
        }
        else
        {
            if (!cond_.wait_until(lk, *deadline, [this]{ return done_ || !q_.empty(); }))
            {
                if (timedOut)
                    *timedOut = true;

                return { };
            }
        }

        if (timedOut)
            *timedOut = false;

        if (done_)
            return { };

        if (!op)
            return { };

        return op();
    }

    Mutex mtx_;
    std::condition_variable_any cond_;
    Queue q_;
    bool done_{ };
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
// FreeList

class FreeList
{
public:
    static constexpr auto End = ~size_t{0};

    FreeList() = default;

    explicit FreeList(size_t size)
    {
        list_.resize(size);
        std::iota(begin(list_), end(list_), 1u);
        list_.back() = End;
    }

    size_t get()
    {
        auto idx = free_;

        if (free_ == End)
            return ~0u;

        free_ = list_[free_];

        return idx;
    }

    void put(size_t idx)
    {
        if (idx < list_.size())
            list_[idx] = free_;

        free_ = idx;
    }

private:
    std::vector<size_t> list_{ };
    size_t free_{ };
};

////////////////////////////////////////////////////////////////////////////////
// BufferPool

class BufferPool: public std::enable_shared_from_this<BufferPool>
{
public:
    using Lock = std::unique_lock<std::mutex>;

    struct Buffer
    {
    public:
        Buffer() = default;

        Buffer(const Buffer &) = delete;
        Buffer &operator=(const Buffer &) = delete;

        Buffer(Buffer &&o) noexcept
        {
            *this = std::move(o);
        }

        Buffer &operator=(Buffer &&o) noexcept
        {
            if (pool_)
                pool_->put(freeIdx_);

            data_ = o.data_;
            o.data_ = nullptr;

            size_ = o.size_;
            o.size_ = 0;

            freeIdx_ = o.freeIdx_;
            o.freeIdx_ = 0;

            pool_ = o.pool_;
            o.pool_ = nullptr;

            return *this;
        }

        ~Buffer() noexcept
        {
            if (pool_)
                pool_->put(freeIdx_);
        }

        void *data() noexcept { return data_; };
        const void *data() const noexcept { return data_; };

        uint8_t *uint8Data() noexcept { return reinterpret_cast<uint8_t *>(data_); };
        const uint8_t *uint8Data() const noexcept { return reinterpret_cast<const uint8_t *>(data_); };

        size_t size() const noexcept { return size_; };

        size_t freeIndex() const noexcept { return freeIdx_; }

        explicit operator bool() const noexcept { return data_; }

    private:
        friend class BufferPool;

        Buffer(const std::shared_ptr<BufferPool> &pool, size_t index, void *data, size_t size):
            data_(data),
            size_(size),
            freeIdx_(index),
            pool_(pool)
        {
        }

        void *data_{ };
        size_t size_{ };
        size_t freeIdx_{ };
        std::shared_ptr<BufferPool> pool_{ };
    };

    static std::shared_ptr<BufferPool> make(size_t chunkSize, size_t count)
    {
        auto p = std::shared_ptr<BufferPool>(new BufferPool);
        p->init(chunkSize, count);
        return p;
    }

    ~BufferPool() noexcept
    {
        done_ = true;
        cond_.notify_all();
    }

    Buffer get()
    {
        Lock lk(mtx_);

        size_t idx{ };

        cond_.wait(lk, [this, &idx] {
            if (done_)
                return true;
            idx = freeList_.get();
            return idx != ~0u;
        });

        if (done_ || idx == ~0u)
            return { };

        return {
            shared_from_this(),
            idx,
            mmap_.uint8Data(idx * chunkSize_),
            chunkSize_
        };
    }

private:
    BufferPool() = default;

    BufferPool(size_t chunkSize, size_t chunkCount):
        chunkSize_(chunkSize),
        chunkCount_(chunkCount)
    {
        init();
    }

    void init(size_t chunkSize, size_t chunkCount)
    {
        chunkSize_ = chunkSize;
        chunkCount_ = chunkCount;

        init();
    }

    void init()
    {
        mmap_ = ScopedMMap::map(
            nullptr,
            roundBlockSize(chunkSize_ * chunkCount_),
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
            -1,
            0);

        freeList_ = FreeList{chunkCount_};
    }

    void put(size_t index)
    {
        if (done_)
            return;

        {
            Lock lk(mtx_);
            freeList_.put(index);
        }

        cond_.notify_one();
    }

    std::mutex mtx_{ };
    std::condition_variable cond_{ };
    FreeList freeList_{ };
    ScopedMMap mmap_{ };
    size_t chunkSize_{ };
    size_t chunkCount_{ };
    bool done_{ };
};

struct MessageBuffer
{
    std::shared_ptr<BufferPool::Buffer> buf;
    size_t fileOffset{ };
    size_t payloadLength{ };
    unsigned fileId{ };
};

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

struct NetworkTarget
{
    std::string ip{ };
    uint16_t port{ };
};

struct AgentConfig
{
    std::vector<NetworkTarget> targets;
    size_t blockSize{1u << 22};
    size_t txRingPwr{5};
};

class MmapAgent
{
public:
    explicit MmapAgent(AgentConfig conf):
        blockSize_(conf.blockSize)
    {
        initNetwork(conf);

        pool_ = BufferPool::make(blockSize_, 1u << conf.txRingPwr);
    }

    void cancel()
    {
        done_ = true;
    }

    int transferFile(const std::string &path, unsigned fileId)
    {
        namespace fs = std::filesystem;

        auto fd = ScopedFd{::open(path.c_str(), O_RDONLY | O_DIRECT)};

        if (fd.get() < 0)
        {
            throw std::system_error(errno, std::system_category(),
                fmt::format("open '{}'", path));
        }

        spdlog::info("file {} fd: {}", path, fd.get());

        auto fileLen = fs::file_size(path);

        done_ = false;

        auto results = std::vector<std::future<std::pair<size_t, size_t>>>{ };
        const auto segmentSize = computeSegmentSize(fileLen, fdMap_.size());
        size_t offset = 0;

        for (const auto &fdInfo : fdMap_)
        {
            const auto &info = fdInfo.second;

            const auto actualSegmentSize = std::min(segmentSize, fileLen);

            auto segment = Segment{
                    offset,
                    offset + actualSegmentSize,
                    fileId
                };

            results.push_back(
                std::async(std::launch::async,
                    [this, inFd = fd.get(), outFd = info.fd.get(), segment] {
                        return transferSegment(inFd, outFd, segment);
                    }));

            offset += actualSegmentSize;
            fileLen -= actualSegmentSize;

            if (!fileLen)
                break;
        }

        size_t chunkCount{ };
        size_t chunkXfer{ };

        for (auto &result : results)
        {
            auto [count, xfer] = result.get();
            chunkCount += count;
            chunkXfer += xfer;
        }

        spdlog::info("xferred {} chunk(s), {} bytes", chunkCount, chunkXfer);

        return 0;
    }

private:
    using SharedMMap = std::shared_ptr<ScopedMMap>;

    struct Segment
    {
        size_t start{ };
        size_t end{ };
        unsigned fileId{ };
    };

    struct FdInfo
    {
        ScopedFd fd{ };
        unsigned speed{1000};
    };

    void initNetwork(const AgentConfig &conf);

    std::pair<size_t, size_t> transferSegment(int inFd, int outFd, const Segment &segment)
    {
        // write segment header.

        auto header = wire::ChunkHeader{ };
        header.magic = wire::ChunkHeader::Magic;
        header.fileOffset = segment.start;
        header.payloadLength = segment.end - segment.start;
        header.fileId = segment.fileId;

        auto iov = iovec {&header, sizeof(header)};
        writeChunk(outFd, &iov, 1);

        // write segment data.

        uint8_t *buf{ };
        if (posix_memalign(reinterpret_cast<void **>(&buf), BlockSize, blockSize_))
            throw std::system_error(errno, std::system_category(), "posix_memalign");

        size_t chunkCount{ };
        size_t chunkXfer{ };

        for (size_t fileOffset = segment.start; !done_ && fileOffset < segment.end; )
        {
            auto len = readChunk(inFd, buf, blockSize_, fileOffset);

            if (!len)
                break;

            auto xferLen = std::min(len, segment.end - fileOffset);

            spdlog::debug("xfer segment {} from offset {}", xferLen, fileOffset);

            auto chunkIov = iovec{buf, xferLen};
            writeChunk(outFd, &chunkIov, 1);

            fileOffset += xferLen;

            ++chunkCount;
            chunkXfer += xferLen;
        }

        return {chunkCount, chunkXfer};
    }

    PollSet poll_{ };
    std::shared_ptr<BufferPool> pool_{ };
    std::unordered_map<int, FdInfo> fdMap_{ };
    std::unordered_map<int, FdInfo>::iterator fdIter_{ };
    size_t blockSize_{1u << 22};
    bool done_{ };
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
    size_t ringPwr{5};
    bool enableDio{ };
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
        initFileState(std::move(conf));

        wrThdStatus_ = std::async(std::launch::async,
            [this] {
                using std::chrono::steady_clock;
                using namespace std::chrono_literals;

                while (!done_)
                {
                    if (auto io = wtq_.get(steady_clock::now() + 200ms))
                        syncWrite(&(*io));
                }

                spdlog::warn("wr thd done");
            });
    }

    ~FileAgent() noexcept
    {
        cancel();
    }

    void cancel() noexcept
    {
        done_ = true;

        try {
            wrThdStatus_.get();
        } catch (...) { }
    }

    int updateFile(const MessageBuffer &buf)
    {
        auto fileId = buf.fileId;
        auto xfer = initWrite(std::move(buf));

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
        using namespace std::chrono_literals;

        cancel();

        size_t count{ };

        for (count = 0; auto io = wtq_.get(std::chrono::steady_clock::now() + 200ms); )
        {
            syncWrite(&(*io));
            ++count;
        }

        spdlog::info("drained {} chunks from file agent queue.", count);

        truncateFiles();
    }

private:
    struct IOState
    {
        MessageBuffer buf{ };
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

    void initFileState(FileAgentConfig conf);

    std::unique_ptr<IOState> initWrite(const MessageBuffer &buf)
    {
        auto iter = fileMap_.find(buf.fileId);
        if (iter == end(fileMap_))
            return nullptr;

        auto xfer = std::make_unique<IOState>();

        xfer->fileOffset = buf.fileOffset;
        xfer->len = buf.payloadLength;
        xfer->fileId = buf.fileId;
        xfer->fd = iter->second.fd.get();

        xfer->buf = std::move(buf);

        xfer->iov.iov_base = const_cast<void *>(xfer->buf.buf->data());
        xfer->iov.iov_len = xfer->buf.payloadLength;

        return xfer;
    }

    int writeChunk(std::unique_ptr<IOState> xfer)
    {
        if (auto stat = asyncWrite(std::move(xfer)))
            spdlog::error("sync write: {}", std::strerror(-stat));

        return 0;
    }

    int asyncWrite(std::unique_ptr<IOState> xfer)
    {
        if (!done_ && xfer)
            wtq_.put(std::move(*xfer));

        return 0;
    }

    int syncWrite(IOState *xfer)
    {
        if (!xfer)
            return 0;

        int stat{ };
        const auto xferLen = xfer->len;

        for (size_t wlen = 0; wlen < xferLen; )
        {
            spdlog::debug("writev {}: {} -> {} ({:#x})"
                , xfer->fileId, xfer->len - wlen, xfer->fileOffset, xfer->fileOffset);

            auto len = ::pwrite(
                xfer->fd,
                xfer->buf.buf->uint8Data() + wlen,
                xfer->len - wlen,
                static_cast<off_t>(xfer->fileOffset + wlen));

            spdlog::info("file id {} pwrite {} -> {}: {}"
                , xfer->fileId, xfer->len - wlen, xfer->fileOffset + wlen, len);

            if (len < 0)
            {
                stat = -errno;
                spdlog::error("file id {} pwrite offset {} len {}: {}"
                    , xfer->fileId
                    , xfer->fileOffset + wlen
                    , xfer->len - wlen
                    , std::strerror(-stat));
                break;
            }

            if (!len)
            {
                spdlog::error("zero-length write - ending transfer.");
                stat = -EOF;
                break;
            }

            wlen += static_cast<size_t>(len);
        }

        return stat;
    }

    void truncateFiles()
    {
        for (auto &[id, state] : fileMap_)
        {
            const auto sz = state.info.status.size;
            spdlog::info("FileAgent: trimming file id {} to size {}", id, sz);
            ftruncate(state.fd.get(), static_cast<off_t>(sz));
        }
    }

    FileStateMap fileMap_{ };
    std::future<void> wrThdStatus_{ };
    WaitQueue<IOState> wtq_{ };
    bool done_{ };
};

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

}

}

#endif
