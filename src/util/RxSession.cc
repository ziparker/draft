/**
 * @file RxSession.cc
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

#include <spdlog/spdlog.h>

#include <draft/util/Hasher.hh>
#include <draft/util/Journal.hh>
#include <draft/util/Receiver.hh>
#include <draft/util/RxSession.hh>
#include <draft/util/Writer.hh>

namespace draft::util {

RxSession::RxSession(SessionConfig conf):
    conf_(std::move(conf))
{
    pool_ = BufferPool::make(BufSize, 35);
    targetFds_ = bindNetworkTargets(conf_.targets);

    hashQueue_.setSizeLimit(100);
}

RxSession::~RxSession() noexcept
{
    finish();
}

void RxSession::start(util::TransferRequest req)
{
    namespace fs = std::filesystem;

    createTargetFiles(conf_.pathRoot, req.config.fileInfo);

    if (!conf_.journalPath.empty())
    {
        journal_ = std::make_unique<Journal>(conf_.journalPath, req.config.fileInfo);
        hashExec_.add(util::Hasher{hashQueue_, journal_}, ThreadExecutor::Options::DoFinalize);
    }

    auto fileInfo = std::vector<FileInfo>{ };
    auto fileMap = FdMap{ };
    for (const auto &item : req.config.fileInfo)
    {
        if (!S_ISREG(item.status.mode))
            continue;

        auto path = rootedPath(
            conf_.pathRoot,
            item.path,
            item.targetSuffix);

        auto fd = ScopedFd{::open(path.c_str(), O_WRONLY | O_DIRECT)};
        auto rawFd = fd.get();

        if (rawFd < 0)
        {
            spdlog::error("unable to open file '{}': {}"
                , path.native()
                , std::strerror(errno));

            continue;
        }

        fileInfo.push_back({
            path,
            std::move(fd),
            item.status.size,
            item.status.mode
        });

        fileMap.insert({item.id, rawFd});
    }

    const auto view = std::views::transform(
        targetFds_, [this](auto &&fd){ return Receiver{std::move(fd), queue_, &hashQueue_}; });

    auto receivers = std::vector<Receiver>{
        std::make_move_iterator(begin(view)),
        std::make_move_iterator(end(view))};

    targetFds_ = std::vector<ScopedFd>{ };

    spdlog::debug("starting receivers.");

    recvExec_.add(std::move(receivers));

    writeExec_.add(Writer(std::move(fileMap), queue_), ThreadExecutor::Options::DoFinalize);

    fileInfo_ = std::move(fileInfo);
}

void RxSession::finish() noexcept
{
    recvExec_.cancel();
    writeExec_.cancel();
    writeExec_.waitFinished();

    // truncate after each file.
    truncateFiles();
}

void RxSession::truncateFiles()
{
    for (const auto &info : fileInfo_)
    {
        if (!S_ISREG(info.mode))
            continue;

        spdlog::debug("truncate '{}' -> {}"
            , info.path
            , info.size);

        const auto sz = std::min(
            static_cast<size_t>(std::numeric_limits<off_t>::max()),
            info.size);

        if (::ftruncate(info.fd.get(), static_cast<off_t>(sz)))
        {
            spdlog::warn("unable to truncate file '{}' to size {} ({})"
                , info.path
                , info.size
                , std::strerror(errno));
        }
    }
}

bool RxSession::runOnce()
{
    auto recvFinished = !recvExec_.runOnce();
    writeExec_.runOnce();

    // return now if we're still receiving data.
    if (!recvFinished)
        return true;

    writeExec_.runOnce();

    return false;
}

}
