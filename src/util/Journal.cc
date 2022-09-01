/**
 * @file Journal.cc
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

#include <chrono>

#include <endian.h>
#include <fcntl.h>
#include <unistd.h>

#include <spdlog/fmt/fmt.h>
#include <nlohmann/json.hpp>

#include <draft/util/Journal.hh>
#include <draft/util/UtilJson.hh>

using std::chrono::system_clock;

namespace draft::util {

namespace {

constexpr auto JournalHeaderOffset = 64u;
constexpr auto JournalBlockSize = 512u;

struct FileHeader
{
    char magic[8]{'D','R','A','F','T','J','F',' '};

    uint64_t journalOffset{ };
    uint64_t cborSize{ };
};

static_assert(sizeof(FileHeader) < JournalHeaderOffset);

struct JournalHeader
{
    static constexpr auto JournalMajorVersion = 0u;
    static constexpr auto JournalMinorVersion = 0u;

    uint16_t versionMajor{ };
    uint16_t versionMinor{ };

    system_clock::time_point birthdate{ };

    uint32_t journalAlignment{ };
};

void to_json(nlohmann::json &j, const JournalHeader &header)
{
    using namespace std::chrono;

    j = {
        {"version_major", header.versionMajor},
        {"version_minor", header.versionMinor},
        {"birthdate_epoch_nsec",
            duration_cast<nanoseconds>(header.birthdate.time_since_epoch()).count()},
        {"journal_alignment", header.journalAlignment}
    };
}

}

Journal::Journal(std::string basename, const std::vector<util::FileInfo> &info)
{
    basename += ".draft";

    fd_ = ScopedFd{
        ::open(
            basename.c_str(),
            O_APPEND | O_CLOEXEC | O_CREAT | O_DIRECT | O_SYNC | O_EXCL,
            S_IRUSR | S_IWUSR | S_IRGRP)};

    if (fd_.get() < 0)
    {
        throw std::system_error(errno, std::system_category(),
            fmt::format("draft - unable to create journal file '{}': {}"
                , basename
                , std::strerror(errno)));
    }

    writeHeader(info);
}

void Journal::sync()
{
    if (::syncfs(fd_.get()) < 0)
        throw std::system_error(errno, std::system_category(), "draft - unable to sync journal");
}

void Journal::writeHeader(const std::vector<util::FileInfo> &info)
{
    auto headerJson = nlohmann::json{ };
    headerJson = JournalHeader{
            JournalHeader::JournalMajorVersion,
            JournalHeader::JournalMinorVersion,
            system_clock::now(),
            JournalBlockSize
        };

    headerJson["file_info"] = info;

    // allocate space for the raw file header before serializing the journal
    // header.
    auto buf = std::vector<uint8_t>{ };
    buf.resize(JournalHeaderOffset);

    // serialize the journal header.
    nlohmann::json::to_cbor(headerJson, buf);

    const auto cborSize = buf.size() - JournalHeaderOffset;

    // pad buffer so that the journal data which follows it will be alligned to
    // the journal block size.
    buf.resize((buf.size() + JournalBlockSize - 1) & ~(JournalBlockSize - 1));

    auto fileHeader = reinterpret_cast<FileHeader *>(buf.data());
    *fileHeader = FileHeader{ };
    fileHeader->journalOffset = htole64(buf.size());
    fileHeader->cborSize = htole64(cborSize);

    if (buf.size() > static_cast<size_t>(std::numeric_limits<off_t>::max()))
    {
        throw std::runtime_error(fmt::format(
            "draft - unable to allocate disk space for journal "
            "header of size {}: {}"
            , buf.size()
            , std::strerror(errno)));
    }

    if (auto err = posix_fallocate(fd_.get(), 0, static_cast<off_t>(buf.size())); err)
    {
        throw std::system_error(err, std::system_category(),
            fmt::format("draft - unable to allocate disk space for journal "
                "header of size {}"
                , buf.size()));
    }

    writeFileData(buf.data(), buf.size());
}

void Journal::writeFileData(const void *data, size_t size)
{
    auto iov = iovec{const_cast<void *>(data), size};

    if (auto len = writeChunk(fd_.get(), &iov, 1, 0); len != size)
    {
        throw std::runtime_error(
            fmt::format("draft: unable to write journal header of size {}", size));
    }
}

}
