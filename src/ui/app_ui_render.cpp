// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
#include "ui/app_ui_internal.h"

#include <cstdio>
#include <ctime>
#include "ui/cloud_time.h"

#include <algorithm>

namespace xc::ui {

void AppUi::drawPads(Canvas& c) {
    // Bottom left, on the line of the button hints (which are right-aligned).
    drawPadRow(c, fonts_.bold, kMargin, kH - 26 - padIconHeight(48), 48, 16, pads_, kBg,
               fonts_.hasIcons ? &fonts_.icons : nullptr);
}

bool AppUi::needsRedraw(uint64_t nowMs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (screen_ == Screen::Streaming) return false;
    if (dirty_ || animating_) return true;
    if (mouseCursorVisible_ && nowMs >= mouseCursorVisibleUntil_) return true;
    // A desktop's toast came or went (the PS5 sends system notifications).
    if (platform::toastsEnabled() && platform::toasts().version(nowMs) != toastsDrawn_) return true;
    // The home's clock: redraw when the minute changes.
    if (screen_ == Screen::Home && nowMs / 60000 != lastRender_ / 60000) return true;
    // Spinners keep moving (about 30 fps is enough).
    bool spinner = screen_ == Screen::Splash || screen_ == Screen::SignIn || screen_ == Screen::Launching ||
                   screen_ == Screen::Updating ||
                   (screen_ == Screen::Settings && ((tester_ == Tester::SignInCheck && signInCheck_.running) ||
                                                    (tester_ == Tester::NetworkTest && networkTest_.running)));
    return spinner && nowMs - lastRender_ >= 33;
}

void AppUi::drawCentered(Canvas& c, const Font& f, const std::string& text, int y, int px, Color color) {
    f.draw(c, text, (kW - f.measure(text, px)) / 2, y, px, color);
}

void AppUi::drawBackground(Canvas& c) {
    c.gradientV({0, 0, kW, kH}, rgba(24, 28, 24), kBg);
}

void AppUi::drawLogo(Canvas& c, float cx, float cy, float r) { drawBrandMark(c, cx, cy, r); }

void AppUi::drawGamerscoreBadge(Canvas& c, float cx, float cy, float r) {
    // A filled disc with a "G" cut out of it, drawn from shapes (a letter of
    // the font never sits in the middle of a circle): a ring open at the
    // upper right, and its bar from the centre to the ring.
    c.fillCircle(cx, cy, r, kGray);
    const float ring = r * 0.56f, t = r * 0.24f;
    const float kGap = 0.95f;  // radians left open, above 3 o'clock
    c.strokeArc(cx, cy, ring, t, 0.0f, 6.2832f - kGap, kBg);
    c.line(cx + r * 0.05f, cy, cx + ring + t * 0.5f, cy, t, kBg);
}

// The account's cloud time (Xbox limits it each month since November 2026),
// under the brand name: the left side, the account's own (name, score, plan)
// being on the right. What is left of the month's hours and a bar; orange
// from 2 h left, red at none. An unlimited account says so; nothing when it
// is not known.
void AppUi::drawCloudTime(Canvas& c, int x, int y) {
    constexpr int kPx = 15, kLineH = 20;
    const int ty = fonts_.semibold.centeredY(y, kLineH, kPx);
    const float iconY = y + kLineH / 2.0f;
    if (playtimeUnlimited_) {
        drawClockIcon(c, x + 7.0f, iconY, 7.0f, kGray);
        fonts_.semibold.draw(c, tr(Str::CloudTimeUnlimited), x + 20, ty, kPx, kGray);
        return;
    }
    if (playtimeRemaining_ < 0) return;
    const Color col = playtimeRemaining_ <= 0      ? rgba(240, 100, 100)
                      : playtimeRemaining_ < 7260 ? rgba(255, 160, 40)
                                                   : kGray;
    drawClockIcon(c, x + 7.0f, iconY, 7.0f, col);
    int end = fonts_.semibold.draw(c, trf(Str::CloudTimeLeft, formatCloudTime(playtimeRemaining_)), x + 20, ty, kPx, col);
    if (playtimeMax_ <= 0) return;
    end = fonts_.semibold.draw(c, " " + trf(Str::CloudTimeOf, formatCloudTime(playtimeMax_)), end, ty, kPx, kDim);
    // As wide as the brand name, or the line when that is longer.
    const int w = std::max(fonts_.semibold.measure(tr(Str::CloudGaming), 22), end - x);
    const Rect bar{x, y + kLineH + 2, w, 4};
    c.fillRect(bar, rgba(255, 255, 255, 36), 2);
    const double f = std::clamp(static_cast<double>(playtimeRemaining_) / static_cast<double>(playtimeMax_), 0.0, 1.0);
    if (const int fw = static_cast<int>(w * f + 0.5); fw > 0) c.fillRect({x, bar.y, std::max(fw, 4), bar.h}, col, 2);
}

namespace {

// "10:41 pm" in the console's local time. A PS4 homebrew app often sees UTC
// only: <data dir>/clock_offset.txt (hours from UTC, e.g. 3) then sets the zone.
std::string clockText() {
    static bool loaded = false;
    static bool haveOffset = false;
    static int offsetMin = 0;
    if (!loaded) {
        loaded = true;
        if (FILE* f = std::fopen((platform::dataDir() + "/clock_offset.txt").c_str(), "r")) {
            double h = 0;
            if (std::fscanf(f, "%lf", &h) == 1 && h > -15 && h < 15) {
                haveOffset = true;
                offsetMin = static_cast<int>(std::lround(h * 60.0));
            }
            std::fclose(f);
        }
    }
    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    if (haveOffset) {
        now += static_cast<std::time_t>(offsetMin) * 60;
        gmtime_r(&now, &tmv);
    } else {
        localtime_r(&now, &tmv);
    }
    int h12 = tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12;
    char buf[24];
    std::snprintf(buf, sizeof buf, "%d:%02d %s", h12, tmv.tm_min, tmv.tm_hour >= 12 ? "pm" : "am");
    return buf;
}

// The header's round-button icons, drawn from shapes (no icon font needed).
void drawNavIcon(Canvas& c, int kind, float cx, float cy, Color col) {
    switch (kind) {
    case 0:  // home
        c.line(cx - 15.0f, cy - 1.0f, cx, cy - 14.0f, 3.2f, col);
        c.line(cx + 15.0f, cy - 1.0f, cx, cy - 14.0f, 3.2f, col);
        c.strokeRect({static_cast<int>(cx) - 10, static_cast<int>(cy) - 2, 20, 16}, col, 3, 2);
        c.fillRect({static_cast<int>(cx) - 3, static_cast<int>(cy) + 5, 6, 9}, col, 1);
        break;
    case 1:  // library (2x2 tiles)
        for (int i = 0; i < 4; ++i)
            c.fillRect({static_cast<int>(cx) - 13 + (i % 2) * 14, static_cast<int>(cy) - 13 + (i / 2) * 14, 12, 12}, col, 3);
        break;
    case 2:  // consoles (a screen on a stand)
        c.strokeRect({static_cast<int>(cx) - 15, static_cast<int>(cy) - 12, 30, 20}, col, 3, 3);
        c.line(cx, cy + 8.0f, cx, cy + 14.0f, 3.0f, col);
        c.line(cx - 8.0f, cy + 14.0f, cx + 8.0f, cy + 14.0f, 3.0f, col);
        break;
    case 3:  // search
        c.strokeCircle(cx - 3.0f, cy - 3.0f, 10.0f, 3.2f, col);
        c.line(cx + 4.5f, cy + 4.5f, cx + 13.0f, cy + 13.0f, 3.6f, col);
        break;
    case 4:  // Xbox accounts: a head and shoulders
        c.strokeCircle(cx, cy - 7.0f, 5.5f, 2.8f, col);
        c.strokeArc(cx, cy + 13.0f, 13.0f, 2.8f, 3.1416f, 3.1416f, col);
        c.line(cx - 13.0f, cy + 13.0f, cx - 13.0f, cy + 8.0f, 2.8f, col);
        c.line(cx + 13.0f, cy + 13.0f, cx + 13.0f, cy + 8.0f, 2.8f, col);
        break;
    default:  // settings: a gear
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.7853982f;
            c.line(cx + std::cos(a) * 10.0f, cy + std::sin(a) * 10.0f, cx + std::cos(a) * 15.0f, cy + std::sin(a) * 15.0f, 4.6f, col);
        }
        c.strokeCircle(cx, cy, 10.0f, 3.4f, col);
        c.strokeCircle(cx, cy, 3.6f, 2.6f, col);
        break;
    }
}

}  // namespace

void AppUi::drawTopBar(Canvas& c) {
    // The player at the left, as the Xbox dashboard shows them: picture, name,
    // then the gamerscore and the plan. The clock (and the cloud time left) at the right.
    constexpr int kAvatar = 64, kAx = kMargin, kAy = 38;
    if (auto pic = images_.get(gamerpicUrl_, kAvatar, kAvatar)) {
        c.drawImage(*pic, kAx, kAy, 255, -1);
    } else {
        c.fillCircle(kAx + kAvatar / 2.0f, kAy + kAvatar / 2.0f, kAvatar / 2.0f, kPanel);
    }
    const int tx = kAx + kAvatar + 18;
    if (!gamertag_.empty()) {
        bool isFree = subscription_.empty() || (subscription_ != "GamePass" && xcloud::planTier(subscription_) == 0);
        bool second = !gamerscore_.empty() || !subscription_.empty() || isFree;
        fonts_.semibold.draw(c, gamertag_, tx, second ? 40 : 56, 26, kWhite);
        int x = tx;
        constexpr int kLineY = 76, kLineH = 22, kLinePx = 17;
        const float midY = kLineY + kLineH / 2.0f;
        if (!gamerscore_.empty()) {
            std::string n;
            const char* sep = language() == Language::English ? "," : ".";
            for (size_t i = 0; i < gamerscore_.size(); ++i) {
                if (i && (gamerscore_.size() - i) % 3 == 0) n += sep;
                n += gamerscore_[i];
            }
            drawGamerscoreBadge(c, x + 8.5f, midY, 8.5f);
            x += 22;
            x = fonts_.semibold.draw(c, n, x, fonts_.semibold.centeredY(kLineY, kLineH, kLinePx), kLinePx, kGray) + 18;
        }
        if (!subscription_.empty()) {
            const char* gp = "GAME PASS";
            x = fonts_.semibold.draw(c, gp, x, fonts_.semibold.centeredY(kLineY, kLineH, 14), 14, kGray) + 8;
            if (subscription_ != "GamePass") {
                std::string plan = subscription_;
                for (auto& ch : plan) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                int pw = fonts_.bold.measure(plan, 11);
                Rect box{x, static_cast<int>(midY) - 8, pw + 10, 16};
                c.strokeRect(box, kGray, 1, 2);
                fonts_.bold.draw(c, plan, box.x + 5, fonts_.bold.centeredY(box.y, box.h, 11), 11, kGray);
            }
        } else if (isFree) {
            const char* freeLabel = "FREE";
            int fw = fonts_.bold.measure(freeLabel, 11);
            Rect box{x, static_cast<int>(midY) - 8, fw + 12, 16};
            c.fillRect(box, rgba(255, 255, 255, 20), 4);
            c.strokeRect(box, rgba(160, 160, 175, 180), 1, 4);
            fonts_.bold.draw(c, freeLabel, box.x + 6, fonts_.bold.centeredY(box.y, box.h, 11), 11, rgba(220, 225, 235, 255));
        }
    }

    // Right: the PBOX mark and the clock; the cloud time left under them.
    const std::string clock = clockText();
    constexpr int kClockPx = 28;
    const int cw = fonts_.semibold.measure(clock, kClockPx);
    const int cxr = kW - kMargin - cw;
    fonts_.semibold.draw(c, clock, cxr, fonts_.semibold.centeredY(40, 36, kClockPx), kClockPx, kWhite);
    drawLogo(c, static_cast<float>(cxr - 34), 58.0f, 17.0f);
    if (playtimeUnlimited_ || playtimeRemaining_ >= 0) drawCloudTime(c, std::min(cxr - 60, kW - kMargin - 250), 82);
}

void AppUi::drawHints(Canvas& c, const std::vector<std::pair<int, const char*>>& hints) {
    // Right-aligned PlayStation button glyphs with labels.
    constexpr int kPx = 22, kR = 14, kGap = 36;
    int x = kW - kMargin;
    const int y = kH - 64;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        // With Circle confirming, the pad reports it as Cross (and the
        // other way round): the hints show the button to press.
        int icon = it->first;
        int w = fonts_.semibold.measure(it->second, kPx);
        x -= w;
        fonts_.semibold.draw(c, it->second, x, y, kPx, kGray);
        if (g_keyboardHints) {  // the keyboard's key, as a key cap
            const char* key = keyLabel(icon);
            int kw = fonts_.bold.measure(key, 15) + 18;
            Rect cap{x - 10 - kw, y + 1, kw, 26};
            c.fillRect(cap, rgba(255, 255, 255, 30), 6);
            c.strokeRect(cap, kGray, 2, 6);
            fonts_.bold.draw(c, key, cap.x + 9, fonts_.bold.centeredY(cap.y, cap.h, 15), 15, kWhite);
            x = cap.x - kGap;
            continue;
        }
        if (circleConfirms_ && (icon == kIconCross || icon == kIconCircle))
            icon = icon == kIconCross ? kIconCircle : kIconCross;
        x -= 10 + 2 * kR;
        float cx = x + kR, cy = y + 13;
        const GlyphStyle style = g_glyphStyle;
        const bool xbox = style == GlyphStyle::Xbox;
        if (icon == kIconL2R2 || icon == kIconL1R1) {
            const char* l = buttonName(style, icon == kIconL1R1 ? PadButton::L1 : PadButton::L2);
            const char* r = buttonName(style, icon == kIconL1R1 ? PadButton::R1 : PadButton::R2);
            int lw = fonts_.bold.measure(l, 14) + 16;
            int rw = fonts_.bold.measure(r, 14) + 16;
            int sepW = fonts_.regular.measure("|", 14);
            constexpr int kPillGap = 6;
            int rightX = x + 2 * kR;
            Rect rPill{rightX - rw, static_cast<int>(cy) - 12, rw, 24};
            c.fillRect(rPill, rgba(255, 255, 255, 40), 8);
            fonts_.bold.draw(c, r, rPill.x + (rw - fonts_.bold.measure(r, 14)) / 2,
                             fonts_.bold.centeredY(rPill.y, rPill.h, 14), 14, kGray);
            int sepX = rPill.x - kPillGap - sepW;
            fonts_.regular.draw(c, "|", sepX, fonts_.regular.centeredY(static_cast<int>(cy) - 12, 24, 14), 14,
                                rgba(255, 255, 255, 120));
            Rect lPill{sepX - kPillGap - lw, static_cast<int>(cy) - 12, lw, 24};
            c.fillRect(lPill, rgba(255, 255, 255, 40), 8);
            fonts_.bold.draw(c, l, lPill.x + (lw - fonts_.bold.measure(l, 14)) / 2,
                             fonts_.bold.centeredY(lPill.y, lPill.h, 14), 14, kGray);
            x = lPill.x;
        } else if (icon == kIconDpadLR) {
            const char* l = "\xE2\x86\x90";
            const char* r = "\xE2\x86\x92";
            int lw = fonts_.bold.measure(l, 14) + 14;
            int rw = fonts_.bold.measure(r, 14) + 14;
            constexpr int kPillGap = 4;
            int rightX = x + 2 * kR;
            Rect rPill{rightX - rw, static_cast<int>(cy) - 12, rw, 24};
            c.fillRect(rPill, rgba(255, 255, 255, 40), 8);
            fonts_.bold.draw(c, r, rPill.x + (rw - fonts_.bold.measure(r, 14)) / 2,
                             fonts_.bold.centeredY(rPill.y, rPill.h, 14), 14, kGray);
            Rect lPill{rPill.x - kPillGap - lw, static_cast<int>(cy) - 12, lw, 24};
            c.fillRect(lPill, rgba(255, 255, 255, 40), 8);
            fonts_.bold.draw(c, l, lPill.x + (lw - fonts_.bold.measure(l, 14)) / 2,
                             fonts_.bold.centeredY(lPill.y, lPill.h, 14), 14, kGray);
            x = lPill.x;
        } else if (icon == kIconR3 || icon == kIconL3) {
            const char* label = buttonName(style, icon == kIconR3 ? PadButton::R3 : PadButton::L3);
            int pillW = fonts_.bold.measure(label, 14) + 16;
            int rightX = x + 2 * kR;
            Rect pill{rightX - pillW, static_cast<int>(cy) - 12, pillW, 24};
            c.fillRect(pill, rgba(255, 255, 255, 40), 8);
            fonts_.bold.draw(c, label, pill.x + (pillW - fonts_.bold.measure(label, 14)) / 2,
                             fonts_.bold.centeredY(pill.y, pill.h, 14), 14, kGray);
            x = pill.x;
        } else if (xbox && icon == kIconTouchpad) {  // View
            drawXboxViewButton(c, cx, cy, kR, kGray);
        } else if (xbox && icon == kIconOptions) {  // Menu
            drawXboxMenuButton(c, cx, cy, kR, kGray);
        } else if (icon == kIconTouchpad) {
            Rect pad{x - 18, static_cast<int>(cy) - 10, 2 * kR + 12, 20};
            c.fillRect(pad, rgba(255, 255, 255, 40), 6);
            c.strokeRect(pad, kGray, 2, 6);
            x -= 10;
        } else if (icon == kIconOptions) {
            c.fillRect({x - 4, static_cast<int>(cy) - 11, 2 * kR + 8, 22}, rgba(255, 255, 255, 40), 11);
            for (int k = -1; k <= 1; ++k) c.line(cx - 8, cy + k * 5, cx + 8, cy + k * 5, 2, kGray);
        } else {
            drawFaceButton(c, fonts_.bold, icon, cx, cy, kR);
        }
        x -= kGap;
    }
}

