/* @file gtest_draft.cc
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

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <ranges>
#include <regex>
#include <string>

#include <strings.h>
#include <sys/eventfd.h>

#include <spdlog/spdlog.h>

#include <draft/util/PollSet.hh>
#include <draft/util/ScopedTempFile.hh>
#include <draft/util/Util.hh>

////////////////////////////////////////////////////////////////////////////////
// Util

using namespace draft::util;

// InfoReceiver.hh
// Protocol.hh
// Reader.hh
// Receiver.hh
// RxSession.hh
// Sender.hh
// TaskPool.hh
// ThreadExecutor.hh
// TxSession.hh
// Util.hh
// UtilJson.hh
// WaitQueue.hh
// Writer.hh

////////////////////////////////////////////////////////////////////////////////
// Buffer

TEST(buffer, default_ctor)
{
    auto b = Buffer{ };

    EXPECT_EQ(b.size(), 0u);
    EXPECT_EQ(b.data(), nullptr);
    EXPECT_EQ(b.uint8Data(), nullptr);
}

TEST(buffer, size_ctor)
{
    const auto size = 16u;
    auto b = Buffer{size};

    EXPECT_EQ(b.size(), size);
    EXPECT_NE(b.data(), nullptr);
}

TEST(buffer, size_ctor_invalid)
{
    const auto size = std::numeric_limits<size_t>::max();
    EXPECT_THROW(Buffer{size}, std::bad_alloc);
}

TEST(buffer, data_access)
{
    const auto size = 16u;
    auto b = Buffer{size};

    ASSERT_NE(b.uint8Data(), nullptr);

    std::memset(b.data(), 0x55, b.size());
    EXPECT_EQ(*b.uint8Data(), 0x55);
    EXPECT_EQ(*(b.uint8Data() + size - 1), 0x55);
}

TEST(buffer, vector)
{
    const auto size = 16u;
    auto b = Buffer{size};

    ASSERT_NE(b.uint8Data(), nullptr);
    std::memset(b.data(), 0x55, b.size());

    const auto v = b.vector();

    ASSERT_EQ(b.size(), v.size());
    EXPECT_EQ(0, bcmp(v.data(), b.data(), b.size()));
}

TEST(buffer, vec_ctor)
{
    const auto size = 16u;

    auto v = std::vector<uint8_t>{ };
    v.resize(size);
    std::memset(v.data(), 0x55, v.size());

    auto b = Buffer(v);

    ASSERT_NE(b.data(), nullptr);
    ASSERT_EQ(b.size(), v.size());
    EXPECT_EQ(0, bcmp(v.data(), b.data(), b.size()));
}

TEST(buffer, raw_ctor)
{
    const auto size = 16u;

    auto v = std::vector<uint8_t>{ };
    v.resize(size);
    std::memset(v.data(), 0x55, v.size());

    auto b = Buffer(v.data(), v.size());

    ASSERT_NE(b.data(), nullptr);
    ASSERT_EQ(b.size(), v.size());
    EXPECT_EQ(0, bcmp(v.data(), b.data(), b.size()));
}

TEST(buffer, raw_ctor_null)
{
    auto b = Buffer(nullptr, 0);

    EXPECT_EQ(b.data(), nullptr);
    EXPECT_EQ(b.size(), 0u);

    auto v = b.vector();
    EXPECT_TRUE(v.empty());
}

TEST(buffer, raw_ctor_zero_sz)
{
    const auto size = 16u;

    auto src = std::vector<uint8_t>{ };
    src.resize(size);
    std::memset(src.data(), 0x55, src.size());

    auto b = Buffer(src.data(), 0);

    EXPECT_EQ(b.data(), nullptr);
    EXPECT_EQ(b.size(), 0u);

    auto v = b.vector();
    EXPECT_TRUE(v.empty());
}

TEST(buffer, copy_ctor)
{
    const auto size = 16u;

    auto b1 = Buffer{size};
    ASSERT_EQ(b1.size(), size);
    std::memset(b1.data(), 0x55, b1.size());

    auto b2 = b1;
    EXPECT_EQ(b1.size(), b2.size());
    EXPECT_EQ(0, bcmp(b1.data(), b2.data(), b2.size()));
}

TEST(buffer, copy_assign)
{
    const auto size = 16u;

    auto b1 = Buffer{size};
    ASSERT_EQ(b1.size(), size);
    std::memset(b1.data(), 0x55, b1.size());

    auto b2 = Buffer{ };
    b2 = b1;
    EXPECT_EQ(b1.size(), b2.size());
    EXPECT_EQ(0, bcmp(b1.data(), b2.data(), b2.size()));
}

TEST(buffer, move_ctor)
{
    const auto size = 16u;

    auto b1 = Buffer{size};
    ASSERT_EQ(b1.size(), size);
    std::memset(b1.data(), 0x55, b1.size());

    auto b2 = std::move(b1);
    ASSERT_NE(b2.data(), nullptr);
    EXPECT_EQ(b2.size(), size);
    EXPECT_EQ(*b2.uint8Data(), 0x55);
    EXPECT_EQ(*(b2.uint8Data() + size - 1), 0x55);
}

TEST(buffer, move_assign)
{
    const auto size = 16u;

    auto b1 = Buffer{size};
    ASSERT_EQ(b1.size(), size);
    std::memset(b1.data(), 0x55, b1.size());

    auto b2 = Buffer{ };
    b2 = std::move(b1);
    ASSERT_NE(b2.data(), nullptr);
    EXPECT_EQ(b2.size(), size);
    EXPECT_EQ(*b2.uint8Data(), 0x55);
    EXPECT_EQ(*(b2.uint8Data() + size - 1), 0x55);
}

TEST(buffer, resize_up)
{
    const auto size1 = 16u;
    const auto size2 = 32u;

    {
        auto b = Buffer{size1};
        ASSERT_EQ(b.size(), size1);
        std::memset(b.data(), 0x55, b.size());

        b.resize(size2);
        ASSERT_EQ(b.size(), size2);
        std::memset(b.uint8Data() + size1, 0xAA, size2 - size1);

        EXPECT_EQ(*b.uint8Data(), 0x55);
        EXPECT_EQ(*(b.uint8Data() + b.size() - 1), 0xAA);
    }
}

TEST(buffer, resize_down)
{
    const auto size1 = 16u;
    const auto size2 = 32u;

    {
        auto b = Buffer{size2};
        ASSERT_EQ(b.size(), size2);
        std::memset(b.data(), 0x55, b.size());

        b.resize(size1);
        ASSERT_EQ(b.size(), size1);
        std::memset(b.uint8Data(), 0xAA, b.size());

        EXPECT_EQ(*(b.uint8Data() + b.size() - 1), 0xAA);
    }
}

////////////////////////////////////////////////////////////////////////////////
// BufferPool

TEST(buffer_pool, get)
{
    auto pool = BufferPool::make(64, 10);

    auto buf = pool->get();
    ASSERT_NE(buf.data(), nullptr);

    EXPECT_EQ(buf.size(), 64u);
    EXPECT_TRUE(buf);

    auto p1 = buf.uint8Data();
    auto p2 = buf.uint8Data() + buf.size() - 1;
    *p1 = 0x42;
    *p2 = 0x42;

    EXPECT_EQ(*p1, *p2);
}

TEST(buffer_pool, get_tmo)
{
    using namespace std::chrono_literals;

    // 0-sized pools not allowed, so create & consume initial buffer.
    auto pool = BufferPool::make(64, 1);

    auto buf = pool->get();
    auto buf2 = pool->get(std::chrono::steady_clock::now() + 1ns);
    EXPECT_TRUE(buf);
    EXPECT_FALSE(buf2);
    EXPECT_EQ(nullptr, buf2.data());
}

TEST(buffer_pool, buf_move_ctor)
{
    auto pool = BufferPool::make(64, 10);

    auto buf = pool->get();
    ASSERT_NE(buf.data(), nullptr);

    const auto size = buf.size();

    *buf.uint8Data() = 0x42;

    auto buf2 = std::move(buf);
    EXPECT_EQ(buf2.size(), size);
    EXPECT_EQ(*buf2.uint8Data(), 0x42);
}

TEST(buffer_pool, buf_move_assign)
{
    auto pool = BufferPool::make(64, 10);

    auto buf = pool->get();
    ASSERT_NE(buf.data(), nullptr);

    const auto size = buf.size();

    *buf.uint8Data() = 0x42;

    auto buf2 = BufferPool::Buffer{ };
    buf2 = std::move(buf);
    EXPECT_EQ(buf2.size(), size);
    EXPECT_EQ(*buf2.uint8Data(), 0x42);
}

TEST(buffer_pool, deplete)
{
    using namespace std::chrono_literals;

    const size_t size = 64;
    const size_t count = 5;

    auto pool = BufferPool::make(size, count);

    auto bufs = std::vector<BufferPool::Buffer>{ };

    for (size_t i = 0; i < count; ++i)
    {
        auto buf = pool->get();
        EXPECT_TRUE(buf);
        EXPECT_NE(buf.data(), nullptr);
        EXPECT_EQ(buf.size(), size);
        bufs.push_back(std::move(buf));
    }

    auto fut = std::async(std::launch::async, [&] { return pool->get(); });

    // run for a while to make sure the pool get times-out :/
    EXPECT_EQ(fut.wait_for(10ms), std::future_status::timeout);

    // return a buffer to the queue.
    bufs.pop_back();

    ASSERT_EQ(fut.wait_for(100ms), std::future_status::ready);

    auto buf = fut.get();
    EXPECT_TRUE(buf);
    EXPECT_EQ(buf.size(), size);
    EXPECT_NE(buf.data(), nullptr);
}

TEST(buffer_pool, put_all)
{
    const size_t size = 64;
    const size_t count = 5;

    auto pool = BufferPool::make(size, count);

    auto bufs = std::vector<BufferPool::Buffer>{ };

    for (size_t i = 0; i < count; ++i)
        bufs.push_back(pool->get());

    // release all bufs, so they should return to the pool.
    bufs.clear();

    // verify that we can pull all of the buffers back out again.
    for (size_t i = 0; i < count; ++i)
        bufs.push_back(pool->get());
}

////////////////////////////////////////////////////////////////////////////////
// PollSet

TEST(pollset, empty_poll)
{
    auto ps = PollSet{ };

    EXPECT_NO_THROW(ps.remove(0));
    EXPECT_TRUE(ps.empty());

    ps.waitOnce(1);
}

TEST(pollset, evt_poll)
{
    auto fd = ScopedFd(eventfd(0, EFD_CLOEXEC));
    ASSERT_GE(fd.get(), 0);

    int evt{ };
    unsigned events{ };

    auto ps = PollSet{ };
    ps.add(fd.get(), EPOLLIN, [&evt, &events](unsigned flags){ events = flags, ++evt; return true; });

    EXPECT_FALSE(ps.empty());

    // with no events, expect a timeout.
    auto count = ps.waitOnce(10);
    EXPECT_EQ(count, 0);

    const auto postEvent = [&] {
        // post an event.
        auto v = uint64_t{42};
        ASSERT_EQ(::write(fd.get(), &v, sizeof(v)), sizeof(v));
    };

    const auto checkEvent = [&](int expectedWaitCount, int expectedEvtCount) {
        // expect non-zero event count & updated event flag.
        count = ps.waitOnce(10);
        EXPECT_EQ(count, expectedWaitCount);
        EXPECT_EQ(events, EPOLLIN);

        EXPECT_EQ(evt, expectedEvtCount);
    };

    postEvent();
    checkEvent(1, 1);

    postEvent();
    postEvent();
    // only 1 descriptor is ready, callback invoked once.
    checkEvent(1, 2);

    // remove callback.
    ps.remove(fd.get());

    // expect no change in event count.
    postEvent();
    checkEvent(0, 2);
}

TEST(pollset, evt_poll_multi)
{
    auto fd = ScopedFd(eventfd(0, EFD_CLOEXEC));
    auto fd2 = ScopedFd(eventfd(0, EFD_CLOEXEC));
    ASSERT_GE(fd.get(), 0);
    ASSERT_GE(fd2.get(), 0);

    int evt[2]{ };
    unsigned events[2]{ };

    auto ps = PollSet{ };

    // add a handler that sticks around forever.
    ps.add(fd.get(), EPOLLIN,
        [&evt, &events](unsigned flags) {
            events[0] = flags, ++evt[0]; return true;
        });

    // expect exception in trying to add a second handler for an existing
    // descriptor.
    auto cb = [&evt, &events](unsigned flags) {
            events[1] = flags, ++evt[1]; return false;
        };

    EXPECT_THROW(ps.add(fd.get(), EPOLLIN, cb), std::runtime_error);

    ps.add(fd2.get(), EPOLLIN, cb);

    const auto postEvent = [&] {
        auto v = uint64_t{42};
        ASSERT_EQ(::write(fd.get(), &v, sizeof(v)), sizeof(v));
        ASSERT_EQ(::write(fd2.get(), &v, sizeof(v)), sizeof(v));
    };

    postEvent();
    ps.waitOnce(1);
    EXPECT_EQ(evt[0], 1);
    EXPECT_EQ(evt[1], 1);
    EXPECT_EQ(events[0], EPOLLIN);
    EXPECT_EQ(events[1], EPOLLIN);

    postEvent();
    ps.waitOnce(1);
    EXPECT_EQ(evt[0], 2);
    EXPECT_EQ(evt[1], 1);
    EXPECT_EQ(events[0], EPOLLIN);
    EXPECT_EQ(events[1], EPOLLIN);
}

TEST(pollset, evt_poll_callback)
{
    auto fd = ScopedFd(eventfd(0, EFD_CLOEXEC));
    ASSERT_GE(fd.get(), 0);

    int evt{ };
    unsigned events{ };

    auto ps = PollSet{ };
    ps.add(fd.get(), EPOLLIN,
        [&evt, &events](unsigned flags) {
            events = flags, ++evt; return true;
        });

    // post an event.
    auto v = uint64_t{42};
    ASSERT_EQ(::write(fd.get(), &v, sizeof(v)), sizeof(v));

    auto count = ps.waitOnce(1,
        [&evt, &events](const std::vector<epoll_event> &evs) {
            ASSERT_FALSE(evs.empty());
            events = evs[0].events;
            ++evt;
        });

    ASSERT_EQ(count, 1);

    EXPECT_EQ(evt, 1);
    EXPECT_EQ(events, EPOLLIN);
}

////////////////////////////////////////////////////////////////////////////////
// IOVec

TEST(iovec, stack_no_copy)
{
    IOVec_<5> iov(5);
    EXPECT_EQ(iov.count(), 5u);

    auto p = iov.get();

    ASSERT_NE(p, nullptr);
}

TEST(iovec, heap_no_copy)
{
    IOVec_<5> iov(6);
    EXPECT_EQ(iov.count(), 6u);

    auto p = iov.get();

    ASSERT_NE(p, nullptr);
}

TEST(iovec, stack_copy)
{
    struct iovec iovs[3] { };
    const auto iovsCount = sizeof(iovs)/sizeof(*iovs);

    iovs[0].iov_base = iovs;
    iovs[0].iov_len = sizeof(iovs);

    IOVec_<5> iov(iovs, iovsCount);
    auto p = iov.get();

    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p[0].iov_base, iovs[0].iov_base);
    EXPECT_EQ(p[0].iov_len, iovs[0].iov_len);
    EXPECT_EQ(iov.count(), iovsCount);
}

////////////////////////////////////////////////////////////////////////////////
// ScopedFd

namespace {

bool fdOpened(int fd)
{
    return std::filesystem::exists(
        fmt::format("/proc/self/fd/{}", fd));
}

}

TEST(scoped_fd, ctor)
{
    auto fd = ScopedFd{ };
    EXPECT_EQ(fd.get(), -1);

    fd = ScopedFd{-1};
    EXPECT_EQ(fd.get(), -1);

    fd = ScopedFd{42};
    EXPECT_EQ(fd.get(), 42);
    EXPECT_EQ(static_cast<int>(fd), 42);

    fd = ScopedFd(24);
    EXPECT_EQ(fd.get(), 24);
}

TEST(scoped_fd, dtor)
{
    int rawFd{-1};

    {
        auto fd = ScopedFd{::eventfd(0, EFD_CLOEXEC)};
        ASSERT_NE(fd.get(), -1);

        rawFd = fd.get();
        EXPECT_TRUE(fdOpened(rawFd));
    }

    EXPECT_FALSE(fdOpened(rawFd));
}

TEST(scoped_fd, close)
{
    auto fd = ScopedFd{::eventfd(0, EFD_CLOEXEC)};
    ASSERT_NE(fd.get(), -1);

    auto rawFd = fd.get();
    EXPECT_TRUE(fdOpened(rawFd));

    fd.close();

    EXPECT_FALSE(fdOpened(rawFd));
}

TEST(scoped_fd, release)
{
    int rawFd{-1};

    {
        auto fd = ScopedFd{::eventfd(0, EFD_CLOEXEC)};
        ASSERT_NE(fd.get(), -1);

        rawFd = fd.get();
        EXPECT_TRUE(fdOpened(rawFd));

        rawFd = fd.release();
    }

    EXPECT_TRUE(fdOpened(rawFd));
    ASSERT_EQ(::close(rawFd), 0);
}

////////////////////////////////////////////////////////////////////////////////
// ScopedMMap

namespace {

std::pair<size_t, size_t> findMappingRange(void *base)
{
    auto stream = std::ifstream("/proc/self/maps");

    if (!stream)
        return { };

    const auto rex = std::regex{"([0-9a-f]+)-([0-9a-f]+).*"};

    std::string line;
    while (std::getline(stream, line))
    {
        auto match = std::smatch{ };
        if (std::regex_match(line, match, rex))
        {
            if (match.size() < 2u)
                continue;

            const auto start = std::stoul(match[1], nullptr, 16);
            const auto end = std::stoul(match[2], nullptr, 16);

            if (start == reinterpret_cast<uintptr_t>(base))
                return {start, end};
        }
    }

    return { };
}

} // namespace

TEST(scoped_mmap, map)
{
    EXPECT_NO_THROW(ScopedMMap::map(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
}

TEST(scoped_mmap, unmap)
{
    const size_t size = 4096;

    auto sm = ScopedMMap::map(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    // look-up our anonymous mapping.
    auto base = sm.data();
    auto range = findMappingRange(base);
    ASSERT_NE(range.first, 0);

    // check range size.
    EXPECT_EQ(range.second - range.first, size);

    EXPECT_EQ(sm.unmap(), 0);

    // expect range to not be found after unmap().
    range = findMappingRange(base);
    EXPECT_EQ(range.first, 0);
}

TEST(scoped_mmap, scope_unmap)
{
    const size_t size = 4096;
    void *base{ };
    std::pair<size_t, size_t> range{ };

    {
        auto sm = ScopedMMap::map(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        // look-up our anonymous mapping.
        base = sm.data();
        range = findMappingRange(base);
        ASSERT_NE(range.first, 0);

        // check range size.
        EXPECT_EQ(range.second - range.first, size);
    }

    // expect range to not be found after end of scope.
    range = findMappingRange(base);
    EXPECT_EQ(range.first, 0);
}

TEST(scoped_mmap, data)
{
    const size_t size = 4096;
    auto sm = ScopedMMap::map(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    ASSERT_EQ(sm.size(), size);

    std::memset(sm.data(), 0x55, sm.size());
    EXPECT_EQ(*sm.uint8Data(), 0x55);

    EXPECT_TRUE(sm.offsetValid(0));
    EXPECT_TRUE(sm.offsetValid(size - 1));
    EXPECT_FALSE(sm.offsetValid(size));
}

////////////////////////////////////////////////////////////////////////////////
// FreeList

TEST(free_list, get)
{
    auto list = FreeList{10};

    for (size_t i = 0u; i < 10u; ++i)
        EXPECT_EQ(i, list.get());

    EXPECT_EQ(FreeList::End, list.get());
}

TEST(free_list, put_interleaved)
{
    auto list = FreeList{2};

    EXPECT_EQ(0, list.get());
    EXPECT_EQ(1, list.get());
    EXPECT_EQ(FreeList::End, list.get());

    list.put(0);
    list.put(1);
    EXPECT_EQ(1, list.get());
    EXPECT_EQ(0, list.get());
    EXPECT_EQ(FreeList::End, list.get());
}

////////////////////////////////////////////////////////////////////////////////
// WaitQueue

TEST(wait_q, put_get)
{
    auto q = WaitQueue<int>{ };
    q.put(42);

    auto v = q.get();
    ASSERT_TRUE(v);
    EXPECT_EQ(*v, 42);
}

////////////////////////////////////////////////////////////////////////////////
// ScopedTempFile

TEST(makeTempFile, create)
{
    namespace fs = std::filesystem;

    auto path = []{
            auto f = ScopedTempFile("foo", "bar");
            EXPECT_TRUE(fs::exists(f.path()));
            return f.path();
        }();

    EXPECT_FALSE(fs::exists(path));
}
