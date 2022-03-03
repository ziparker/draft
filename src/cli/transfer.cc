/**
 * @file transfer.cc
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

#include <cstdlib>

#include <getopt.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <draft/util/InfoReceiver.hh>
#include <draft/util/RxSession.hh>
#include <draft/util/Stats.hh>
#include <draft/util/TxSession.hh>
#include <draft/util/Util.hh>
#include <draft/util/UtilJson.hh>

#include "Cmd.hh"

namespace {

sig_atomic_t done_;

void handleSigint(int)
{
    if (done_)
    {
        fprintf(stderr, "draft send: interrupted twice - ending transfer NOW\n");
        std::exit(2);
    }

    done_ = 1;
}

void installSigHandler()
{
    struct sigaction action{ };
    action.sa_handler = handleSigint;
    sigaction(SIGINT, &action, nullptr);

    // TODO: handle sigpipe for when rx is killed
}

struct Options
{
    draft::util::SessionConfig session;
};

Options parseOptions(int argc, char **argv)
{
    static constexpr const char *shortOpts = "hnp:s:t:";
    static constexpr struct option longOpts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"nodirect", no_argument, nullptr, 'n'},
        {"path", required_argument, nullptr, 'p'},
        {"service", required_argument, nullptr, 's'},
        {"target", required_argument, nullptr, 't'},
        {nullptr, 0, nullptr, 0}
    };

    auto subArgc = argc - 1;
    auto subArgv = argv + 1;

    const auto usage = [argv] {
            spdlog::info(
                "usage: {} (send|recv) [-h][-n][-p <path>][-s <server[:port]>] -t ip[:port] [-t ip[:port] -t ...]\n"
                , ::basename(argv[0]));
        };

    auto opts = Options{ };

    for (int c = 0; (c = getopt_long(subArgc, subArgv, shortOpts, longOpts, 0)) >= 0; )
    {
        switch (c)
        {
            case 'h':
                usage();
                std::exit(0);
            case 'n':
                opts.session.useDirectIO = false;
                break;
            case 'p':
                opts.session.pathRoot = optarg;
                break;
            case 's':
                opts.session.service = draft::util::parseTarget(optarg);
                break;
            case 't':
                opts.session.targets.push_back(draft::util::parseTarget(optarg));
                break;
            case '?':
                std::exit(1);
            default:
                break;
        }
    }

    if (optind < subArgc)
    {
        spdlog::error("trailing args..");
        std::exit(1);
    }

    if (opts.session.service.ip.empty() || !opts.session.service.port)
    {
        spdlog::error("missing required argument: --service");
        std::exit(1);
    }

    if (opts.session.targets.empty())
    {
        spdlog::error("missing required argument: --target");
        std::exit(1);
    }

    spdlog::info("service: {}:{}", opts.session.service.ip, opts.session.service.port);

    spdlog::info("targets:");
    for (const auto &target : opts.session.targets)
        spdlog::info("  {}:{}", target.ip, target.port);

    return opts;
}

void updateFileStats(const std::vector<draft::util::FileInfo> &info)
{
    for (const auto &item : info)
    {
        if (S_ISREG(item.status.mode))
            draft::util::stats().fileByteCount += item.status.size;
    }
}

std::optional<draft::util::TransferRequest> awaitTransferRequest(draft::util::ScopedFd fd)
{
    auto rx = draft::util::InfoReceiver{std::move(fd)};

    while (!done_ && !rx.runOnce())
        ;

    if (done_)
        return { };

    auto info = rx.info();

    updateFileStats(info.config.fileInfo);

    return info;
}

void sendTransferRequest(draft::util::ScopedFd fd, const std::vector<draft::util::FileInfo> &info)
{
    auto request = draft::util::generateTransferRequestMsg(info);
    draft::util::net::writeAll(fd.get(), request.data(), request.size());

    updateFileStats(info);

    spdlog::debug("sent xfer req: {}", request.size());
}

void dumpStats(const draft::util::Stats &stats)
{
    spdlog::info(
        "stats:\n"
        "  file byte count:         {}\n"
        "  disk byte count:         {}\n"
        "   (includes padding on rx side)\n"
        "  net byte count:          {}\n"
        "   (includes padding on tx side)\n"
        "  queued block count:      {}\n"
        "  dequeued block count:    {}\n"
        , stats.fileByteCount
        , stats.diskByteCount
        , stats.netByteCount
        , stats.queuedBlockCount
        , stats.dequeuedBlockCount);
}

}

namespace draft::cmd {

using namespace std::chrono_literals;

int recv(int argc, char **argv)
{
    using namespace draft::util;

    spdlog::info("recv");

    const auto opts = parseOptions(argc, argv);
    const auto &service = opts.session.service;

    installSigHandler();

    auto sess = draft::util::RxSession(opts.session);

    auto req = awaitTransferRequest(
        net::bindTcp(service.ip, service.port));

    if (!req)
        return 1;

    spdlog::info("starting rx session.");
    sess.start(std::move(*req));

    while (!done_ && sess.runOnce())
        std::this_thread::sleep_for(100ms);

    spdlog::info("ending rx session.");
    sess.finish();

    dumpStats(stats());

    return 0;
}

int send(int argc, char **argv)
{
    using namespace draft::util;

    const auto opts = parseOptions(argc, argv);
    const auto &path = opts.session.pathRoot;

    installSigHandler();

    // TODO: figure out if fileinfo / xfer req should be part of session start
    // this is redundant atm.
    auto fileInfo = getFileInfo(path);
    auto sess = draft::util::TxSession(opts.session);

    auto fd = net::connectTcp(opts.session.service.ip, opts.session.service.port);
    sendTransferRequest(std::move(fd), fileInfo);

    spdlog::info("starting tx session.");
    sess.start(path);

    while (!done_ && sess.runOnce())
        std::this_thread::sleep_for(100ms);

    spdlog::info("ending tx session.");

    dumpStats(stats());

    return 0;
}

}
