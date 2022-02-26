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

    spdlog::info("draft ui test");

    signal(SIGINT, handleSig);

    auto d = draft::ui::ProgressDisplay{ };

    while (!done_)
    {
        d.runOnce();
        std::this_thread::sleep_for(200ms);
    }

    spdlog::info("draft ui test exiting.");

    return 0;
}
