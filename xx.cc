#include <atomic>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <list>
#include <ranges>
#include <thread>

#include <poll.h>
#include <signal.h>
#include <sys/socket.h>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "Util.hh"
#include "UtilJson.hh"

namespace draft::util {

using namespace std::chrono_literals;

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

struct Stats
{
    std::atomic_uint diskByteCount{ };
    std::atomic_uint queuedBlockCount{ };
    std::atomic_uint dequeuedBlockCount{ };
    std::atomic_uint netByteCount{ };
    std::atomic_uint fileByteCount{ };
} stats_;

using BufQueue = WaitQueue<BDesc>;
using BufferPtr = std::shared_ptr<BufferPool::Buffer>;
using FdMap = std::unordered_map<unsigned, int>;

class Reader
{
public:
    using Buffer = BufferPool::Buffer;

    Reader(ScopedFd fd, unsigned fileId, Segment segment, const BufferPoolPtr &pool, BufQueue &queue):
        fd_{std::move(fd)},
        segment_{segment},
        pool_{pool},
        queue_{&queue},
        fileId_{fileId}
    {
    }

    int operator()()
    {
        auto buf = std::make_unique<Buffer>(pool_->get());

        auto len = read(*buf);

        if (!len)
            return 0;

        stats_.diskByteCount += len;

        queue_->put({
            std::move(buf),
            fileId_,
            segment_.offset,
            len
        });

        ++stats_.queuedBlockCount;

        segment_.offset += len;

        return 0;
    }

private:
    size_t read(Buffer &buf)
    {
        spdlog::debug("reader segment progress: {}/{} ({:.1f})"
            , segment_.offset
            , segment_.len
            , static_cast<double>(segment_.offset) / segment_.len * 100);

        auto len = roundBlockSize(segment_.len - segment_.offset);
        len = std::min(len, buf.size());

        return readChunk(fd_.get(), buf.data(), len, segment_.offset);
    }

    ScopedFd fd_{ };
    Segment segment_{ };
    BufferPoolPtr pool_{ };
    BufQueue *queue_{ };
    unsigned fileId_{ };
};

class Sender
{
public:
    using Buffer = BufferPool::Buffer;

    Sender(ScopedFd fd, BufQueue &queue):
        queue_(&queue),
        fd_(std::move(fd))
    {
    }

    bool runOnce()
    {
        using Clock = std::chrono::steady_clock;

        while (auto desc = queue_->get(Clock::now() + 200ms))
        {
            ++stats_.dequeuedBlockCount;
            stats_.netByteCount += write(std::move(*desc)) - sizeof(wire::ChunkHeader);
        }

        return true;
    }

private:
    size_t write(BDesc desc)
    {
        auto header = wire::ChunkHeader{ };
        header.magic = wire::ChunkHeader::Magic;
        header.fileOffset = desc.offset;
        header.payloadLength = desc.len;
        header.fileId = desc.fileId;

        iovec iov[2] = {
            {&header, sizeof(header)},
            {desc.buf->data(), desc.len}
        };

        return writeChunk(fd_.get(), iov, 2);
    }

    BufQueue *queue_{ };
    ScopedFd fd_{ };
};

class Receiver
{
public:
    using Buffer = BufferPool::Buffer;

    Receiver(ScopedFd fd, BufQueue &queue):
        queue_(&queue),
        svcFd_(std::move(fd))
    {
        pool_ = BufferPool::make(1u << 21, 35);
    }

    bool runOnce()
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

        return waitData();
    }

private:
    int waitConnect()
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

        try {
            spdlog::info("accepted connection from '{}'"
                , util::net::peerName(fd_.get()));
        } catch (const std::exception &ex) {
            spdlog::error("unable to resolve peer name: {}", ex.what());
        }

