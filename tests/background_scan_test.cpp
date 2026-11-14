#include "ui/background_scan.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <stdexcept>

using namespace std::chrono_literals;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
template<class F> void eventually(F f) {
    const auto limit=std::chrono::steady_clock::now()+2s;
    while (!f()) {
        check(std::chrono::steady_clock::now()<limit,"worker failed to finish");
        std::this_thread::sleep_for(1ms);
    }
}
int main() {
    nx::ui::BackgroundScan<int> scan;
    std::promise<void> started, release;
    auto start=started.get_future();
    auto gate=release.get_future().share();
    std::atomic<int> calls{0};
    auto blocked=[&] { ++calls; started.set_value(); gate.wait(); return 7; };
    scan.poll(0,blocked);
    check(start.wait_for(2s)==std::future_status::ready,"scan did not start");
    // These calls must return even though the worker cannot finish yet.
    for (int i=0;i<1000;++i) scan.poll(100,blocked);
    check(calls==1 && scan.running() && scan.value()==0,"overlapping scan or premature result");
    release.set_value();
    eventually([&] { scan.poll(100,blocked); return !scan.running(); });
    check(scan.value()==7,"completed result was not published");
    scan.poll(104.9,blocked);
    check(!scan.running() && calls==1,"cache refreshed before its deadline");

    std::promise<void> release_refresh;
    auto refresh_gate=release_refresh.get_future().share();
    auto refresh=[&] { ++calls; refresh_gate.wait(); return 9; };
    scan.poll(105,refresh);
    check(scan.running() && scan.value()==7,"refresh discarded cached results");
    release_refresh.set_value();
    eventually([&] { scan.poll(105,refresh); return !scan.running(); });
    check(scan.value()==9 && calls==2,"refresh result missing");
    auto failure=[]()->int { throw std::runtime_error("unavailable drive"); };
    scan.poll(110,failure);
    eventually([&] { scan.poll(110,failure); return !scan.running(); });
    check(scan.value()==9,"failed scan discarded cached result");

    // Closing the owner must not join a worker stuck in filesystem I/O.
    std::promise<void> gone, finish;
    auto finish_gate=finish.get_future().share();
    auto done=gone.get_future();
    {
        nx::ui::BackgroundScan<int> closing;
        closing.poll(0,[finish_gate, &gone] { finish_gate.wait(); gone.set_value(); return 1; });
    }
    finish.set_value();
    check(done.wait_for(2s)==std::future_status::ready,"worker lost its storage after owner closed");
    std::puts("background scan: stalled worker, caching, single flight, failure and close passed");
}
