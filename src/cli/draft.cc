/* @file draft.cc
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

#include <draft/util/DraftUstat.hh>
#include <draft/util/UtilJson.hh>
#include <draft/util/Version.hh>
#include "Cmd.hh"

namespace {

int dispatchSubcommand(int argc, char **argv)
{
    using Cmd = std::function<int(int, char **)>;
    using namespace draft;

    const auto subProgs = std::map<std::string, Cmd>{
        {"journal", cmd::journal},
        {"send", cmd::send},
        {"recv", cmd::recv},
        #ifdef DRAFT_HAVE_COMPRESS
        {"compress", cmd::compress},
        {"decompress", cmd::decompress},
        #endif
        #ifdef DRAFT_HAVE_CUDA
        {"nvcompress", cmd::nvcompress},
        #endif
        {"serve", cmd::serve},
    };

    const auto usage = [argv, &subProgs] {
            std::cout << fmt::format("usage: {} <subcmd> [options...]\n"
                , ::basename(argv[0]));
            std::cout << "  subcmds:\n";
            for (const auto &[name, _] : subProgs)
                std::cout << "    " << name << "\n";
        };

    if (argc < 2)
    {
        usage();
        return 1;
    }

    auto subProg = std::string{argv[1]};

    auto cmd = subProgs.find(subProg);
    if (cmd == end(subProgs))
    {
        usage();
        return 1;
    }

    return cmd->second(argc, argv);
}

}

int main(int argc, char **argv)
{
    spdlog::cfg::load_env_levels();

    spdlog::info("draft build {}", draft::util::versionString());

    draft::metric::configure();

    try {
        dispatchSubcommand(argc, argv);
    } catch (const std::exception &ex) {
        spdlog::error("exception: {}", ex.what());
        return 1;
    }

    return 0;
}