        return 1;
    }

    bool waitData()
    {
        if (!haveHeader_)
        {
            if (auto stat = readHeader(); stat <= 0)
                return stat == EOF ? false : true;

            buf_ = pool_->get();

            haveHeader_ = true;
            offset_ = 0;
        }

        if (read())
        {
            spdlog::trace("receiver put {} -> id {}"
                , header_.payloadLength
                , header_.fileId);

            queue_->put({
                std::make_shared<Buffer>(std::move(buf_)),
                header_.fileId,
                header_.fileOffset,
                header_.payloadLength
            });

            ++stats_.queuedBlockCount;

            haveHeader_ = false;
            offset_ = 0;
        }

        return true;
    }

    int readHeader()
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

    bool read()
    {
        auto len = ::read(fd_.get(), buf_.uint8Data() + offset_, header_.payloadLength - offset_);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "read");

        stats_.netByteCount += len;

        offset_ += static_cast<size_t>(len);

        if (offset_ >= header_.payloadLength)
            return true;

        return false;
    }

    BufferPoolPtr pool_{ };
    BufQueue *queue_{ };
    wire::ChunkHeader header_{ };
    Buffer buf_{ };
    size_t offset_{ };
    ScopedFd fd_{ };
    ScopedFd svcFd_{ };
    bool haveHeader_{ };
};

class Writer
{
public:
    using Buffer = BufferPool::Buffer;

    Writer(FdMap fdMap, BufQueue &queue):
        queue_(&queue),
        fdMap_(std::move(fdMap))
    {
    }
        
    bool runOnce()
    {
        while (auto desc = queue_->tryGet())
        {
            if (!desc->buf)
                break;

            ++stats_.dequeuedBlockCount;

            stats_.diskByteCount += write(std::move(*desc));
        }

        return true;
    }

private:
    int getFd(unsigned id)
    {
        auto iter = fdMap_.find(id);
        if (iter == end(fdMap_))
            return -1;

        return iter->second;
    }

    size_t write(BDesc desc)
    {
        const auto fd = getFd(desc.fileId);

        iovec iov{
            desc.buf->data(),
            roundBlockSize(desc.len)
        };

        spdlog::trace("write {} -> id {}"
            , iov.iov_len
            , desc.fileId);

        return writeChunk(fd, &iov, 1, desc.offset);
    }

    BufQueue *queue_{ };
    FdMap fdMap_{ };
};

// TODO: runnable concept
class Executor
{
public:
    template <typename ...Args>
    Executor(Args &&...args)
    {
        (add(std::forward<Args>(args)), ...);
    }

    template <typename T>
    Executor &add(T &&runnable)
    {
        runq_.push_back(
            std::make_unique<Runnable_<T>>(std::forward<T>(runnable)));

        return *this;
    }

    template <typename T>
    Executor &add(std::vector<T> runnables)
    {
        auto runnablesView = std::views::transform(
            runnables, [](auto &&r) {
                return std::make_unique<Runnable_<T>>(
                    std::forward<T>(r));
            });

        runq_.insert(
            end(runq_),
            std::make_move_iterator(begin(runnablesView)),
            std::make_move_iterator(end(runnablesView)));

        return *this;
    }

    void runOnce()
    {
        for (auto &r : runq_)
            r->runOnce();
    }

    bool empty() const noexcept
    {
        return runq_.empty();
    }

private:
    class Runnable
    {
    public:
        virtual ~Runnable() = default;
        virtual bool runOnce() = 0;
    };

    template <typename T>
    class Runnable_: public Runnable
    {
    public:
        Runnable_(T t):
            t_(std::move(t))
        {
        }

    private:
        bool runOnce() override
        {
            return t_.runOnce();
        }

        T t_;
    };

    std::vector<std::unique_ptr<Runnable>> runq_;
};

class TaskPool
{
public:
    TaskPool() = default;

    explicit TaskPool(size_t size)
    {
        resize(size);
    }

    TaskPool(TaskPool &&) = default;
    TaskPool &operator=(TaskPool &&) = default;

    ~TaskPool() noexcept
    {
        cancel();
    }

    void cancel() noexcept
    {
        q_.cancel();
    }

    size_t size() const noexcept
    {
        return threads_.size();
    }

    void resize(size_t newSize)
    {
        const auto prevSize = threads_.size();
        threads_.resize(newSize);

        for (auto i = prevSize; i < newSize; ++i)
            threads_[i] = std::jthread([this]{ stealWork(); });
    }

