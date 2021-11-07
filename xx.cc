#include "Util.hh"

namespace draft::util {

struct BDesc
{
    BufferPool::Buffer buf{ };
    unsigned fileId{ };
    off_t offset{ };
    size_t len{ };
};

class Reader
{
public:
    struct Segment
    {
        off_t offset{ };
        size_t len{ };
    };

    Reader(int fd, unsigned fileId, Segment chunk, const BufferPoolPtr &pool, WaitQueue<BDesc> *queue):
        fd_{fd},
        segment_{segment},
        pool_(pool),
        queue_(queue)
        fileId_(fileId)
    {
    }

    void run()
    {
    }

    void runOnce()
    {
        auto buf = pool_->get();

        auto len = read(buf);
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
        auto desc = queue.get();
        write(desc);
    }
};

class Receiver
{
public:
    void runOnce()
    {
        auto buf = pool.get();
        auto len = read(buf);
        queue.put({std::move(buf), len});
    }
};

class Writer
{
public:
    void runOnce()
    {
        auto desc = queue.get();
        write(desc);
    }
};

}

// readers -> queue -> Senders -> net -> receivers -> queue -> writers -> disk

int main()
{
    return 0;
}
