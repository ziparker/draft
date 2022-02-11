/**
 * @file ScopedMMap.hh
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

#ifndef __DRAFT_UTIL_SCOPED_MMAP_HH__
#define __DRAFT_UTIL_SCOPED_MMAP_HH__

#include <cstdint>
#include <system_error>

#include <sys/mman.h>

namespace draft::util {

class ScopedMMap
{
public:
    static ScopedMMap map(void *addr, size_t len, int prot, int flags, int fildes, off_t off);

    ScopedMMap() = default;
    ~ScopedMMap() noexcept;
    ScopedMMap(const ScopedMMap &) = delete;
    ScopedMMap &operator=(const ScopedMMap &) = delete;
    ScopedMMap(ScopedMMap &&o) noexcept;
    ScopedMMap &operator=(ScopedMMap &&o) noexcept;

    int unmap() noexcept;

    void *data(size_t offset = 0) const noexcept;
    uint8_t *uint8Data(size_t offset = 0) const noexcept;

    size_t size() const noexcept;

    bool offsetValid(size_t offset) const noexcept;

private:
    ScopedMMap(void *addr, size_t len) noexcept;

    void *addr_{ };
    size_t len_{ };
};

}

#endif
