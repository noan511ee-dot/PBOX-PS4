// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "platform/platform.h"
#include "platform/ime_text.h"
#include "util/log.h"

#include <orbis/ImeDialog.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
int sceSysmoduleLoadModule(uint32_t id);
int sceSysmoduleLoadModuleInternal(uint32_t id);
int sceCommonDialogInitialize(void);
int sceUserServiceGetInitialUser(int32_t* userId);
}

namespace xc::platform {

namespace {

static_assert(sizeof(wchar_t) == 4, "PS4 IME text buffers use 32-bit wchar_t");
static_assert(sizeof(OrbisImeDialogSetting) == 96, "Unexpected PS4 IME dialog ABI");

int g_available = -1;
bool g_open = false;
std::wstring g_title;
std::vector<wchar_t> g_buffer;

}  // namespace

bool systemKeyboardAvailable() {
    if (g_available >= 0) return g_available == 1;
    sceSysmoduleLoadModuleInternal(0x80000018); // COMMON_DIALOG
    sceSysmoduleLoadModule(0x0096);             // IME_DIALOG
    int rc = sceCommonDialogInitialize();
    g_available = (rc >= 0 || rc == static_cast<int>(0x80610005)) ? 1 : 0;
    return g_available == 1;
}

bool openSystemKeyboard(const std::string& title, const std::string& text, size_t maxLength, KeyboardKind kind) {
    if (!systemKeyboardAvailable() || g_open) return false;
    int32_t user = -1;
    int userRc = sceUserServiceGetInitialUser(&user);
    if (userRc < 0 || user < 0) {
        XC_LOGW("keyboard: no active PS4 user (0x%08x)", static_cast<unsigned>(userRc));
        return false;
    }

    if (maxLength == 0 || maxLength > 2048) maxLength = 2048;
    ime_text::utf8ToWide(title, g_title);
    std::wstring initial;
    ime_text::utf8ToWide(text, initial);
    if (initial.size() > maxLength) initial.resize(maxLength);

    g_buffer.assign(maxLength + 1, L'\0');
    std::memcpy(g_buffer.data(), initial.data(), initial.size() * sizeof(wchar_t));

    OrbisImeDialogSetting p{};
    p.userId = user;
    p.supportedLanguages = 0;
    switch (kind) {
        case KeyboardKind::Text: p.type = ORBIS_TYPE_DEFAULT; break;
        // Passwords for publisher accounts are usually Latin. Request the
        // basic-Latin layout explicitly while retaining PS4's masked-entry flag.
        case KeyboardKind::Password: p.type = ORBIS_TYPE_BASIC_LATIN; p.option = 0x4; break;
        case KeyboardKind::Number: p.type = ORBIS_TYPE_NUMBER; break;
        case KeyboardKind::Email: p.type = ORBIS_TYPE_MAIL; break;
        case KeyboardKind::Url: p.type = ORBIS_TYPE_TYPE_URL; break;
    }
    p.maxTextLength = static_cast<uint32_t>(maxLength);
    p.inputTextBuffer = g_buffer.data();
    p.posx = 1920 / 2.0f;
    p.posy = 1080 / 2.0f;
    p.horizontalAlignment = ORBIS_H_CENTER;
    p.verticalAlignment = ORBIS_V_CENTER;
    p.title = g_title.c_str();

    int rc = sceImeDialogInit(&p, nullptr);
    if (rc != 0) {
        XC_LOGW("sceImeDialogInit failed (kind %d): 0x%08x", static_cast<int>(kind), static_cast<unsigned>(rc));
        return false;
    }
    g_open = true;
    XC_LOGI("keyboard: opened (kind %d, max length %zu)", static_cast<int>(kind), maxLength);
    return true;
}

KeyboardStatus pollSystemKeyboard(std::string& text) {
    if (!g_open) return KeyboardStatus::Closed;
    OrbisDialogStatus status = sceImeDialogGetStatus();
    if (status == ORBIS_DIALOG_STATUS_RUNNING) return KeyboardStatus::Open;
    OrbisDialogResult res{};
    int rc = sceImeDialogGetResult(&res);
    sceImeDialogTerm();
    g_open = false;
    XC_LOGI("keyboard: closed (status %d, result 0x%08x, end %d)",
            status, static_cast<unsigned>(rc), res.endstatus);
    if (status != ORBIS_DIALOG_STATUS_STOPPED || rc != 0 || res.endstatus != ORBIS_DIALOG_OK)
        return KeyboardStatus::Cancelled;
    ime_text::wideToUtf8(g_buffer.data(), text);
    return KeyboardStatus::Accepted;
}

}  // namespace xc::platform
