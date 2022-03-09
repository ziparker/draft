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
    namespace util = draft::util;

    signal(SIGINT, handleSig);

    auto p = draft::ui::ProgressDisplay{ };
    p.init();

    p.update("foo", 0);

    while (!done_)
    {
        p.update();
        std::this_thread::sleep_for(100ms);
    }

    return 0;
}