void AppUi::drawSpinner(Canvas& c, float cx, float cy, float r, uint64_t nowMs) {
    float t = static_cast<float>(nowMs % 1000) / 1000.0f * 6.2832f;
    c.strokeArc(cx, cy, r, r * 0.18f, t, 4.4f, kWhite);
}

void AppUi::drawToast(Canvas& c, uint64_t nowMs) {
    if (toast_.empty()) return;
    if (toastUntil_ == 0) toastUntil_ = nowMs + 3500;
    if (nowMs >= toastUntil_) {
        toast_.clear();
        return;
    }
    int w = fonts_.semibold.measure(toast_, 24) + 64;
    Rect r{(kW - w) / 2, kH - 150, w, 56};
    c.fillRect(r, rgba(40, 40, 40, 235), 28);
    fonts_.semibold.draw(c, toast_, r.x + 32, r.y + 14, 24, kWhite);
    animating_ = true;
}

bool AppUi::drawHero(Canvas& c, uint64_t nowMs, const std::string& url, bool details) {
    if (url != heroUrl_) {
        // The previous art may only cross-fade into a new one that is already
        // there; it is never kept up while the new one downloads.
        prevHeroUrl_ = heroShownAt_ ? heroUrl_ : std::string();
        heroUrl_ = url;
        heroSince_ = nowMs;
        heroShownAt_ = 0;
    }
    auto current = heroUrl_.empty() ? nullptr : images_.get(heroUrl_, kW, kH);
    if (current) {
        if (!heroShownAt_) heroShownAt_ = nowMs;
        uint64_t age = nowMs - heroShownAt_;
        // Arrived at once (cached): cross-fade from the previous art. Arrived
        // later: fade in from the plain background.
        bool crossFade = heroShownAt_ - heroSince_ < 150 && !prevHeroUrl_.empty();
        auto previous = crossFade && age < kHeroFadeMs ? images_.get(prevHeroUrl_, kW, kH) : nullptr;
        if (previous) c.drawImage(*previous, 0, 0);
        uint8_t alpha = age >= kHeroFadeMs ? 255 : static_cast<uint8_t>(age * 255 / kHeroFadeMs);
        c.drawImage(*current, 0, 0, alpha);
        if (alpha < 255) animating_ = true;
    } else {
        prevHeroUrl_.clear();  // nothing on screen to fade from any more
    }
    // Darken towards the left (text) and the bottom (rows).
    if (details) {
        c.gradientH({0, 0, 1400, kH}, rgba(0, 0, 0, 240), rgba(0, 0, 0, 0));
        c.gradientV({0, 640, kW, 440}, withAlpha(kBg, 0), withAlpha(kBg, 230));
    } else {
        // The home keeps the art bright, as the Xbox dashboard does: a light
        // shade at the left and a fade into the rows at the bottom.
        c.gradientH({0, 0, 900, kH}, rgba(0, 0, 0, 100), rgba(0, 0, 0, 0));
        c.gradientV({0, 380, kW, 520}, withAlpha(kBg, 0), withAlpha(kBg, 244));
        c.fillRect({0, 900, kW, kH - 900}, kBg);
    }
    c.gradientV({0, 0, kW, 160}, rgba(0, 0, 0, 150), rgba(0, 0, 0, 0));
    return current != nullptr;
}

void AppUi::drawSplash(Canvas& c, uint64_t nowMs) {
    if (!splashSince_) splashSince_ = nowMs;
    const uint64_t age = nowMs - splashSince_;
    const float reveal = std::min(1.0f, static_cast<float>(age) / 2000.0f);
    const float pulse = 1.0f + 0.025f * std::sin(static_cast<float>(age) * 0.004f);
    c.clear(rgba(0, 0, 0));
    const float cx = kW / 2.0f, cy = 380.0f;
    // Thin green light traces and a soft lens-like sweep, kept as primitives
    // so the intro has no video decoder, large buffer, or startup I/O.
    for (int i = 0; i < 5; ++i) {
        const float y = cy - 170.0f + i * 86.0f + std::sin(static_cast<float>(age) * 0.003f + i) * 8.0f;
        c.line(180.0f, y + 120.0f, kW - 180.0f, y - 120.0f, 2.0f,
               rgba(100, 220, 150, static_cast<uint8_t>(18 + i * 5)));
    }
    const uint8_t alpha = static_cast<uint8_t>(std::lround(255.0f * reveal));
    drawBrandMark(c, cx, cy, 74.0f * pulse, rgba(255, 255, 255, alpha));
    if (age >= 1500) {
        const float shine = std::fmod(static_cast<float>(age - 1500) / 2200.0f, 1.0f);
        c.line(cx - 150.0f + shine * 300.0f, cy - 100.0f, cx - 70.0f + shine * 300.0f, cy + 100.0f,
               8.0f, rgba(150, 255, 190, 70));
    }
    if (age >= 1900) {
        drawCentered(c, fonts_.bold, "PBOX", 520, 52, rgba(255, 255, 255, alpha));
        drawCentered(c, fonts_.regular, status_, 600, 28, kGray);
    }
    // Sign-in/network startup can take longer than five seconds. Keep the
    // Xbox-style pulse alive for the whole splash instead of leaving one
    // frame frozen while the account and library are loading.
    animating_ = true;
}

