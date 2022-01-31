#include <atomic>
#include <chrono>
#include <filesystem>
#include <iterator>
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

    bool runOnce()
    {
        auto buf = std::make_unique<Buffer>(pool_->get());

        auto len = read(*buf);

        if (!len)
            return false;

        queue_->put({
            std::move(buf),
            fileId_,
            segment_.offset,
            len
        });

        segment_.offset += len;
        segment_.len -= len;

        return true;
    }

private:
    size_t read(Buffer &buf)
    {
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

    Sender(int fd, BufQueue &queue):
        queue_(&queue),
        fd_(fd)
    {
    }
        
    bool runOnce()
    {
        using Clock = std::chrono::steady_clock;

        while (auto desc = queue_->get(Clock::now() + 200ms))
            write(std::move(*desc));

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

        return writeChunk(fd_, iov, 2);
    }

    BufQueue *queue_{ };
    int fd_{-1};
};

class Receiver
{
public:
    using Buffer = BufferPool::Buffer;

    Receiver(ScopedFd fd, BufQueue &queue):
        queue_(&queue),
        svcFd_(std::move(fd))
    {
    }

    bool runOnce()
    {
        if (fd_.get() < 0 && !waitConnect())
            return true;

        return waitData();
    }

private:
    bool waitConnect()
    {
        auto pfd = pollfd{svcFd_.get(), POLLIN, 0};
        auto count = ::poll(&pfd, 1, 50);

        if (!count || !(pfd.revents & POLLIN))
            return false;

        fd_ = util::net::accept(svcFd_.get());

        if (fd_.get() >= 0)
        {
            spdlog::info("accepted connection from '{}'"
                , util::net::peerName(fd_.get()));
        }

        return true;
    }

    bool waitData()
    {
        if (!haveHeader_)
        {
            if (auto stat = readHeader(); stat <= 0)
                return stat == EOF ? false : true;

            buf_ = pool_->get();
            offset_ = 0;
        }

        if (read())
        {
            queue_->put({
                std::make_shared<Buffer>(std::move(buf_)),
                header_.fileId,
                header_.fileOffset,
                header_.payloadLength
            });

            haveHeader_ = false;
        }

        return true;
    }

    int readHeader()
    {
        auto buf = reinterpret_cast<uint8_t *>(&header_) + offset_;
        auto sz = sizeof(header_) - offset_;

        auto len = ::read(fd_.get(), buf, sz);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "read");

        if (!len)
            return EOF;

        offset_ += static_cast<size_t>(len);

        if (offset_ >= sizeof(header_))
        {
            haveHeader_ = true;
            return 1;
        }

        return 0;
    }

    bool read()
    {
        auto len = ::read(fd_.get(), buf_.uint8Data() + offset_, buf_.size() - offset_);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "read");

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
            write(std::move(*desc));

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
            desc.len
        };

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
        const auto count = std::erase_if(runq_,
            [](const auto &r) {
                auto rm = !r->runOnce();
                if (rm) spdlog::info("removing thd exec entry.");
                return rm;
            });

        spdlog::trace("thd exec runonce {} count {}", (void *)this, count);

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
        pool_ = BufferPool::make(1u << 21, 35);
        targetFds_ = connectTargets(conf_.targets);
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
            targetFds_, [this](const auto &fd){ return Sender{fd.get(), queue_}; });

        auto senders = std::vector<Sender>{
            std::make_move_iterator(begin(view)),
            std::make_move_iterator(end(view))};

        sendExec_.add(std::move(senders));

        info_ = getFileInfo(path);
        fileIter_ = begin(info_);

        if (fileIter_ != end(info_))
            startFile(*fileIter_);
    }

    void finish() noexcept
    {
        readExec_.cancel();
        sendExec_.cancel();
    }

    bool runOnce()
    {
        auto readFinished = !readExec_.runOnce();
        sendExec_.runOnce();

        // return now if we're still reading from the current file.
        if (!readFinished)
            return true;

        // finished reading the current file - select next file, skip directories.
        while (++fileIter_ != end(info_) && S_ISDIR(fileIter_->status.mode))
            ;

        if (fileIter_ == end(info_))
        {
            // path xfer completed.
            // run senders again to flush queued data.
            spdlog::info("tx path transfer completed - flushing sender data.");

            sendExec_.runOnce();

            return false;
        }

        startFile(*fileIter_);

        return true;
    }

