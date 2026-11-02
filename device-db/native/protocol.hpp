// SPDX-License-Identifier: GPL-2.0-or-later
// Wire formats documented by the pinned OpenRGB source; see ../PROVENANCE.md.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"

namespace vrgbdb {
using Json = nlohmann::json;
using Color = std::array<uint8_t, 3>;
using Packet = std::array<uint8_t, 65>;

inline int integer(const Json &object, const char *key, int minimum, int maximum) {
    const auto &value = object.at(key);
    if (!value.is_number_integer()) throw std::runtime_error(std::string(key) + " must be an integer");
    const auto n = value.get<int64_t>();
    if (n < minimum || n > maximum) throw std::runtime_error(std::string(key) + " is outside the supported range");
    return static_cast<int>(n);
}

inline std::vector<Color> colors(const Json &request, size_t maximum) {
    const auto &values = request.at("colors");
    if (!values.is_array() || values.empty() || values.size() > maximum)
        throw std::runtime_error("colors must be a non-empty RGB array within the adapter limit");
    std::vector<Color> result;
    result.reserve(values.size());
    for (const auto &value : values) {
        if (!value.is_array() || value.size() != 3) throw std::runtime_error("Each color must contain exactly three bytes");
        Color color{};
        for (size_t i = 0; i < 3; ++i) {
            if (!value[i].is_number_integer()) throw std::runtime_error("Color components must be integers");
            const auto n = value[i].get<int64_t>();
            if (n < 0 || n > 255) throw std::runtime_error("Color components must be 0..255");
            color[i] = static_cast<uint8_t>(n);
        }
        result.push_back(color);
    }
    return result;
}

inline int corsairChannels(unsigned int pid) {
    switch (pid) {
    case 0x0c1a: case 0x0c1e: return 1;
    case 0x0c0b: case 0x0c10: case 0x1d00: case 0x1d04: case 0x0c23: return 2;
    default: return 0;
    }
}

inline Packet corsairCommit() {
    Packet p{}; p[1] = 0x33; p[2] = 0xff; return p;
}

inline std::vector<Packet> corsairFrame(unsigned int pid, unsigned int channel, const std::vector<Color> &rgb) {
    const auto channels = corsairChannels(pid);
    if (!channels || channel >= static_cast<unsigned int>(channels) || rgb.empty() || rgb.size() > 204)
        throw std::runtime_error("Unsupported Corsair device, channel, or LED count");
    std::vector<Packet> packets;
    Packet state{}; state[1] = 0x38; state[2] = static_cast<uint8_t>(channel); state[3] = 2;
    packets.push_back(state);
    for (size_t start = 0; start < rgb.size(); start += 50) {
        const auto count = std::min<size_t>(50, rgb.size() - start);
        for (size_t component = 0; component < 3; ++component) {
            Packet p{};
            p[1] = 0x32; p[2] = static_cast<uint8_t>(channel); p[3] = static_cast<uint8_t>(start);
            p[4] = static_cast<uint8_t>(count); p[5] = static_cast<uint8_t>(component);
            for (size_t i = 0; i < count; ++i) p[6 + i] = rgb[start + i][component];
            packets.push_back(p);
        }
    }
    packets.push_back(corsairCommit());
    return packets;
}

inline bool qmkRevision(int revision) { return revision == 9 || revision == 11 || revision == 12 || revision == 13; }

inline Packet qmkDirectMode() {
    Packet p{};
    p[1] = 7; p[3] = 255; p[4] = 255; p[5] = 1; p[6] = 127;
    // p[7] is the flash-save flag; it remains zero.
    return p;
}

inline std::vector<Packet> qmkFrame(int revision, const std::vector<Color> &rgb, const std::vector<uint8_t> &indices = {}) {
    if (!qmkRevision(revision) || rgb.empty() || rgb.size() > 255)
        throw std::runtime_error("Unsupported QMK OpenRGB revision or LED count; VIA alone is insufficient");
    const bool indexed = revision == 13;
    if (indexed && indices.size() != rgb.size()) throw std::runtime_error("Revision D requires the probed physical LED indices");
    if (indexed) {
        auto unique = indices;
        std::sort(unique.begin(), unique.end());
        if (std::adjacent_find(unique.begin(), unique.end()) != unique.end()) throw std::runtime_error("Duplicate QMK LED indices");
    }
    const size_t chunk = indexed ? 15 : 20;
    std::vector<Packet> packets;
    for (size_t start = 0; start < rgb.size(); start += chunk) {
        Packet p{}; p[1] = 9;
        const auto count = std::min(chunk, rgb.size() - start);
        if (indexed) {
            p[2] = static_cast<uint8_t>(count);
            for (size_t i = 0; i < count; ++i) {
                p[3 + i * 4] = indices[start + i];
                std::copy(rgb[start + i].begin(), rgb[start + i].end(), p.begin() + 4 + i * 4);
            }
        } else {
            p[2] = static_cast<uint8_t>(start); p[3] = static_cast<uint8_t>(count);
            for (size_t i = 0; i < count; ++i)
                std::copy(rgb[start + i].begin(), rgb[start + i].end(), p.begin() + 4 + i * 3);
        }
        packets.push_back(p);
    }
    return packets;
}

inline std::vector<Packet> plan(const Json &request) {
    const auto adapter = request.at("adapter").get<std::string>();
    if (adapter == "corsair-lighting-node") {
        const auto rgb = colors(request, 204);
        return corsairFrame(static_cast<unsigned int>(integer(request, "pid", 0, 65535)),
                            static_cast<unsigned int>(integer(request, "channel", 0, 1)), rgb);
    }
    if (adapter == "qmk-openrgb") {
        const auto rgb = colors(request, 255);
        std::vector<uint8_t> indices;
        if (request.contains("indices")) {
            if (!request["indices"].is_array()) throw std::runtime_error("indices must be an array");
            for (const auto &value : request["indices"]) {
                if (!value.is_number_integer() || value.get<int64_t>() < 0 || value.get<int64_t>() > 254)
                    throw std::runtime_error("Invalid QMK LED index");
                indices.push_back(value.get<uint8_t>());
            }
        }
        auto packets = qmkFrame(integer(request, "revision", 0, 255), rgb, indices);
        packets.insert(packets.begin(), qmkDirectMode());
        return packets;
    }
    if (adapter == "openrgb-driver-core") throw std::runtime_error("Use native-driver-core stream for this provider; see device-db/core/README.md");
    throw std::runtime_error("No executable native adapter for this record; protocol-source does not mean runtime support");
}
}
