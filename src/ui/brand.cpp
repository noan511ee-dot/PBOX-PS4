// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Customizations & PS4 edition branding by Nawaf Al-Otaibi (Ravok)
#include "ui/brand.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace xc::ui {

namespace {

constexpr const char* kDefaultBrand = "PBOX";
constexpr const char* kDefaultSubtitle = "PS4 Edition — Nawaf Al-Otaibi (Ravok)";
constexpr const char* kEnvBrand = "XC_BRAND_NAME";
constexpr const char* kEnvSubtitle = "XC_BRAND_SUBTITLE";
constexpr const char* kEnvAccent = "XC_BRAND_ACCENT";

std::string trimAscii(std::string s) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

std::string toLowerAscii(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool parseHexColor(const std::string& raw, uint32_t& outRgba) {
    std::string s = trimAscii(raw);
    if (s.empty()) return false;
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s.erase(0, 2);
    else if (!s.empty() && s.front() == '#') s.erase(0, 1);

    if (s.size() != 6 && s.size() != 8) return false;
    for (char ch : s) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    }

    char* end = nullptr;
    unsigned long val = std::strtoul(s.c_str(), &end, 16);
    if (!end || *end != '\0') return false;

    if (s.size() == 6) {
        outRgba = (static_cast<uint32_t>(val) << 8) | 0xFFu;
    } else {
        outRgba = static_cast<uint32_t>(val);
    }
    return true;
}

bool parseNamedAccent(const std::string& raw, uint32_t& outRgba) {
    std::string k = toLowerAscii(trimAscii(raw));
    if (k.empty()) return false;
    if (k == "green" || k == "xbox") {
        outRgba = 0x107C10FFu;
        return true;
    }
    if (k == "blue" || k == "ps4" || k == "playstation") {
        outRgba = 0x006FCDFFu;
        return true;
    }
    if (k == "purple" || k == "violet" || k == "ravok") {
        outRgba = 0x7A3EE6FFu;
        return true;
    }
    if (k == "amber" || k == "gold") {
        outRgba = 0xD98218FFu;
        return true;
    }
    if (k == "teal" || k == "cyan") {
        outRgba = 0x0D9488FFu;
        return true;
    }
    if (k == "crimson" || k == "red") {
        outRgba = 0xC83232FFu;
        return true;
    }
    return false;
}

}  // namespace

const BrandConfig& brandConfig() {
    static const BrandConfig cfg = [] {
        BrandConfig out;
        out.appName = kDefaultBrand;
        out.subtitle = kDefaultSubtitle;

        if (const char* envName = std::getenv(kEnvBrand)) {
            std::string v = trimAscii(envName);
            if (!v.empty() && v.size() <= 32) out.appName = v;
        }
        if (const char* envSub = std::getenv(kEnvSubtitle)) {
            std::string v = trimAscii(envSub);
            if (!v.empty() && v.size() <= 64) out.subtitle = v;
        }
        if (const char* envAcc = std::getenv(kEnvAccent)) {
            uint32_t parsed = 0;
            if (parseNamedAccent(envAcc, parsed) || parseHexColor(envAcc, parsed)) {
                out.accentRgba = parsed;
                out.hasCustomAccent = true;
            }
        }
        return out;
    }();
    return cfg;
}

const char* brandAppName() { return brandConfig().appName.c_str(); }

const char* brandSubtitle() { return brandConfig().subtitle.c_str(); }

bool isCustomBrand() {
    const auto& cfg = brandConfig();
    return cfg.appName != kDefaultBrand || cfg.subtitle != kDefaultSubtitle || cfg.hasCustomAccent;
}

}  // namespace xc::ui
