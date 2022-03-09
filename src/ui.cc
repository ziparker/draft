#include <chrono>
#include <cstdio>
#include <thread>

#include <signal.h>

#include <draft/util/ProgressDisplay.hh>
#include <draft/util/Stats.hh>
#include <spdlog/spdlog.h>

namespace {

sig_atomic_t done_;

void handleSig(int)
{
    done_ = 1;
}

}

int main()
{
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;

    namespace util = draft::util;

    signal(SIGINT, handleSig);

    auto p = draft::ui::ProgressDisplay{ };
    p.init();

    p.add("foo");
    p.add("bar baz path");
    p.add("baz");

    auto updateTime = Clock::now() + 1s;
    float fooPct{ }, barPct{ };
    while (!done_)
    {
        if (auto now = Clock::now(); now > updateTime)
        {
            p.update("foo", fooPct += .1);
            p.update("bar baz path", barPct += .05);
            updateTime = now + 1s;

            p.remove("baz");
        }

        p.update();
        std::this_thread::sleep_for(100ms);
    }

    return 0;
}
