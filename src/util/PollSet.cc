/**
 * @file PollSet.cc
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Zachary Parker
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

#include <string>

#include <errno.h>

#include <spdlog/spdlog.h>

#include <draft/util/PollSet.hh>

namespace draft::util {

PollSet::PollSet():
    epollFd_(epoll_create1(EPOLL_CLOEXEC))
{
    if (epollFd_.get() < 0)
        throw std::system_error(errno, std::system_category(), "PollSet: epoll_create1");
}

int PollSet::add(int fd, unsigned events, Callback &&cb)
{
    struct epoll_event evt{ };
    evt.events = events;
    evt.data.fd = fd;

    if (!members_.insert({fd, Member{std::move(cb)}}).second)
        throw std::runtime_error("PollSet::add: unable to add member fd " + std::to_string(fd));

    if (epoll_ctl(epollFd_.get(), EPOLL_CTL_ADD, fd, &evt))
        throw std::system_error(errno, std::system_category(), "PollSet::add: epoll_ctl_add");

    return 0;
}

int PollSet::remove(int fd)
{
    if (epoll_ctl(epollFd_.get(), EPOLL_CTL_DEL, fd, nullptr))
        spdlog::warn("PollSet::remove: epoll_ctl_del");

    members_.erase(fd);

    return 0;
}

int PollSet::waitOnce(int tmoMs, const EventCallback &cb)
{
    if (members_.empty())
        return 0;

    auto events = std::vector<struct epoll_event>{members_.size()};
    auto count = epoll_wait(epollFd_.get(), events.data(), static_cast<int>(events.size()), tmoMs);

    if (count < 0)
    {
        if (errno == EINTR)
            return 0;

        throw std::system_error(errno, std::system_category(), "PollSet::epoll_wait");
    }

    if (cb)
        cb(events);

    return count;
}

int PollSet::waitOnce(int tmoMs)
{
    if (members_.empty())
        return 0;

    auto events = std::vector<struct epoll_event>{members_.size()};
    auto count = epoll_wait(epollFd_.get(), events.data(), static_cast<int>(events.size()), tmoMs);

    if (count < 0)
    {
        if (errno == EINTR)
            return 0;

        throw std::system_error(errno, std::system_category(), "PollSet::epoll_wait");
    }

    for (size_t i = 0; i < static_cast<size_t>(count); ++i)
    {
        auto member = members_.find(events[i].data.fd);
        if (member == end(members_))
            continue;

        if (member->second.callback)
        {
            if (!member->second.callback(events[i].events))
                remove(member->first);
        }
    }

    return count;
}

}
