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
#include <unistd.h>

#include <cuda.h>
#include <cuda_runtime.h>
#include <cufile.h>
#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#include "Cmd.hh"
#include "Util.hh"

namespace {

using namespace draft;

struct CompressOptions
{
    std::string inPath{"in"};
    std::string outPath{ };
    size_t blockSize{1u << 20};
    size_t chunkSize{1u << 16};
};

struct CudaError: public std::runtime_error
{
    explicit CudaError(const cudaError_t &err):
        std::runtime_error(cudaGetErrorString(err))
    {
    }
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
            case 'c':
            {
                size_t pos{ };
                opts.chunkSize = std::stoul(optarg, &pos);

                if (pos != std::string(optarg).size())
                {
                    spdlog::error("invalid chunk size value: {}", optarg);
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

    if (optind < subArgc)
    {
        spdlog::error("trailing args..");
        std::exit(1);
    }

    return opts;
}

void compress(const CompressOptions &opts)
{
	CUfileDescr_t cfr_descr;
	CUfileHandle_t cfr_handle;

    auto fd = util::ScopedFd{open(opts.inPath.c_str(), O_RDONLY | O_DIRECT)};

    if (fd.get() < 0)
        throw std::system_error(errno, std::system_category(), "draft.compress: open");

    const auto fileSize = std::filesystem::file_size(opts.inPath);

    auto desc = CUfileDescr_t{ };
    desc.handle.fd = fd.get();
    desc.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;

    auto handle = CUfileHandle_t{ };

    if (auto stat = cuFileHandleRegister(&handle, &desc);
        stat.err != CU_FILE_SUCCESS)
    {
        throw std::runtime_error(fmt::format("draft.compress: unable to register file '{}'", opts.inPath));
    }

    cudaSetDevice(0);
    if (auto err = cudaGetLastError(); err != cudaSuccess)
        throw CudaError(err);

    uint8_t *gpuInBuf{ };
    uint8_t *gpuOutBuf{ };

    cudaMalloc(&gpuInBuf, opts.blockSize);
    if (auto err = cudaGetLastError(); err != cudaSuccess)
        throw CudaError(err);

    if (auto stat = cuFileBufRegister(gpuInBuf, opts.blockSize, 0);
        stat.err != CU_FILE_SUCCESS)
    {
        throw std::runtime_error("draft.compress: unable to register file buffer.");
    }

    for (size_t offset = 0; offset < fileSize; )
    {
        auto len = cuFileRead(handle, gpuInBuf, opts.blockSize, static_cast<off_t>(offset), 0);

        if (len < 0)
        {
            spdlog::error("cuFileRead returned {}", len);
            break;
        }

        if (!len)
        {
            spdlog::error("cuFileRead returned 0 - ending transfer.");
            break;
        }

        offset += static_cast<size_t>(len);
    }

    cuFileBufDeregister(gpuInBuf);
    cudaFree(gpuInBuf);
    //cudaFree(gpuOutBuf);
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

}
