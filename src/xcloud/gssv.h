// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 RafaelNGP
// Client for the xCloud game-streaming service ("gssv"): login, title list,
// and the session lifecycle (provision -> connect -> SDP/ICE -> keepalive).
#pragma once

#include "auth/xbox_live.h"

#include <string>
#include <vector>

namespace xc::xcloud {

struct Region {
    std::string name;
    std::string baseUri;  // https://<region>.core.gssv-play-prod.xboxlive.com
    bool isDefault = false;
};

struct GssvLogin {
    std::string gsToken;
    std::string market;
    int64_t expiresAt = 0;  // unix seconds
    std::vector<Region> regions;

    const Region* defaultRegion() const;
};

struct Title {
    std::string titleId;    // gssv id used to start a session
    std::string productId;  // Microsoft Store "big id", used for names/art
    std::string name;       // filled in by hydrateTitles()
    std::string imageUrl;   // box art / tile
    bool hasEntitlement = false;
    std::string xboxTitleId;  // Xbox Live title id (titlehub)
    bool isFreeInStore = false;
    std::vector<std::string> alternateProductIds;
    bool viaSubscription = false;
    bool mouseKeyboard = false;
    std::vector<std::string> allowedDeviceTypes;
    bool freeTier = false;
    int maxSessionSeconds = 0;
};

struct Console {
    std::string serverId;
    std::string deviceName;
    std::string consoleType;
    std::string powerState;
    std::string playPath;
    bool outOfHomeWarning = false, wirelessWarning = false;
};

enum class SessionState { Unknown, Provisioning, WaitingForResources, ReadyToConnect, Provisioned, Failed };

struct SessionStatus {
    SessionState state = SessionState::Unknown;
    std::string raw;
    std::string errorCode;
    std::string errorMessage;
};

struct IceCandidate {
    std::string candidate;
    std::string sdpMid;
    int sdpMLineIndex = 0;
};

enum class Resolution { P1080, P720, P1440, P1080HQ };

const char* loginCountryAddress(const std::string& country);
std::string errorFields(const std::string& body);

class GssvClient {
public:
    explicit GssvClient(std::string offering = "xgpuweb") : offering_(std::move(offering)) {}

    bool login(const auth::XblToken& gssvXsts, std::string& err);
    void setLoginCountry(const std::string& country) { loginCountry_ = country, loginStatus_ = 0; }
    int loginStatus() const { return loginStatus_; }
    bool loginRefused() const { return loginStatus_ >= 400 && loginStatus_ < 500; }
    const GssvLogin& session() const { return login_; }
    void setRegion(const Region& r) { region_ = r; }
    void setResolution(Resolution r) { resolution_ = r; }
    void setPreviewFeatures(bool preview) { previewFeatures_ = preview; }
    bool previewFeatures() const { return previewFeatures_; }
    Resolution resolution() const { return resolution_; }
    const Region& region() const { return region_; }
    bool isHome() const { return offering_ == "xhome"; }

    bool listTitles(std::vector<Title>& out, std::string& err, bool recentOnly = false);
    bool hydrateTitles(std::vector<Title>& titles, const std::string& market, const std::string& lang,
                       std::string& err);

    bool listConsoles(std::vector<Console>& out, std::string& err);

    bool startSession(const std::string& titleId, const std::string& locale, std::string& err, int* status = nullptr);
    bool hasSession() const { return !sessionPath_.empty(); }
    bool sessionState(SessionStatus& out, std::string& err);
    int titleWaitSeconds(const std::string& titleId);
    bool connect(const std::string& msaTransferToken, std::string& err);
    bool sendSdpOffer(const std::string& sdp, std::string& err);
    bool pollSdpAnswer(std::string& answer, std::string& err);
    bool sendIceCandidates(const std::vector<IceCandidate>& candidates, std::string& err);
    bool pollIceCandidates(std::vector<IceCandidate>& out, std::string& err);
    bool keepalive(std::string& err);
    void stopSession();

private:
    std::string url(const std::string& path) const;
    std::string sessionUrl(const std::string& suffix) const;

    std::string offering_;
    std::string loginCountry_;
    int loginStatus_ = 0;
    Resolution resolution_ = Resolution::P1080;
    GssvLogin login_;
    Region region_;
    std::string sessionPath_;
    bool previewFeatures_ = true;
};

const char* toString(SessionState s);

}  // namespace xc::xcloud
