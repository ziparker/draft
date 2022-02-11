/**
 * @file ScopedMMap.cc
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

#include <errno.h>

#include <draft/util/ScopedMMap.hh>

namespace draft::util {

ScopedMMap ScopedMMap::map(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    auto p = ::mmap(addr, len, prot, flags, fildes, off);

    if (p == MAP_FAILED)
        throw std::system_error(errno, std::system_category(), "draft::ScopedMMap");

    return {p, len};
}

ScopedMMap::~ScopedMMap() noexcept
{
    unmap();
}

ScopedMMap::ScopedMMap(ScopedMMap &&o) noexcept
{
    *this = std::move(o);
}

ScopedMMap &ScopedMMap::operator=(ScopedMMap &&o) noexcept
{
    using std::swap;

    unmap();

    swap(o.addr_, addr_);
    swap(o.len_, len_);

    return *this;
}

int ScopedMMap::unmap() noexcept
{
    auto stat = 0;

    if (addr_)
        stat = ::munmap(addr_, len_);

    addr_ = nullptr;
    len_ = 0;

    return stat;
}

void *ScopedMMap::data(size_t offset) const noexcept
{
    return uint8Data(offset);
}

uint8_t *ScopedMMap::uint8Data(size_t offset) const noexcept
{
    return reinterpret_cast<uint8_t *>(addr_) + offset;
}

size_t ScopedMMap::size() const noexcept
{
    return len_;
}

bool ScopedMMap::offsetValid(size_t offset) const noexcept
{
    return addr_ && offset < len_;
}

ScopedMMap::ScopedMMap(void *addr, size_t len) noexcept:
    addr_(addr),
    len_(len)
{
}

}
