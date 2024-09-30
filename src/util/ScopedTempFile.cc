/**
 * @file ScopedTempFile.cc
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2023 Zachary Parker
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

#include <draft/util/ScopedTempFile.hh>
#include <draft/util/Util.hh>

namespace draft::util {

ScopedTempFile::ScopedTempFile(std::string prefix, std::string suffix, int flags)
{
    auto [fd, path] = makeTempFile(std::move(prefix), std::move(suffix), flags);

    fd_ = std::move(fd);
    path_ = std::move(path);
}

ScopedTempFile::~ScopedTempFile() noexcept
{
    close();
}

int ScopedTempFile::fd() const noexcept
{
    return fd_.get();
}

ScopedFd ScopedTempFile::releaseFd() noexcept
{
    return ScopedFd{fd_.release()};
}

int ScopedTempFile::close() noexcept
{
    const auto stat = unlink();

    fd_.close();
    path_.clear();

    return stat;
}

int ScopedTempFile::unlink() noexcept
{
    if (::unlink(path_.c_str()))
        return -errno;

    return 0;
}

}
