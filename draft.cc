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

#include <liburing.h>
#include <signal.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "Cmd.hh"
#include "UtilJson.hh"

namespace {

int dispatchSubcommand(int argc, char **argv)
{
    using namespace draft;

    const auto usage = [argv] {
            std::cout << fmt::format("usage: {} (send|recv) [OPTS]\n"
                , ::basename(argv[0]));
        };

    if (argc < 2)
    {
        usage();
        return 1;
    }

    auto subProg = std::string{argv[1]};

    if (subProg == "send")
        return cmd::send(argc, argv);

    if (subProg == "recv")
        return cmd::recv(argc, argv);

    usage();

    return 1;
}

}

int main(int argc, char **argv)
{
    spdlog::cfg::load_env_levels();

    return dispatchSubcommand(argc, argv);
}