void AppUi::drawSignIn(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    const int left = 180;
    drawLogo(c, left + 26, 160, 26);
    fonts_.semibold.draw(c, tr(Str::AppName), left + 70, 144, 30, kWhite);
    fonts_.bold.draw(c, tr(Str::SignInTitle), left, 260, 72, kWhite);
    fonts_.regular.draw(c, tr(Str::SignInStep1), left, 400, 32, kGray);
    std::string shortUrl = codeUrl_;
    for (const char* p : {"https://", "http://", "www."})
        if (shortUrl.rfind(p, 0) == 0) shortUrl = shortUrl.substr(std::string(p).size());
    fonts_.semibold.draw(c, shortUrl, left, 450, 44, kWhite);
    fonts_.regular.draw(c, tr(Str::SignInStep2), left, 540, 32, kGray);

    // The code, letter by letter, in a panel.
    constexpr int kPx = 96;
    int letters = static_cast<int>(code_.size());
    int cell = 82;
    Rect panel{left, 600, std::max(400, letters * cell + 64), 150};
    c.fillRect(panel, kPanel, 18);
    int x = panel.x + 32;
    for (char ch : code_) {
        std::string s(1, ch);
        fonts_.bold.draw(c, s, x + (cell - fonts_.bold.measure(s, kPx)) / 2, panel.y + 18, kPx, kWhite);
        x += cell;
    }
    drawSpinner(c, left + 18, 830, 14, nowMs);
    fonts_.regular.draw(c, tr(Str::SignInWaiting), left + 48, 815, 26, kDim);

    if (!platform::dataDirWritable()) {
        std::string pathAndErr = platform::dataDir() + " (errno " + std::to_string(platform::dataDirErrno()) + ")";
        std::string warn1 = trf(Str::StorageNotWritable, pathAndErr);
        std::string warn2 = trf(Str::StorageFixHint, platform::dataDir());
        fonts_.semibold.draw(c, warn1, left, 875, 22, rgba(255, 160, 40));
        fonts_.regular.draw(c, warn2, left, 905, 20, kGray);
    }

    // QR code for the sign-in page. Not "?otc=<code>": that prefilled link
    // goes through a consent flow that rejects this client id ("first party
    // application ... pre-authorization"), so the code is typed on the phone.
    std::string target = codeUrl_.empty() ? "https://www.microsoft.com/link" : codeUrl_;
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    if (qrcodegen_encodeText(target.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                             qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        int n = qrcodegen_getSize(qr);
        constexpr int kBox = 440;
        int module = (kBox - 60) / (n + 2);
        int size = module * (n + 2);
        Rect box{1280, 300, size + 40, size + 40};
        c.fillRect(box, kWhite, 24);
        int ox = box.x + 20 + module, oy = box.y + 20 + module;
        for (int yy = 0; yy < n; ++yy)
            for (int xx = 0; xx < n; ++xx)
                if (qrcodegen_getModule(qr, xx, yy)) c.fillRect({ox + xx * module, oy + yy * module, module, module}, kBg);
        int w = fonts_.regular.measure(tr(Str::SignInScan), 26);
        fonts_.regular.draw(c, tr(Str::SignInScan), box.x + (box.w - w) / 2, box.y + box.h + 24, 26, kGray);
    }
    if (caps_.openUrl) drawHints(c, {{kIconCross, tr(Str::OpenInBrowser)}});  // a desktop: the page in the browser
}

void AppUi::drawTabs(Canvas& c) {
    // Six round buttons in the middle of the header: Game Pass, your games,
    // consoles, search, Xbox accounts, settings. The current one sits
    // on a lighter pill; L1 / R1 still step through the three tabs.
    const int active = searching_ ? 3 : static_cast<int>(tab_);
    for (int i = 0; i < kNavCount; ++i) {
        const NavSlot n = homeNavSlot(i);
        const bool on = i == active;
        if (on) {
            c.fillRect({n.x, n.y, n.w, n.h}, rgba(255, 255, 255, 46), n.h / 2);
            c.fillRect({n.x + n.w / 2 - 14, n.y + n.h - 5, 28, 3}, rgba(92, 214, 110), 2);
        }
        if (navFocus_ == i && !searching_) {  // the pad is on this one
            c.fillRect({n.x, n.y, n.w, n.h}, rgba(92, 214, 110, 40), n.h / 2);
            c.strokeRect({n.x - 4, n.y - 4, n.w + 8, n.h + 8}, rgba(92, 214, 110), 4, n.h / 2 + 4);
        }
        drawNavIcon(c, i, n.x + n.w / 2.0f, n.y + n.h / 2.0f - (on ? 1.0f : 0.0f), on ? kWhite : rgba(214, 222, 232, 220));
        if (navFocus_ == 4 && i == 4) {
            const std::string label = tr(Str::AccountManager);
            const int tw = fonts_.semibold.measure(label, 16);
            Rect tag{n.x + (n.w - tw - 22) / 2, n.y + n.h + 7, tw + 22, 25};
            c.fillRect(tag, rgba(18, 27, 40, 245), 12);
            c.strokeRect(tag, rgba(35, 211, 157, 180), 1, 12);
            fonts_.semibold.draw(c, label, tag.x + 11, fonts_.semibold.centeredY(tag.y, tag.h, 16), 16, kWhite);
        }
    }
    auto shoulder = [&](const char* name, int cx) {
        Rect r{cx - 18, homeNavSlot(0).y + 17, 36, 26};
        c.fillRect(r, rgba(255, 255, 255, 26), 7);
        fonts_.bold.draw(c, name, cx - fonts_.bold.measure(name, 14) / 2,
                         fonts_.bold.centeredY(r.y, r.h, 14), 14, kGray);
    };
    shoulder(g_keyboardHints ? "Q" : buttonName(g_glyphStyle, PadButton::L1), homeNavSlot(0).x - 34);
    shoulder(g_keyboardHints ? "E" : buttonName(g_glyphStyle, PadButton::R1), homeNavSlot(kNavCount - 1).x + homeNavSlot(0).w + 34);
}

void AppUi::drawHomeTile(Canvas& c, const GameTile& t, int x, int y, int w, int h, bool focused, bool square,
                         bool gamePassBadge) {
    const int radius = square ? 10 : 12;
    // Some square art carries a promotional strip along its bottom edge ("FREE DLC
    // AVAILABLE NOW" and the like). Ask for 1/9 more height than is shown and keep the
    // top part only: the strip is cut off, the rest stays. (Cached per size.)
    const int askH = square ? h + h / 9 : h;
    if (auto img = images_.get(t.tileUrl, w, askH)) {
        if (square) {
            const std::string key = t.tileUrl + "@" + std::to_string(w) + "x" + std::to_string(h);
            auto it = tileTrim_.find(key);
            if (it == tileTrim_.end()) {
                if (tileTrim_.size() > 48) tileTrim_.clear();
                Image cut;
                cut.w = img->w;
                cut.h = std::min(h, img->h);
                cut.opaque = img->opaque;
                cut.px.assign(img->px.begin(), img->px.begin() + static_cast<long>(cut.w) * cut.h);
                it = tileTrim_.emplace(key, std::move(cut)).first;
            }
            c.drawImage(it->second, x, y, 255, radius);
        } else {
            c.drawImage(*img, x, y, 255, radius);
        }
    } else {
        c.fillRect({x, y, w, h}, kPlaceholder, radius);
        if (square) {  // (a wide tile shows its name at the bottom anyway)
            auto lines = fonts_.semibold.wrap(t.name, 20, w - 24, 3);
            for (size_t k = 0; k < lines.size(); ++k)
                fonts_.semibold.draw(c, lines[k], x + 12, y + 12 + static_cast<int>(k) * 28, 20, kGray);
        }
    }
    if (!square) {
        // Wide tile: the name on a shade at the bottom, the Game Pass mark at the top.
        c.gradientV({x + 6, y + h - 92, w - 12, 86}, rgba(0, 0, 0, 0), rgba(0, 0, 0, 205));
        auto name = fonts_.semibold.wrap(t.name, 22, w - 32, 1);
        if (!name.empty()) fonts_.semibold.draw(c, name[0], x + 16, y + h - 42, 22, kWhite);
        if (gamePassBadge && t.plan != 0) {
            const char* label = "GAME PASS";
            const int tw = fonts_.bold.measure(label, 12);
            Rect pill{x + 12, y + 12, tw + 34, 26};
            c.fillRect(pill, rgba(0, 0, 0, 200), 6);
            c.fillCircle(pill.x + 14.0f, pill.y + pill.h / 2.0f, 6.0f, rgba(92, 214, 110));
            fonts_.bold.draw(c, label, pill.x + 26, fonts_.bold.centeredY(pill.y, pill.h, 12), 12, kWhite);
        }
    }
    if (focused) {
        c.strokeRect({x - 6, y - 6, w + 12, h + 12}, rgba(92, 214, 110), 4, radius + 6);
    }
}

void AppUi::drawCard(Canvas& c, const GameTile& t, int x, int y, bool focused, bool gamePassBadge) {
    if (auto img = images_.get(t.tileUrl, kCard, kCard)) {
        c.drawImage(*img, x, y, 255, 10);
    } else {
        c.fillRect({x, y, kCard, kCard}, kPlaceholder, 10);
        auto lines = fonts_.semibold.wrap(t.name, 22, kCard - 32, 3);
        for (size_t k = 0; k < lines.size(); ++k)
            fonts_.semibold.draw(c, lines[k], x + 16, y + 16 + static_cast<int>(k) * 30, 22, kGray);
    }
    // Top-left: time played on this title, when recorded.
    uint64_t cardPlayedSec = 0;
    if (!t.productId.empty()) {
        auto it = settings_.titlePlaytime.find(t.productId);
        if (it != settings_.titlePlaytime.end()) cardPlayedSec = it->second;
    }
    if (!cardPlayedSec && !t.titleId.empty()) {
        auto it = settings_.titlePlaytime.find(t.titleId);
        if (it != settings_.titlePlaytime.end()) cardPlayedSec = it->second;
    }
    if (cardPlayedSec > 0) {
        std::string ptLabel = formatPlaytime(cardPlayedSec);
        int pw = std::min(kCard - 56, fonts_.bold.measure(ptLabel, 11) + 14);
        Rect ptBadge{x + 10, y + 10, pw, 22};
        c.fillRect(ptBadge, rgba(0, 0, 0, 215), 4);
        fonts_.bold.draw(c, ptLabel, ptBadge.x + (ptBadge.w - fonts_.bold.measure(ptLabel, 11)) / 2,
                         fonts_.bold.centeredY(ptBadge.y, ptBadge.h, 11), 11, kWhite);
    }
    // A row may mix catalog-gallery titles with Game Pass titles. The row flag
    // only opts in to showing badges; the membership feed decides per card.
    if ((gamePassBadge && t.gamePassCatalogMember) || t.gamePassIncluded) {
        const char* label = "GAME PASS";
        const int badgeW = fonts_.bold.measure(label, 10) + 16;
        Rect badge{x + 10, y + (cardPlayedSec > 0 ? 36 : 10), badgeW, 20};
        c.fillRect(badge, rgba(16, 92, 34, 235), 5);
        fonts_.bold.draw(c, label, badge.x + 8, fonts_.bold.centeredY(badge.y, badge.h, 10), 10, kWhite);
    }
    // Top-right: Wishlist blue star when marked (or lock if unplayable and not wishlisted)
    bool isWishlisted = !t.productId.empty() && wishlist_.count(t.productId) > 0;
    if (isWishlisted) {
        c.fillCircle(x + kCard - 24, y + 24, 14, rgba(10, 20, 35, 230));
        c.strokeCircle(x + kCard - 24, y + 24, 14, 1.5f, rgba(40, 140, 255, 180));
        drawStar(c, x + kCard - 24, y + 24, 7.5f, rgba(50, 160, 255));
    }

    // Bottom-right: console badge and input badge.
    // Calculate rightBadgeLeft so bottom-left badges never overlap them.
    int rightEdge = x + kCard - 10;
    int rightBadgeLeft = rightEdge;
    if (const char* p = platformLabel(t.platform)) {
        int pTextW = fonts_.bold.measure(p, 11);
        int pBadgeW = std::clamp(pTextW + 12, 54, 66);
        Rect badge{rightBadgeLeft - pBadgeW, y + kCard - 34, pBadgeW, 24};
        c.fillRect(badge, rgba(0, 0, 0, 220), 4);
        fonts_.bold.draw(c, p, badge.x + (badge.w - pTextW) / 2,
                         fonts_.bold.centeredY(badge.y, badge.h, 11), 11, kWhite);
        rightBadgeLeft = badge.x - 4;
    }
    if (t.mouseKeyboard || fonts_.hasIcons) {
        constexpr int kInputW = 28;
        Rect k{rightBadgeLeft - kInputW, y + kCard - 34, kInputW, 24};
        c.fillRect(k, rgba(0, 0, 0, 220), 4);
        const char* glyph = t.mouseKeyboard ? kIconKeyboardMouse : kIconGamepad;
        if (fonts_.hasIcons) {
            int gw = fonts_.icons.measure(glyph, 18);
            int gx = k.x + (k.w - gw) / 2;
            fonts_.icons.draw(c, glyph, gx, k.y + 3, 18, kWhite);
        } else {
            int kw = 16, kh = kw * 10 / 24;
            int kx = k.x + (k.w - kw) / 2;
            int ky = k.y + (k.h - kh) / 2;
            drawKeyboardGlyph(c, kx, ky, kw, kWhite);
        }
        rightBadgeLeft = k.x - 6;
    }

    // Bottom-left: price badge, plan badge, or friends playing; bounded by rightBadgeLeft.
    int maxLeftW = std::max(40, rightBadgeLeft - (x + 10) - 4);
    const char* planBadge = planBadgeLabel(t.plan);
    uint8_t myPlan = xcloud::planTier(subscription_);
    bool otherPlan = planBadge && (!t.playable || (myPlan > 0 && !xcloud::planIncludes(myPlan, t.plan, isFreeGame(t))));
    bool showPlan = otherPlan || (planBadge && searching_ && tab_ == Tab::GamePass && filterPlan_ > 0 && t.friends.empty());
    bool isF2P = isFreeGame(t);
    bool effectivePlayable = isEffectivePlayableLocked(t);
    if (!effectivePlayable && !t.playable && (isF2P || t.purchasable)) {
        c.fillRect({x, y, kCard, kCard}, rgba(0, 0, 0, 70), 10);
        auto price = prices_.find(t.productId);
        bool free = isFreeGame(t);
        std::string label = free                     ? tr(Str::Free)
                            : price != prices_.end() ? price->second.now
                                                     : tr(Str::BuyBadge);
        int tw = fonts_.bold.measure(label, 11);
        int w = std::min(maxLeftW, 20 + tw + 8);
        if (w < 20 + tw + 8) {
            label = fonts_.bold.fit(label, 11, w - 22);
        }
        Rect badge{x + 10, y + kCard - 34, w, 24};
        c.fillRect(badge, rgba(16, 124, 16, 235), 4);
        drawBag(c, badge.x + 10, badge.y + badge.h / 2, kWhite, 0.52f);
        fonts_.bold.draw(c, label, badge.x + 18, fonts_.bold.centeredY(badge.y, badge.h, 11), 11, kWhite);
    } else if (!effectivePlayable) {
        c.fillRect({x, y, kCard, kCard}, rgba(0, 0, 0, 150), 10);
        drawLock(c, isWishlisted ? x + kCard - 54 : x + kCard - 34, isWishlisted ? y + 24 : y + 30);
        if (planBadge) {
            int w = std::min(maxLeftW, fonts_.bold.measure(planBadge, 11) + 14);
            Rect badge{x + 10, y + kCard - 34, w, 24};
            c.fillRect(badge, rgba(0, 0, 0, 220), 4);
            fonts_.bold.draw(c, planBadge, badge.x + (badge.w - fonts_.bold.measure(planBadge, 11)) / 2,
                             fonts_.bold.centeredY(badge.y, badge.h, 11), 11, kWhite);
        }
    } else if (showPlan) {
        int w = std::min(maxLeftW, fonts_.bold.measure(planBadge, 11) + 14);
        Rect badge{x + 10, y + kCard - 34, w, 24};
        c.fillRect(badge, rgba(0, 0, 0, 220), 4);
        fonts_.bold.draw(c, planBadge, badge.x + (badge.w - fonts_.bold.measure(planBadge, 11)) / 2,
                         fonts_.bold.centeredY(badge.y, badge.h, 11), 11, kWhite);
    } else if (!t.friends.empty()) {
        int numAvatars = std::min<int>(static_cast<int>(t.friends.size()), 2);
        int extraCount = static_cast<int>(t.friends.size()) - numAvatars;
        constexpr int kAvatarSz = 20;
        constexpr int kAvatarOverlap = 13;
        constexpr int kPillH = 26;
        constexpr int kPadX = 4;
        constexpr int kPadY = 3;

        int avatarsW = numAvatars == 1 ? kAvatarSz : (kAvatarSz + (numAvatars - 1) * kAvatarOverlap);
        std::string tagText;
        if (t.friends.size() == 1) {
            tagText = t.friends[0].gamertag;
        } else if (extraCount > 0) {
            tagText = "+" + std::to_string(extraCount);
        } else if (t.friends.size() == 2) {
            tagText = t.friends[0].gamertag;
        }

        int maxPillW = maxLeftW;
        int maxTextW = maxPillW - avatarsW - kPadX * 2 - 8;
        int textW = !tagText.empty() ? fonts_.semibold.measure(tagText, 12) + 6 : 0;
        if (textW > maxTextW && maxTextW > 15) {
            while (!tagText.empty() && fonts_.semibold.measure(tagText + "…", 12) + 6 > maxTextW) {
                tagText.pop_back();
            }
            tagText += "…";
            textW = fonts_.semibold.measure(tagText, 12) + 6;
        }

        int pillW = kPadX * 2 + avatarsW + textW;
        Rect pill{x + 10, y + kCard - 10 - kPillH, pillW, kPillH};
        c.fillRect(pill, rgba(0, 0, 0, 215), kPillH / 2);
        c.strokeRect(pill, rgba(255, 255, 255, 50), 1, kPillH / 2);

        int curX = pill.x + kPadX;
        int curY = pill.y + kPadY;
        for (int i = 0; i < numAvatars; ++i) {
            int ax = curX + i * kAvatarOverlap;
            const auto& friendInfo = t.friends[static_cast<size_t>(i)];
            if (auto pic = images_.get(friendInfo.gamerpicUrl, kAvatarSz, kAvatarSz)) {
                c.drawImage(*pic, ax, curY, 255, -1);
            } else {
                c.fillCircle(ax + kAvatarSz / 2.0f, curY + kAvatarSz / 2.0f, kAvatarSz / 2.0f, rgba(48, 56, 68, 255));
                if (!friendInfo.gamertag.empty()) {
                    std::string initial(1, static_cast<char>(std::toupper(static_cast<unsigned char>(friendInfo.gamertag[0]))));
                    int iw = fonts_.bold.measure(initial, 11);
                    int iy = fonts_.bold.centeredY(curY, kAvatarSz, 11);
                    fonts_.bold.draw(c, initial, ax + (kAvatarSz - iw) / 2, iy, 11, kWhite);
                }
            }
            c.strokeCircle(ax + kAvatarSz / 2.0f, curY + kAvatarSz / 2.0f, kAvatarSz / 2.0f, 1.2f, rgba(255, 255, 255, 200));
        }

        if (!tagText.empty()) {
            int tx = curX + avatarsW + 4;
            int ty = fonts_.semibold.centeredY(pill.y, pill.h, 12);
            if (extraCount > 0 && numAvatars == 2) {
                fonts_.bold.draw(c, tagText, tx, ty, 12, rgba(16, 124, 16, 255));
            } else {
                fonts_.semibold.draw(c, tagText, tx, ty, 12, kWhite);
            }
        }
    }
    if (focused) {
        // Xbox-style green focus treatment: one soft outer ring plus a crisp
        // inner edge, without a costly animation or extra image pass.
        c.strokeRect({x - 7, y - 7, kCard + 14, kCard + 14}, rgba(80, 220, 170, 110), 8, 16);
        c.strokeRect({x - 3, y - 3, kCard + 6, kCard + 6}, rgba(130, 255, 205, 255), 3, 12);
    }
}

void AppUi::drawGrid(Canvas& c, const std::vector<GameTile>& tiles, int x0, int y0, int cols, float scroll, int focus,
                     int clipTop) {
    int offset = static_cast<int>(std::lround(scroll));
    c.setClip({0, clipTop, kW, kH - clipTop});
    int firstRow = std::max(0, (clipTop - y0 + offset - kCard - 40) / kGridPitchY);
    size_t startIdx = static_cast<size_t>(firstRow) * static_cast<size_t>(cols);
    for (size_t i = startIdx; i < tiles.size(); ++i) {
        int col = static_cast<int>(i) % cols, row = static_cast<int>(i) / cols;
        int x = x0 + col * kCardPitch;
        int y = y0 + row * kGridPitchY - offset;
        if (y + kCard + 40 < clipTop) continue;
        if (y > kH) break;
        bool focused = static_cast<int>(i) == focus;
        drawCard(c, tiles[i], x, y, focused, false);
        auto name = fonts_.semibold.wrap(tiles[i].name, 20, kCard, 1);
        if (!name.empty()) fonts_.semibold.draw(c, name[0], x, y + kCard + 12, 20, focused ? kWhite : kGray);
    }
    c.clearClip();
    c.fillRect({0, 0, kW, clipTop}, kBg);
    c.gradientV({0, clipTop, kW, 12}, kBg, withAlpha(kBg, 0));
}

void AppUi::drawLibrary(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
    const int left = (kW - kGridW) / 2;
    const int clipTop = kLibraryTop - 10;
    const LibraryLayout& L = libraryLayout();
    int offset = static_cast<int>(std::lround(gridScroll_.value));
    c.setClip({0, clipTop, kW, kH - clipTop});
    // Each section has its heading in the grid: a title, the count, a line
    // of explanation (the games to buy have a bag).
    auto heading = [&](int section, const char* title, size_t count, const char* line, bool bag) {
        int hy = kLibraryTop + L.headerY[section] - offset;
        if (hy < clipTop - kSectionHeaderH || hy > kH) return;
        int x = left;
        if (bag) {
            drawBag(c, left + 14, hy + 30, kGreen);
            x += 40;
        }
        fonts_.bold.draw(c, title, x, hy + 8, 34, kWhite);
        int w = fonts_.bold.measure(title, 34);
        fonts_.semibold.draw(c, gamesCount(count), x + w + 20, hy + 20, 22, kDim);
        if (line) fonts_.regular.draw(c, line, x, hy + 52, 22, kGray);
    };
    if (!owned_.empty()) heading(0, tr(Str::YourGames), owned_.size(), nullptr, false);
    if (!purchasable_.empty()) heading(1, tr(Str::AvailableToBuy), purchasable_.size(), tr(Str::BuyToPlay), true);
    if (!hiddenTiles_.empty()) heading(2, tr(Str::HiddenSection), hiddenTiles_.size(), tr(Str::HiddenHint), false);
    int minRowY = clipTop - kLibraryTop + offset - kCard - 40;
    auto rowIt = std::lower_bound(L.rowY.begin(), L.rowY.end(), minRowY);
    int firstVisRow = static_cast<int>(rowIt - L.rowY.begin());
    auto tileIt = std::lower_bound(L.rowOf.begin(), L.rowOf.end(), firstVisRow);
    for (size_t i = static_cast<size_t>(tileIt - L.rowOf.begin()); i < L.rowOf.size(); ++i) {
        int y = kLibraryTop + L.rowY[static_cast<size_t>(L.rowOf[i])] - offset;
        if (y + kCard + 40 < clipTop) continue;
        if (y > kH) break;
        int x = left + L.colOf[i] * kCardPitch;
        const GameTile* t = libraryTile(static_cast<int>(i));
        bool focused = static_cast<int>(i) == gridFocus_;
        drawCard(c, *t, x, y, focused, t->gamePassIncluded);
        if (hidden_.count(t->productId)) c.fillRect({x, y, kCard, kCard}, rgba(0, 0, 0, 120), 10);
        auto name = fonts_.semibold.wrap(t->name, 20, kCard, 1);
        if (!name.empty()) fonts_.semibold.draw(c, name[0], x, y + kCard + 12, 20, focused ? kWhite : kGray);
    }
    c.clearClip();
    c.fillRect({0, 0, kW, clipTop}, kBg);
    c.gradientV({0, clipTop, kW, 12}, kBg, withAlpha(kBg, 0));
    drawTopBar(c);
    drawTabs(c);
    if (owned_.empty() && purchasable_.empty() && hiddenTiles_.empty()) {
        if (!ownedKnown_) {
            drawSpinner(c, kW / 2.0f, 470, 26, nowMs);
            drawCentered(c, fonts_.semibold, tr(Str::LoadingGames), 530, 28, kGray);
            animating_ = true;
        } else {
            drawCentered(c, fonts_.semibold, tr(Str::NoGames), 500, 30, kGray);
        }
    }
    {
        drawLoadingBar(c, left, 112, 420, nowMs);
        // The order (the whole tab's), right-aligned under the tabs, with its button.
        Str sortName = librarySort_ == LibrarySort::AZ        ? Str::SortAZ
                       : librarySort_ == LibrarySort::Console ? Str::SortConsole
                                                              : Str::SortRecent;
        std::string label = trf(Str::SortLabel, tr(sortName));
        int w = fonts_.semibold.measure(label, 22);
        constexpr int kAvatar = 52;
        int right = kW - kMargin - kAvatar - 16;
        fonts_.semibold.draw(c, label, right - w, fonts_.semibold.centeredY(112, 28, 22), 22, kGray);
    }
    c.gradientV({0, kH - 190, kW, 110}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    const GameTile* focus = libraryTile(gridFocus_);
    bool focusHidden = focus && hidden_.count(focus->productId);
    drawHints(c, {{kIconCross, tr(Str::Select)},
                  {kIconSquare, tr(focusHidden ? Str::Unhide : Str::Hide)},
                  {kIconTriangle, tr(Str::TabSearch)},
                  {kIconL3, tr(Str::RefreshLibrary)},
                  {kIconR3, tr(Str::SortHint)},
                  {kIconL2R2, tr(Str::Sections)},
                  {kIconOptions, tr(Str::Settings)}});
    drawPads(c);
    drawToast(c, nowMs);
}

void AppUi::drawSearch(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    // Results on the right.
    drawGrid(c, results_, kResultsX, kGridTop, kResultCols, resultScroll_.value, searchOnKeys_ ? -1 : resultFocus_,
             kGridTop - 20);
    drawTopBar(c);
    drawTabs(c);
    const char* scope = tab_ == Tab::GamePass ? tr(Str::TabGamePass) : tr(Str::YourGames);
    bool filtering = anyFilter();
    if (query_.empty() && !filtering) {
        fonts_.semibold.draw(c, tr(Str::SearchHint), kResultsX, 150, 26, kDim);
    } else if (results_.empty()) {
        const char* emptyMsg = (tab_ == Tab::Library && filterWishlist_) ? tr(Str::WishlistEmpty) : tr(Str::NoResults);
        fonts_.semibold.draw(c, emptyMsg, kResultsX, 150, 26, kGray);
    } else {
        fonts_.semibold.draw(c, trf(Str::ResultsCount, std::to_string(results_.size())), kResultsX, 150, 26, kGray);
    }
    drawLoadingBar(c, kResultsX + 420, 150, 420, nowMs);

    // The query.
    const int kbW = kKeyCols * (kKeyW + kKeyGap) - kKeyGap;
    Rect box{kMargin, 140, kbW, 76};
    c.fillRect(box, kPanel, 14);
    if (searchOnKeys_) c.strokeRect(box, rgba(255, 255, 255, 90), 2, 14);
    int tx = box.x + 24;
    if (query_.empty()) {
        fonts_.regular.draw(c, trf(Str::SearchIn, scope), tx, box.y + 22, 26, kDim);
    } else {
        fonts_.semibold.draw(c, query_, tx, box.y + 20, 30, kWhite);
        tx += fonts_.semibold.measure(query_, 30) + 4;
    }
    if (searchOnKeys_ && (nowMs / 530) % 2 == 0) c.fillRect({query_.empty() ? box.x + 20 : tx, box.y + 18, 3, 40}, kWhite);
    if (searchOnKeys_) animating_ = true;  // the caret blinks

    // The keys.
    for (int r = 0; r <= kKeyRows; ++r) {
        int n = r == kKeyRows ? 3 : kKeyCols;
        for (int k = 0; k < n; ++k) {
            int col = r == kKeyRows ? kWideCols[k] : k;
            int span = r == kKeyRows ? kWideSpan[k] : 1;
            Rect key{kMargin + col * (kKeyW + kKeyGap), kKeysY + r * (kKeyH + kKeyGap),
                     span * kKeyW + (span - 1) * kKeyGap, kKeyH};
            bool focused = searchOnKeys_ && keyRow_ == r && keyCol_ == k;
            c.fillRect(key, focused ? kWhite : kPanel, 10);
            std::string label = r == kKeyRows ? tr(k == 0 ? Str::KeySpace : k == 1 ? Str::KeyDelete : Str::KeyClear)
                                               : std::string(1, static_cast<char>(std::toupper(kKeys[r * kKeyCols + k])));
            int px = r == kKeyRows ? 22 : 28;
            int w = fonts_.semibold.measure(label, px);
            fonts_.semibold.draw(c, label, key.x + (key.w - w) / 2, fonts_.semibold.centeredY(key.y, kKeyH, px), px,
                                 focused ? kBg : kWhite);
        }
    }
    c.gradientV({0, kH - 190, kW, 110}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    // The filters, 3x3 grid under the keys: green when on; the lists with a chevron.
    const int gap = kChipGap, px = 18, fh = 48;
    const int fy0 = kKeysY + (kKeyRows + 1) * (kKeyH + kKeyGap) + 8;
    constexpr int kCols = 3;
    const int chipW = (kKeysW - gap * (kCols - 1)) / kCols;
    Rect openChip{};
    for (int row = 0; row < 3; ++row) {
        auto chips = filterRow(row);
        if (chips.empty()) continue;
        int fy = fy0 + row * (fh + 8);
        for (size_t k = 0; k < chips.size(); ++k) {
            Filter f = chips[k];
            bool on = filterOn(f);
            bool focused = searchOnKeys_ && keyRow_ == kKeyRows + 1 + row && keyCol_ == static_cast<int>(k);
            Rect chip{kMargin + static_cast<int>(k) * (chipW + gap), fy, chipW, fh};
            if (filterList_ == static_cast<int>(f)) openChip = chip;
            c.fillRect(chip, focused ? kWhite : on ? rgba(16, 124, 16, 255) : kPanel, fh / 2);
            if (on && focused) c.strokeRect({chip.x + 3, chip.y + 3, chip.w - 6, chip.h - 6}, kGreen, 3, fh / 2 - 3);
            Color text = focused ? kBg : kWhite;
            int room = chip.w - (isList(f) ? 56 : 20);
            int chipPx = px;
            while (chipPx > 13 && fonts_.semibold.measure(filterText(f), chipPx) > room) --chipPx;
            std::string label = fonts_.semibold.fit(filterText(f), chipPx, room);
            int tw = fonts_.semibold.measure(label, chipPx);
            int tx = chip.x + (chip.w - tw) / 2;
            fonts_.semibold.draw(c, label, tx, fonts_.semibold.centeredY(chip.y, chip.h, chipPx), chipPx, text);
            if (isList(f)) {  // a list: a chevron at the right
                float cx = static_cast<float>(chip.x + chip.w - 20), cy = static_cast<float>(chip.y + chip.h / 2);
                c.line(cx - 5, cy - 3, cx, cy + 2, 2.2f, text);
                c.line(cx, cy + 2, cx + 5, cy - 3, 2.2f, text);
            }
        }
    }
    if (filterList_ >= 0) {
        // The open list, above its chip (they are near the bottom); scrolls
        // past kVisible choices.
        Filter f = static_cast<Filter>(filterList_);
        auto options = filterOptions(f);
        int sel = filterSelected(f);
        constexpr int kItemH = 54, kVisible = 9;
        int n = static_cast<int>(options.size()), visible = std::min(n, kVisible);
        if (filterListIndex_ < filterListTop_) filterListTop_ = filterListIndex_;
        if (filterListIndex_ >= filterListTop_ + visible) filterListTop_ = filterListIndex_ - visible + 1;
        int listW = std::max(openChip.w, 460);
        Rect panel{openChip.x, openChip.y - 10 - (visible * kItemH + 16), listW, visible * kItemH + 16};
        panel.y = std::max(120, panel.y);
        c.fillRect({panel.x + 6, panel.y + 10, panel.w, panel.h}, rgba(0, 0, 0, 120), 18);  // shadow
        c.fillRect(panel, rgba(38, 38, 38, 250), 18);
        c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 18);
        for (int k = 0; k < visible; ++k) {
            int i = filterListTop_ + k;
            Rect item{panel.x + 8, panel.y + 8 + k * kItemH, panel.w - 16, kItemH};
            bool on = i == filterListIndex_;
            if (on) c.fillRect(item, kWhite, 12);
            Color text = on ? kBg : (i == sel ? kWhite : kGray);
            std::string label = fonts_.semibold.fit(options[static_cast<size_t>(i)], 24, item.w - 70);
            fonts_.semibold.draw(c, label, item.x + 20, fonts_.semibold.centeredY(item.y, item.h, 24), 24, text);
            if (i == sel) {  // a check mark on the current choice
                float cx = static_cast<float>(item.x + item.w - 30), cy = static_cast<float>(item.y + item.h / 2);
                c.line(cx - 9, cy, cx - 3, cy + 6, 3, on ? kBg : kGreen);
                c.line(cx - 3, cy + 6, cx + 9, cy - 6, 3, on ? kBg : kGreen);
            }
        }
        if (filterListTop_ > 0) {  // more above / below
            float cx = static_cast<float>(panel.x + panel.w / 2), cy = static_cast<float>(panel.y - 12);
            c.line(cx - 9, cy + 4, cx, cy - 4, 3, kGray);
            c.line(cx, cy - 4, cx + 9, cy + 4, 3, kGray);
        }
        if (filterListTop_ + visible < n) {
            float cx = static_cast<float>(panel.x + panel.w / 2), cy = static_cast<float>(panel.y + panel.h + 12);
            c.line(cx - 9, cy - 4, cx, cy + 4, 3, kGray);
            c.line(cx, cy + 4, cx + 9, cy - 4, 3, kGray);
        }
    }
    if (filterList_ >= 0)
        drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconCircle, tr(Str::Back)}});
    else if (searchOnKeys_)
        drawHints(c, {{kIconCross, tr(Str::KeyType)}, {kIconSquare, tr(Str::KeyDelete)}, {kIconCircle, tr(Str::Back)}});
    else
        drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconSquare, tr(Str::KeyDelete)}, {kIconCircle, tr(Str::Back)}});
    drawToast(c, nowMs);
}

