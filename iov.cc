#include <gtest/gtest.h>

#include <sys/uio.h>

namespace {

class IovBuffer
{
public:
    void reset()
    {
        free_[0].iov_base = data_;
        free_[0].iov_len = size_;
        free_[1] = { };

        used_[0] = used_[1] = { };
    }

    void write(size_t len)
    {
        
    }

    iovec *used()
    {
        return used_;
    }

    iovec *unused()
    {
        return free_;
    }

private:
    void *data_{ };
    size_t size_{ };
    iovec free_[2]{ };
    iovec used_[2]{ };
};

}

TEST(iov, init)
{
    auto buf = IovBuffer{ };

    auto u = buf.used();
    EXPECT_NOT_NULL(u[0].iov_base);
}

TEST(iov, advance)
{
}
