/**
 * @file ScopedFd.hh
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

#ifndef __DRAFT_UTIL_SCOPED_FD_HH__
#define __DRAFT_UTIL_SCOPED_FD_HH__

#include <unistd.h>

namespace draft::util {

class ScopedFd
{
public:
    ScopedFd() = default;

    explicit ScopedFd(int fd) noexcept:
        fd_{fd}
    {
    }

    ~ScopedFd() noexcept
    {
        close();
    }

    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;

    ScopedFd(ScopedFd &&o) noexcept
    {
        *this = std::move(o);
    }

    ScopedFd &operator=(ScopedFd &&o) noexcept
    {
        close();
        std::swap(o.fd_, fd_);
        return *this;
    }

    int close() noexcept
    {
        auto stat = 0;

        if (fd_ >= 0)
            stat = ::close(fd_);

        fd_ = -1;

        return stat;
    }

    int get() const noexcept
    {
        return fd_;
    }

    int release() noexcept
    {
        auto fd = fd_;

        fd_ = -1;

        return fd;
    }

    explicit operator int() const noexcept
    {
        return fd_;
    }

private:
    int fd_{-1};
};

}

#endif
