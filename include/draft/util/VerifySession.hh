/**
 * @file VerifySession.hh
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

#ifndef __DRAFT_UTIL_VERIFY_SESSION_HH__
#define __DRAFT_UTIL_VERIFY_SESSION_HH__

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Hasher.hh"
#include "Journal.hh"
#include "ScopedTempFile.hh"
#include "TaskPool.hh"
#include "ThreadExecutor.hh"
#include "Util.hh"

namespace draft::util {

class VerifySession
{
public:
    struct Config
    {
        bool useDirectIO{true};
    };

    explicit VerifySession(Config conf);
    ~VerifySession() noexcept;

    // TODO: consider restart cases, adjust to remove 2 stage start.
    void start(const Journal &input);
    void start(std::vector<FileInfo> input);
    void finish() noexcept;
    bool finished() const;

    bool runOnce();

    // once finished, return the diff result.
    std::optional<JournalFileDiff> diff();
    std::optional<Journal> releaseJournal() &&;

private:
    using file_info_iter_type = std::vector<FileInfo>::const_iterator;

    file_info_iter_type nextFile(file_info_iter_type first, file_info_iter_type last);

    bool startFile(const FileInfo &info);
    void handleHash(const Hasher::DigestInfo &info);

    WaitQueue<BDesc> hashQueue_;
    std::shared_ptr<BufferPool> pool_;
    TaskPool readExec_;
    std::vector<std::future<int>> readResults_;
    ThreadExecutor hashExec_;
    std::vector<FileInfo> info_;
    std::vector<FileInfo>::const_iterator fileIter_;
    Config conf_;
    util::ScopedTempFile journalFile_;
    Journal journal_;
    std::string inputJournalPath_;
};

}

#endif
