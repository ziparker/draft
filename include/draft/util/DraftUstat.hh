/**
 * @file DraftUstat.hh
 *
 * Licensed under the MIT License <https://opensource.org/licenses/MIT>.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2023 Zachary Parker
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

#ifndef __DRAFT_USTAT_HH__
#define __DRAFT_USTAT_HH__

#ifdef DRAFT_HAVE_USTAT
# include <ustat/ustat.hh>
# include <ustat/util/scoped_timer.hh>
#endif
#include <spdlog/spdlog.h>

namespace draft::metric {

#ifdef DRAFT_HAVE_USTAT
using ::ustat::metric;
using ::ustat::counter_metric;

inline void configure()
{
    using namespace ustat;
    ustat::configuration()
        .set("pj_ip", ustat::get_env("PJ_IP").value_or("127.0.0.1"))
        .set("pj_port", ustat::get_env<int>("PJ_PORT").value_or(9870));
}

using ustat::metric;
using ustat::counter_metric;
using ustat::util::scoped_timer_metric;
#else
inline void configure(...) { spdlog::warn("nope"); }

struct metric
{
    int publish(...) { return 0; }
};

struct counter_metric
{
    int increment(...) { return 0; }
    int decrement(...) { return 0; }
};

struct scoped_timer
{
    double elapsedFloatTime() const noexcept { return 0.0; };
};

inline scoped_timer scoped_counter_metric(...) { return { }; }
#endif

}

#endif
