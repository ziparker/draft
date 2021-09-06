/* @file compress.cc
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

#include <filesystem>
#include <iostream>

#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include <blosc2.h>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "Cmd.hh"
#include "Util.hh"

namespace {

using namespace draft;

struct CompressOptions
{
    std::string inPath{"in"};
    std::string outPath{"out"};
    size_t blockSize{1u << 20};
    unsigned level{5};
    unsigned nThreads{0};
};

CompressOptions parseOptions(int argc, char **argv)
{
    constexpr const char *shortOpts = "b:c:h";
    constexpr const struct option longOpts[] = {
        {"block-size", required_argument, nullptr, 'b'},
        {"chunk-size", required_argument, nullptr, 'c'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    auto subArgc = argc - 1;
    auto subArgv = argv + 1;

    const auto usage = [argv] {
            std::cout << fmt::format(
                "usage: {} compress [-b <block size>][-c <chunk size>[-h] <file>\n"
                , ::basename(argv[0]));
        };

    auto opts = CompressOptions{ };

    for (int c = 0; (c = getopt_long(subArgc, subArgv, shortOpts, longOpts, 0)) >= 0; )
    {
        switch (c)
        {
            case 'b':
            {
                size_t pos{ };
                opts.blockSize = std::stoul(optarg, &pos);

                if (pos != std::string(optarg).size())
                {
                    spdlog::error("invalid block size value: {}", optarg);
                    std::exit(1);
                }

                break;
            }
            case 'h':
                usage();
                std::exit(0);
            case '?':
                std::exit(1);
            default:
                break;
        }
    }

    if (!opts.nThreads)
        opts.nThreads = static_cast<unsigned>(get_nprocs());

    if (optind < subArgc)
    {
        spdlog::error("trailing args..");
        std::exit(1);
    }

    return opts;
}

void compress(const CompressOptions &opts)
{
    auto fd = util::ScopedFd{open(opts.inPath.c_str(), O_RDONLY | O_DIRECT)};

    if (fd.get() < 0)
        throw std::system_error(errno, std::system_category(), "draft.compress: open");

    const auto fileSize = std::filesystem::file_size(opts.inPath);

    blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
    cparams.typesize = 1;
    cparams.compcode = BLOSC_BLOSCLZ;
    cparams.clevel = opts.level;
    cparams.nthreads = opts.nThreads;

    auto storage = blosc2_storage{ };
    storage.cparams = &cparams;
    storage.contiguous = true;
    auto urlPath = opts.outPath;
    storage.urlpath = urlPath.data();

    auto *chunk = blosc2_schunk_new(&storage);
    if (!chunk)
        throw std::runtime_error("draft.compress: unable to allocate blosc chunk.");

    auto map = util::ScopedMMap::map(
        nullptr, opts.blockSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    auto buf = map.uint8Data();

    for (size_t offset = 0; offset < fileSize; )
    {
        auto len = ::read(fd.get(), buf, opts.blockSize);

        if (len < 0)
            throw std::system_error(errno, std::system_category(), "draft.compress: read");

        if (!len)
        {
            spdlog::error("read returned 0 - ending transfer.");
            break;
        }

        if (auto stat = blosc2_schunk_append_buffer(chunk, buf, len); stat < 0)
            throw std::runtime_error(fmt::format("blosc_schunk_append_buffer: {}", stat));

        offset += static_cast<size_t>(len);
    }

    if (chunk)
    {
        if (chunk->cbytes)
            spdlog::info("compression ratio: {:.1f}", 1.0 * chunk->nbytes / chunk->cbytes);

        blosc2_schunk_free(chunk);
    }

    blosc_destroy();
}

void decompress(const CompressOptions &opts)
{
    auto outFd = util::ScopedFd{open(opts.outPath.c_str(), O_WRONLY | O_DIRECT | O_CREAT | O_TRUNC, 0664)};

    if (outFd.get() < 0)
        throw std::system_error(errno, std::system_category(), "draft.decompress: open");

    blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
    dparams.nthreads = opts.nThreads;

    auto *chunk = blosc2_schunk_open(opts.inPath.c_str());
    if (!chunk)
    {
        throw std::runtime_error(fmt::format(
            "draft.decompress: unable to open blosc chunk file '{}'."
            , opts.inPath));
    }

    auto map = util::ScopedMMap::map(
        nullptr, opts.blockSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    auto buf = map.uint8Data();

    size_t nChunks{ };

    for (int i = 0; true; ++i)
    {
        auto len = blosc2_schunk_decompress_chunk(chunk, i, buf, opts.blockSize);

        if (len < 0)
        {
            spdlog::error("decompress {}", len);
            throw std::runtime_error(fmt::format("blosc_schunk_decompress_chunk: {}", len));
        }

        ++nChunks;

        for (size_t offset = 0; offset < static_cast<size_t>(len); )
        {
            auto wlen = ::write(outFd.get(), buf + offset, static_cast<size_t>(len) - offset);

            if (wlen < 0)
                throw std::system_error(errno, std::system_category(), "draft.decompress write");

            if (wlen == 0)
            {
                throw std::runtime_error(fmt::format(
                    "draft.decompress: 0 sized write to output file '{}'"
                    , opts.outPath));
            }

            offset += static_cast<size_t>(wlen);
        }
    }

    spdlog::info("decompressed {} chunks.", nChunks);

    if (chunk)
        blosc2_schunk_free(chunk);

    blosc_destroy();
}

} // namespace

namespace draft::cmd {

int compress(int argc, char **argv)
{
    if (argc < 2)
        std::exit(1);

    auto opts = parseOptions(argc, argv);

    compress(opts);

    return 0;
}

int decompress(int argc, char **argv)
{
    if (argc < 2)
        std::exit(1);

    auto opts = parseOptions(argc, argv);

    decompress(opts);

    return 0;
}

}
