// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#pragma once

namespace Issue9Policy {
constexpr bool enabled(int darwin, bool liluStarted, bool off) {
    return liluStarted && !off && darwin >= 20 && darwin <= 24;
}
constexpr bool quarantined(unsigned vendor, unsigned device) {
    return vendor == 0x144D && device == 0xA80D;
}
constexpr bool mayClearFlag(int darwin, bool configured, bool eventCreated) {
    return darwin >= 23 && darwin <= 24 && configured && eventCreated;
}
}