    template <typename Function, typename ...Args>
    [[nodiscard]]
    auto launch(Function &&f, Args &&...args)
    {
        using result_type =
            std::invoke_result_t<
                std::decay_t<Function>,
                std::decay_t<Args>...>;

        auto promise = std::promise<result_type>{ };
        auto future = promise.get_future();

        q_.put({
            std::forward<std::decay_t<Function>>(f),
            std::move(promise),
            std::forward<Args>(args)...});

            //std::forward_as_tuple(args...)});
            //promise = std::move(promise)});
//                try {
//                    promise.set_value(std::invoke(std::move(f), std::move(args)));
//                } catch (...) {
//                    promise.set_exception(std::current_exception());
//                }
//            });

        return future;
    }

private:
    struct Work
    {
        Work() = default;

        template <typename Fn, typename ...Args>
        Work(Fn &&f, auto &&promise, Args &&...args):
            impl_{std::make_unique<Impl_<Fn, Args...>>(
                std::move(f), std::forward<Args>(args)...)}
        {
            (void)promise;
        }

        void operator()()
        {
            impl_->invoke();
        }

        struct Impl
        {
            Impl() = default;
            Impl(const Impl &) = delete;
            Impl &operator=(const Impl &) = delete;
            virtual ~Impl() = default;

            virtual void invoke() = 0;
        };

        template <typename Fn, typename ...Args>
        struct Impl_: public Impl
        {
            Impl_(Fn &&f, Args &&...args):
                f_(std::move(f)),
                args_(std::forward_as_tuple(args...))
            {
            }

            void invoke() override
            {
                if constexpr (!std::is_same_v<std::decay_t<Fn>, Work>) {
                    std::apply(f_, args_);
                }
                else { }
            }

            Fn f_;
            std::tuple<Args...> args_;
        };

        std::unique_ptr<Impl> impl_;
    };

    // TODO: forward stop token.
    void stealWork()
    {
        while (!q_.done())
        {
            if (auto work = q_.get())
                (*work)();
        }
    }

    WaitQueue<Work, std::allocator<Work>, std::list> q_;
    std::vector<std::jthread> threads_;
};

class ThreadExecutor
{
public:
    template <typename ...Args>
    ThreadExecutor(Args &&...args)
    {
        (add(std::forward<Args>(args)), ...);
    }

    template <typename T>
    ThreadExecutor &add(T &&runnable)
    {
        runq_.push_back(
            std::make_unique<Runnable_<T>>(std::forward<T>(runnable)));

        return *this;
    }

    template <typename T>
    ThreadExecutor &add(std::vector<T> runnables)
    {
        auto runnablesView = std::views::transform(
            runnables, [](auto &&r) {
                return std::make_unique<Runnable_<T>>(
                    std::forward<T>(r));
            });

        runq_.insert(
            end(runq_),
            std::make_move_iterator(begin(runnablesView)),
            std::make_move_iterator(end(runnablesView)));

        return *this;
    }

    bool runOnce()
    {
        std::erase_if(runq_,
            [](const auto &r) {
                auto rm = !r->runOnce();

                if (rm)
                    spdlog::info("removing thd exec entry.");

                return rm;
            });

        return !empty();
    }

    bool empty() const noexcept
    {
        return runq_.empty();
    }

    void cancel() noexcept
    {
        for (auto &r : runq_)
            r->cancel();
    }

    void waitFinished() noexcept
    {
        try {
            runq_.clear();
        } catch (...) {
        }
    }

private:
    class Runnable
    {
    public:
        virtual ~Runnable() = default;
        virtual bool runOnce() const = 0;
        virtual void cancel() noexcept = 0;
    };

    template <typename T>
    class Runnable_: public Runnable
    {
    public:
        Runnable_(T t)
        {
            thd_ = std::jthread(
                [this](std::stop_token token, T t_) mutable {
                    while (!token.stop_requested() && t_.runOnce())
                        ;

                    finished_ = true;
                    spdlog::debug("thd runnable exiting.");
                }, std::move(t));
        }

        ~Runnable_() noexcept
        {
            thd_.request_stop();
        }

