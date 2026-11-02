// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace vrgbdb {
inline Json load(const std::filesystem::path &file, size_t limit = 16 * 1024 * 1024) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Cannot open JSON file: " + file.string());
    const auto size = stream.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > limit) throw std::runtime_error("JSON exceeds size limit");
    std::string content(static_cast<size_t>(size), '\0');
    stream.seekg(0);
    if (!content.empty() && !stream.read(content.data(), static_cast<std::streamsize>(content.size()))) throw std::runtime_error("Cannot read JSON file");
    return Json::parse(content);
}
inline std::string lower(std::string text) {
    for (auto &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

inline void attachProviders(Json &catalog, const Json &providers) {
    for (const auto &mapping : providers.at("mappings")) {
        if (mapping.value("provider", "") != "openrgb-driver-core" || mapping.value("status", "") != "native-experimental" || mapping.value("hardwareVerified", true)) continue;
        for (auto &record : catalog.at("records")) {
            if (record.at("id") != mapping.at("recordId") || record.at("name") != mapping.at("detectorName") || record.at("transport") != mapping.at("transport")) continue;
            record["providers"] = Json::array({mapping});
            record["status"] = "native-experimental";
            record["hardwareVerified"] = false;
            if (!record.contains("nativeAdapter") || record["nativeAdapter"].is_null()) record["nativeAdapter"] = "openrgb-driver-core";
            break;
        }
    }
}
inline Json loadCatalog(const std::filesystem::path &directory) {
    auto catalog = load(directory / "catalog.json");
    const auto providers = directory / "providers.json";
    if (std::filesystem::exists(providers)) attachProviders(catalog, load(providers));
    return catalog;
}

// Identification candidates only. Additional detector/firmware checks are mandatory before writes.
inline Json candidates(const Json &catalog, const Json &device) {
    Json result = Json::array();
    for (const auto &record : catalog.at("records")) {
        if (record.at("transport") != device.at("transport")) continue;
        const auto &match = record.at("matcher");
        bool conflict = false, hasIdentity = false;
        Json missing = Json::array();
        for (const char *key : {"vid", "pid", "interface", "usagePage", "usage", "bus", "vendor", "device", "subvendor", "subdevice", "address", "jedec", "dramType"}) {
            if (!match.contains(key)) continue;
            if (!match[key].is_number_integer()) throw std::runtime_error("Invalid catalog matcher");
            if (match[key].get<int64_t>() < 0) continue; // Upstream wildcard.
            if (!device.contains(key)) { missing.push_back(key); continue; }
            hasIdentity = true;
            if (device[key] != match[key]) { conflict = true; break; }
        }
        if (conflict || !hasIdentity) continue;
        Json candidate = record;
        candidate["missingConstraints"] = missing;
        candidate["signatureMatched"] = record.at("signatureComplete").get<bool>() && missing.empty();
        candidate["identificationOnly"] = true;
        result.push_back(std::move(candidate));
    }
    return result;
}

inline Json find(const Json &catalog, const std::string &query) {
    const auto q = lower(query);
    if (q.empty()) throw std::runtime_error("Search text is empty");
    Json result = Json::array();
    for (const auto &record : catalog.at("records"))
        if (lower(record.at("name").get<std::string>()).find(q) != std::string::npos ||
            lower(record.at("family").get<std::string>()).find(q) != std::string::npos || record.at("id") == query)
            result.push_back(record);
    return result;
}
}
