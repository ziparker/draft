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
        
    void runOnce()
    {
        using namespace std::chrono_literals;
        using Clock = std::chrono::steady_clock;

        while (auto desc = queue_->get(Clock::now() + 1ms))
            write(std::move(*desc));
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

    void runOnce()
    {
        if (!haveHeader_)
        {
            if (!readHeader())
                return;

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
    }

private:
    bool readHeader()
    {
        auto buf = reinterpret_cast<uint8_t *>(&header_) + offset_;
        auto sz = sizeof(header_) - offset_;

        auto len = ::read(fd_, buf, sz);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "read");

        if (!len)
            return false;

        if (static_cast<size_t>(len) + offset_ == sizeof(header_))
        {
            haveHeader_ = true;
            return true;
        }

        offset_ += static_cast<size_t>(len);

        return false;
    }

    bool read()
    {
        auto len = ::read(fd_, buf_.uint8Data() + offset_, buf_.size() - offset_);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "read");

        if (!len)
            return false;

        offset_ += static_cast<size_t>(len);

        if (offset_ == buf_.size())
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
    void runOnce()
    {
        //auto desc = queue.get();
        //write(desc);
    }
};

}

// readers -> queue -> Senders -> net -> receivers -> queue -> writers -> disk

int main(int, char **argv)
{
    using namespace draft::util;
    namespace fs = std::filesystem;

    auto filename = std::string{argv[1]};
    auto pool = BufferPool::make(1u << 21, 35);
    auto queue = WaitQueue<BDesc>{ };

    auto netFd = net::connectTcp("localhost", 5000);
    auto sender = Sender(netFd.get(), queue);

    auto fd = ScopedFd{::open(filename.c_str(), O_RDONLY | O_DIRECT)};
    auto fileSz = fs::file_size(filename);

    auto diskRead = Reader(fd.get(), 1, {0, fileSz}, pool, queue);

    while (diskRead.runOnce())
        sender.runOnce();

    sender.runOnce();

    return 0;
}
