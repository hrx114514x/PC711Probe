// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace ResourceObservation {
constexpr unsigned ScanLimit = 64;
struct Summary {
    unsigned count {0}, scanned {0}, invalid {0}, matches {0};
    uint64_t firstTag {0}, firstLength {0};
    bool truncated {false};
};
template<class Reader> Summary summarize(Reader &reader) {
    Summary out;
    out.count = reader.count();
    out.truncated = out.count > ScanLimit;
    const unsigned limit = out.truncated ? ScanLimit : out.count;
    for (unsigned i=0; i<limit; ++i) {
        uint64_t tag=0, length=0;
        ++out.scanned;
        if (!reader.descriptor(i,tag,length)) { ++out.invalid; continue; }
        if ((tag & 0xFFU) == 0x10U) {
            if (!out.matches) { out.firstTag=tag; out.firstLength=length; }
            ++out.matches;
        }
    }
    return out;
}
constexpr bool usableBarEncoding(uint32_t low) {
    return low != 0xFFFFFFFFU && !(low & 1U) && ((low & 6U)==0 || (low & 6U)==4);
}
constexpr uint64_t barBase(uint32_t low, uint32_t high) {
    return static_cast<uint64_t>(low & 0xFFFFFFF0U) |
        ((low & 6U)==4 ? static_cast<uint64_t>(high)<<32 : 0);
}
constexpr uint64_t bridgeBase(uint16_t value) {
    return static_cast<uint64_t>(value & 0xFFF0U)<<16;
}
constexpr uint64_t bridgeLimit(uint16_t value) {
    return (static_cast<uint64_t>(value & 0xFFF0U)<<16) | 0xFFFFFULL;
}
// 0 unknown/not assessed, 1 inside, 2 outside. Only the ordinary non-prefetchable
// bridge window is assessed. A result is an observation, not a repair decision.
constexpr unsigned fit(uint32_t low, uint32_t high, uint64_t length,
                       bool uniqueDescriptor, uint16_t baseReg, uint16_t limitReg) {
    const auto base=barBase(low,high);
    const auto lower=bridgeBase(baseReg), upper=bridgeLimit(limitReg);
    return !uniqueDescriptor || !usableBarEncoding(low) || (low & 8U) || !base ||
        !length || (baseReg & 0xFU) || (limitReg & 0xFU) || lower>upper || length-1>UINT64_MAX-base ? 0 :
        (base>=lower && base+length-1<=upper ? 1 : 2);
}
}