        void cancel() noexcept
        {
            thd_.request_stop();
        }

    private:
        bool runOnce() const override
        {
            return !finished_;
        }

        std::atomic_bool finished_{ };
        std::jthread thd_{ };
    };

    std::vector<std::unique_ptr<Runnable>> runq_;
};

struct Target
{
    std::string addr;
    unsigned port{ };
};

struct SessionConfig
{
    std::vector<Target> targets;
    Target service;
    std::string pathRoot;
};

std::vector<ScopedFd> connectTargets(const std::vector<Target> &targets)
{
    const auto view = std::views::transform(
        targets,
        [](const Target &t) {
            return net::connectTcp(t.addr, t.port);
        });

    return {begin(view), end(view)};
}

std::vector<ScopedFd> bindTargets(const std::vector<Target> &targets)
{
    const auto view = std::views::transform(
        targets,
        [](const Target &t) {
            return net::bindTcp(t.addr, t.port);
        });

    return {begin(view), end(view)};
}

class TxSession
{
public:
    TxSession(SessionConfig conf):
        conf_(std::move(conf))
    {
        readExec_.resize(1);

        pool_ = BufferPool::make(1u << 21, 35);
        targetFds_ = connectTargets(conf_.targets);

        spdlog::info("connected tx targets.");
    }

    ~TxSession() noexcept
    {
        finish();
    }

    void start(const std::string &path)
    {
        // TODO: should we also own the rx service connection, and send xfer
        // request here?

        const auto view = std::views::transform(
            targetFds_, [this](auto &&fd){ return Sender{std::move(fd), queue_}; });

        auto senders = std::vector<Sender>{
            std::make_move_iterator(begin(view)),
            std::make_move_iterator(end(view))};

        targetFds_ = std::vector<ScopedFd>{ };

        sendExec_.add(std::move(senders));

        info_ = getFileInfo(path);
        fileIter_ = nextFile(begin(info_), end(info_));

        if (fileIter_ != end(info_))
            startFile(*fileIter_);
    }

    void finish() noexcept
    {
        // wait on readers.
        readExec_.cancel();
        sendExec_.cancel();
    }

    bool runOnce()
    {
        // wait on any readers.
        for (auto &r : readResults_)
        {
            if (r.valid() && r.wait_for(1ms) == std::future_status::ready)
                r.get();
        }

        // remove ready readers.
        std::erase_if(readResults_, [](const auto &r) { return !r.valid(); });

        sendExec_.runOnce();

        while (readResults_.size() < readExec_.size())
        {
            // finished reading the current file - select next file, skip directories.
            fileIter_ = nextFile(++fileIter_, end(info_));

            if (fileIter_ == end(info_))
            {
                // path xfer completed.
                // run senders again to flush queued data.
                spdlog::info("tx path transfer completed - waiting on readers & flushing sender data.");

                for (auto &r : readResults_)
                {
                    if (r.valid())
                        r.get();
                }

                sendExec_.runOnce();

                return false;
            }

            startFile(*fileIter_);
        }

        return true;
    }

private:
    using file_info_iter_type = std::vector<FileInfo>::const_iterator;

    file_info_iter_type nextFile(file_info_iter_type first, file_info_iter_type last)
    {
        while (first != last && !S_ISREG(first->status.mode))
            ++first;

        return first;
    }

    void startFile(const FileInfo &info)
    {
        const auto &filename = info.path;

        auto fd = ScopedFd{::open(filename.c_str(), O_RDONLY | O_DIRECT)};
        spdlog::debug("tx opened file id {}: {} @ fd {}", info.id, filename, fd.get());

        auto fileSz = std::filesystem::file_size(filename);

        auto diskRead = Reader(std::move(fd), info.id, {0, fileSz}, pool_, queue_);

        readResults_.push_back(readExec_.launch(std::move(diskRead)));
    }

    WaitQueue<BDesc> queue_;
    std::shared_ptr<BufferPool> pool_;
    TaskPool readExec_;
    std::vector<std::future<int>> readResults_;
    ThreadExecutor sendExec_;
    std::vector<FileInfo> info_;
    std::vector<FileInfo>::const_iterator fileIter_;
    SessionConfig conf_;
    std::vector<ScopedFd> targetFds_;
};

