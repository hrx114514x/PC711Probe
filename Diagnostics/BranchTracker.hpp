// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace BranchObservation {
enum : unsigned { Provider = 1, Map = 2, Address = 4, Mappers = 8 };
struct Frame {
    uintptr_t thread {0};
    uintptr_t controller {0};
    uintptr_t provider {0};
    bool target {false};
    uint64_t call {0}, cid {0}, pid {0};
    unsigned branches {0};
    bool registered {false};
    Frame *next {nullptr};
};

// The kernel caller supplies a short-held lock. No allocation or logging here.
// Every InitializePCI call, including off-target calls, pushes a frame so nested
// off-target failures cannot be attributed to a target call on the same thread.
template<size_t Capacity> class Tracker {
    Frame *head {nullptr};
    size_t count {0}, overflow {0};
public:
    bool push(Frame &frame) {
        if (count >= Capacity) {
            ++overflow;
            return false;
        }
        frame.next = head;
        frame.registered = true;
        head = &frame;
        ++count;
        return true;
    }
    void pop(Frame &frame) {
        if (!frame.registered) {
            if (overflow) --overflow;
            return;
        }
        auto cursor = &head;
        while (*cursor && *cursor != &frame) cursor = &(*cursor)->next;
        if (*cursor) {
            *cursor = frame.next;
            --count;
        }
        frame.registered = false;
        frame.next = nullptr;
    }
    Frame *mark(uintptr_t thread, uintptr_t controller, unsigned branch) {
        // Any untracked nested call could otherwise be mistaken for its parent.
        if (overflow) return nullptr;
        for (auto frame = head; frame; frame = frame->next) {
            if (frame->thread != thread) continue;
            if (!frame->target || (controller && controller != frame->controller)) return nullptr;
            frame->branches |= branch;
            return frame;
        }
        return nullptr;
    }
};
}
