/**
 * @file RxSession.hh
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

#ifndef __DRAFT_UTIL_RX_SESSION_HH__
#define __DRAFT_UTIL_RX_SESSION_HH__

#include <memory>
#include <vector>

#include "ThreadExecutor.hh"
#include "Util.hh"

namespace draft::util {

class Journal;

class RxSession
{
public:
    RxSession(SessionConfig conf);
    ~RxSession() noexcept;

    void start(util::TransferRequest req);
    void finish() noexcept;

    void truncateFiles();

    bool runOnce();

private:
    struct FileInfo
    {
        std::string path;
        ScopedFd fd;
        size_t size{ };
        mode_t mode{ };
    };

    std::pair<FdMap, std::vector<FileInfo>> createFiles(
        const util::TransferRequest &req);

    WaitQueue<BDesc> queue_;
    WaitQueue<BDesc> hashQueue_;
    std::shared_ptr<BufferPool> pool_;
    ThreadExecutor recvExec_;
    ThreadExecutor writeExec_;
    ThreadExecutor hashExec_;
    SessionConfig conf_;
    std::vector<ScopedFd> targetFds_;
    std::vector<FileInfo> fileInfo_;
    std::shared_ptr<Journal> journal_;
};

}

#endif