void AppUi::drawConsoles(Canvas& c, uint64_t nowMs) {
    drawBackground(c);
    drawTopBar(c);
    drawTabs(c);
    {
        constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
        const int left = (kW - kGridW) / 2;
        drawLoadingBar(c, left, 112, 420, nowMs);
    }
    fonts_.bold.draw(c, tr(Str::TabConsoles), kMargin, 170, 52, kWhite);
    fonts_.regular.draw(c, tr(Str::ConsolesHint), kMargin, 240, 26, kGray);
    if (consoles_.empty()) {
        if (!consolesKnown_) {
            drawSpinner(c, kW / 2.0f, 520, 26, nowMs);
            drawCentered(c, fonts_.semibold, tr(Str::ConsolesLoading), 580, 28, kGray);
            animating_ = true;
            drawHints(c, {{kIconOptions, tr(Str::Settings)}, {kIconTouchpad, tr(Str::HoldSignOut)}});
        } else {
            // How a console shows up: three numbered steps, and Microsoft's
            // help on the phone.
            fonts_.bold.draw(c, tr(Str::NoConsoles), kMargin, 340, 40, kWhite);
            int y = 420;
            const Str steps[] = {Str::ConsoleStep1, Str::ConsoleStep2, Str::ConsoleStep3};
            for (int i = 0; i < 3; ++i) {
                c.fillCircle(static_cast<float>(kMargin + 22), static_cast<float>(y + 20), 22, kGreen);
                std::string num = std::to_string(i + 1);
                fonts_.bold.draw(c, num, kMargin + 22 - fonts_.bold.measure(num, 24) / 2,
                                 fonts_.bold.centeredY(y - 2, 44, 24), 24, kWhite);
                for (const auto& line : fonts_.regular.wrap(tr(steps[i]), 28, 980, 2)) {
                    fonts_.regular.draw(c, line, kMargin + 70, y + 2, 28, kGray);
                    y += 40;
                }
                y += 34;
            }
            Rect box = drawQr(c, "https://support.xbox.com/help/games-apps/game-setup-and-play/how-to-use-remote-play",
                              kW - kMargin, 330, 340);
            if (box.w > 0) {
                int w = fonts_.semibold.measure(tr(Str::ScanForHelp), 24);
                fonts_.semibold.draw(c, tr(Str::ScanForHelp), box.x + (box.w - w) / 2, box.y + box.h + 22, 24, kWhite);
            }
            drawHints(c, {{kIconCross, tr(Str::SearchAgain)},
                          {kIconOptions, tr(Str::Settings)},
                          {kIconTouchpad, tr(Str::HoldSignOut)}});
        }
        drawPads(c);
        return;
    }
    // One card per console: a drawn Xbox, its name, model and state.
    constexpr int kCardW = 480, kCardH = 380, kGap = 40, kTop = 320;
    for (size_t i = 0; i < consoles_.size(); ++i) {
        const ConsoleTile& con = consoles_[i];
        int x = kMargin + static_cast<int>(i) * (kCardW + kGap);
        if (x > kW) break;
        bool focused = static_cast<int>(i) == consoleFocus_;
        Rect card{x, kTop, kCardW, kCardH};
        if (focused) c.strokeRect({card.x - 7, card.y - 7, card.w + 14, card.h + 14}, kWhite, 4, 22);
        c.fillRect(card, kPanel, 16);
        // The console: a Series X stands as a tower, the others lie flat.
        bool tower = con.type == "XboxSeriesX";
        Rect box = tower ? Rect{x + kCardW / 2 - 55, kTop + 40, 110, 170} : Rect{x + kCardW / 2 - 120, kTop + 90, 240, 70};
        c.fillRect(box, rgba(20, 20, 20), 12);
        c.strokeRect(box, rgba(70, 70, 70), 2, 12);
        bool on = con.power == "On";
        c.fillCircle(static_cast<float>(box.x + (tower ? box.w / 2 : 24)), static_cast<float>(box.y + (tower ? 22 : box.h / 2)),
                     tower ? 16.0f : 9.0f, on ? kGreen : rgba(90, 90, 90));
        std::string name = con.name.empty() ? "Xbox" : con.name;
        auto lines = fonts_.semibold.wrap(name, 30, kCardW - 48, 1);
        if (!lines.empty()) fonts_.semibold.draw(c, lines[0], x + 24, kTop + 236, 30, kWhite);
        std::string model = con.type == "XboxSeriesX"   ? "Xbox Series X"
                            : con.type == "XboxSeriesS" ? "Xbox Series S"
                            : con.type == "XboxOneX"    ? "Xbox One X"
                            : con.type == "XboxOneS"    ? "Xbox One S"
                            : con.type == "XboxOne"     ? "Xbox One"
                                                        : con.type;
        fonts_.regular.draw(c, model, x + 24, kTop + 284, 24, kGray);
        const char* state = on ? tr(Str::ConsoleOn)
                            : con.power == "ConnectedStandby" ? tr(Str::ConsoleSleeping)
                                                              : tr(Str::ConsoleOff);
        c.fillCircle(static_cast<float>(x + 32), static_cast<float>(kTop + 336), 6, on ? rgba(60, 200, 60) : kDim);
        fonts_.semibold.draw(c, state, x + 48, kTop + 322, 24, on ? kWhite : kGray);
    }
    drawHints(c, {{kIconCross, tr(Str::Play)},
                  {kIconTriangle, tr(Str::SearchAgain)},
                  {kIconOptions, tr(Str::Settings)},
                  {kIconTouchpad, tr(Str::HoldSignOut)}});
    drawPads(c);
    drawToast(c, nowMs);
}

