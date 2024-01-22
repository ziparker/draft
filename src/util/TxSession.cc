/**
 * @file TxSession.cc
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

#include <filesystem>
#include <iterator>

#include <sys/stat.h>

#include <draft/util/Journal.hh>
#include <draft/util/Reader.hh>
#include <draft/util/ScopedTimer.hh>
#include <draft/util/Sender.hh>
#include <draft/util/ThreadExecutor.hh>
#include <draft/util/TxSession.hh>

namespace draft::util {

TxSession::TxSession(SessionConfig conf):
    conf_(std::move(conf))
{
    readExec_.resize(1);
    readExec_.setQueueSizeLimit(10);

    queue_.setSizeLimit(100);

    pool_ = BufferPool::make(BufSize, 35);
    targetFds_ = connectNetworkTargets(conf_.targets);

    spdlog::info("connected tx targets.");
}

TxSession::~TxSession() noexcept
{
    finish();
}

void TxSession::start(const std::string &path)
{
    namespace fs = std::filesystem;

    // TODO: should we also own the rx service connection, and send xfer
    // request here?

    const auto view = std::views::transform(
        targetFds_, [this](auto &&fd){ return Sender{std::move(fd), queue_}; });

    auto senders = std::vector<Sender>{
        std::make_move_iterator(begin(view)),
        std::make_move_iterator(end(view))};

    info_ = getFileInfo(path);

    if (!conf_.journalPath.empty())
    {
        journal_ = std::make_unique<Journal>(conf_.journalPath, info_);

        for (auto &sender : senders)
            sender.useHashLog(journal_);
    }

    // clear targets since we've moved them into senders.
    targetFds_ = std::vector<ScopedFd>{ };

    sendExec_.add(std::move(senders), ThreadExecutor::Options::DoFinalize);

    fileIter_ = nextFile(begin(info_), end(info_));
}

void TxSession::finish() noexcept
{
    spdlog::debug("txsession: cancelling read & send tasks.");

    readExec_.cancel();
    sendExec_.cancel();

    if (journal_)
        journal_->sync();
}

bool TxSession::runOnce()
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

    sendExec_.runOnce();

    if (sendExec_.finished() && sendExec_.haveException())
    {
        spdlog::warn("send context finished early - canceling transfer.");
        finish();
        return false;
    }

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
    //
    // TODO: use info list, resubmit failed submissions.
    // TODO: check for resubmission requirement, flush only when we're
    // done reading.
    if (fileIter_ == end(info_) && readResults_.empty())
    {
        spdlog::trace("waiting on xfer completion.");

        sendExec_.cancel();
        return !sendExec_.finished();
    }

    return true;
}

TxSession::file_info_iter_type TxSession::nextFile(file_info_iter_type first, file_info_iter_type last)
{
    while (first != last && (!S_ISREG(first->status.mode) || !first->status.size))
        ++first;

    return first;
}

bool TxSession::startFile(const FileInfo &info)
{
    using namespace std::chrono_literals;

    using Clock = std::chrono::steady_clock;

    const auto &filename = info.path;
    auto flags = O_RDONLY;

    if (conf_.useDirectIO)
        flags |= O_DIRECT;

    auto fd = std::make_shared<ScopedFd>(
        ScopedFd{::open(filename.c_str(), flags)});

    spdlog::debug("tx opened file id {}: {} @ fd {}", info.id, filename, fd->get());

    auto fileSz = std::filesystem::file_size(filename);

    // try for a while to submit this reader.
    // we may time-out here if the network is bottlenecking things.
    const auto deadline = Clock::now() + 50ms;
    while (!readExec_.cancelled() && Clock::now() < deadline)
    {
        const auto rateDeadline = Clock::now() + 1ms;

        auto diskRead = Reader(fd, info.id, {0, fileSz}, pool_, &queue_);

        if (auto future = readExec_.launch(std::move(diskRead)))
        {
            readResults_.push_back(std::move(*future));
            return true;
        }

        std::this_thread::sleep_until(rateDeadline);
    }

    spdlog::debug("start file {}: timed-out, will resubmit later on."
        , filename);

    return false;
}

}
