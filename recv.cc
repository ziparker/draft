/**
 * @file recv.cc
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

#include <cstdio>
#include <iostream>
#include <optional>

#include <getopt.h>
#include <poll.h>
#include <signal.h>

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "Cmd.hh"
#include "Receiver.hh"
#include "Util.hh"
#include "UtilJson.hh"

namespace {

using namespace draft;
namespace net = util::net;

sig_atomic_t done_;

void sigHandler(int)
{
    done_ = 1;
}

struct Options
{
    std::vector<draft::util::NetworkTarget> targets{ };
    draft::util::NetworkTarget service{"localhost", 2020};
    size_t rxBufSize{1u << 23};
    size_t rxRingPwr{5};
    bool noFile{ };
    bool enableDio{ };
};

size_t parseSize(const std::string &arg)
{
    size_t pos{ };
    auto sz = std::stoul(arg, &pos);

    if (pos != arg.size())
    {
        spdlog::error("invalid size: {}", arg);
        std::exit(1);
    }

    return sz;
}

Options parseOpts(int argc, char **argv)
{
    constexpr const char *shortOpts = "b:dhi:ns:t:";
    constexpr const struct option longOpts[] = {
        {"dio", no_argument, nullptr, 'd'},
        {"help", no_argument, nullptr, 'h'},
        {"iodepth", required_argument, nullptr, 'i'},
        {"nofile", no_argument, nullptr, 'n'},
        {"rxbuf", required_argument, nullptr, 'b'},
        {"service", required_argument, nullptr, 's'},
        {"target", required_argument, nullptr, 't'},
        {nullptr, 0, nullptr, 0}
    };

    auto subArgc = argc - 1;
    auto subArgv = argv + 1;

    const auto usage = [argv] {
            std::cerr << fmt::format(
                "usage: {} recv [-b <rx buf size>][-d][-h][-i <depth pwr>][-n][-s <server[:port]>] -t target [-t target ...]\n"
                , ::basename(argv[0]));
        };

    auto opts = Options{ };

    for (int c = 0; (c = getopt_long(subArgc, subArgv, shortOpts, longOpts, 0)) >= 0; )
    {
        switch (c)
        {
            case 'b':
                opts.rxBufSize = parseSize(optarg);
                break;
            case 'd':
                opts.enableDio = true;
                break;
            case 'h':
                usage();
                std::exit(0);
            case 'i':
                opts.rxRingPwr = draft::util::parseSize(optarg);
                break;
            case 'n':
                opts.noFile = true;
                break;
            case 's':
                opts.service = draft::util::parseTarget(optarg);
                break;
            case 't':
                opts.targets.push_back(draft::util::parseTarget(optarg));
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

    if (opts.targets.empty())
    {
        spdlog::error("no targets specified.");
        usage();

        std::exit(1);
    }

    spdlog::info("service: {}:{}", opts.service.ip, opts.service.port);

    return opts;
}

std::vector<util::ScopedFd> bindTargets(const std::vector<draft::util::NetworkTarget> &targets)
{
    auto fds = std::vector<util::ScopedFd>{ };

    for (const auto &target : targets)
    {
        auto fd = net::bindTcp(target.ip, target.port);

        if (fd.get() < 0)
        {
            spdlog::error("unable to bind to {}:{} - ignoring."
                , target.ip, target.port);

            continue;
        }

        fds.push_back(std::move(fd));
    }

    return fds;
}

util::TransferRequest awaitTransferRequest(const Options &opts)
{
    auto serverFd = net::bindTcp(opts.service.ip, opts.service.port);
    auto clientFd = util::ScopedFd{ };

    if (serverFd.get() < 0)
        throw std::system_error(errno, std::system_category(), "awaitTransferRequest: bindTcp");

    auto pollSet = util::PollSet{ };
    pollSet.add(serverFd.get(), EPOLLIN,
        [&serverFd, &clientFd](unsigned) {
            clientFd = net::accept(serverFd.get());
            return false;
        });

    spdlog::info("awaiting transfer request.");

    while (!done_ && !pollSet.empty())
        pollSet.waitOnce(100);

    if (done_)
        return { };

    auto reqBuf = util::Buffer{ };
    bool haveReq{ };

    const auto clientFdRaw = clientFd.get();
    auto rx = util::Receiver{
        std::move(clientFd),
        [&reqBuf, &haveReq](util::Receiver &, const util::MessageBuffer &buf) {
            spdlog::info("req chunk received.");

            reqBuf.resize(buf.payloadLength);
            std::memcpy(reqBuf.data(), buf.buf->data(), buf.payloadLength);
            haveReq = true;
        }};

    pollSet.add(clientFdRaw, EPOLLIN,
        [&rx](unsigned) {
            rx.runOnce();
            return false;
        });

    while (!done_ && !pollSet.empty())
        pollSet.waitOnce(100);

    if (done_)
        return { };

    return util::deserializeTransferRequest(reqBuf);
}

void awaitTransfer(const Options &opts)
{
    // bind receipt ports so we're ready for data immediately after handling
    // the transfer request.
    auto serviceFds = bindTargets(opts.targets);

    auto rxMgr = draft::util::ReceiverManager(
        std::move(serviceFds), opts.rxBufSize, opts.rxRingPwr);

    // wait for a transfer request.
    auto req = awaitTransferRequest(opts);
    req.config.root = ".";
    req.config.ringPwr = opts.rxRingPwr;
    req.config.enableDio = opts.enableDio;

    spdlog::info("creating target files.");
    util::createTargetFiles(".", req.config.fileInfo);

    auto fileAgent = std::unique_ptr<draft::util::FileAgent>{ };

    if (!opts.noFile)
        fileAgent = std::make_unique<draft::util::FileAgent>(req.config);

    rxMgr.setChunkCallback(
        [&fileAgent](int, const util::MessageBuffer &buf) {
            if (fileAgent)
                fileAgent->updateFile(std::move(buf));
        });

    spdlog::info("starting transfer.");

    while (!done_)
    {
        rxMgr.runOnce(100);

        if (rxMgr.finished())
        {
            spdlog::info("transmission connections have closed - leaving.");
            break;
        }
    }

    if (fileAgent)
        fileAgent->drain();

    rxMgr.setChunkCallback({ });
    rxMgr.cancel();

    spdlog::info("rx'd {}", rxMgr.chunkCount());
}

} // namespace

namespace draft::cmd {

int recv(int argc, char **argv)
{
    signal(SIGINT, sigHandler);

    auto opts = parseOpts(argc, argv);

    awaitTransfer(opts);

    spdlog::info("recv complete.");

    return 0;
}

}