void AppUi::drawHome(Canvas& c, uint64_t nowMs) {
    if (searching_) return drawSearch(c, nowMs);
    if (tab_ == Tab::Library) return drawLibrary(c, nowMs);
    if (tab_ == Tab::Consoles) return drawConsoles(c, nowMs);
    const GameTile* focus = focusedTile();
    c.clear(kBg);
    std::string hero;
    if (focus) {
        hero = focus->heroUrl;
        bool hadFocusHero = !hero.empty();
        if (hero.empty() && !focus->productId.empty()) {
            auto it = detailInfo_.find(focus->productId);
            if (it != detailInfo_.end() && !it->second.heroUrl.empty()) {
                hero = it->second.heroUrl;
            }
        }
        if (hero.empty()) {
            auto matchHero = [&](const GameTile& t) -> bool {
                if (t.heroUrl.empty()) return false;
                if (!focus->productId.empty() && !t.productId.empty()) {
                    return t.productId == focus->productId;
                }
                if (!focus->productId.empty() && t.productId == focus->productId) return true;
                if (!focus->titleId.empty() && t.titleId == focus->titleId) return true;
                if (!focus->xboxTitleId.empty() && t.xboxTitleId == focus->xboxTitleId) return true;
                return false;
            };
            const GameTile* matched = nullptr;
            for (const auto& r : rows_) {
                for (const auto& t : r.tiles) {
                    if (matchHero(t)) { matched = &t; break; }
                }
                if (matched) break;
            }
            if (!matched) {
                for (const auto& r : allRows_) {
                    for (const auto& t : r.tiles) {
                        if (matchHero(t)) { matched = &t; break; }
                    }
                    if (matched) break;
                }
            }
            if (!matched) {
                for (const auto* list : {&gamePassPool_, &libraryPool_, &allOwned_, &allPurchasable_}) {
                    for (const auto& t : *list) {
                        if (matchHero(t)) { matched = &t; break; }
                    }
                    if (matched) break;
                }
            }
            if (matched) {
                hero = matched->heroUrl;
                syncHeroToRowsLocked(matched->productId, matched->titleId, matched->heroUrl,
                                     matched->description, matched->publisher, matched->categories);
                hadFocusHero = true;
            }
        }
        if (!hadFocusHero && !hero.empty()) {
            syncHeroToRowsLocked(focus->productId, focus->titleId, hero);
        }
    }
    // The focused game's artwork fills the background; it follows the focus.
    drawHero(c, nowMs, hero, false);

    // Give the selected title a clear hero area, as on a console dashboard.
    // Only catalogue metadata already present on the tile is shown; this does
    // not imply ownership, cloud eligibility, or a promotion.
    if (focus && !focus->name.empty()) {
        const int textX = kMargin;
        constexpr int titlePx = 48;
        const std::string title = fonts_.bold.fit(focus->name, titlePx, 1050);
        fonts_.bold.draw(c, title, textX, 182, titlePx, kWhite);

        int metaY = 246;
        if (!focus->publisher.empty()) {
            const std::string publisher = fonts_.semibold.fit(focus->publisher, 22, 980);
            fonts_.semibold.draw(c, publisher, textX, metaY, 22, rgba(232, 236, 242));
            metaY += 38;
        }
        if (!focus->description.empty()) {
            const auto summary = fonts_.regular.wrap(focus->description, 20, 940, 2);
            for (const auto& line : summary) {
                fonts_.regular.draw(c, line, textX, metaY, 20, rgba(218, 224, 232));
                metaY += 29;
            }
        }
    }

    if (rows_.empty()) {
        drawCentered(c, fonts_.semibold, tr(Str::NoGames), 500, 32, kGray);
        drawHints(c, {{kIconOptions, tr(Str::Settings)}, {kIconTouchpad, tr(Str::HoldSignOut)}});
        c.fillRect({0, 0, kW, 140}, kBg);
        c.gradientV({0, 140, kW, 16}, kBg, withAlpha(kBg, 0));
        drawTopBar(c);
        drawTabs(c);
        {
            constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
            const int left = (kW - kGridW) / 2;
            drawLoadingBar(c, left, 112, 420, nowMs);
        }
        return;
    }

    constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
    const int gridLeft = (kW - kGridW) / 2;
    int afterGridOffset = 0;
    constexpr int clipTop = 150;
    c.setClip({0, clipTop, kW, kH - 220});

    for (size_t r = 0; r < rows_.size(); ++r) {
        float rel = static_cast<float>(r) - rowY_.value;
        int y = kRowTop + static_cast<int>(std::lround(rel * kRowPitch)) + afterGridOffset;
        const GameRow& row = rows_[r];

        if (row.isGrid) {
            int offset = static_cast<int>(std::lround(rowScroll_[r].value));
            int hy = y - offset;
            int cardY = hy + 48;

            if (hy > 80 && hy < kH) {
                fonts_.bold.draw(c, row.title, gridLeft, hy + 8, 34, kWhite);
                int w = fonts_.bold.measure(row.title, 34);
                fonts_.semibold.draw(c, gamesCount(row.tiles.size()), gridLeft + w + 20, hy + 20, 22, kDim);
            }

            int firstGridRow = std::max(0, (clipTop - cardY - kCard - 40) / kGridPitchY);
            size_t startIdx = static_cast<size_t>(firstGridRow) * static_cast<size_t>(kLibraryCols);
            for (size_t i = startIdx; i < row.tiles.size(); ++i) {
                int c_col = static_cast<int>(i) % kLibraryCols;
                int c_row = static_cast<int>(i) / kLibraryCols;
                int cy = cardY + c_row * kGridPitchY;
                if (cy + kCard + 40 < clipTop) continue;
                if (cy > kH) break;
                int cx = gridLeft + c_col * kCardPitch;
                bool focused = static_cast<int>(r) == focusRow_ && static_cast<int>(i) == focusCol_[r];
                drawCard(c, row.tiles[i], cx, cy, focused, row.gamePassBadges);
                if (hidden_.count(row.tiles[i].productId)) c.fillRect({cx, cy, kCard, kCard}, rgba(0, 0, 0, 120), 10);
                auto name = fonts_.semibold.wrap(row.tiles[i].name, 20, kCard, 1);
                if (!name.empty()) fonts_.semibold.draw(c, name[0], cx, cy + kCard + 12, 20, focused ? kWhite : kGray);
            }

            int gridHeight = 48 + ((static_cast<int>(row.tiles.size()) + kLibraryCols - 1) / kLibraryCols) * kGridPitchY;
            afterGridOffset += gridHeight - offset - kRowPitch;
        } else {
            if (rel < -0.7f && afterGridOffset == 0) continue;
            if (y > kH && afterGridOffset == 0) break;
            fonts_.semibold.draw(c, row.title, kMargin, y + 8, 22, rgba(255, 255, 255, 215));
            const int cardY = y + 48;
            const int ri = static_cast<int>(r);
            const bool rowFoc = ri == focusRow_;
            int scroll = static_cast<int>(std::lround(rowScroll_[r].value));
            int firstCol = std::max(0, scroll / homePitch(ri) - 1);
            for (size_t col = static_cast<size_t>(firstCol); col < row.tiles.size(); ++col) {
                const bool focused = rowFoc && static_cast<int>(col) == focusCol_[r];
                const int x = homeTileX(ri, static_cast<int>(col), focusCol_[r], rowFoc, scroll);
                if (x > kW) break;
                const int tw = homeTileW(ri, focused), th = homeTileH(ri, focused);
                if (x + tw < 0) continue;
                drawHomeTile(c, row.tiles[col], x, cardY, tw, th, focused, ri == 0, row.gamePassBadges);
                if (hidden_.count(row.tiles[col].productId)) c.fillRect({x, cardY, tw, th}, rgba(0, 0, 0, 120), 10);
                if (focused && ri == 0) {
                    // The name under the large tile, as the dashboard shows it.
                    auto name = fonts_.semibold.wrap(row.tiles[col].name, 26, kHomeFocus + 160, 1);
                    if (!name.empty()) fonts_.semibold.draw(c, name[0], x, cardY + th + 16, 26, kWhite);
                }
            }
        }
    }

    c.clearClip();

    drawTopBar(c);
    drawTabs(c);
    {
        constexpr int kGridW = kLibraryCols * kCardPitch - kCardGap;
        const int left = (kW - kGridW) / 2;
        drawLoadingBar(c, left, 120, 420, nowMs);
    }

    // Fade the rows out under the button hints.
    c.gradientV({0, kH - 150, kW, 70}, withAlpha(kBg, 0), withAlpha(kBg, 245));
    c.fillRect({0, kH - 80, kW, 80}, withAlpha(kBg, 245));
    drawHints(c, {{kIconCross, tr(Str::Select)},
                  {kIconSquare, tr(focus && hidden_.count(focus->productId) ? Str::Unhide : Str::Hide)},
                  {kIconTriangle, tr(Str::TabSearch)},
                  {kIconL3, tr(Str::RefreshLibrary)},
                  {kIconOptions, tr(Str::Settings)},
                  {kIconTouchpad, tr(Str::HoldSignOut)}});
    drawPads(c);
    drawToast(c, nowMs);
}

