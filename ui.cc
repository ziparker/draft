#include <thread>

#include <signal.h>

#include <draft/util/ProgressDisplay.hh>
#include <spdlog/spdlog.h>

namespace {

sig_atomic_t done_;

void handleSig(int)
{
    done_ = true;
}

}

int main()
{
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;

    spdlog::info("draft ui test");

    signal(SIGINT, handleSig);

    auto d = draft::ui::ProgressDisplay{ };

    auto printTime = Clock::now() + 1s;
    while (!done_)
    {
        if (auto now = Clock::now(); now >= printTime)
        {
            spdlog::info("time: {:.3f}", Duration(now.time_since_epoch()).count());
            printTime = now + 1s;
        }

        d.runOnce();
        std::this_thread::sleep_for(100ms);
    }

    spdlog::info("draft ui test exiting.");

    return 0;
}
