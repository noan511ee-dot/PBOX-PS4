// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include "platform/platform.h"
#include "util/json.h"
#include "util/log.h"

#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>

namespace xc::xcloud {
namespace detail {
inline bool sensitiveJsonKey(const std::string& key) {
    std::string lower;
    lower.reserve(key.size());
    for (unsigned char c : key) lower.push_back(static_cast<char>(std::tolower(c)));
    auto contains = [&](const char* needle) { return lower.find(needle) != std::string::npos; };
    return contains("token") || contains("authorization") || contains("credential") || contains("password") ||
           contains("secret") || contains("cookie") || contains("sessionpath") || contains("sessionid") ||
           contains("clientsessionid") || contains("serverid") || lower == "sdp" || contains("icecandidate") ||
           lower == "candidate" || lower == "ip" || contains("ipaddress") || lower == "address";
}

inline json::Value redactServiceJson(const json::Value& value) {
    if (value.isObject()) {
        json::Value clean = json::Value::object();
        for (const auto& [key, child] : value.members()) {
            clean.set(key, sensitiveJsonKey(key) ? json::Value("<redacted>") : redactServiceJson(child));
        }
        return clean;
    }
    if (value.isArray()) {
        json::Value clean = json::Value::array();
        for (const auto& child : value.items()) clean.push(redactServiceJson(child));
        return clean;
    }
    return value;
}
}  // namespace detail

// Saves only the selected entitlement/catalog responses and failed session starts.
// The file is private to the app's data directory and bounded to avoid filling storage.
inline void logServiceJson(const std::string& endpoint, int status, const std::string& body) {
    auto parsed = json::parse(body);
    const std::string payload = parsed ? detail::redactServiceJson(*parsed).dump() : std::string();
    std::string directory = platform::dataDir();
    if (directory.empty()) directory = "/tmp";
    const std::string path = directory + "/xcloud-json.log";
    static std::mutex mutex;
    constexpr long kMaxFileBytes = 8 * 1024 * 1024;
    constexpr size_t kMaxResponseBytes = 2 * 1024 * 1024;
    std::lock_guard<std::mutex> lock(mutex);
    FILE* file = std::fopen(path.c_str(), "a+");
    if (!file) {
        XC_LOGW("xCloud diagnostics: could not open %s", path.c_str());
        return;
    }
    std::fseek(file, 0, SEEK_END);
    long current = std::ftell(file);
    if (current < 0) current = 0;
    if (current > kMaxFileBytes) {
        std::fclose(file);
        file = std::fopen(path.c_str(), "w");
        if (!file) {
            XC_LOGW("xCloud diagnostics: could not rotate %s", path.c_str());
            return;
        }
        std::fputs("[older diagnostic records removed to keep this file bounded]\n", file);
    }
    const std::string safeEndpoint = json::escape(endpoint);
    std::fprintf(file, "{\"endpoint\":%s,\"httpStatus\":%d,\"response\":", safeEndpoint.c_str(), status);
    if (parsed && payload.size() <= kMaxResponseBytes) {
        std::fputs(payload.c_str(), file);
    } else if (parsed) {
        std::fputs("\"<valid JSON omitted: response exceeded diagnostic limit>\"", file);
    } else {
        std::fprintf(file, "\"<response was not valid JSON; %zu bytes omitted>\"", body.size());
    }
    std::fputs("}\n", file);
    std::fflush(file);
    std::fclose(file);
}
}  // namespace xc::xcloud