void AppUi::drawDetails(Canvas& c, uint64_t nowMs) {
    const GameTile* g = &detail_;
    playRect_ = {};
    achRect_ = {};
    claimRect_ = {};
    c.clear(kBg);
    if (g->productId.empty() && g->titleId.empty()) return;

    // A game-specific backdrop and restrained dark veil keep the cover and
    // text legible, while echoing the Xbox detail-page composition.
    if (!drawHero(c, nowMs, g->heroUrl, true)) drawBackground(c);
    c.gradientV({0, 0, kW, kH}, rgba(0, 0, 0, 110), rgba(0, 0, 0, 205));
    drawTopBar(c);

    const bool isF2P = isFreeGame(*g);
    const bool effectivePlayable = isEffectivePlayableLocked(*g);
    const bool hasAchievements = effectivePlayable && !g->xboxTitleId.empty();
    const bool playFocused = detailButtonFocus_ == 0;
    const bool achFocused = detailButtonFocus_ == 1 && hasAchievements;
    const char* planName = planDisplayName(g->plan);
    const bool includedWithAccountPlan = g->plan > 0 &&
        xcloud::planIncludes(xcloud::planTier(subscription_), g->plan, g->freeInStore);

    // Title block: one strong title line, followed by store metadata and a
    // short description, similar to the reference screen.
    const int titleY = 150;
    const int titleSize = 58;
    auto titleLines = fonts_.bold.wrap(g->name, titleSize, 1450, 1);
    if (!titleLines.empty()) fonts_.bold.draw(c, titleLines.front(), kMargin, titleY, titleSize, kWhite);
    int metaY = 220;
    std::string publisher;
    if (!g->publisher.empty()) publisher = trf(Str::PublisherBy, g->publisher);
    if (planName && g->playable) {
        if (!publisher.empty()) publisher += "   â¢   ";
        publisher += trf(Str::IncludedWithPlan, planName);
    }
    if (!publisher.empty()) {
        fonts_.semibold.draw(c, publisher, kMargin, metaY, 25, rgba(226, 230, 236));
        metaY += 38;
    }
    if (!g->description.empty()) {
        int lines = 0;
        for (const auto& line : fonts_.regular.wrap(g->description, 23, 1580, 2)) {
            fonts_.regular.draw(c, line, kMargin, metaY, 23, rgba(218, 222, 230));
            metaY += 32;
            if (++lines == 2) break;
        }
    }

    // Age rating badge and descriptors (only when supplied by Xbox metadata).
    if (!g->ratingAge.empty()) {
        Color badgeBg = rgba(100, 100, 100);
        Color badgeFg = kWhite;
        if (g->ratingAge == "Livre" || g->ratingAge == "E" || g->ratingAge == "3+" || g->ratingAge == "0") badgeBg = rgba(0, 145, 70);
        else if (g->ratingAge == "10+" || g->ratingAge == "E10+" || g->ratingAge == "7+") badgeBg = rgba(0, 105, 200);
        else if (g->ratingAge == "12+") { badgeBg = rgba(220, 170, 0); badgeFg = rgba(20, 20, 20); }
        else if (g->ratingAge == "14+" || g->ratingAge == "T") badgeBg = rgba(215, 90, 0);
        else if (g->ratingAge == "16+" || g->ratingAge == "M") badgeBg = rgba(195, 25, 30);
        else if (g->ratingAge == "18+" || g->ratingAge == "AO") badgeBg = rgba(28, 28, 28);
        int tw = fonts_.bold.measure(g->ratingAge, 19);
        Rect badge{kW - kMargin - std::max(48, tw + 22), 160, std::max(48, tw + 22), 34};
        c.fillRect(badge, badgeBg, 7);
        fonts_.bold.draw(c, g->ratingAge, badge.x + (badge.w - tw) / 2,
                         fonts_.bold.centeredY(badge.y, badge.h, 19), 19, badgeFg);
        if (!g->ratingSystem.empty())
            fonts_.semibold.draw(c, g->ratingSystem, badge.x - 210, badge.y + 6, 20, rgba(225, 228, 234));
    }

    // Xbox-style detail layout: cover, friends, player statistics and a
    // capture panel. The preview is labelled as official artwork; PBOX cannot
    // retrieve Xbox-console capture files for a cloud-streamed title.
    const Rect cover{kMargin, 344, 340, 430};
    const Rect friends{kMargin + 364, 344, 420, 430};
    const Rect stats{kMargin + 808, 344, 420, 430};
    const Rect captures{kMargin + 1252, 344, kW - kMargin - (kMargin + 1252), 430};
    const Color glass = rgba(8, 12, 20, 196);
    c.fillRect({cover.x - 8, cover.y - 8, cover.w + 16, cover.h + 16}, rgba(255, 255, 255, 44), 20);
    if (auto img = images_.get(g->tileUrl, cover.w, cover.h)) c.drawImage(*img, cover.x, cover.y, 255, 12);
    else {
        c.fillRect(cover, rgba(27, 34, 45), 12);
        for (const auto& line : fonts_.semibold.wrap(g->name, 27, cover.w - 40, 4))
            fonts_.semibold.draw(c, line, cover.x + 20, cover.y + 30, 27, kWhite);
    }
    auto panel = [&](Rect r, Str heading) {
        c.fillRect(r, glass, 18);
        c.strokeRect(r, rgba(255, 255, 255, 42), 1, 18);
        fonts_.bold.draw(c, tr(heading), r.x + 24, r.y + 22, 26, kWhite);
    };
    panel(friends, Str::FriendsPlayingNow);
    panel(stats, Str::GameStatistics);
    panel(captures, Str::Captures);

    if (g->friends.empty()) {
        const auto lines = fonts_.regular.wrap(tr(Str::NoFriendsPlaying), 22, friends.w - 52, 3);
        int y = friends.y + 170;
        for (const auto& line : lines) {
            const int tw = fonts_.regular.measure(line, 22);
            fonts_.regular.draw(c, line, friends.x + (friends.w - tw) / 2, y, 22, rgba(194, 202, 214));
            y += 34;
        }
    } else {
        const int count = std::min<int>(4, static_cast<int>(g->friends.size()));
        for (int i = 0; i < count; ++i) {
            const FriendPlaying& friendInfo = g->friends[static_cast<size_t>(i)];
            const int y = friends.y + 76 + i * 78;
            c.fillCircle(static_cast<float>(friends.x + 52), static_cast<float>(y + 24), 25, rgba(64, 83, 102));
            if (!friendInfo.gamerpicUrl.empty())
                if (auto avatar = images_.get(friendInfo.gamerpicUrl, 50, 50)) c.drawImage(*avatar, friends.x + 27, y, 255, -1);
            c.fillCircle(static_cast<float>(friends.x + 85), static_cast<float>(y + 45), 6, kGreen);
            fonts_.semibold.draw(c, friendInfo.gamertag, friends.x + 100, y + 12, 22, kWhite);
            fonts_.regular.draw(c, tr(Str::FriendsPlayingNow), friends.x + 100, y + 42, 17, rgba(170, 181, 196));
        }
    }

    fonts_.semibold.draw(c, trf(Str::PlaytimePlayed, formatPlaytime([&] {
        uint64_t seconds = 0;
        if (!g->productId.empty()) { auto it = settings_.titlePlaytime.find(g->productId); if (it != settings_.titlePlaytime.end()) seconds = it->second; }
        if (!seconds && !g->titleId.empty()) { auto it = settings_.titlePlaytime.find(g->titleId); if (it != settings_.titlePlaytime.end()) seconds = it->second; }
        return seconds;
    }())), stats.x + 24, stats.y + 78, 22, rgba(228, 232, 238));
    fonts_.semibold.draw(c, tr(Str::Achievements), stats.x + 24, stats.y + 133, 22, rgba(228, 232, 238));
    auto ach = achievements_.find(g->xboxTitleId);
    if (!g->xboxTitleId.empty() && ach != achievements_.end() && ach->second.totalCount > 0) {
        const auto& data = ach->second;
        drawGamerscoreBadge(c, static_cast<float>(stats.x + 46), static_cast<float>(stats.y + 191), 15.0f);
        const std::string progress = std::to_string(data.unlockedCount) + " / " + std::to_string(data.totalCount);
        fonts_.bold.draw(c, progress, stats.x + 74, stats.y + 164, 30, kWhite);
        const std::string score = std::to_string(data.unlockedGamerscore) + " / " + std::to_string(data.totalGamerscore) + " G";
        fonts_.semibold.draw(c, score, stats.x + 76, stats.y + 202, 20, rgba(198, 205, 216));
        const float fraction = static_cast<float>(data.unlockedCount) / static_cast<float>(data.totalCount);
        Rect bar{stats.x + 24, stats.y + 244, stats.w - 48, 8};
        c.fillRect(bar, rgba(255, 255, 255, 45), 4);
        c.fillRect({bar.x, bar.y, static_cast<int>(bar.w * std::clamp(fraction, 0.0f, 1.0f)), bar.h}, kGreen, 4);
    } else {
        fonts_.regular.draw(c, tr(Str::NoAchievements), stats.x + 24, stats.y + 174, 20, rgba(194, 202, 214));
    }
    std::string accessText = effectivePlayable ? std::string(tr(Str::Play))
        : (g->plan > 0 && !includedWithAccountPlan ? std::string(tr(Str::GamePassRequired))
           : (g->purchasable && !isF2P ? std::string(tr(Str::BuyToPlay))
              : (planName ? trf(Str::IncludedWithPlan, planName) : std::string(tr(Str::NotPlayable)))));
    if (g->purchasable && !effectivePlayable && !isF2P) {
        for (const auto& line : fonts_.regular.wrap(accessText, 17, 220, 2))
            fonts_.regular.draw(c, line, stats.x + 24, stats.y + 278, 17, rgba(225, 190, 150));
        auto price = prices_.find(g->productId);
        if (price != prices_.end()) fonts_.bold.draw(c, price->second.now, stats.x + 24, stats.y + 324, 26, kWhite);
        const int qrSize = 96;
        Rect qr = drawQr(c, "https://www.xbox.com/games/store/p/" + g->productId,
                         stats.x + stats.w - qrSize - 18, stats.y + 308, qrSize);
        if (qr.w > 0) {
            const char* scan = tr(Str::ScanToBuy);
            const int textW = fonts_.semibold.measure(scan, 14);
            fonts_.semibold.draw(c, scan, qr.x + (qr.w - textW) / 2, qr.y + qr.h + 2, 14, kWhite);
        }
        for (const auto& line : fonts_.regular.wrap(tr(Str::BuyHint), 14, 210, 2))
            fonts_.regular.draw(c, line, stats.x + 24, stats.y + 358, 14, rgba(195, 202, 214));
    } else {
        for (const auto& line : fonts_.regular.wrap(accessText, 18, stats.w - 48, 2))
            fonts_.regular.draw(c, line, stats.x + 24, stats.y + 286, 18,
                                effectivePlayable ? rgba(125, 220, 164) : rgba(225, 190, 150));
    }

    const Rect preview{captures.x + 18, captures.y + 68, captures.w - 36, 238};
    c.fillRect(preview, rgba(18, 24, 34), 12);
    if (!g->heroUrl.empty()) {
        if (auto art = images_.get(g->heroUrl, preview.w, preview.h)) c.drawImage(*art, preview.x, preview.y, 235, 12);
    } else {
        c.fillCircle(static_cast<float>(preview.x + preview.w / 2), static_cast<float>(preview.y + 74), 34, rgba(255, 255, 255, 70));
        c.fillRect({preview.x + preview.w / 2 - 62, preview.y + 128, 124, 64}, rgba(255, 255, 255, 45), 12);
    }
    c.gradientV({preview.x, preview.y + preview.h - 74, preview.w, 74}, rgba(0, 0, 0, 0), rgba(0, 0, 0, 185));
    int captureY = captures.y + 324;
    for (const auto& line : fonts_.regular.wrap(tr(Str::NoCloudCaptures), 18, captures.w - 44, 3)) {
        fonts_.regular.draw(c, line, captures.x + 22, captureY, 18, rgba(205, 211, 220));
        captureY += 25;
    }

    if (!g->ratingDescriptors.empty()) {
        std::string descriptors;
        for (const auto& d : g->ratingDescriptors) {
            std::string candidate = descriptors.empty() ? d : descriptors + "  •  " + d;
            if (fonts_.regular.measure(candidate, 18) > captures.w - 44) break;
            descriptors = candidate;
        }
        if (!descriptors.empty())
            fonts_.regular.draw(c, descriptors, captures.x + 22, captures.y + captures.h - 29, 16, rgba(174, 184, 198));
    }

    // Action row stays usable with the same Cross/left-right semantics and
    // mouse hit regions. A purchasable cloud title can be attempted even if the
    // local ownership/plan snapshot is uncertain; Xbox decides the session.
    const int actionY = 834;
    if (canAttemptLaunchLocked(*g)) {
        Rect play{kMargin, actionY, 365, 82};
        playRect_ = play;
        if (playFocused) c.strokeRect({play.x - 7, play.y - 7, play.w + 14, play.h + 14}, kWhite, 4, 43);
        c.fillRect(play, playFocused ? kGreen : rgba(16, 124, 65, 205), 38);
        const float cx = static_cast<float>(play.x + 52), cy = static_cast<float>(play.y + play.h / 2);
        c.line(cx - 7, cy - 12, cx - 7, cy + 12, 4, kWhite);
        c.line(cx - 7, cy - 12, cx + 13, cy, 4, kWhite);
        c.line(cx - 7, cy + 12, cx + 13, cy, 4, kWhite);
        fonts_.semibold.draw(c, tr(Str::PlayInCloud), play.x + 90, play.y + 23, 30, kWhite);
        if (hasAchievements) {
            std::string label = tr(Str::Achievements);
            if (ach != achievements_.end() && ach->second.totalCount > 0)
                label = std::to_string(ach->second.unlockedCount) + "/" + std::to_string(ach->second.totalCount);
            int textW = fonts_.semibold.measure(label, 25);
            Rect button{play.x + play.w + 28, actionY, std::max(245, textW + 76), 82};
            achRect_ = button;
            if (achFocused) c.strokeRect({button.x - 7, button.y - 7, button.w + 14, button.h + 14}, kWhite, 4, 43);
            c.fillRect(button, achFocused ? kGreen : rgba(255, 255, 255, 35), 38);
            drawGamerscoreBadge(c, static_cast<float>(button.x + 34), static_cast<float>(button.y + 41), 14.0f);
            fonts_.semibold.draw(c, label, button.x + 62, button.y + 27, 25, kWhite);
        }
        int nextX = hasAchievements ? achRect_.x + achRect_.w + 26 : play.x + play.w + 24;
        if (queueTitle_ == g->titleId && queueSeconds_ >= 0) {
            std::string wait = queueSeconds_ < 60 ? trf(Str::QueueSeconds, std::to_string(queueSeconds_))
                                                  : trf(Str::QueueMinutes, std::to_string((queueSeconds_ + 59) / 60));
            int w = fonts_.regular.measure(wait, 22);
            Rect pill{nextX, actionY + 17, w + 36, 48};
            c.fillRect(pill, rgba(0, 0, 0, 170), 24);
            fonts_.regular.draw(c, wait, pill.x + 18, pill.y + 11, 22, kWhite);
            nextX += pill.w + 16;
        }
        uint64_t played = 0;
        if (!g->productId.empty()) { auto it = settings_.titlePlaytime.find(g->productId); if (it != settings_.titlePlaytime.end()) played = it->second; }
        if (!played && !g->titleId.empty()) { auto it = settings_.titlePlaytime.find(g->titleId); if (it != settings_.titlePlaytime.end()) played = it->second; }
        if (played) {
            std::string text = trf(Str::PlaytimePlayed, formatPlaytime(played));
            int w = fonts_.semibold.measure(text, 21);
            Rect pill{nextX, actionY + 17, w + 36, 48};
            c.fillRect(pill, rgba(0, 0, 0, 170), 24);
            fonts_.semibold.draw(c, text, pill.x + 18, pill.y + 12, 21, kWhite);
        }
    } else if (isF2P && !g->playable) {
        const char* label = tr(Str::ClaimFree);
        int textW = fonts_.semibold.measure(label, 29);
        Rect claim{kMargin, actionY, std::max(330, textW + 72), 82};
        claimRect_ = claim;
        if (playFocused) c.strokeRect({claim.x - 7, claim.y - 7, claim.w + 14, claim.h + 14}, kWhite, 4, 43);
        c.fillRect(claim, kGreen, 38);
        fonts_.semibold.draw(c, label, claim.x + (claim.w - textW) / 2, claim.y + 26, 29, kWhite);
    } else {
        Rect locked{kMargin, actionY, 550, 82};
        c.fillRect(locked, rgba(255, 255, 255, 32), 38);
        drawLock(c, locked.x + 43, locked.y + locked.h / 2);
        fonts_.semibold.draw(c, accessText, locked.x + 82, locked.y + 26, 22, kWhite);
    }

    std::vector<Hint> hints;
    if (effectivePlayable) hints.push_back({kIconCross, tr(achFocused ? Str::Achievements : Str::Play)});
    else if (isF2P && !g->playable) hints.push_back({kIconCross, tr(Str::ClaimFree)});
    hints.push_back({kIconOptions, tr(Str::GameSettings)});
    hints.push_back({kIconTriangle, tr(favorites_.count(g->productId) ? Str::RemoveFavorite : Str::AddFavorite)});
    hints.push_back({kIconSquare, tr(hidden_.count(g->productId) ? Str::Unhide : Str::Hide)});
    if (!isAcquired(*g)) hints.push_back({kIconR3, tr(wishlist_.count(g->productId) ? Str::RemoveWishlist : Str::AddWishlist)});
    if (!effectivePlayable && !g->xboxTitleId.empty()) hints.push_back({kIconL3, tr(Str::Achievements)});
    hints.push_back({kIconCircle, tr(Str::Back)});
    drawHints(c, hints);
    drawToast(c, nowMs);
}


void AppUi::drawLaunching(Canvas& c, uint64_t nowMs) {
    c.clear(kBg);
    if (auto hero = images_.get(launching_.heroUrl, kW, kH)) c.drawImage(*hero, 0, 0);
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 175));
    drawSpinner(c, kW / 2.0f, 420, 40, nowMs);
    drawCentered(c, fonts_.bold, launching_.name, 500, 48, kWhite);
    drawCentered(c, fonts_.semibold, tr(Str::GettingReady), 576, 30, kGray);
    drawCentered(c, fonts_.regular, status_, 630, 26, kDim);
    // Helpful tips rotating every ~7 seconds from when the launching screen opened.
    static constexpr Str kTips[] = {
        Str::TipTouchpadMenu,
        Str::TipKeyboardMouse,
        Str::TipFreeStore,
        Str::TipBugReportQr,
        Str::TipSupportKofi,
        Str::Tip120HzVideo,
        Str::TipAnalogDeadzone,
        Str::TipDualSenseMic,
        Str::TipTouchpadMouse,
        Str::TipGyroAiming,
        Str::TipFSRUpscaling,
        Str::TipCustomBitrate,
        Str::TipNativeTrophies,
        Str::TipPerGameTrophies,
        Str::TipSmartGlassWebUI,
        Str::TipClaimFreeStore,
        Str::TipButtonRemap,
        Str::TipInstantResume,
        Str::TipInGameMenu,
        Str::TipBackgroundSync,
    };
    constexpr size_t kTipCount = sizeof(kTips) / sizeof(kTips[0]);
    if (!launchingStartMs_) {
        launchingStartMs_ = nowMs;
        uint32_t titleHash = 2166136261u;
        for (char ch : launching_.name) {
            titleHash = (titleHash ^ static_cast<uint8_t>(ch)) * 16777619u;
        }
        launchingSeed_ = static_cast<size_t>(nowMs ^ titleHash);
    }
    uint64_t elapsedMs = nowMs >= launchingStartMs_ ? (nowMs - launchingStartMs_) : 0;
    size_t tipIndex = (launchingSeed_ + (elapsedMs / 7000)) % kTipCount;
    drawCentered(c, fonts_.regular, tr(kTips[tipIndex]), 900, 22, kDim);
    drawHints(c, {{kIconCircle, tr(Str::Cancel)}});
}

