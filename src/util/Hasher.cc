/**
 * @file Hasher.cc
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

#include "xxhash.h"

#include <draft/util/Journal.hh>
#include <draft/util/Hasher.hh>
#include <draft/util/ScopedTimer.hh>
#include <draft/util/Stats.hh>

namespace draft::util {

Hasher::Hasher(BufQueue &queue, const std::shared_ptr<Journal> &hashLog):
    queue_(&queue),
    hashLog_(hashLog)
{
}

bool Hasher::runOnce(std::stop_token stopToken)
{
    using namespace std::chrono_literals;

    using Clock = std::chrono::steady_clock;

    while (auto desc = queue_->get(Clock::now() + 1ms))
    {
        // TODO: maybe this for hashes?
        //if (auto s = stats(desc->fileId))
        //    ++s->dequeuedBlockCount;

        if (!desc->buf)
            continue;

        auto digest = uint64_t{ };

        {
            auto timer = util::ScopedTimer{[&desc](double sec) {
                    spdlog::info("xx3 file {} offset {} len {} - {:.06f} sec"
                        , desc->fileId
                        , desc->offset
                        , desc->len
                        , sec);
                }};

            digest = hash(*desc);

            hashLog_->writeHash(
                desc->fileId,
                desc->offset,
                desc->len,
                digest);
        }

        spdlog::info("hash: {:#x}", digest);
    }

    return !stopToken.stop_requested();
}

uint64_t Hasher::hash(const BDesc &desc)
{
    if (!desc.buf)
        return 0;

    return XXH3_64bits(desc.buf->data(), desc.buf->size());
}

}
