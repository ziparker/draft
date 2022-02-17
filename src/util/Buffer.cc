/**
 * @file Buffer.cc
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

#include <cstring>

#include <draft/util/Buffer.hh>

namespace draft::util {

Buffer::Buffer(std::size_t size):
    data_(std::malloc(size)),
    size_(size)
{
    if (!data_)
        throw std::bad_alloc();
}

Buffer::Buffer(const std::vector<uint8_t> &vec):
    Buffer(vec.data(), vec.size())
{
}

Buffer::Buffer(const void *data, std::size_t size)
{
    if (!data || !size)
        return;

    auto p = std::malloc(size);
    if (!p)
        throw std::bad_alloc();

    data_ = p;
    size_ = size;

    std::memcpy(data_, data, size_);
}

Buffer &Buffer::operator=(const Buffer &o)
{
    if (this == &o)
        return *this;

    resize(o.size_);

    std::memcpy(data_, o.data_, size_);

    return *this;
}

Buffer &Buffer::operator=(Buffer &&o) noexcept
{
    if (this == &o)
        return *this;

    std::free(data_);

    data_ = o.data_;
    size_ = o.size_;
    o.data_ = nullptr;
    o.size_ = 0u;

    return *this;
}

Buffer::~Buffer() noexcept
{
    std::free(data_);
    data_ = nullptr;
    size_ = 0;
}

void Buffer::resize(std::size_t size)
{
    auto p = std::realloc(data_, size);

    if (!p)
    {
        std::free(data_);
        size_ = 0;
        throw std::runtime_error("Buffer: realloc failed");
    }

    data_ = p;
    size_ = size;
}

std::vector<uint8_t> Buffer::vector() const
{
    return {uint8Data(), uint8Data() + size_};
}

}
