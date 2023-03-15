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
#include <cstdio>

#include <endian.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
// include after spdog.
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/fmt/fmt.h>

#include <draft/util/Journal.hh>
#include <draft/util/UtilJson.hh>

using std::chrono::system_clock;

namespace draft::util {
namespace {

constexpr auto JournalHeaderOffset = 64u;
constexpr auto JournalBlockSize = 512u;

struct FileHeader
{
    static constexpr char Magic[] = {'D','R','A','F','T','J','F',' '};
    char magic[8]{ };

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

inline void from_json(const nlohmann::json &j, JournalHeader &header)
{
    using namespace std::chrono;

    int64_t nsec{ };

    j.at("version_major").get_to(header.versionMajor);
    j.at("version_minor").get_to(header.versionMinor);
    j.at("birthdate_epoch_nsec").get_to(nsec);
    j.at("journal_alignment").get_to(header.journalAlignment);

    header.birthdate = system_clock::time_point{nanoseconds{nsec}};
}

FileHeader readFileHeader(int fd)
{
    auto header = FileHeader{ };

    util::readChunk(fd, &header, sizeof(header), 0);

    return header;
}

nlohmann::json readJournalHeaderJson(int fd)
{
    const auto header = readFileHeader(fd);

    auto cbor = std::vector<uint8_t>{ };
    cbor.resize(header.cborSize);

    util::readChunk(fd, cbor.data(), cbor.size(), JournalHeaderOffset);

    return nlohmann::json::from_cbor(cbor);
}

} // namespace anonymous

namespace internal {

inline size_t journalRecordCount(int fd, size_t hashOffset)
{
    struct stat st{ };
    auto stat = fstat(fd, &st);

    if (stat)
    {
        throw std::system_error(errno, std::system_category(),
            "draft journal cursor: unable to determine journal record count (fstat)");
    }

    if (st.st_size < 0 || static_cast<size_t>(st.st_size) <= hashOffset)
        return 0;

    return (static_cast<size_t>(st.st_size) - hashOffset) / sizeof(Journal::HashRecord);
}

} // namespace internal

////////////////////////////////////////////////////////////////////////////////
// Journal

Journal::Journal(std::string path)
{
    fd_ = ScopedFd{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};

    if (fd_.get() < 0)
    {
        throw std::system_error(errno, std::system_category(),
            fmt::format("draft - unable to open journal file '{}'"
                , path));
    }

    checkFileHeader();

    path_ = std::move(path);
}

Journal::Journal(std::string path, const std::vector<util::FileInfo> &info)
{
    fd_ = ScopedFd{
        ::open(
            path.c_str(),
            O_RDWR | O_CLOEXEC | O_CREAT | O_EXCL,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)};

    if (fd_.get() < 0)
    {
        throw std::system_error(errno, std::system_category(),
            fmt::format("draft - unable to create journal file '{}'"
                , path));
    }

    writeHeader(info);

    path_ = std::move(path);
}

Journal::Journal(int fd, std::string path, const std::vector<FileInfo> &info):
    fd_(ScopedFd{fd})
{
    if (fd_.get() < 0)
        throw std::invalid_argument(fmt::format("invalid journal file descriptor '{}'", fd));

    writeHeader(info);

    path_ = std::move(path);
}

std::vector<util::FileInfo> Journal::fileInfo() const
{
    const auto header = readJournalHeaderJson(fd_.get());

    auto fileInfo = std::vector<util::FileInfo>{ };
    header.at("file_info").get_to(fileInfo);

    return fileInfo;
}

std::chrono::system_clock::time_point Journal::creationDate() const
{
    using namespace std::chrono;

    const auto header = readJournalHeaderJson(fd_.get());

    uint64_t nsec{ };
    header.at("birthdate_epoch_nsec").get_to(nsec);

    return system_clock::time_point{nanoseconds{nsec}};
}

void Journal::sync()
{
    if (::syncfs(fd_.get()) < 0)
        throw std::system_error(errno, std::system_category(), "draft - unable to sync journal");
}

int Journal::writeHash(uint16_t fileId, size_t offset, size_t size, uint64_t hash)
{
    const auto record = HashRecord {
            hash,
            offset,
            size,
            fileId,
            { }
        };

    return writeHash(record);
}

int Journal::writeHash(const HashRecord &record)
{
    auto iov = iovec{const_cast<HashRecord *>(&record), sizeof(record)};

    // this RWF_APPEND behavior is linux-specific (added in 4.16).
    if (auto len = writeChunk(fd_.get(), &iov, 1, 0, RWF_APPEND); len != sizeof(record))
    {
        throw std::system_error(
            errno,
            std::system_category(),
            fmt::format("draft: unable to write journal hash record for file {} offset {} len {} hash {:#x}"
                , record.fileId
                , record.offset
                , record.size
                , record.hash));
    }

    return 0;
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

    // pad buffer so that the journal data which follows it will be aligned to
    // the journal block size.
    buf.resize((buf.size() + JournalBlockSize - 1) & ~(JournalBlockSize - 1));

    auto fileHeader = reinterpret_cast<FileHeader *>(buf.data());
    *fileHeader = FileHeader{ };
    std::copy(FileHeader::Magic, FileHeader::Magic + sizeof(FileHeader::Magic), fileHeader->magic);
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

size_t Journal::hashCount() const
{
    const auto header = readFileHeader(fd_.get());
    return internal::journalRecordCount(fd_.get(), header.journalOffset);
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

void Journal::checkFileHeader() const
{
    namespace fs = std::filesystem;

    struct stat st{ };

    if (::fstat(fd_.get(), &st) < 0)
        throw std::system_error(errno, std::system_category(), "journal file header check");

    const auto header = readFileHeader(fd_.get());

    if (header.journalOffset >= std::numeric_limits<off_t>::max())
    {
        throw std::runtime_error(fmt::format(
            "journal: file header cbor payload size is too large (for off_t): {} from offset {}"
            , header.cborSize
            , header.journalOffset));
    }

    if (header.journalOffset > static_cast<size_t>(st.st_size))
    {
        throw std::runtime_error(fmt::format(
            "journal: file header journal offset + cbor payload size {} is larger than journal file size {}"
            , header.journalOffset + header.cborSize
            , st.st_size));
    }

    if (!std::ranges::equal(
        header.magic, header.magic + sizeof(header.magic),
        FileHeader::Magic, FileHeader::Magic + sizeof(header.magic)))
    {
        throw std::runtime_error(fmt::format(
            "journal: file header has invalid magic: {:spn}"
            , spdlog::to_hex(header.magic, header.magic + sizeof(header.magic))));
    }
}

Cursor Journal::cursor() const
{
    auto fd = ScopedFd{open(path_.c_str(), O_RDONLY | O_CLOEXEC)};

    if (fd.get() < 0)
    {
        throw std::system_error(errno, std::system_category(),
            "draft Journal::Begin");
    }

    return Cursor{std::make_shared<ScopedFd>(std::move(fd))};
}

Journal::const_iterator Journal::begin() const
{
    return cursor().seek(0, Cursor::Set);
}

Journal::const_iterator Journal::end() const
{
    return cursor().seek(0, Cursor::End);
}

int Journal::rename(const std::string &path)
{
    auto stat = ::rename(path_.c_str(), path.c_str());

    if (stat)
        return -errno;

    path_ = std::move(path);

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Cursor

Cursor::Cursor():
    fd_(std::make_shared<ScopedFd>())
{
}

Cursor &Cursor::seek(off_t count, Whence whence)
{
    auto idx = recordIdx_;
    const auto recordCount = journalRecordCount();

    if (!recordCount)
        return *this;

    const auto countAbsSz = static_cast<size_t>(std::abs(count));

    switch (whence)
    {
        case Set:
            idx = count < 0 || countAbsSz > recordCount ? ~size_t{ } : countAbsSz;
            break;
        case Current:
            if (count < 0)
            {
                if (idx == ~size_t{ })
                    idx = recordCount - countAbsSz;
                else if (countAbsSz > idx)
                    idx = ~size_t{ };
                else
                    idx -= countAbsSz;
            }
            else if (auto newIdx = idx + countAbsSz; newIdx < idx || newIdx >= recordCount)
            {
                idx = ~size_t{ };
            }
            else
            {
                if (idx != ~size_t{ })
                    idx += countAbsSz;
            }

            break;
        case End:
            if (count >= 0 || countAbsSz > recordCount)
                idx = ~size_t{ };
            else
                idx = recordCount - countAbsSz;

            break;
    }

    recordIdx_ = idx;

    return *this;
}

bool Cursor::valid() const
{
    const auto recordCount = journalRecordCount();

    return recordCount &&
        recordIdx_ < recordCount;
}

std::optional<Journal::HashRecord> Cursor::hashRecord() const
{
    if (!valid())
        return std::nullopt;

    const auto offset =
        hashOffset_ +
        recordIdx_ * sizeof(Journal::HashRecord);

    auto record = Journal::HashRecord{ };
    util::readChunk(fd_->get(), &record, sizeof(record), offset);

    return record;
}

size_t Cursor::position() const
{
    return recordIdx_;
}

size_t Cursor::journalRecordCount() const
{
    return internal::journalRecordCount(fd_->get(), hashOffset_);
}

size_t Cursor::journalHashOffset() const
{
    const auto header = readFileHeader(fd_->get());

    return header.journalOffset;
}

Cursor::Cursor(const std::shared_ptr<ScopedFd> &fd):
    fd_(fd)
{
    hashOffset_ = journalHashOffset();
}

////////////////////////////////////////////////////////////////////////////////
// CursorIter

const CursorIter::HashRecord *CursorIter::operator->() const
{
    record_ = cursor_.hashRecord();

    if (!record_)
        throw std::runtime_error("draft journal: out of range access (operator ->)");

    return record_.operator->();
}

const CursorIter::HashRecord &CursorIter::operator*() const
{
    record_ = cursor_.hashRecord();

    if (!record_)
        throw std::runtime_error("draft journal: out of range access (operator *)");

    return *record_;
}

}
