/* @file Protocol.hh
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

#ifndef __DRAFT_PROTOCOL_HH__
#define __DRAFT_PROTOCOL_HH__

#include <cstdint>

namespace draft::wire {

/**
 * Control protocol frame header.
 */
struct Frame
{
    static constexpr uint64_t Magic = 0x55aa'aa55'c721'a000;
    static constexpr uint64_t MagicVersionMask = 0xfff;
    static constexpr uint64_t MagicMask = ~MagicVersionMask;

    uint64_t magic{ };
    uint64_t payloadLength{ };
    uint8_t pad0[4]{ };
};

static_assert(sizeof(Frame) == 24);
static_assert(alignof(Frame) == 8);

struct ChunkHeader
{
    enum Flag
    {
        More = 1
    };

    static constexpr size_t BlockSize = 4096u;

    static constexpr uint64_t Magic = 0x55aa'aa55'da7a'0000;
    static constexpr uint64_t MagicVersionMask = 0xffff;
    static constexpr uint64_t MagicMask = ~MagicVersionMask;

    uint64_t magic{ };
    uint64_t fileOffset{ };
    uint64_t payloadLength{ };
    uint16_t fileId{ };
    uint8_t flags{ };
    uint8_t pad0[3]{ };
    uint8_t pad_align[BlockSize - 32]{ };
};

constexpr size_t UnalignedChunkHeaderSize =
    sizeof(ChunkHeader) - sizeof(ChunkHeader::pad_align);

static_assert(sizeof(ChunkHeader) == ChunkHeader::BlockSize);
static_assert(alignof(ChunkHeader) == 8);

}

#endif
