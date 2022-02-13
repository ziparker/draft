/**
 * @file IOVec.hh
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

#ifndef __DRAFT_UTIL_IOVEC_HH__
#define __DRAFT_UTIL_IOVEC_HH__

#include <algorithm>
#include <cstring>
#include <memory>

#include <sys/uio.h>

namespace draft::util {

/**
 * An iovec wrapper which allocates a fixed amount of space in-line for iovecs,
 * and uses heap memory for longer vectors.
 */
template <size_t N>
struct IOVec_
{
public:
    /**
     * Allocate an iovec array of the specified length.
     *
     * @param len The length of iovec array to create.
     */
    explicit IOVec_(size_t len)
    {
        struct iovec *dst = nullptr;

        if (len <= N)
        {
            dst = iovs_.stack;
        }
        else
        {
            iovs_.heap = std::make_unique<struct iovec[]>(len);
            dst = iovs_.heap.get();
        }

        std::memset(dst, 0, len * sizeof(struct iovec));
        count_ = len;
    }

    /**
     * Make a copy of the specified iovec array.
     *
     * @param iov The iovec array to copy.
     * @param len The length of the specified iovec array.
     */
    IOVec_(const struct iovec *iov, size_t len):
        IOVec_(len)
    {
        std::copy_n(iov, len, len <= N ? iovs_.stack : iovs_.heap.get());
        count_ = len;
    }

    /**
     * Destroy the underlying iovec array, if necessary.
     *
     * This does not free any memory pointed to by the elements of the iovec
     * array.
     */
    ~IOVec_() noexcept
    {
        using std::unique_ptr;

        if (count_ > N)
            iovs_.heap.~unique_ptr<struct iovec[]>();
    }

    /**
     * Get a pointer to the managed iovec array.
     *
     * @return A pointer to the sart of the underlying iovec array.
     */
    struct iovec *get()
    {
        if (count_ <= N)
            return iovs_.stack;

        return iovs_.heap.get();
    }

    /**
     * Get a pointer to the managed iovec array.
     *
     * @return A pointer to the sart of the underlying iovec array.
     */
    const struct iovec *get() const
    {
        if (count_ <= N)
            return iovs_.stack;

        return iovs_.heap;
    }

    size_t count() const noexcept
    {
        return count_;
    }

private:
    union Data {
        struct iovec stack[N];
        std::unique_ptr<struct iovec[]> heap;

        ~Data() noexcept { }
    };

    Data iovs_ { };
    size_t count_ { };
};

/**
 * Name the default IOVec_ type, with default length.
 */
using IOVec = IOVec_<10>;

}

#endif
