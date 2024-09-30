/**
 * @file TxSession.hh
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

#ifndef __DRAFT_UTIL_TX_SESSION_HH__
#define __DRAFT_UTIL_TX_SESSION_HH__

#include <memory>
#include <string>
#include <vector>

#include "TaskPool.hh"
#include "ThreadExecutor.hh"
#include "Util.hh"

namespace draft::util {

class Journal;

class TxSession
{
public:
    TxSession(SessionConfig conf);
    ~TxSession() noexcept;

    void start(const std::string &path);
    void finish() noexcept;

    bool runOnce();

private:
    using file_info_iter_type = std::vector<FileInfo>::const_iterator;

    file_info_iter_type nextFile(file_info_iter_type first, file_info_iter_type last);

    bool startFile(const FileInfo &info);

    WaitQueue<BDesc> queue_;
    std::shared_ptr<BufferPool> pool_;
    TaskPool readExec_;
    std::vector<std::future<int>> readResults_;
    ThreadExecutor sendExec_;
    std::vector<FileInfo> info_;
    std::vector<FileInfo>::const_iterator fileIter_;
    SessionConfig conf_;
    std::vector<ScopedFd> targetFds_;
    std::shared_ptr<Journal> journal_;
};

}

#endif
