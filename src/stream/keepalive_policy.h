// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstdint>

namespace xc::stream {

// A single transient cloud-service or network error should not silently leave
// a live session without keepalives. Retry twice, then let the existing
// recoverable-close path recreate the WebRTC session.
inline constexpr unsigned kKeepaliveMaxAttempts = 3;
inline constexpr uint64_t kKeepaliveSpawnRetryDelayMs = 5000;

// `failedAttempts` is one-based and counts the just-failed request.
inline constexpr uint64_t keepaliveRetryDelayMs(unsigned failedAttempts) noexcept {
    return failedAttempts == 1 ? 1000 : failedAttempts == 2 ? 2000 : 0;
}

}  // namespace xc::stream
