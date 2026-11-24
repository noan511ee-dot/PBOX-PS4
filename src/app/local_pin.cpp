// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/local_pin.h"

#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "platform/platform.h"
#include "util/json.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace xc::app::localpin {
namespace {
constexpr char kHex[] = "0123456789abcdef";
std::string encodeHex(const uint8_t* bytes, size_t size) {
    std::string result;
    result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        result.push_back(kHex[bytes[i] >> 4]);
        result.push_back(kHex[bytes[i] & 15]);
    }
    return result;
}
int fromHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool decodeHex(const std::string& text, uint8_t* out, size_t size) {
    if (text.size() != size * 2) return false;
    for (size_t i = 0; i < size; ++i) {
        int high = fromHex(text[i * 2]), low = fromHex(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}
bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t size) {
    volatile uint8_t difference = 0;
    for (size_t i = 0; i < size; ++i) difference = static_cast<uint8_t>(difference | (a[i] ^ b[i]));
    return difference == 0;
}
bool readVerifier(const std::string& path, std::array<uint8_t, kSaltBytes>& salt,
                  std::array<uint8_t, kVerifierBytes>& verifier) {
    std::string text;
    if (!platform::readFile(path, text)) return false;
    auto root = json::parse(text);
    if (!root || !root->isObject() || (*root)["version"].asInt() != 1) return false;
    return decodeHex((*root)["salt"].str(), salt.data(), salt.size()) &&
           decodeHex((*root)["verifier"].str(), verifier.data(), verifier.size());
}
}

bool validPin(const std::string& pin) {
    if (pin.size() < kMinDigits || pin.size() > kMaxDigits) return false;
    for (unsigned char c : pin) if (c < '0' || c > '9') return false;
    return true;
}

bool derive(const std::string& pin, const uint8_t* salt, size_t saltSize, uint8_t* out, size_t outSize) {
    if (!validPin(pin) || !salt || saltSize < 8 || !out || outSize == 0 || outSize > UINT32_MAX) return false;
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
        reinterpret_cast<const unsigned char*>(pin.data()), pin.size(), salt, saltSize,
        kIterations, static_cast<uint32_t>(outSize), out) == 0;
}

bool enabled(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

bool set(const std::string& path, const std::string& pin) {
    if (!validPin(pin)) return false;
    std::array<uint8_t, kSaltBytes> salt{};
    std::array<uint8_t, kVerifierBytes> verifier{};
    if (!platform::randomBytes(salt.data(), salt.size()) || !derive(pin, salt.data(), salt.size(), verifier.data(), verifier.size())) {
        std::memset(salt.data(), 0, salt.size());
        std::memset(verifier.data(), 0, verifier.size());
        return false;
    }
    json::Value root = json::Value::object();
    root.set("version", 1);
    root.set("salt", encodeHex(salt.data(), salt.size()));
    root.set("verifier", encodeHex(verifier.data(), verifier.size()));
    const bool ok = platform::writeFileAtomic(path, root.dump());
    if (ok) platform::makePrivate(path);
    std::memset(salt.data(), 0, salt.size());
    std::memset(verifier.data(), 0, verifier.size());
    return ok;
}

bool verify(const std::string& path, const std::string& pin) {
    if (!validPin(pin)) return false;
    std::array<uint8_t, kSaltBytes> salt{};
    std::array<uint8_t, kVerifierBytes> expected{};
    std::array<uint8_t, kVerifierBytes> actual{};
    bool ok = readVerifier(path, salt, expected) && derive(pin, salt.data(), salt.size(), actual.data(), actual.size());
    if (ok) ok = constantTimeEqual(expected.data(), actual.data(), actual.size());
    std::memset(salt.data(), 0, salt.size());
    std::memset(expected.data(), 0, expected.size());
    std::memset(actual.data(), 0, actual.size());
    return ok;
}

bool clear(const std::string& path) {
    if (std::remove(path.c_str()) == 0) return true;
    return !enabled(path);
}

}  // namespace xc::app::localpin
