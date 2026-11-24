// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xc::app::localpin {

constexpr size_t kMinDigits = 4;
constexpr size_t kMaxDigits = 8;
constexpr size_t kSaltBytes = 16;
constexpr size_t kVerifierBytes = 32;
constexpr uint32_t kIterations = 200000;

bool validPin(const std::string& pin);
bool derive(const std::string& pin, const uint8_t* salt, size_t saltSize, uint8_t* out, size_t outSize);
bool enabled(const std::string& path);
bool set(const std::string& path, const std::string& pin);
bool verify(const std::string& path, const std::string& pin);
bool clear(const std::string& path);

}  // namespace xc::app::localpin
