#include "../Diagnostics/BranchTracker.hpp"
#include <cassert>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
using namespace BranchObservation;

Frame make(uintptr_t thread, uintptr_t controller, bool target) {
    Frame frame;
    frame.thread=thread; frame.controller=controller; frame.target=target;
    return frame;
}

int main() {
    Tracker<4> state;
    auto a=make(1,100,true), b=make(1,200,false), c=make(2,300,true), d=make(1,400,true);
    assert(!state.mark(1,0,Map));
    assert(state.push(a));
    assert(state.mark(1,0,Map)==&a && a.branches==Map);
    assert(state.push(b));
    assert(!state.mark(1,0,Address)); // Nested off-target call shadows target.
    assert(state.push(c));
    assert(state.mark(2,300,Mappers)==&c);
    assert(!state.mark(2,100,Mappers)); // Explicit mapper owner must match.
    assert(state.push(d));
    assert(state.mark(1,400,Address)==&d);
    assert(!state.mark(1,100,Provider));
    state.pop(c); // Interleaved thread may finish from the middle of the list.
    state.pop(d);
    assert(!state.mark(1,0,Address));
    state.pop(b);
    assert(state.mark(1,100,Provider)==&a);
    assert(a.branches==(Map|Provider));
    state.pop(a);
    assert(!state.mark(1,0,Map));

    Tracker<1> small;
    auto one=make(1,100,true), overflow1=make(1,200,false), overflow2=make(2,300,true);
    assert(small.push(one));
    assert(!small.push(overflow1));
    assert(!small.push(overflow2));
    assert(!small.mark(1,0,Map));
    small.pop(overflow1);
    assert(!small.mark(1,0,Map));
    small.pop(overflow2);
    assert(small.mark(1,100,Map)==&one);
    small.pop(one);

    // Same operations as the kernel: lock only while updating/looking up frames,
    // never across the original call. Exercise interleaved thread lifetimes.
    Tracker<32> parallel;
    std::mutex lock;
    std::vector<std::thread> workers;
    for(uintptr_t thread=1;thread<=8;++thread) workers.emplace_back([&,thread] {
        for(unsigned i=0;i<1000;++i) {
            auto outer=make(thread,thread*100,true);
            auto inner=make(thread,thread*100+1,false);
            { std::lock_guard<std::mutex> guard(lock); assert(parallel.push(outer)); }
            std::this_thread::yield();
            { std::lock_guard<std::mutex> guard(lock); assert(parallel.push(inner)); }
            { std::lock_guard<std::mutex> guard(lock); assert(!parallel.mark(thread,0,Address)); }
            { std::lock_guard<std::mutex> guard(lock); parallel.pop(inner); }
            { std::lock_guard<std::mutex> guard(lock); assert(parallel.mark(thread,thread*100,Map)==&outer); }
            { std::lock_guard<std::mutex> guard(lock); parallel.pop(outer); }
            assert(outer.branches==Map);
        }
    });
    for(auto &worker:workers) worker.join();
    assert(!parallel.mark(1,0,Map));
    std::puts("PASS: branch attribution, nesting, overflow and 8000 interleaved lifetimes");
}
