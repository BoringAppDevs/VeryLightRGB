// SPDX-License-Identifier: GPL-2.0-or-later
#include "database.hpp"
#include <iostream>

using namespace vrgbdb;
static unsigned int checks = 0;
static void expect(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
template <class Function> static void rejects(Function action, const char *message) {
    ++checks;
    try { action(); } catch (const std::exception &) { return; }
    throw std::runtime_error(message);
}

int wmain() {
    try {
        // Packet fixtures transcribed from the pinned upstream wire formats, independent of import output.
        auto packets = corsairFrame(0x0c0b, 1, {{0x12, 0x34, 0x56}, {0x9a, 0xbc, 0xde}});
        expect(packets.size() == 5, "Corsair two-color transfer length");
        expect(packets[0][1] == 0x38 && packets[0][2] == 1 && packets[0][3] == 2, "Corsair software port state");
        expect(packets[1][1] == 0x32 && packets[1][3] == 0 && packets[1][4] == 2 && packets[1][5] == 0, "Corsair direct packet header");
        expect(packets[1][6] == 0x12 && packets[1][7] == 0x9a, "Corsair red planar order");
        expect(packets[2][5] == 1 && packets[2][6] == 0x34 && packets[2][7] == 0xbc, "Corsair green planar order");
        expect(packets[3][5] == 2 && packets[3][6] == 0x56 && packets[3][7] == 0xde, "Corsair blue planar order");
        expect(packets[4][1] == 0x33 && packets[4][2] == 0xff, "Corsair commit");
        packets = corsairFrame(0x0c0b, 0, std::vector<Color>(51, {1, 2, 3}));
        expect(packets.size() == 8 && packets[4][3] == 50 && packets[4][4] == 1, "Corsair 50-LED packet boundary");
        packets = corsairFrame(0x0c0b, 0, std::vector<Color>(204, {1, 2, 3}));
        expect(packets.size() == 17 && packets[13][3] == 200 && packets[13][4] == 4, "Corsair maximum LED length");
        rejects([] { corsairFrame(0x0c1a, 1, {{1, 2, 3}}); }, "Core has no channel 2");
        rejects([] { corsairFrame(0xffff, 0, {{1, 2, 3}}); }, "Unknown PID rejected");
        rejects([] { corsairFrame(0x0c0b, 0, std::vector<Color>(205)); }, "Oversize frame rejected");
        rejects([] { corsairFrame(0x0c0b, 0, {}); }, "Empty frame rejected");
        for (int revision : {9, 11, 12}) {
            packets = qmkFrame(revision, std::vector<Color>(21, {0x12, 0x34, 0x56}));
            expect(packets.size() == 2, "QMK 20-LED chunk boundary");
            expect(packets[0][1] == 9 && packets[0][2] == 0 && packets[0][3] == 20, "QMK sequential frame header");
            expect(packets[1][2] == 20 && packets[1][3] == 1 && packets[1][4] == 0x12, "QMK sequential tail");
        }
        packets = qmkFrame(13, {{0x12, 0x34, 0x56}, {0x9a, 0xbc, 0xde}}, {4, 8});
        expect(packets.size() == 1 && packets[0][2] == 2 && packets[0][3] == 4 && packets[0][7] == 8, "QMK D explicit physical indices");
        expect(packets[0][4] == 0x12 && packets[0][8] == 0x9a, "QMK D RGB ordering");
        rejects([] { qmkFrame(14, {{1, 2, 3}}); }, "Revision E mode mapping differs and must be rejected");
        rejects([] { qmkFrame(13, {{1, 2, 3}}); }, "D requires probed indices");
        rejects([] { qmkFrame(13, {{1, 2, 3}, {4, 5, 6}}, {2, 2}); }, "Duplicate physical indices rejected");
        expect(qmkDirectMode()[7] == 0, "QMK flash flag stays disabled");
        rejects([] { colors(Json{{"colors", {{-1, 0, 0}}}}, 204); }, "Negative colors rejected");
        rejects([] { colors(Json{{"colors", {{1.5, 0, 0}}}}, 204); }, "Fractional colors rejected");
        rejects([] { colors(Json{{"colors", {{256, 0, 0}}}}, 204); }, "Large colors rejected");
        rejects([] { plan(Json{{"adapter", "arbitrary-usb-write"}}); }, "Unknown adapters cannot send arbitrary packets");
        Json record{{"id", "test"}, {"transport", "hid"}, {"matcher", {{"vid", 0x1234}, {"pid", 0x5678}, {"interface", 2}, {"usagePage", 0xff00}}}, {"signatureComplete", true}};
        Json catalog{{"records", Json::array({record})}};
        expect(candidates(catalog, Json{{"transport", "hid"}, {"vid", 0x1234}, {"pid", 0x9999}}).empty(), "Same vendor is insufficient");
        auto partial = candidates(catalog, Json{{"transport", "hid"}, {"vid", 0x1234}, {"pid", 0x5678}});
        expect(partial.size() == 1 && !partial[0]["signatureMatched"].get<bool>(), "Missing endpoint remains a candidate");
        expect(candidates(catalog, Json{{"transport", "hid"}, {"vid", 0x1234}, {"pid", 0x5678}, {"interface", 1}}).empty(), "Wrong interface is rejected");
        auto exact = candidates(catalog, Json{{"transport", "hid"}, {"vid", 0x1234}, {"pid", 0x5678}, {"interface", 2}, {"usagePage", 0xff00}});
        expect(exact.size() == 1 && exact[0]["signatureMatched"].get<bool>() && exact[0]["identificationOnly"].get<bool>(), "Signature match is still identification only");
        catalog["records"][0]["name"] = "Fixture";
        catalog["records"][0]["nativeAdapter"] = nullptr;
        Json mapping{{"recordId", "test"}, {"detectorName", "Fixture"}, {"transport", "hid"}, {"provider", "openrgb-driver-core"}, {"status", "native-experimental"}, {"hardwareVerified", false}};
        attachProviders(catalog, Json{{"mappings", Json::array({mapping})}});
        expect(catalog["records"][0]["nativeAdapter"] == "openrgb-driver-core", "Compiled provider is exposed by lookup");
        expect(catalog["records"][0]["hardwareVerified"] == false, "Compiled provider never claims physical validation");
        expect(catalog["records"][0]["matcher"] == record["matcher"], "Provider metadata cannot relax endpoint constraints");
        catalog["records"][0]["nativeAdapter"] = "focused-adapter";
        attachProviders(catalog, Json{{"mappings", Json::array({mapping})}});
        expect(catalog["records"][0]["nativeAdapter"] == "focused-adapter", "Focused native adapter retains priority");
        std::cout << checks << " native protocol and matching checks passed; no hardware opened\n";
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
