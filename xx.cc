#include "Util.hh"

namespace draft::util {

struct BDesc
{
    std::shared_ptr<BufferPool::Buffer> buf{ };
    unsigned fileId{ };
    size_t offset{ };
    size_t len{ };
};

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

    Reader(int fd, unsigned fileId, Segment segment, const BufferPoolPtr &pool, WaitQueue<BDesc> *queue):
        fd_{fd},
        segment_{segment},
        pool_{pool},
        queue_{queue},
        fileId_{fileId}
    {
    }

    void runOnce()
    {
        auto buf = std::make_unique<Buffer>(pool_->get());

        auto len = read(*buf);

        queue_->put({
            std::move(buf),
            fileId_,
            segment_.offset,
            len
        });

        segment_.offset += len;
        segment_.len -= len;
    }

private:
    using Queue = WaitQueue<BDesc>;

    size_t read(Buffer &buf)
    {
        auto len = roundBlockSize(segment_.len - segment_.offset);
        len = std::min(len, buf.size());

        return readChunk(fd_, buf.data(), len, segment_.offset);
    }

    int fd_{-1};
    Segment segment_{ };
    BufferPoolPtr pool_{ };
    Queue *queue_{ };
    unsigned fileId_{ };
};

class Sender
{
public:
    void runOnce()
    {
        //auto desc = queue.get();
        //write(desc);
    }
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

int main()
{
    return 0;
}
