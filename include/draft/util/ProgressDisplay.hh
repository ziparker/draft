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

#include <math.h>
#include <sys/ioctl.h>

#include "Stats.hh"
#include "Util.hh"

namespace draft::ui {

namespace term {

struct ColorNormal { };

struct ClearScreen { };
struct CursorHome { };
struct CursorPosition { size_t row{ }, col{ }; };
struct CursorUp { };
struct CursorDown { };
struct CursorRight { size_t cols{ }; };
struct CursorLeft { };
struct CursorCol { size_t col{ }; };
struct CursorBeginDown { size_t lines{ }; };
struct CursorBeginUp { };

struct EraseLine { };
struct EraseCursorToEnd { };

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

inline std::ostream &operator<<(std::ostream &s, const CursorPosition &c)
{
    return s << "\x1b[" << c.row << ';' << c.col << 'f';
}

inline std::ostream &operator<<(std::ostream &s, const CursorRight &c)
{
    return s << "\x1b[" << c.cols << 'C';
}

inline std::ostream &operator<<(std::ostream &s, const CursorBeginDown &c)
{
    return s << "\x1b[" << c.lines << 'E';
}

inline std::ostream &operator<<(std::ostream &s, const CursorCol &c)
{
    return s << "\x1b[" << c.col << 'G';
}

inline std::ostream &operator<<(std::ostream &s, const EraseLine &)
{
    return s << "\x1b[2K";
}

inline std::ostream &operator<<(std::ostream &s, const EraseCursorToEnd &)
{
    return s << "\x1b[1J";
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

using namespace std::chrono_literals;

struct WhirlyState
{
public:
    static constexpr const char Chars[] = "|/-\\";

    WhirlyState():
        updateTime_{Clock::now() + 250ms}
    {
    }

    void tick()
    {
        const auto now = Clock::now();

        if (now < updateTime_)
            return;

        updateTime_ = now + 250ms;

        ++idx_;

        if (idx_ >= sizeof(Chars) - 1)
            idx_ = 0;
    }

    char get() const noexcept
    {
        return Chars[idx_];
    }

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point updateTime_;
    size_t idx_{ };
};

inline std::ostream &operator<<(std::ostream &s, const Progress &p)
{
    const size_t len = p.startCol < p.endCol ? p.endCol - p.startCol : 0;

    auto pctLen = static_cast<size_t>(std::round(len * p.pct));
    pctLen = std::clamp(pctLen, size_t{ }, len);

    const auto meter = std::string(pctLen, '=');

    return s << meter << term::CursorCol(p.endCol + 1);
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
        io::WhirlyState startChar{ }, endChar{ };
        float pct{ };
        unsigned row{ };
        unsigned startCol{ };
        unsigned endCol{ };
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

        unsigned row = 1;
        for (auto &[name, conf] : lineMap_)
        {
            std::cout <<
                term::CursorPosition{row, 0} <<
                term::EraseLine{ } <<
                name <<
                term::CursorRight{2} <<
                conf.startChar.get() <<
                io::Progress{conf.startCol, std::min(winSz.cols, conf.endCol), row, conf.pct} <<
                conf.endChar.get() <<
                term::CursorBeginDown{0} <<
                term::EraseLine{ };

            conf.startChar.tick();
            conf.endChar.tick();

            ++row;
        }

        std::cout << term::CursorBeginDown{1};
    }

    void update(const std::string &key, float pct)
    {
        lineMap_[key].pct = pct;
    }

    void add(const std::string &key, float pct = 0.0f)
    {
        auto &conf = lineMap_[key];
        conf.pct = std::clamp(pct, 0.0f, 1.0f);
        conf.row = rows_++;
        // leave room for space + start char.
        conf.startCol = key.size() + 2;
        conf.endCol = 120;
    }

    void remove(const std::string &key)
    {
        lineMap_.erase(key);

        std::cout <<
            term::CursorPosition{lineMap_.size(), 0} <<
            term::EraseLine{ };
    }

private:
    std::map<std::string, LineConfig> lineMap_;
    unsigned rows_{ };
    bool updateWinSz_{ };
};

}

#endif
