/**
 * @file VerifySession.cc
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

#include <filesystem>
#include <iterator>

#include <sys/stat.h>

#include <draft/util/Hasher.hh>
#include <draft/util/Reader.hh>
#include <draft/util/ScopedTimer.hh>
#include <draft/util/ThreadExecutor.hh>
#include <draft/util/VerifySession.hh>

namespace draft::util {

VerifySession::VerifySession(Config conf):
    conf_(std::move(conf))
{
    readExec_.resize(1);
    readExec_.setQueueSizeLimit(10);

    hashQueue_.setSizeLimit(100);

    pool_ = BufferPool::make(BufSize, 35);
}

VerifySession::~VerifySession() noexcept
{
    finish();
}

void VerifySession::start(const std::string &journalPath)
{
    inputJournalPath_ = journalPath;
    const auto inputJournal = Journal{journalPath};

    journalFile_ = util::ScopedTempFile("journal_", ".draft", O_RDWR | O_CLOEXEC);
    fchmod(journalFile_.fd(), 0644);

    spdlog::debug("verify session: create temporary journal file, '{}'"
        , journalFile_.path());

    journal_ = Journal{journalFile_.path(), inputJournal.fileInfo()};

    // hashers are in a separate executor to make it easier to tell when read
    // execs finish.
    for (int i = 0; i < 2; ++i)
    {
        hashExec_.add(
            util::Hasher{
                hashQueue_,
                [this](const auto &digest) { handleHash(digest); }},
            ThreadExecutor::Options::DoFinalize);
    }

    fileIter_ = nextFile(begin(info_), end(info_));

    spdlog::info("verify session: {} files", info_.size());
}

void VerifySession::finish() noexcept
{
    spdlog::debug("verify session: cancelling read & hashing tasks.");

    readExec_.cancel();
    hashExec_.cancel();
}

bool VerifySession::finished() const
{
    return hashExec_.finished();
}

bool VerifySession::runOnce()
{
    using namespace std::chrono_literals;

    // process results from completed readers.
    for (auto &r : readResults_)
    {
        if (r.valid() && r.wait_for(0ns) == std::future_status::ready)
            r.get();
    }

    // remove completed readers from the results list.
    std::erase_if(readResults_, [](const auto &r) { return !r.valid(); });

    hashExec_.runOnce();

    // if there are more files to read, try to submit reads for them.
    // if our reader queue is full, we'll time-out and try again later.
    while (fileIter_ != end(info_) && startFile(*fileIter_))
    {
        // advance file iterator, skipping things we don't want to send,
        // like directories, and on to the next regular file.
        fileIter_ = nextFile(++fileIter_, end(info_));
    }

    // once we've finished submitting reads for all of our files, start
    // processing completions until we're done.
    if (fileIter_ == end(info_) && readResults_.empty())
    {
        spdlog::trace("waiting on xfer completion.");

        hashExec_.cancel();
        return !hashExec_.finished();
    }

    return true;
}

std::optional<JournalFileDiff> VerifySession::diff()
{
    if (!hashExec_.finished())
        return std::nullopt;

    const auto inputJournal = Journal{inputJournalPath_};

    return diffJournals(inputJournal, journal_);
}

VerifySession::file_info_iter_type VerifySession::nextFile(file_info_iter_type first, file_info_iter_type last)
{
    while (first != last && (!S_ISREG(first->status.mode) || !first->status.size))
        ++first;

    return first;
}

bool VerifySession::startFile(const FileInfo &info)
{
    using namespace std::chrono_literals;

    using Clock = std::chrono::steady_clock;

    const auto &filename = info.path;
    auto flags = O_RDONLY;

    if (conf_.useDirectIO)
        flags |= O_DIRECT;

    auto fd = std::make_shared<ScopedFd>(
        ScopedFd{::open(filename.c_str(), flags)});

    spdlog::debug("verifier opened file id {}: {} @ fd {}", info.id, filename, fd->get());

    auto fileSz = std::filesystem::file_size(filename);

    // try for a while to submit this reader.
    // we may time-out here if the network is bottlenecking things.
    const auto deadline = Clock::now() + 50ms;
    while (!readExec_.cancelled() && Clock::now() < deadline)
    {
        const auto rateDeadline = Clock::now() + 1ms;

        auto diskRead = Reader(fd, info.id, {0, fileSz}, pool_, nullptr);
        diskRead.setHashQueue(hashQueue_);

        if (auto future = readExec_.launch(std::move(diskRead)))
        {
            spdlog::info("submitted file {}", info.path);
            readResults_.push_back(std::move(*future));
            return true;
        }

        std::this_thread::sleep_until(rateDeadline);
    }

    spdlog::debug("start file {}: timed-out, will resubmit later on."
        , filename);

    return false;
}

void VerifySession::handleHash(const Hasher::DigestInfo &info)
{
    spdlog::info("hash info: {} @{}: {:#08x}"
        , info.fileId
        , info.offset
        , info.digest);

    journal_.writeHash(info.fileId, info.offset, info.size, info.digest);
}

}