class RxSession
{
public:
    RxSession(SessionConfig conf):
        conf_(std::move(conf))
    {
        pool_ = BufferPool::make(1u << 21, 35);
        targetFds_ = bindTargets(conf_.targets);
    }

    ~RxSession() noexcept
    {
        finish();
    }

    void start(util::TransferRequest req)
    {
        createTargetFiles(conf_.pathRoot, req.config.fileInfo);

        auto fileInfo = std::vector<FileInfo>{ };
        auto fileMap = FdMap{ };
        for (const auto &item : req.config.fileInfo)
        {
            auto path = rootedPath(
                conf_.pathRoot,
                item.path,
                item.targetSuffix);

            auto fd = ScopedFd{::open(path.c_str(), O_WRONLY | O_DIRECT)};
            auto rawFd = fd.get();

            fileInfo.push_back({
                path,
                std::move(fd),
                item.status.size,
                item.status.mode
            });

            fileMap.insert({item.id, rawFd});
        }

        const auto view = std::views::transform(
            targetFds_, [this](auto &&fd){ return Receiver{std::move(fd), queue_}; });

        auto receivers = std::vector<Receiver>{
            std::make_move_iterator(begin(view)),
            std::make_move_iterator(end(view))};

        targetFds_ = std::vector<ScopedFd>{ };

        spdlog::debug("starting receivers.");

        recvExec_.add(std::move(receivers));

        writeExec_.add(Writer(std::move(fileMap), queue_));

        fileInfo_ = std::move(fileInfo);
    }

    void finish() noexcept
    {
        recvExec_.cancel();
        writeExec_.cancel();
        writeExec_.waitFinished();

        // truncate after each file.
        truncateFiles();
    }

    void truncateFiles()
    {
        for (const auto &info : fileInfo_)
        {
            if (!S_ISREG(info.mode))
                continue;

            spdlog::debug("truncate '{}' -> {}"
                , info.path
                , info.size);

            const auto sz = std::min(
                static_cast<size_t>(std::numeric_limits<off_t>::max()),
                info.size);

            if (::ftruncate(info.fd.get(), static_cast<off_t>(sz)))
            {
                spdlog::warn("unable to truncate file '{}' to size {} ({})"
                    , info.path
                    , info.size
                    , std::strerror(errno));
            }
        }
    }

    bool runOnce()
    {
        auto recvFinished = !recvExec_.runOnce();
        writeExec_.runOnce();

        // return now if we're still receiving data.
        if (!recvFinished)
            return true;

        writeExec_.runOnce();

        return false;
    }

private:
    struct FileInfo
    {
        std::string path;
        ScopedFd fd;
        size_t size{ };
        mode_t mode{ };
    };

    WaitQueue<BDesc> queue_;
    std::shared_ptr<BufferPool> pool_;
    ThreadExecutor recvExec_;
    ThreadExecutor writeExec_;
    SessionConfig conf_;
    std::vector<ScopedFd> targetFds_;
    std::vector<FileInfo> fileInfo_;
};

class InfoReceiver
{
public:
    explicit InfoReceiver(ScopedFd fd):
        srvFd_(std::move(fd))
    {
    }

    bool runOnce()
    {
        if (fd_.get() < 0)
        {
            fd_ = util::net::accept(srvFd_.get());

            if (fd_.get() < 0)
                return false;

            spdlog::info("accepted service connection @ fd {}", fd_.get());
        }

        if (buf_.size() - offset_ < 4096)
            buf_.resize(buf_.size() + 4096);

        auto data = buf_.data() + offset_;
        auto sz = buf_.size() - offset_;

        auto len = ::recv(fd_.get(), data, sz, 0);
        spdlog::debug("rx'd {} for info", len);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "recv");

        offset_ += static_cast<size_t>(len);

        if (!len)
        {
            buf_.resize(offset_);
            haveInfo_ = true;
            return true;
        }