void AppUi::drawError(Canvas& c) {
    drawBackground(c);
    int panelW = errorUrl_.empty() ? 1040 : 1300;
    int maxTextW = errorUrl_.empty() ? (panelW - 112) : (panelW - 460);
    auto lines = fonts_.regular.wrap(error_, 26, maxTextW, 8);
    int h = 132 + static_cast<int>(lines.size()) * 38 + 56;
    if (!errorUrl_.empty() && h < 460) h = 460;
    Rect panel{(kW - panelW) / 2, (kH - h) / 2 - 40, panelW, h};
    c.fillRect(panel, kPanel, 24);
    fonts_.bold.draw(c, tr(Str::ErrorTitle), panel.x + 56, panel.y + 52, 44, kWhite);
    int y = panel.y + 132;
    for (const auto& line : lines) {
        fonts_.regular.draw(c, line, panel.x + 56, y, 26, kGray);
        y += 38;
    }
    if (!errorUrl_.empty()) {
        y += 16;
        for (const auto& line : fonts_.regular.wrap(tr(Str::ScanQrToResolve), 24, maxTextW, 4)) {
            fonts_.regular.draw(c, line, panel.x + 56, y, 24, rgba(220, 220, 255));
            y += 34;
        }
        drawQr(c, errorUrl_, panel.x + panel.w - 56, panel.y + 56, 320, qrcodegen_Ecc_LOW);
    }
    if (!errorGame_.titleId.empty() || !errorGame_.productId.empty()) {
        bool free = (isFreeGame(errorGame_) || errorGame_.freeInStore) && !errorGame_.playable;
        const char* acceptLabel = free ? tr(Str::ClaimFree) : tr(Str::TryAgain);
        drawHints(c, {{kIconCross, acceptLabel}, {kIconTriangle, tr(Str::ReportProblem)}, {kIconCircle, tr(Str::Back)}});
    }
    else if (regionRefused_)
        drawHints(c, {{kIconCross, tr(Str::TryAgain)}, {kIconSquare, tr(Str::TryOtherCountry)},
                      {kIconOptions, tr(Str::Settings)}, {kIconTriangle, tr(Str::ReportProblem)},
                      {kIconTouchpad, tr(Str::HoldSignOut)}});
    else if (!rows_.empty() || !owned_.empty())
        drawHints(c, {{kIconCross, tr(Str::TryAgain)}, {kIconSquare, tr(Str::OfflineMode)},
                      {kIconTriangle, tr(Str::ReportProblem)}, {kIconTouchpad, tr(Str::HoldSignOut)}});
    else
        drawHints(c, {{kIconCross, tr(Str::TryAgain)}, {kIconTriangle, tr(Str::ReportProblem)},
                      {kIconTouchpad, tr(Str::HoldSignOut)}});
}

void AppUi::drawReport(Canvas& c) {
    // A panel over the screen: the heading and what the QR code does on the
    // left, the code on the right (dense: about 1.8 KB, so large).
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 170));
    constexpr int kPanelW = 1500, kPanelH = 720;
    Rect panel{(kW - kPanelW) / 2, (kH - kPanelH) / 2 - 30, kPanelW, kPanelH};
    c.fillRect({panel.x + 8, panel.y + 12, panel.w, panel.h}, rgba(0, 0, 0, 120), 26);
    c.fillRect(panel, kPanel, 24);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 24);
    Rect qr = drawQr(c, reportUrl_, panel.x + panel.w - 48, panel.y + 48, 600, qrcodegen_Ecc_LOW);
    int textW = (qr.w ? qr.x : panel.x + panel.w) - panel.x - 112;
    int y = panel.y + 56;
    for (const auto& line : fonts_.bold.wrap(reportHeading_, 40, textW, 3)) {
        fonts_.bold.draw(c, line, panel.x + 56, y, 40, kWhite);
        y += 54;
    }
    y += 24;
    for (Str s : {Str::ReportScan, Str::ReportIncludes}) {
        for (const auto& line : fonts_.regular.wrap(tr(s), 26, textW, 4)) {
            fonts_.regular.draw(c, line, panel.x + 56, y, 26, kGray);
            y += 38;
        }
        y += 20;
    }
    if (!reportDetail_.empty()) {
        y += 8;
        for (const auto& line : fonts_.regular.wrap(reportDetail_, 22, textW, 3)) {
            fonts_.regular.draw(c, line, panel.x + 56, y, 22, kWhite);
            y += 32;
        }
    }
    c.fillRect({0, kH - 80, kW, 80}, kBg);
    if (caps_.openUrl)  // a desktop: Cross opens the form in the browser
        drawHints(c, {{kIconCross, tr(Str::OpenInBrowser)}, {kIconCircle, tr(Str::Back)}});
    else
        drawHints(c, {{kIconCircle, tr(Str::Back)}});
}

void AppUi::drawCountryPicker(Canvas& c) {
    // Over the refused sign-in: the warning, then the countries.
    constexpr int kPanelW = 1100, kItemH = 58;
    auto warning = fonts_.regular.wrap(tr(Str::LoginCountryWarning), 26, kPanelW - 112, 6);
    int n = 1 + static_cast<int>(std::size(kLoginCountries));
    int listY = 124 + static_cast<int>(warning.size()) * 36 + 24;
    Rect panel = drawModal(c, kPanelW, listY + n * kItemH + 40, tr(Str::TryOtherCountry));
    int y = panel.y + 124;
    for (const auto& line : warning) {
        fonts_.regular.draw(c, line, panel.x + 56, y, 26, kGray);
        y += 36;
    }
    int sel = settingSelected(8);
    for (int i = 0; i < n; ++i) {
        Rect item{panel.x + 48, panel.y + listY + i * kItemH, panel.w - 96, kItemH};
        bool on = i == countryIndex_;
        if (on) c.fillRect(item, kWhite, 12);
        std::string label = i ? loginCountryName(kLoginCountries[i - 1]) : loginCountryOffText();
        fonts_.semibold.draw(c, label, item.x + 24, fonts_.semibold.centeredY(item.y, item.h, 26), 26,
                             on ? kBg : (i == sel ? kWhite : kGray));
        if (i == sel) {  // a check mark on the current choice
            float cx = static_cast<float>(item.x + item.w - 36), cy = static_cast<float>(item.y + item.h / 2);
            c.line(cx - 10, cy, cx - 3, cy + 7, 3, on ? kBg : kGreen);
            c.line(cx - 3, cy + 7, cx + 10, cy - 7, 3, on ? kBg : kGreen);
        }
    }
}

