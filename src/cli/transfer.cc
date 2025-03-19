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
#include <draft/util/ProgressDisplay.hh>
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

void handleSigpipe(int)
{
    fprintf(stderr, "draft send: sigpipe\n");
    done_ = 1;
}

void installSigHandler()
{
    struct sigaction action{ };

    action.sa_handler = handleSigint;
    sigaction(SIGINT, &action, nullptr);

    action.sa_handler = handleSigpipe;
    sigaction(SIGPIPE, &action, nullptr);
}

struct Options
{
    draft::util::SessionConfig session;
    bool showProgress{ };
    bool doJournal{ };
};

enum class TransferMode { Send, Recv };

Options parseOptions(int argc, char **argv, TransferMode mode)
{
    namespace fs = std::filesystem;

    enum LongOnlyOpts
    {
        OptJournalPath = 128
    };

    static constexpr const char *shortOpts = "hjJ:nNp:Ps:t:";
    static constexpr struct option longOpts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"journal", no_argument, nullptr, 'j'},
        {"nodirect", no_argument, nullptr, 'n'},
        {"nowrites", no_argument, nullptr, 'N'},
        {"path", required_argument, nullptr, 'p'},
        {"progress", no_argument, nullptr, 'P'},
        {"service", required_argument, nullptr, 's'},
        {"target", required_argument, nullptr, 't'},
        {"journal-path", required_argument, nullptr, 'J'},
        {nullptr, 0, nullptr, 0}
    };

    auto subArgc = argc - 1;
    auto subArgv = argv + 1;

    const auto usage = [argv] {
            std::cout << fmt::format(
                "usage: {} (send|recv) [-h][-j][-J [<path>][-n][-N][-p <path>][-P][-s <server[:port]>] -t ip[:port] [-t ip[:port] -t ...]\n"
                , ::basename(argv[0]));
        };

    const auto help = [argv] {
            std::cout << fmt::format(
                "help: {} (send|recv) OPTIONS -s <ip>:<port> -t <ip>:<port> [-t <ip>:<port>]...]\n"
                "  OPTIONS:\n"
                "   -h | --help\n"
                "       show this help message.\n"
                "   -j | --journal\n"
                "       enable hash journaling, and optionally specify the journal file path.\n"
                "       the default path is <transfer path root>/(tx,rx)_journal.draft for directories.\n"
                "       and is <transfer path root>_(tx,rx)_journal.draft for single file transfers.\n"
                "   -J | --journal-path <path>\n"
                "       enable journal, same as as '-j', but with the specified path.\n"
                "   -n | --nodirect\n"
                "       disable the use of direct-io.\n"
                "       this enables usage on filesystems that don't support it.\n"
                "   -N | --nowrites\n"
                "       disable writes to disk (receive side).\n"
                "   -p | --path <transfer path root>\n"
                "       (send only) - path to directory to send.\n"
                "       the target tree is recreated, in full, on the receive side.\n"
                "   -P | --progress\n"
                "       enable progress reporting (disables info message output)\n"
                "   -s | --service <ip>:<port>\n"
                "       specify the IP & port to bind to for control messages.\n"
                "   -t | --target <ip>:<port>\n"
                "       specify a IP & port to bind to for data transfer.\n"
                "       may specify multiple times to parallelize traffic over multiple routes.\n"
                , ::basename(argv[0]));
        };

    auto opts = Options{ };

    for (int c = 0; (c = getopt_long(subArgc, subArgv, shortOpts, longOpts, 0)) >= 0; )
    {
        switch (c)
        {
            case 'h':
                help();
                std::exit(0);
            case 'j':
                opts.doJournal = true;
                break;
            case 'J':
                opts.doJournal = true;
                opts.session.journalPath = optarg;
                break;
            case 'n':
                opts.session.useDirectIO = false;
                break;
            case 'N':
                opts.session.noWrite = true;
                break;
            case 'p':
                opts.session.pathRoot = optarg;
                break;
            case 'P':
                opts.showProgress = true;
                spdlog::set_level(spdlog::level::warn);
                break;
            case 's':
                opts.session.service = draft::util::parseTarget(optarg);
                break;
            case 't':
                opts.session.targets.push_back(draft::util::parseTarget(optarg));
                break;
            case '?':
                usage();
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

    if (opts.doJournal && opts.session.journalPath.empty())
    {
        if (fs::is_directory(opts.session.pathRoot))
        {
            opts.session.journalPath = fs::path{opts.session.pathRoot} /
                (mode == TransferMode::Send ? "tx_journal.draft" : "rx_journal.draft");
        }
        else
        {
            opts.session.journalPath = fs::path{opts.session.pathRoot +
                (mode == TransferMode::Send ? "_tx_journal.draft" : "_rx_journal.draft")};
        }

        spdlog::info("using default journal path: {}", opts.session.journalPath);
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
        {
            draft::util::stats().fileByteCount += item.status.size;

            if (auto s = draft::util::stats(item.id))
                s->fileByteCount = item.status.size;
        }
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

using Clock = std::chrono::steady_clock;
using Duration = std::chrono::duration<double>;

int recv(int argc, char **argv)
{
    using namespace draft::util;

    spdlog::info("recv");

    const auto opts = parseOptions(argc, argv, TransferMode::Recv);
    const auto &service = opts.session.service;

    installSigHandler();

    auto sess = draft::util::RxSession(opts.session);

    auto req = awaitTransferRequest(
        net::bindTcp(service.ip, service.port));

    if (!req)
        return 1;

    statsMgr().reallocate(req->config.fileInfo.size());

    spdlog::info("starting rx session.");
    sess.start(std::move(*req));

    auto deadline = Clock::now();
    while (!done_ && sess.runOnce())
    {
        std::this_thread::sleep_until(deadline);

        deadline = Clock::now() + 100ms;
    }

    spdlog::info("ending rx session.");
    sess.finish();

    dumpStats(stats());

    return 0;
}

namespace {

void updateDisplay(draft::ui::ProgressDisplay &disp, const std::string &label, draft::util::BandwidthMonitor &bw)
{
    using namespace draft::util;

    const auto &stats = statsMgr().get();

    disp.update(label
        , static_cast<double>(stats.netByteCount) / static_cast<double>(stats.fileByteCount));

    const auto globalBw = bw.update(stats.netByteCount);
    disp.updateBandwidth(globalBw);

    const auto globalEta = bw.etaSec(stats.fileByteCount);
    disp.updateEta(globalEta);

    disp.update();
}

}

int send(int argc, char **argv)
{
    using namespace draft::util;

    static constexpr auto GlobalDisplayLabel = "tx progress";

    const auto opts = parseOptions(argc, argv, TransferMode::Send);
    const auto &path = opts.session.pathRoot;

    installSigHandler();

    // TODO: figure out if fileinfo / xfer req should be part of session start
    // this is redundant atm.
    auto fileInfo = getFileInfo(path);
    auto sess = draft::util::TxSession(opts.session);

    statsMgr().reallocate(fileInfo.size());

    auto fd = net::connectTcp(opts.session.service.ip, opts.session.service.port);
    sendTransferRequest(std::move(fd), fileInfo);

    spdlog::info("starting tx session.");
    sess.start(path);

    auto bwMon = BandwidthMonitor{ };
    auto disp = draft::ui::ProgressDisplay{ };
    if (opts.showProgress)
    {
        disp.init();
        disp.add("tx progress");
    }

    auto deadline = Clock::now();
    while (!done_ && sess.runOnce())
    {
        if (opts.showProgress)
            updateDisplay(disp, GlobalDisplayLabel, bwMon);

        std::this_thread::sleep_until(deadline);

        deadline = Clock::now() + 100ms;
    }

    if (opts.showProgress)
    {
        updateDisplay(disp, GlobalDisplayLabel, bwMon);
        disp.complete();
    }

    spdlog::info("ending tx session.");

    dumpStats(stats());

    return 0;
}

}
