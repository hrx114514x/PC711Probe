// SPDX-License-Identifier: MIT
#pragma once
namespace ReadOnlyPolicy {
constexpr bool allowed(int major, int minor, int revision, bool started, bool optIn, bool off) {
    return major == 24 && minor == 6 && revision == 0 && started && optIn && !off;
}
constexpr bool matches(unsigned vendor, unsigned device, unsigned classRevision) {
    return vendor == 0x144D && device == 0xA80D &&
        (classRevision & 0xFFFFFF00U) == 0x01080200U;
}
// Reader supplies read8(offset). Standard capabilities occupy 0x40..0xFC.
// Never follow a cycle or malformed pointer indefinitely; zero means not found
// or malformed, not proof that the hardware lacks this capability.
template<class Reader> unsigned findCapability(Reader &reader, unsigned id) {
    unsigned long long visited=0;
    unsigned offset=reader.read8(0x34);
    for (unsigned count=0; count<48 && offset; ++count) {
        if (offset < 0x40 || offset > 0xFC || (offset & 3)) return 0;
        const auto bit=1ULL << (offset/4-16);
        if (visited & bit) return 0;
        visited |= bit;
        if (reader.read8(offset) == id) return offset;
        offset=reader.read8(offset+1);
    }
    return 0;
}
}