private:
    void startFile(const FileInfo &info)
    {
        const auto &filename = info.path;

        auto fd = ScopedFd{::open(filename.c_str(), O_RDONLY | O_DIRECT)};
        spdlog::debug("tx opened file {} @ fd {}", filename, fd.get());

        auto fileSz = std::filesystem::file_size(filename);

        auto diskRead = Reader(std::move(fd), info.id, {0, fileSz}, pool_, queue_);

        readExec_.add(std::move(diskRead));
    }

    WaitQueue<BDesc> queue_;
    std::shared_ptr<BufferPool> pool_;
    ThreadExecutor readExec_;
    ThreadExecutor sendExec_;
    std::vector<FileInfo> info_;
    decltype(info_)::const_iterator fileIter_;
    SessionConfig conf_;
    std::vector<ScopedFd> targetFds_;
};

class RxSession
{
public:
    RxSession(SessionConfig conf)
    {
        pool_ = BufferPool::make(1u << 21, 35);
        targetFds_ = bindTargets(conf.targets);
    }

    ~RxSession() noexcept
    {
        finish();
    }

    void start(util::TransferRequest req)
    {
        createTargetFiles("tmp", req.config.fileInfo);

        auto fds = std::vector<ScopedFd>{ };
        auto fileMap = FdMap{ };
        for (const auto &item : req.config.fileInfo)
        {
            auto path = rootedPath(
                ".",
                item.path,
                item.targetSuffix);

            auto fd = ScopedFd{::open(path.c_str(), O_WRONLY | O_DIRECT)};
            auto rawFd = fd.get();

            fds.push_back(std::move(fd));
            fileMap.insert({item.id, rawFd});
        }

        const auto view = std::views::transform(
            fds, [this](auto &&fd){ return Receiver{std::move(fd), queue_}; });

        auto receivers = std::vector<Receiver>{
            std::make_move_iterator(begin(view)),
            std::make_move_iterator(end(view))};

        recvExec_.add(std::move(receivers));

        writeExec_.add(Writer(std::move(fileMap), queue_));
    }

    void finish() noexcept
    {
        recvExec_.cancel();
        writeExec_.cancel();
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
    WaitQueue<BDesc> queue_;
    std::shared_ptr<BufferPool> pool_;
    ThreadExecutor recvExec_;
    ThreadExecutor writeExec_;
    std::vector<ScopedFd> targetFds_;
};

class InfoReceiver
{
public:
    explicit InfoReceiver(int fd):
        fd_(fd)
    {
    }

    bool runOnce()
    {
        if (buf_.size() - offset_ >= 4096)
            buf_.resize(buf_.size() + 4096);

        auto data = buf_.data() + offset_;
        auto sz = buf_.size() - offset_;

        auto len = ::recv(fd_, data, sz, 0);

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
        return deserializeTransferRequest(Buffer{buf_.data(), buf_.size()});
    }

private:
    std::vector<uint8_t> buf_{ };
    size_t offset_{ };
    int fd_{-1};
    bool haveInfo_{ };
};

TransferRequest awaitTransferRequest(ScopedFd fd)
{
    auto rx = InfoReceiver{fd.get()};

    while (!rx.runOnce())
        ;

    return rx.info();
}

void sendTransferRequest(int fd, const std::vector<FileInfo> &info)
{
    auto request = util::generateTransferRequestMsg(info);
    util::net::writeAll(fd, request.data(), request.size());
}

sig_atomic_t done_;

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

int recvCmd(int, char **)
{
    using namespace draft::util;

    spdlog::info("recv");

    auto conf = SessionConfig{
        {
            {"localhost", 5001},
            {"localhost", 5002},
            {"localhost", 5003},
        },
        {"localhost", 5000}
    };

    auto sess = RxSession(std::move(conf));

    auto req = awaitTransferRequest(
        net::bindTcp("localhost", 5000));

    sess.start(std::move(req));

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
    auto conf = SessionConfig{
        {
            {"localhost", 5001},
            {"localhost", 5002},
            {"localhost", 5003},
        },
        {"localhost", 5000}
    };
    auto sess = TxSession(std::move(conf));

    auto fd = net::connectTcp("localhost", 5000);
    sendTransferRequest(fd.get(), fileInfo);

    sess.start(path);

    while (sess.runOnce())
        std::this_thread::sleep_for(200ms);

    return 0;
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

    return 0;
}
