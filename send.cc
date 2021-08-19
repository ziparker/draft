/* @file send.cc
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

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "Cmd.hh"
#include "UtilJson.hh"

namespace {

using namespace draft;
namespace net = util::net;

struct SendOptions
{
    std::vector<util::NetworkTarget> targets{ };
    util::NetworkTarget service{"localhost", 2020};
    std::string path{ };
    std::string suffix{ };
    unsigned mtu{9000};
};

SendOptions parseOptions(int argc, char **argv)
{
    constexpr const char *shortOpts = "hm:p:s:S:t:";
    constexpr const struct option longOpts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"mtu", required_argument, nullptr, 'm'},
        {"path", required_argument, nullptr, 'p'},
        {"service", required_argument, nullptr, 's'},
        {"suffix", required_argument, nullptr, 'S'},
        {"target", required_argument, nullptr, 't'},
        {nullptr, 0, nullptr, 0}
    };

    auto subArgc = argc - 1;
    auto subArgv = argv + 1;

    const auto usage = [argv] {
            std::cout << fmt::format(
                "usage: {} send [-h][-m <mtu>][-p <path>][-s <server[:port]>][-S suffix] -t ip[:port] [-t ip[:port] -t ...]\n"
                , ::basename(argv[0]));
        };

    auto opts = SendOptions{ };

    for (int c = 0; (c = getopt_long(subArgc, subArgv, shortOpts, longOpts, 0)) >= 0; )
    {
        switch (c)
        {
            case 'h':
                usage();
                std::exit(0);
            case 'm':
            {
                size_t pos{ };
                opts.mtu = std::stoul(optarg, &pos);

                if (pos != std::string(optarg).size())
                {
                    spdlog::error("invalid mtu value: {}", optarg);
                    std::exit(1);
                }

                break;
            }
            case 'p':
                opts.path = optarg;
                break;
            case 's':
                opts.service = util::parseTarget(optarg);
                break;
            case 'S':
                opts.suffix = optarg;
                break;
            case 't':
                opts.targets.push_back(
                    util::parseTarget(optarg));
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

    spdlog::info("service: {}:{}", opts.service.ip, opts.service.port);

    spdlog::info("targets:");
    for (const auto &target : opts.targets)
        spdlog::info("  {}:{}", target.ip, target.port);

    return opts;
}

} // namespace

namespace draft::cmd {

int send(int argc, char **argv)
{
    auto opts = parseOptions(argc, argv);

    if (argc < 2)
        std::exit(1);

    if (opts.targets.empty())
        opts.targets.push_back({"localhost", 2021});

    auto fileInfo = util::getFileInfo(opts.path);

    if (!opts.suffix.empty())
    {
        for (auto &info : fileInfo)
        {
            if (!S_ISDIR(info.status.mode))
                info.targetSuffix = opts.suffix;
        }
    }

    auto request = util::generateTransferRequestMsg(fileInfo);

    auto serverFd = net::connectTcp(opts.service.ip, opts.service.port);
    net::writeAll(serverFd.get(), request.data(), request.size());
    serverFd.close();

    auto conf = util::AgentConfig{ };
    conf.targets = opts.targets;
    conf.mtu = opts.mtu;

    auto agent = util::MmapAgent{std::move(conf)};
    for (const auto &info : fileInfo)
        agent.transferFile(info.path, info.id);

    return 0;
}

}
