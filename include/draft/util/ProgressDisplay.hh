/* @file ProgressDisplay.hh
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

#ifndef __DRAFT_UTIL_PROGRESS_DISPLAY_HH__
#define __DRAFT_UTIL_PROGRESS_DISPLAY_HH__

#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <system_error>

#include <sys/ioctl.h>

#include "Stats.hh"
#include "Util.hh"

namespace draft::ui {

namespace term {

struct ColorNormal { };

struct ClearScreen { };
struct CursorHome { };
struct CursorUp { };
struct CursorDown { };
struct CursorRight { };
struct CursorLeft { };
struct CursorBeginDown { unsigned lines{ }; };
struct CursorBeginUp { };

struct EraseLine { };

struct WinSize
{
    unsigned rows{ };
    unsigned cols{ };
};

inline WinSize winSize()
{
    auto sz = ::winsize{ };

    if (ioctl(1, TIOCGWINSZ, &sz) < 0)
        throw std::system_error(errno, std::system_category(), "tiocgwinsz");

    return {sz.ws_row, sz.ws_col};
}

inline std::ostream &operator<<(std::ostream &s, const ClearScreen &)
{
    return s << "\x1b[2J";
}

inline std::ostream &operator<<(std::ostream &s, const CursorHome &)
{
    return s << "\x1b[H";
}

inline std::ostream &operator<<(std::ostream &s, const CursorBeginDown &c)
{
    return s << "\x1b[" << c.lines << "E";
}

inline std::ostream &operator<<(std::ostream &s, const EraseLine &)
{
    return s << "\x1b[2K";
}

}

namespace io {

struct Progress
{
    unsigned startCol{ };
    unsigned endCol{ };
    unsigned row{ };
    float pct{ };
};

inline std::ostream &operator<<(std::ostream &s, const Progress &p)
{
    const size_t len = p.startCol < p.endCol ? p.endCol - p.startCol : 0;
    const auto meter = std::string(len, '=');

    return s << "\x1b[" << p.row << ";" << p.startCol << 'H' << meter;
}

}

class ProgressDisplay
{
public:
    enum class LineType
    {
        Text,
        Progress
    };

    struct LineConfig
    {
        float pct{ };
        unsigned row{ };
        LineType type{ };
    };

    void init()
    {
        std::cout <<
            std::unitbuf <<
            term::ClearScreen{ } << term::CursorHome{ };
    }

    void update()
    {
        const auto winSz = term::winSize();

        for (const auto &[name, conf] : lineMap_)
            std::cout << io::Progress{10, winSz.cols, conf.row, conf.pct};
    }

    void update(const std::string &key, float pct)
    {
        lineMap_[key].pct = pct;
    }

private:
    std::map<std::string, LineConfig> lineMap_;
    bool updateWinSz_{ };
};

}

#endif