void AppUi::drawQuitPrompt(Canvas& c) {
    // As the update pop-up: the question, then Close and Cancel as buttons.
    constexpr int kButtonW = 300, kButtonH = 72, kPanelW = 2 * kButtonW + 24 + 112, kPanelH = 250;
    Rect panel = drawModal(c, kPanelW, kPanelH, tr(Str::QuitTitle));
    const char* labels[2] = {tr(Str::QuitConfirm), tr(Str::Cancel)};
    for (int i = 0; i < 2; ++i) {
        Rect b{panel.x + 56 + i * (kButtonW + 24), panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        bool on = i == promptFocus_;
        c.fillRect(b, on ? kWhite : rgba(255, 255, 255, 30), 14);
        int tw = fonts_.semibold.measure(labels[i], 28);
        fonts_.semibold.draw(c, labels[i], b.x + (b.w - tw) / 2, fonts_.semibold.centeredY(b.y, b.h, 28), 28,
                             on ? kBg : kWhite);
    }
    c.fillRect({0, kH - 80, kW, 80}, kBg);  // its own hints over drawModal's
    drawHints(c, {{kIconCross, tr(Str::Select)}, {kIconCircle, tr(Str::Cancel)}});
}

void AppUi::drawUpdatePrompt(Canvas& c) {
    c.fillRect({0, 0, kW, kH}, rgba(0, 0, 0, 160));
    constexpr int kPanelW = 960, kPanelH = 340, kButtonW = 260, kButtonH = 72;
    Rect panel{(kW - kPanelW) / 2, (kH - kPanelH) / 2 - 40, kPanelW, kPanelH};
    c.fillRect({panel.x + 8, panel.y + 12, panel.w, panel.h}, rgba(0, 0, 0, 120), 26);  // shadow
    c.fillRect(panel, kPanel, 24);
    c.strokeRect(panel, rgba(255, 255, 255, 50), 2, 24);
    fonts_.bold.draw(c, trf(Str::UpdateAvailable, updateAvailable_), panel.x + 56, panel.y + 48, 44, kWhite);
    auto lines = fonts_.regular.wrap(tr(Str::UpdatePrompt), 26, kPanelW - 112, 2);
    for (size_t i = 0; i < lines.size(); ++i)
        fonts_.regular.draw(c, lines[i], panel.x + 56, panel.y + 124 + static_cast<int>(i) * 36, 26, kGray);

    // 3 buttons: Update now (0), What's new (1), Not now (2)
    const char* labels[3] = {tr(Str::UpdateNow), tr(Str::WhatsNew), tr(Str::NotNow)};
    constexpr int kGap = (kPanelW - 112 - 3 * kButtonW) / 2;
    for (int i = 0; i < 3; ++i) {
        Rect b{panel.x + 56 + i * (kButtonW + kGap), panel.y + panel.h - kButtonH - 44, kButtonW, kButtonH};
        bool on = i == promptFocus_;
        c.fillRect(b, on ? kWhite : rgba(255, 255, 255, 30), 14);
        int tw = fonts_.semibold.measure(labels[i], 24);
        fonts_.semibold.draw(c, labels[i], b.x + (b.w - tw) / 2, fonts_.semibold.centeredY(b.y, b.h, 24), 24,
                             on ? kBg : kWhite);
    }
    c.fillRect({0, kH - 80, kW, 80}, kBg);  // the home screen's hints, under the pop-up's
    drawPads(c);
    drawHints(c, {
        {kIconCross, tr(Str::Select)},
        {kIconSquare, tr(Str::WhatsNew)},
        {kIconCircle, tr(Str::NotNow)}
    });
}

void AppUi::drawUpdateManual(Canvas& c) {
    // A build that can't install itself (the PS4's): a window with what is new
    // and a QR code to the release's page; the player downloads it on a phone and
    // installs it by hand. Nothing is deleted or replaced by the app.
    std::string currentLang = languageCode(language());
    if (updateNotesLines_.empty() || updateNotesLang_ != currentLang) formatUpdateNotes();

    constexpr int kW2 = 1440, kH2 = 780;
    Rect panel = drawModal(c, kW2, kH2, trf(Str::UpdateAvailable, updateAvailable_));

    // "New version X - you have Y"
    {
        std::string line = tr(Str::UpdateVersionLine);
        auto fill = [&](const std::string& value) {
            size_t at = line.find("%s");
            if (at != std::string::npos) line.replace(at, 2, value);
        };
        fill(updateAvailable_);
        fill(updateCurrent_);
        fonts_.semibold.draw(c, line, panel.x + 64, panel.y + 104, 24, rgba(120, 220, 120));
    }

    // Left: the notes, scrolled with up / down.
    constexpr int kLineH = 32;
    const int textX = panel.x + 64, textY = panel.y + 166, textH = 440, textW = 780;
    c.fillRect({panel.x + 44, textY - 14, textW + 40, textH + 28}, rgba(18, 18, 18, 220), 12);
    c.strokeRect({panel.x + 44, textY - 14, textW + 40, textH + 28}, rgba(255, 255, 255, 25), 1, 12);
    const int visibleLines = textH / kLineH;
    const int totalLines = static_cast<int>(updateNotesLines_.size());
    updateNotesScroll_ = std::clamp(updateNotesScroll_, 0, updateNotesMaxScroll_);
    int curY = textY;
    for (int i = updateNotesScroll_; i < std::min(totalLines, updateNotesScroll_ + visibleLines); ++i) {
        const auto& fl = updateNotesLines_[static_cast<size_t>(i)];
        if (!fl.text.empty()) {
            if (fl.isHeader) fonts_.bold.draw(c, fl.text, textX, curY, 26, kWhite);
            else if (fl.isSubHeader) fonts_.semibold.draw(c, fl.text, textX, curY, 22, rgba(120, 220, 120));
            else if (fl.isBullet) fonts_.regular.draw(c, fl.text, textX + 6, curY, 22, rgba(235, 235, 235));
            else if (fl.isBulletContinuation) fonts_.regular.draw(c, fl.text, textX + 28, curY, 22, rgba(235, 235, 235));
            else fonts_.regular.draw(c, fl.text, textX, curY, 22, kGray);
        }
        curY += kLineH;
    }
    if (totalLines > visibleLines) {
        const int trackX = panel.x + 44 + textW + 40 - 14, trackY = textY - 6, trackH = textH + 12;
        c.fillRect({trackX, trackY, 6, trackH}, rgba(255, 255, 255, 30), 3);
        const float ratio = static_cast<float>(visibleLines) / static_cast<float>(totalLines);
        const int thumbH = std::max(32, static_cast<int>(static_cast<float>(trackH) * ratio));
        const float pct = updateNotesMaxScroll_ > 0 ? static_cast<float>(updateNotesScroll_) / static_cast<float>(updateNotesMaxScroll_) : 0.0f;
        c.fillRect({trackX, trackY + static_cast<int>(static_cast<float>(trackH - thumbH) * pct), 6, thumbH}, kWhite, 3);
    }

    // Right: the QR code of the release's page, what to do with it, and the safety note.
    const std::string url = std::string(kReleasesTagPage) + updateAvailable_;
    const int colX = panel.x + 44 + textW + 40 + 40;
    const int colW = panel.x + panel.w - 44 - colX;
    Rect qr = drawQr(c, url, colX + colW / 2 + 160, panel.y + 150, 320, qrcodegen_Ecc_MEDIUM);
    int ty = (qr.w > 0 ? qr.y + qr.h : panel.y + 150) + 22;
    for (const auto& l : fonts_.semibold.wrap(tr(Str::UpdateScan), 22, colW, 3)) {
        fonts_.semibold.draw(c, l, colX, ty, 22, kWhite);
        ty += 31;
    }
    ty += 8;
    for (const auto& l : fonts_.regular.wrap(tr(Str::UpdateManualNote), 20, colW, 4)) {
        fonts_.regular.draw(c, l, colX, ty, 20, kGray);
        ty += 28;
    }

    c.fillRect({0, kH - 80, kW, 80}, kBg);  // the home screen's hints, under the window's
    drawPads(c);
    drawHints(c, {{kIconCircle, tr(Str::Back)}});
}

void AppUi::formatUpdateNotes() {
    updateNotesLines_.clear();
    std::string lang = languageCode(language());
    updateNotesLang_ = lang;
    std::string raw = xc::app::filterReleaseNotes(updateNotes_, lang);
    if (raw.empty()) raw = updateNotes_;
    if (raw.empty()) raw = "No release notes provided.";

    constexpr int kModalW = 1200, kModalH = 740;
    // The manual window has the notes in a narrower column, beside the QR code.
    int textW = updateManual_ ? 740 : kModalW - 128 - 28;
    int textH = updateManual_ ? 440 : kModalH - 175;

    size_t pos = 0;
    while (pos < raw.size()) {
        size_t next = raw.find('\n', pos);
        std::string rawLine = raw.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (!rawLine.empty() && rawLine.back() == '\r') rawLine.pop_back();

        if (rawLine.rfind("### ", 0) == 0) {
            std::string sub = rawLine.substr(4);
            auto wrapped = fonts_.semibold.wrap(sub, 22, textW, 4);
            for (const auto& w : wrapped) updateNotesLines_.push_back({w, false, true, false, false});
        } else if (rawLine.rfind("## ", 0) == 0) {
            std::string hdr = rawLine.substr(3);
            auto wrapped = fonts_.bold.wrap(hdr, 26, textW, 4);
            for (const auto& w : wrapped) updateNotesLines_.push_back({w, true, false, false, false});
        } else if (rawLine.rfind("# ", 0) == 0) {
            std::string hdr = rawLine.substr(2);
            auto wrapped = fonts_.bold.wrap(hdr, 28, textW, 4);
            for (const auto& w : wrapped) updateNotesLines_.push_back({w, true, false, false, false});
        } else if (rawLine.rfind("- ", 0) == 0 || rawLine.rfind("* ", 0) == 0) {
            std::string bullet = "•  " + rawLine.substr(2);
            auto wrapped = fonts_.regular.wrap(bullet, 22, textW, 6);
            for (size_t wi = 0; wi < wrapped.size(); ++wi) {
                updateNotesLines_.push_back({wrapped[wi], false, false, wi == 0, wi > 0});
            }
        } else if (!rawLine.empty()) {
            auto wrapped = fonts_.regular.wrap(rawLine, 22, textW, 8);
            for (const auto& w : wrapped) updateNotesLines_.push_back({w, false, false, false, false});
        } else {
            updateNotesLines_.push_back({"", false, false, false, false});
        }

        if (next == std::string::npos) break;
        pos = next + 1;
    }

    constexpr int kLineH = 32;
    int visibleLines = textH / kLineH;
    int totalLines = static_cast<int>(updateNotesLines_.size());
    updateNotesMaxScroll_ = std::max(0, totalLines - visibleLines);
    updateNotesScroll_ = std::clamp(updateNotesScroll_, 0, updateNotesMaxScroll_);
}

void AppUi::drawUpdateNotesModal(Canvas& c) {
    std::string currentLang = languageCode(language());
    if (updateNotesLines_.empty() || updateNotesLang_ != currentLang) {
        formatUpdateNotes();
    }

    std::string title = tr(Str::WhatsNewTitle);
    if (!updateAvailable_.empty()) {
        title += " - " + updateAvailable_;
    }
    constexpr int kModalW = 1200, kModalH = 740;
    Rect panel = drawModal(c, kModalW, kModalH, title);

    int textX = panel.x + 64;
    int textY = panel.y + 115;
    int textH = panel.h - 175;

    constexpr int kLineH = 32;
    int visibleLines = textH / kLineH;
    int totalLines = static_cast<int>(updateNotesLines_.size());
    updateNotesScroll_ = std::clamp(updateNotesScroll_, 0, updateNotesMaxScroll_);

    c.fillRect({panel.x + 44, textY - 12, panel.w - 88, textH + 24}, rgba(18, 18, 18, 220), 12);
    c.strokeRect({panel.x + 44, textY - 12, panel.w - 88, textH + 24}, rgba(255, 255, 255, 25), 1, 12);

    int curY = textY;
    int endIdx = std::min(totalLines, updateNotesScroll_ + visibleLines);
    for (int i = updateNotesScroll_; i < endIdx; ++i) {
        const auto& fl = updateNotesLines_[static_cast<size_t>(i)];
        if (!fl.text.empty()) {
            if (fl.isHeader) {
                fonts_.bold.draw(c, fl.text, textX, curY, 26, kWhite);
            } else if (fl.isSubHeader) {
                fonts_.semibold.draw(c, fl.text, textX, curY, 22, rgba(120, 220, 120));
            } else if (fl.isBullet) {
                fonts_.regular.draw(c, fl.text, textX + 6, curY, 22, rgba(235, 235, 235));
            } else if (fl.isBulletContinuation) {
                fonts_.regular.draw(c, fl.text, textX + 28, curY, 22, rgba(235, 235, 235));
            } else {
                fonts_.regular.draw(c, fl.text, textX, curY, 22, kGray);
            }
        }
        curY += kLineH;
    }

    if (totalLines > visibleLines) {
        int barTrackX = panel.x + panel.w - 60;
        int barTrackY = textY - 4;
        int barTrackH = textH + 8;
        c.fillRect({barTrackX, barTrackY, 6, barTrackH}, rgba(255, 255, 255, 30), 3);
        float ratio = static_cast<float>(visibleLines) / totalLines;
        int thumbH = std::max(32, static_cast<int>(barTrackH * ratio));
        float scrollPct = updateNotesMaxScroll_ > 0 ? static_cast<float>(updateNotesScroll_) / updateNotesMaxScroll_ : 0.0f;
        int thumbY = barTrackY + static_cast<int>((barTrackH - thumbH) * scrollPct);
        c.fillRect({barTrackX, thumbY, 6, thumbH}, kWhite, 3);
    }

    c.fillRect({0, kH - 80, kW, 80}, kBg);
    drawPads(c);
    drawHints(c, {
        {kIconCircle, tr(Str::Back)}
    });
}

void AppUi::drawAchievementsModal(Canvas& c, uint64_t nowMs) {
    Rect panel = drawModal(c, 1180, 720, tr(Str::Achievements));

    auto it = achievements_.find(detail_.xboxTitleId);
    if (it == achievements_.end()) {
        drawSpinner(c, static_cast<float>(panel.x + panel.w / 2), static_cast<float>(panel.y + panel.h / 2), 24.0f, nowMs);
        animating_ = true;
        c.fillRect({0, kH - 80, kW, 80}, kBg);
        drawHints(c, {{kIconCircle, tr(Str::Back)}});
        return;
    }

    const auto& ach = it->second;
    if (ach.totalCount == 0) {
        std::string emptyStr = tr(Str::NoAchievements);
        int tw = fonts_.regular.measure(emptyStr, 28);
        fonts_.regular.draw(c, emptyStr, panel.x + (panel.w - tw) / 2, panel.y + panel.h / 2, 28, kDim);
        c.fillRect({0, kH - 80, kW, 80}, kBg);
        drawHints(c, {{kIconCircle, tr(Str::Back)}});
        return;
    }

    std::string summary = std::to_string(ach.unlockedCount) + " / " + std::to_string(ach.totalCount) +
                          " (" + std::to_string(ach.unlockedGamerscore) + " / " + std::to_string(ach.totalGamerscore) + " G)";
    int sw = fonts_.semibold.measure(summary, 24);
    fonts_.semibold.draw(c, summary, panel.x + panel.w - 56 - sw, panel.y + 56, 24, kWhite);

    int barX = panel.x + 56;
    int barY = panel.y + 104;
    int barW = panel.w - 112;
    int barH = 10;
    c.fillRect({barX, barY, barW, barH}, rgba(255, 255, 255, 30), 5);
    if (ach.totalCount > 0 && ach.unlockedCount > 0) {
        int fillW = std::clamp(static_cast<int>(barW * static_cast<float>(ach.unlockedCount) / ach.totalCount), 10, barW);
        c.fillRect({barX, barY, fillW, barH}, kGreen, 5);
    }

    constexpr int kRowH = 80;
    constexpr int kGap = 8;
    constexpr int kVisibleCount = 5;
    int y = barY + 22;

    int total = static_cast<int>(ach.items.size());
    int startIdx = std::clamp(achScroll_, 0, std::max(0, total - kVisibleCount));

    for (int i = startIdx; i < total && i < startIdx + kVisibleCount; ++i) {
        const auto& item = ach.items[i];
        Rect r{panel.x + 56, y, panel.w - 112, kRowH};
        c.fillRect(r, rgba(255, 255, 255, 14), 14);

        float cx = static_cast<float>(r.x + 36), cy = static_cast<float>(r.y + r.h / 2);
        if (item.unlocked) {
            c.fillCircle(cx, cy, 18.0f, kGreen);
            c.line(cx - 7, cy, cx - 2, cy + 6, 3.0f, kWhite);
            c.line(cx - 2, cy + 6, cx + 9, cy - 6, 3.0f, kWhite);
        } else {
            c.fillCircle(cx, cy, 18.0f, rgba(255, 255, 255, 20));
            c.strokeCircle(cx, cy - 3.0f, 4.0f, 2.0f, kDim);
            c.fillRect({static_cast<int>(cx) - 6, static_cast<int>(cy) - 2, 12, 10}, kDim, 2);
        }

        int textX = r.x + 72;
        int maxTextW = r.w - 200;
        if (item.unlocked) {
            fonts_.semibold.draw(c, item.name, textX, r.y + 14, 24, kWhite);
            std::string desc = fonts_.regular.fit(item.description, 20, maxTextW);
            fonts_.regular.draw(c, desc, textX, r.y + 44, 20, kGray);
        } else if (item.isSecret) {
            fonts_.semibold.draw(c, tr(Str::SecretAchievement), textX, r.y + 14, 24, kDim);
            fonts_.regular.draw(c, tr(Str::AchievementLocked), textX, r.y + 44, 20, rgba(255, 255, 255, 90));
        } else {
            fonts_.semibold.draw(c, item.name, textX, r.y + 14, 24, rgba(230, 230, 230));
            std::string desc = item.lockedDescription.empty() ? item.description : item.lockedDescription;
            desc = fonts_.regular.fit(desc, 20, maxTextW);
            fonts_.regular.draw(c, desc, textX, r.y + 44, 20, kDim);
        }

        std::string score = std::to_string(item.gamerscore) + " G";
        int scw = fonts_.semibold.measure(score, 22);
        Color scCol = item.unlocked ? rgba(110, 225, 120) : kDim;
        fonts_.semibold.draw(c, score, r.x + r.w - 28 - scw, fonts_.semibold.centeredY(r.y, r.h, 22), 22, scCol);

        y += kRowH + kGap;
    }

    if (total > kVisibleCount) {
        int trackH = kVisibleCount * (kRowH + kGap) - kGap;
        int trackY = barY + 22;
        int trackX = panel.x + panel.w - 40;
        c.fillRect({trackX, trackY, 4, trackH}, rgba(255, 255, 255, 20), 2);
        int thumbH = std::max(20, trackH * kVisibleCount / total);
        int thumbY = trackY + (trackH - thumbH) * startIdx / (total - kVisibleCount);
        c.fillRect({trackX, thumbY, 4, thumbH}, rgba(255, 255, 255, 120), 2);
    }

    c.fillRect({0, kH - 80, kW, 80}, kBg);
    drawHints(c, {{kIconCircle, tr(Str::Back)}});
}

void AppUi::render(Canvas& c, uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    float dt = lastRender_ ? std::min(0.1f, (nowMs - lastRender_) / 1000.0f) : 0.0f;
    lastRender_ = nowMs;
    animating_ = rowY_.step(dt);
    for (auto& a : rowScroll_) animating_ |= a.step(dt);
    animating_ |= gridScroll_.step(dt);
    animating_ |= resultScroll_.step(dt);
    dirty_ = false;
    switch (screen_) {
        case Screen::Splash: drawSplash(c, nowMs); break;
        case Screen::SignIn: drawSignIn(c, nowMs); break;
        case Screen::Home: drawHome(c, nowMs); break;
        case Screen::Details:
            drawDetails(c, nowMs);
            if (gameSettingsOpen_) drawGameSettings(c);
            if (achievementsOpen_) drawAchievementsModal(c, nowMs);
            break;
        case Screen::Launching: drawLaunching(c, nowMs); break;
        case Screen::Error: drawError(c); break;
        case Screen::Settings: drawSettings(c); break;
        case Screen::Updating: drawUpdating(c, nowMs); break;
        case Screen::Streaming: break;
    }
    if ((screen_ == Screen::Home || (screen_ == Screen::Splash && startupAccountChooser_)) && accountManagerOpen_ &&
        !updatePrompt_ && !quitPrompt_) {
        if (removeAccountConfirmOpen_) drawRemoveAccountConfirmModal(c);
        else drawAccountManagerModal(c);
    }
    if (screen_ == Screen::Home && updatePrompt_) {
        if (showingUpdateNotes_) drawUpdateNotesModal(c);
        else if (updateManual_) drawUpdateManual(c);
        else drawUpdatePrompt(c);
    }
    if (screen_ == Screen::Home && quitPrompt_) drawQuitPrompt(c);
    if (screen_ == Screen::Error && countryPickerOpen_) drawCountryPicker(c);
    if (reportOpen_) drawReport(c);
    if ((screen_ == Screen::Home && !updatePrompt_) || screen_ == Screen::Error) drawSignOutHold(c, nowMs);
    if (!toast_.empty()) animating_ = true;  // to expire it
    if (platform::toastsEnabled()) {
        // platform::notify() on a desktop: over everything, under the top bar's right side.
        toastsDrawn_ = platform::toasts().version(nowMs);
        drawToasts(c, fonts_, platform::toasts().visible(nowMs), kW - kMargin, 132);
    }
    if (mouseCursorVisible_ && nowMs >= mouseCursorVisibleUntil_) {
        mouseCursorVisible_ = false;
    }
    if (mouseCursorVisible_ && screen_ != Screen::Streaming && screen_ != Screen::Splash) {
        drawMouseCursor(c, mouseCursorX_, mouseCursorY_);
    }
}

static const char* const kCursorRows[23] = {
    "#...............",
    "##..............",
    "#W#.............",
    "#WW#............",
    "#WWW#...........",
    "#WWWW#..........",
    "#WWWWW#.........",
    "#WWWWWW#........",
    "#WWWWWWW#.......",
    "#WWWWWWWW#......",
    "#WWWWWWWWW#.....",
    "#WWWWWWWWWW#....",
    "#WWWWWWWWWWW#...",
    "#WWWWWW######...",
    "#WWWW##WW#......",
    "#WW##.#WW#......",
    "#W#...#WW#......",
    "##.....#WW#.....",
    "#......#WW#.....",
    "........#WW#....",
    "........#WW#....",
    ".........#WW#...",
    ".........####..."
};

void AppUi::drawMouseCursor(Canvas& c, float cursorX, float cursorY) {
    static bool s_inited = false;
    static uint8_t s_outline[23 * 16];
    static uint8_t s_fill[23 * 16];
    static uint8_t s_shadow[23 * 16];
    if (!s_inited) {
        s_inited = true;
        for (int y = 0; y < 23; ++y) {
            for (int x = 0; x < 16; ++x) {
                char ch = kCursorRows[y][x];
                s_shadow[y * 16 + x] = (ch == '#' || ch == 'W') ? 255 : 0;
                s_outline[y * 16 + x] = (ch == '#') ? 255 : 0;
                s_fill[y * 16 + x] = (ch == 'W') ? 255 : 0;
            }
        }
    }
    int ix = static_cast<int>(std::round(cursorX));
    int iy = static_cast<int>(std::round(cursorY));
    // Sombra sutil
    c.drawMask(s_shadow, 16, 23, 16, ix + 2, iy + 2, rgba(0, 0, 0, 80));
    // Borda preta fina
    c.drawMask(s_outline, 16, 23, 16, ix, iy, rgba(0, 0, 0, 240));
    // Seta branca nítida
    c.drawMask(s_fill, 16, 23, 16, ix, iy, rgba(255, 255, 255, 255));
}


}  // namespace xc::ui
