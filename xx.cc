#include "Util.hh"

namespace draft::util {

struct BDesc
{
    std::shared_ptr<BufferPool::Buffer> buf{ };
    unsigned fileId{ };
    size_t offset{ };
    size_t len{ };
};

using BufQueue = WaitQueue<BDesc>;

class Reader
{
public:
    struct Segment
    {
        size_t offset{ };
        size_t len{ };
    };

    using Buffer = BufferPool::Buffer;
    using BufferPtr = std::shared_ptr<BufferPool::Buffer>;

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
    Sender(int fd, BufQueue &queue):
        queue_(&queue),
        fd_(fd)
    {
    }
        
    void runOnce()
    {
        if (auto desc = queue_->get())
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
    void runOnce()
    {
        //auto buf = pool.get();
        //auto len = read(buf);
        //queue.put({std::move(buf), len});
    }
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
    auto fd = ScopedFd{::open(filename.c_str(), O_RDONLY | O_DIRECT)};
    auto fileSz = fs::file_size(filename);

    auto diskRead = Reader(fd.get(), 1, {0, fileSz}, pool, queue);

    while (diskRead.runOnce())
        ;

    return 0;
}
