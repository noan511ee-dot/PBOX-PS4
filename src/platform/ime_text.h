// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xc::platform::ime_text {

inline void utf8ToWide(const std::string& input, std::wstring& output) {
    output.clear();
    output.reserve(input.size());
    std::size_t i = 0;
    while (i < input.size()) {
        const auto lead = static_cast<uint8_t>(input[i]);
        uint32_t cp = 0xFFFD;
        std::size_t length = 1;
        uint32_t minimum = 0;
        if (lead < 0x80) {
            cp = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            cp = lead & 0x1F;
            length = 2;
            minimum = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            cp = lead & 0x0F;
            length = 3;
            minimum = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            cp = lead & 0x07;
            length = 4;
            minimum = 0x10000;
        }

        bool valid = length == 1 || i + length <= input.size();
        if (valid && length > 1) {
            for (std::size_t j = 1; j < length; ++j) {
                const auto next = static_cast<uint8_t>(input[i + j]);
                if ((next & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
                cp = (cp << 6) | (next & 0x3F);
            }
            valid = valid && cp >= minimum && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
        }
        if (!valid) {
            cp = 0xFFFD;
            length = 1;
        }
        output.push_back(static_cast<wchar_t>(cp));
        i += length;
    }
}

inline void wideToUtf8(const wchar_t* input, std::string& output) {
    output.clear();
    if (!input) return;
    for (std::size_t i = 0; input[i] != 0; ++i) {
        uint32_t cp = static_cast<uint32_t>(input[i]);
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        if (cp < 0x80) {
            output.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            output.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            output.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
}

}  // namespace xc::platform::ime_text
