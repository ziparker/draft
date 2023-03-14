/**
 * @file journal.cc
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

#include <iostream>
#include <string>
#include <vector>

#include <getopt.h>

#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>

#include <draft/util/Journal.hh>

#include "Cmd.hh"
#include <draft/util/JournalOperations.hh>

namespace draft::cmd {
namespace {

using draft::util::Journal;

struct Options
{
    struct Operations
    {
        unsigned dumpInfo   : 1;
        unsigned dumpHashes : 1;
        unsigned dumpBirthdate  : 1;
        unsigned diff       : 1;
        unsigned verify     : 1;
    };

    enum class OutputFormat
    {
        Standard,
        CSV
    };

    std::vector<std::string> journals{ };
    OutputFormat format{ };
    Operations ops{ };
};

Options parseOptions(int argc, char **argv)
{
    using namespace std::string_literals;

    static constexpr const char *shortOpts = "d:Df:hv";
    static constexpr struct option longOpts[] = {
        {"diff", no_argument, nullptr, 'D'},
        {"dump", required_argument, nullptr, 'd'},
        {"format", required_argument, nullptr, 'f'},
        {"help", no_argument, nullptr, 'h'},
        {"verify", no_argument, nullptr, 'v'},
        {nullptr, 0, nullptr, 0}
    };

    auto subArgc = argc - 1;
    auto subArgv = argv + 1;

    const auto usage = [argv] {
            std::cout << fmt::format(
                "usage: {} journal OPTIONS <journal file>\n"
                "  OPTIONS:\n"
                "   -d | --dump <type>\n"
                "       types: birthdate, hashes, info\n"
                "   -D | --diff\n"
                "       diff the specified journal files - requires exactly 2 journal arguments.\n"
                "   -f | --format <formats>\n"
                "       formats: standard (default), csv\n"
                "   -h | --help\n"
                "       show this help\n"
                "   -v | --verify <journal file>\n"
                "       verify a journal against local filesystem contents.\n"
                , ::basename(argv[0]));
        };

    auto opts = Options{ };

    for (int c = 0; (c = getopt_long(subArgc, subArgv, shortOpts, longOpts, 0)) >= 0; )
    {
        switch (c)
        {
            case 'd':
                if (optarg == "birthdate"s)
                    opts.ops.dumpBirthdate = 1;
                else if (optarg == "hashes"s)
                    opts.ops.dumpHashes = 1;
                else if (optarg == "info"s)
                    opts.ops.dumpInfo = 1;
                else
                    std::cerr<< "error: cannot dump '" << optarg << "'\n";
                break;
            case 'D':
                opts.ops.diff = 1;
                break;
            case 'f':
                if (optarg == "standard"s)
                    opts.format = Options::OutputFormat::Standard;
                else if (optarg == "csv"s)
                    opts.format = Options::OutputFormat::CSV;
                else
                    std::cerr << "error: cannot output in '" << optarg << "' format\n";
                break;
            case 'h':
                usage();
                std::exit(0);
            case 'v':
                opts.ops.verify = 1;
                break;
            case '?':
                std::exit(1);
            default:
                break;
        }
    }

    if (optind >= subArgc)
    {
        usage();
        std::exit(1);
    }

    if (opts.ops.diff && subArgc - optind != 2)
    {
        std::cerr << "diff option (-D) requires exactly 2 journal file arguments.\n";
        std::exit(1);
    }

    opts.journals.insert(end(opts.journals), subArgv + optind, subArgv + subArgc);

    spdlog::info("journals ({}):", opts.journals.size());
    for (const auto &j : opts.journals)
        spdlog::info("\t{}", j);

    return opts;
}

void dumpBirthdate(const Journal &journal, const Options &opts)
{
    using namespace std;

    switch (opts.format)
    {
        case Options::OutputFormat::Standard:
            std::cout << fmt::format(
                "journal creation date: {}\n"
                , journal.creationDate().time_since_epoch().count());

            break;
        case Options::OutputFormat::CSV:
            std::cout << "# journal creation date\n";
            std::cout << journal.creationDate().time_since_epoch().count() << "\n";

            break;
    }
}

void dumpHashes(const Journal &journal, const Options &opts)
{
    switch (opts.format)
    {
        case Options::OutputFormat::Standard:
            for (const auto &rec : journal)
            {
                std::cout << fmt::format(
                    "{} @ {} for {}: {:#016x}\n"
                    , rec.fileId
                    , rec.offset
                    , rec.size
                    , rec.hash);
            }

            break;
        case Options::OutputFormat::CSV:
            for (const auto &rec : journal)
            {
                std::cout << fmt::format(
                    "{}, {}, {}, {}\n"
                    , rec.fileId
                    , rec.offset
                    , rec.size
                    , rec.hash);
            }

            break;
    }
}

void dumpDiff(const util::JournalFileDiff &diff, const Options &opts)
{
    switch (opts.format)
    {
        case Options::OutputFormat::Standard:
            for (const auto &mismatch : diff.diffs)
            {
                if (!!mismatch.hashA ^ !!mismatch.hashB)
                    std::cout << fmt::format("only in {}: ", mismatch.hashA ? "ours" : "theirs");

                std::cout << fmt::format(
                    "file {} @ offset {} for {}, us: {:#016x} them: {:#016x}\n"
                    , mismatch.fileId
                    , mismatch.offset
                    , mismatch.size
                    , mismatch.hashA
                    , mismatch.hashB);
            }

            break;
        case Options::OutputFormat::CSV:
            std::cout << "file_id, offset, size, us (base 16), them (base 16)\n";

            for (const auto &mismatch : diff.diffs)
            {
                std::cout << fmt::format(
                    "{}, {}, {}, {:016x}, {:016x}\n"
                    , mismatch.fileId
                    , mismatch.offset
                    , mismatch.size
                    , mismatch.hashA
                    , mismatch.hashB);
            }

            break;
    }
}

void verifyJournal(const Journal &journal, const Options &opts)
{
    auto diff = util::verifyJournal(journal);
    dumpDiff(diff, opts);
}

void dumpFileInfo(const Journal &journal, const Options &opts)
{
    const auto &info = journal.fileInfo();

    switch (opts.format)
    {
        case Options::OutputFormat::Standard:
            for (const auto &item : info)
            {
                std::cout << fmt::format(
                    "{}: {:o}\t{}\t{}\t{}\t{}\n"
                    , item.id
                    , item.status.mode
                    , item.status.uid
                    , item.status.gid
                    , item.status.size
                    , item.path);
            }

            break;
        case Options::OutputFormat::CSV:
            std::cout << "# file_id, mode, uid, gid, size, path\n";
            for (const auto &item : info)
            {
                std::cout << fmt::format(
                    "{}, {}, {}, {}, {}, {}\n"
                    , item.id
                    , item.status.mode
                    , item.status.uid
                    , item.status.gid
                    , item.status.size
                    , item.path);
            }

            break;
    }
}

void processJournal(const std::string &journalPath, const Options &opts)
{
    auto journal = Journal{journalPath};

    if (opts.ops.dumpBirthdate)
        dumpBirthdate(journal, opts);

    if (opts.ops.dumpInfo)
        dumpFileInfo(journal, opts);

    if (opts.ops.dumpHashes)
        dumpHashes(journal, opts);

    if (opts.ops.verify)
        verifyJournal(journal, opts);
}

void diffJournals(const Options &opts)
{
    if (opts.journals.size() < 2)
        std::cerr << "diff requires exactly 2 journal files.\n";

    auto journalA = Journal{opts.journals[0]};
    auto journalB = Journal{opts.journals[1]};

    auto diff = util::diffJournals(journalA, journalB);

    if (diff.diffs.empty())
        std::cout << "\t(no differences to display)\n";
    else
        dumpDiff(diff, opts);
}

}

int journal(int argc, char **argv)
{
    const auto opts = parseOptions(argc, argv);

    for (const auto &journal : opts.journals)
        processJournal(journal, opts);

    if (opts.ops.diff)
        diffJournals(opts);

    return 0;
}

}
