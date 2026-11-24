// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>

namespace xc::app {

// Receiver counters restart from zero when a new RTP session is created.
// Do not let unsigned subtraction turn that reset into a huge fake delta.
inline uint64_t counterDelta(uint64_t current, uint64_t previous) noexcept {
    return current >= previous ? current - previous : 0;
}

// Treat repeated local drops/late frames or work exceeding a 60 Hz frame
// budget as pipeline pressure; a single transient miss is not enough.
inline bool localVideoOverloaded(uint64_t droppedDelta, uint64_t lateDelta,
                                 uint64_t decodeAvgUs, uint64_t drawAvgUs) noexcept {
    constexpr uint64_t kFrameBudgetUs = 16667;
    return droppedDelta >= 3 || lateDelta >= 3 || decodeAvgUs > kFrameBudgetUs || drawAvgUs > kFrameBudgetUs;
}

// The UI's FPS is a presentation metric: count frames confirmed shown by the
// display, not decoded frames or calls to present().
inline double shownFps(uint64_t currentShown, uint64_t previousShown, double elapsedSeconds) noexcept {
    if (currentShown < previousShown || !(elapsedSeconds >= 0.05)) return 0.0;
    return static_cast<double>(currentShown - previousShown) / elapsedSeconds;
}

}  // namespace xc::app
