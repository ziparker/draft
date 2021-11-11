#include <signal.h>
#include <sys/socket.h>

#include "Util.hh"
#include "UtilJson.hh"

namespace draft::util {

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

    Reader(int fd, unsigned fileId, Segment segment, const BufferPoolPtr &pool, BufQueue &queue):
        fd_{fd},
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

        return readChunk(fd_, buf.data(), len, segment_.offset);
    }

    int fd_{-1};
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
        while (auto desc = queue_->tryGet())
            write(std::move(*desc));

        return true;
    }

private:
    size_t write(BDesc desc)
    {
        auto header = wire::ChunkHeader{ };
        // TODO: fill-in info

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

    Receiver(int fd, BufQueue &queue):
        queue_(&queue),
        fd_(fd)
    {
    }

    bool runOnce()
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

private:
    int readHeader()
    {
        auto buf = reinterpret_cast<uint8_t *>(&header_) + offset_;
        auto sz = sizeof(header_) - offset_;

        auto len = ::read(fd_, buf, sz);

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
        auto len = ::read(fd_, buf_.uint8Data() + offset_, buf_.size() - offset_);

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
    int fd_{-1};
    bool haveHeader_{ };
};

class Writer
{
public:
    using Buffer = BufferPool::Buffer;

    // TODO: use fd map instead of fd
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

    void runOnce()
    {
        for (auto &r : runq_)
            r->runOnce();
    }

private:
    class Runnable
    {
    public:
        virtual ~Runnable() = default;
        virtual void runOnce() = 0;
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
        void runOnce() override
        {
            t_.runOnce();
        }

        T t_;
    };

    std::vector<std::unique_ptr<Runnable>> runq_;
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
void recvChunks(draft::util::FdMap fileMap, std::vector<draft::util::ScopedFd> receiverFds)
{
    using namespace draft::util;
    namespace fs = std::filesystem;

    // per-session:
    auto pool = BufferPool::make(1u << 21, 35);
    auto queue = WaitQueue<BDesc>{ };

    auto netReceivers = Executor{ };
    for (const auto &fd : receiverFds)
        netReceivers.add(Receiver(fd.get(), queue));

    auto diskWriter = Writer(std::move(fileMap), queue);

    auto diskWriters = Executor{
        std::move(diskWriter)
    };

    // recv file:
    while (!done_)
    {
        netReceivers.runOnce();
        diskWriters.runOnce();
    }

    diskWriters.runOnce();
}

int recvCmd(int, char **)
{
    using namespace draft::util;

    spdlog::info("recv");

    auto rxFds = std::vector<ScopedFd>{ };
    rxFds.push_back(net::bindTcp("localhost", 5000));

    auto info = awaitTransferRequest(
        net::bindTcp("localhost", 4000));

    createTargetFiles(".", info.config.fileInfo);

    auto fds = std::vector<ScopedFd>{ };
    auto fileMap = FdMap{ };
    for (const auto &item : info.config.fileInfo)
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

    recvChunks(std::move(fileMap), std::move(rxFds));

    return 0;
}

void sendFile(const std::string &filename, unsigned)
{
    using namespace draft::util;
    namespace fs = std::filesystem;

    // per-session:
    auto pool = BufferPool::make(1u << 21, 35);
    auto queue = WaitQueue<BDesc>{ };

    auto netFd = net::connectTcp("localhost", 5000);
    auto sender = Sender(netFd.get(), queue);

    auto netSenders = Executor{
        std::move(sender)
    };

    // per-file:
    auto fd = ScopedFd{::open(filename.c_str(), O_RDONLY | O_DIRECT)};
    auto fileSz = fs::file_size(filename);

    auto diskRead = Reader(fd.get(), 1, {0, fileSz}, pool, queue);

    auto diskReaders = Executor{
        std::move(diskRead)
    };

    // send file:
    while (!done_)
    {
        diskReaders.runOnce();
        netSenders.runOnce();
    }

    netSenders.runOnce();
}

int sendCmd(int, char **argv)
{
    using namespace draft::util;

    const auto path = std::string{argv[1]};
    spdlog::info("send path {}", path);

    auto fileInfo = getFileInfo(path);

    auto fd = net::connectTcp("localhost", 4000);
    sendTransferRequest(fd.get(), fileInfo);

    for (const auto &info : fileInfo)
    {
        if (S_ISDIR(info.status.mode))
            continue;

        sendFile(info.path, info.id);
    }

    return 0;
}

int main(int argc, char **argv)
{
    using namespace draft::util;

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
