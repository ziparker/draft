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

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <system_error>

#include <sys/ioctl.h>
#include <termios.h>

#include "Stats.hh"
#include "Util.hh"

namespace draft::ui {

namespace term {

struct ColorNormal { };

struct CursorInvisible { };
struct CursorVisible { };

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
struct SaveCursorPosition { };
struct RestoreCursorPosition { };

struct EraseLine { };
struct EraseCursorToEnd { };

struct ETA { double duration{ }; };

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

inline std::ostream &operator<<(std::ostream &stream, const ETA &e)
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    const auto d = duration<double>{e.duration};

    if (d > 24h)
        return stream << "more than a day. is your network healthy?";

    if (d > 10h)
        return stream << "a long while (> 10h)";

    if (d > 5h)
        return stream << "a good while (> 5h)";

    if (d > 2h)
        return stream << "a while (> 2h)";

    const auto h = duration_cast<hours>(d);
    const auto m = duration_cast<minutes>(d - h);
    const auto s = duration_cast<seconds>(d - h - m);

    if (h > hours::zero())
        stream << h.count() << " h ";

    if (h > hours::zero() || m > minutes::zero())
        return stream << m.count() << " m ";

    // only show seconds if we're less than 1 minute.
    stream << s.count() << " s";

    return stream;
}

inline std::ostream &operator<<(std::ostream &s, const CursorInvisible &)
{
    return s << "\x1b[?25l";
}

inline std::ostream &operator<<(std::ostream &s, const CursorVisible &)
{
    return s << "\x1b[?25h";
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

inline std::ostream &operator<<(std::ostream &s, const SaveCursorPosition &)
{
    return s << "\x1b[s";
}

inline std::ostream &operator<<(std::ostream &s, const RestoreCursorPosition &)
{
    return s << "\x1b[u";
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
    return s << "\x1b[0J";
}

}

namespace io {

struct Progress
{
    unsigned startCol{ };
    unsigned endCol{ };
    float pct{ };
};

using namespace std::chrono_literals;

struct WhirlyState
{
public:
    static constexpr const char Chars[] = "|/-\\";

    WhirlyState():
        updateTime_{Clock::now() + 150ms}
    {
    }

    void tick()
    {
        const auto now = Clock::now();

        if (now < updateTime_)
            return;

        updateTime_ = now + 150ms;

        ++idx_;

        if (idx_ >= sizeof(Chars) - 1)
            idx_ = 0;
    }

    char get() const noexcept
    {
        return Chars[idx_];
    }

    void reset()
    {
        idx_ = 0;
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
    using Clock = std::chrono::steady_clock;

public:
    enum class LineType
    {
        Text,
        Progress
    };

    struct LineConfig
    {
        io::WhirlyState startChar{ }, endChar{ };
        Clock::time_point completionTime{ };
        float pct{ };
        unsigned row{ };
        unsigned startCol{ };
        unsigned endCol{ };
        LineType type{ };
    };

    ~ProgressDisplay() noexcept
    {
        std::cout << term::CursorVisible{ };

        if (initialTermState_)
            tcsetattr(1, TCSANOW, &initialTermState_.value());
    }

    void init()
    {
        // capture terminal state, if possible, so we can attempt to restore it later.
        initialTermState_.reset();
        if (termios state; !tcgetattr(1, &state))
            initialTermState_ = state;

        std::cout << std::unitbuf;
        std::cout << term::CursorInvisible{ };
    }

    void update()
    {
        using namespace std::chrono_literals;

        const auto winSz = term::winSize();

        std::cout <<
            term::CursorCol{0} <<
            term::SaveCursorPosition{ };

        for (auto &[name, conf] : lineMap_)
        {
            if (conf.pct >= 1.0)
            {
                conf.startChar.reset();
                conf.endChar.reset();
            }

            std::cout <<
                term::EraseLine{ } <<
                name <<
                term::CursorRight{2} <<
                conf.startChar.get() <<
                io::Progress{conf.startCol, std::min(winSz.cols, conf.endCol), conf.pct} <<
                conf.endChar.get() <<
                term::CursorBeginDown{1};

            conf.startChar.tick();
            conf.endChar.tick();
        }

        std::cout << term::EraseLine{ } << term::CursorBeginDown{1} <<
            term::EraseLine{ } << "ETA: " << std::setprecision(1) << std::fixed << term::ETA{globalEta_};

        std::cout <<
            term::CursorBeginDown{1} <<
            term::EraseCursorToEnd{ } <<
            term::RestoreCursorPosition{ };

        // erase expired items.
        std::erase_if(lineMap_,
            [now = Clock::now()](const auto &kv) {
                const auto &conf = kv.second;
                return conf.pct >= 1.0f && now - conf.completionTime >= 1s;
            });
    }

    void updateBandwidth(double bps)
    {
        globalBw_ = bps;
    }

    void updateEta(double sec)
    {
        globalEta_ = .7 * globalEta_ + .3 * sec;
    }

    void update(const std::string &key, float pct)
    {
        auto iter = lineMap_.find(key);

        if (iter == end(lineMap_))
            return;

        auto &line = iter->second;

        line.pct = pct;

        if (pct >= 1.0f && line.completionTime == Clock::time_point{ })
            line.completionTime = Clock::now();
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
    }

    void complete()
    {
        globalEta_ = 0;
        update();

        std::cout << term::CursorBeginDown(2) << "\n";
    }

private:
    std::map<std::string, LineConfig> lineMap_;
    unsigned rows_{ };
    double globalEta_{ };
    double globalBw_{ };
    bool updateWinSz_{ };

    std::optional<termios> initialTermState_{ };
};

}

#endif
