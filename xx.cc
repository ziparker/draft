#include <signal.h>

#include "Util.hh"

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

    Writer(int fd, BufQueue &queue):
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
        iovec iov{
            desc.buf->data(),
            desc.len
        };

        return writeChunk(fd_, &iov, 1, desc.offset);
    }

    BufQueue *queue_{ };
    int fd_{-1};
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
    void add(T &&runnable)
    {
        runq_.push_back(
            std::make_unique<Runnable_<T>>(std::forward<T>(runnable)));
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

sig_atomic_t done_;

void handleSig(int)
{
    done_ = 1;
}

}

// readers -> queue -> Senders -> net -> receivers -> queue -> writers -> disk

int main(int, char **argv)
{
    using namespace draft::util;
    namespace fs = std::filesystem;

    struct sigaction action{ };
    action.sa_handler = handleSig;
    sigaction(SIGINT, &action, nullptr);

    auto filename = std::string{argv[1]};
    auto pool = BufferPool::make(1u << 21, 35);
    auto queue = WaitQueue<BDesc>{ };

    auto netFd = net::connectTcp("localhost", 5000);
    auto sender = Sender(netFd.get(), queue);

    auto fd = ScopedFd{::open(filename.c_str(), O_RDONLY | O_DIRECT)};
    auto fileSz = fs::file_size(filename);

    auto diskRead = Reader(fd.get(), 1, {0, fileSz}, pool, queue);

    auto diskReaders = Executor{
        std::move(diskRead)
    };

    auto netSenders = Executor{
        std::move(sender)
    };

    while (!done_)
    {
        diskReaders.runOnce();
        netSenders.runOnce();
    }

    netSenders.runOnce();

    return 0;
}