        return false;
    }

    TransferRequest info() const
    {
        auto header = reinterpret_cast<const wire::ChunkHeader *>(buf_.data());

        if (header->magic != wire::ChunkHeader::Magic)
            throw std::runtime_error("invalid chunk magic");

        if (sizeof(*header) + header->payloadLength > buf_.size())
        {
            throw std::runtime_error(
                "invalid payload length: " + std::to_string(header->payloadLength));
        }

        return deserializeTransferRequest(
            Buffer{buf_.data() + sizeof(*header), header->payloadLength});
    }

private:
    std::vector<uint8_t> buf_{ };
    size_t offset_{ };
    ScopedFd fd_{ };
    ScopedFd srvFd_{ };
    bool haveInfo_{ };
};

sig_atomic_t done_;

std::optional<TransferRequest> awaitTransferRequest(ScopedFd fd)
{
    auto rx = InfoReceiver{std::move(fd)};

    while (!done_ && !rx.runOnce())
        ;

    if (done_)
        return { };

    auto info = rx.info();

    for (const auto &item : info.config.fileInfo)
    {
        if (S_ISREG(item.status.mode))
            stats_.fileByteCount += item.status.size;
    }

    return info;
}

void sendTransferRequest(ScopedFd fd, const std::vector<FileInfo> &info)
{
    auto request = util::generateTransferRequestMsg(info);
    util::net::writeAll(fd.get(), request.data(), request.size());

    for (const auto &item : info)
    {
        if (S_ISREG(item.status.mode))
            stats_.fileByteCount += item.status.size;
    }

    spdlog::debug("sent xfer req: {}", request.size());
}

void handleSig(int)
{
    done_ = 1;
}

}

// readers -> queue -> Senders -> net -> receivers -> queue -> writers -> disk

// take temporary receivers list so receivers ports can be opened prior to this
// call, at the same time as the session fd, so receivers are ready immediately
// after session setup.
//
// this should be removed when rx is encapsulated in an rx handler.

auto conf_ = draft::util::SessionConfig{
    {
        {"10.77.2.101", 5000},
        {"10.77.3.101", 5000},
        {"10.77.4.101", 5000},
    },
    {"10.77.2.101", 5002},
    "tmp"
};

int recvCmd(int, char **)
{
    using namespace draft::util;

    spdlog::info("recv");

    auto sess = RxSession(conf_);

    auto req = awaitTransferRequest(
        net::bindTcp(conf_.service.addr, conf_.service.port));

    if (!req)
        return 1;

    spdlog::info("starting rx session.");
    sess.start(std::move(*req));

    while (sess.runOnce())
        std::this_thread::sleep_for(200ms);

    return 0;
}

int sendCmd(int, char **argv)
{
    using namespace draft::util;

    const auto path = std::string{argv[1]};

    // TODO: figure out if fileinfo / xfer req should be part of session start
    // this is redundant atm.
    auto fileInfo = getFileInfo(path);
    auto sess = TxSession(conf_);

    auto fd = net::connectTcp(conf_.service.addr, conf_.service.port);
    sendTransferRequest(std::move(fd), fileInfo);

    sess.start(path);

    while (sess.runOnce())
        std::this_thread::sleep_for(200ms);

    return 0;
}

void dumpStats(const draft::util::Stats &stats)
{
    spdlog::info(
        "stats:\n"
        "  file byte count:         {}\n"
        "  disk byte count:         {}\n"
        "  net byte count:          {}\n"
        "  queued block count:      {}\n"
        "  dequeued block count:    {}\n"
        , stats.fileByteCount
        , stats.diskByteCount
        , stats.netByteCount
        , stats.queuedBlockCount
        , stats.dequeuedBlockCount);
}

int main(int argc, char **argv)
{
    using namespace draft::util;

    spdlog::cfg::load_env_levels();

    struct sigaction action{ };
    action.sa_handler = handleSig;
    sigaction(SIGINT, &action, nullptr);

    const auto cmd = std::string{argv[1]};
    if (cmd == "rx")
        recvCmd(argc - 1, argv + 1);
    else if (cmd == "tx")
        sendCmd(argc - 1, argv + 1);
    else
        return 1;

    dumpStats(stats_);

    return 0;
}
