#include "animator/effects.h"
#include "models/event_preset.h"
#include "models/profile.h"
#include "models/zone_map.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int failures = 0;

void fail(const QString& message) {
    ++failures;
    std::cerr << "FAIL: " << message.toStdString() << '\n';
}

void expect(bool condition, const QString& message) {
    if (!condition) fail(message);
}

template <typename A, typename B>
void expectEq(const A& actual, const B& expected, const QString& message) {
    if (!(actual == expected)) {
        fail(QString("%1 (actual=%2 expected=%3)")
                 .arg(message)
                 .arg(actual)
                 .arg(expected));
    }
}

QString sourcePath(const QString& relative) {
    return QDir(QStringLiteral(ALIENWARE_RGB_SOURCE_DIR)).filePath(relative);
}

void writeJson(const QString& path, const QJsonObject& root) {
    QFile f(path);
    expect(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "open json for write: " + path);
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
}

void testEffects() {
    const QStringList names = allEffectNames();
    expectEq(names.size(), 20, "effect catalog size");

    QSet<QString> seen;
    for (const QString& name : names) {
        expect(!seen.contains(name), "duplicate effect name: " + name);
        seen.insert(name);
        expectEq(effectTypeToString(effectTypeFromString(name)), name, "effect round trip: " + name);
    }

    expectEq(effectTypeToString(effectTypeFromString("slow_pulse")), QString("slow-pulse"), "underscore alias");
    expectEq(effectTypeToString(effectTypeFromString("rainbow")), QString("rainbow-cycle"), "rainbow alias");
    expectEq(effectTypeToString(effectTypeFromString("missing-effect")), QString("solid"), "unknown effect fallback");

    const QSet<QString> per_zone = {
        "sparkle", "wave", "comet", "rainbow-cycle", "circuit",
        "bounce", "split-wave", "plasma",
    };
    for (const QString& name : names) {
        expectEq(effectIsPerZone(effectTypeFromString(name)), per_zone.contains(name), "per-zone classification: " + name);
    }
}

void testCalibratedZoneMap() {
    ZoneMap zm;
    expect(zm.load(sourcePath("calibration/aw3423dwf-zone-map-20260508.json").toStdString()), "load calibrated zone map");
    expectEq(zm.zoneCount(), 101, "calibrated zone count");

    std::vector<int> side_orders;
    side_orders.reserve(91);
    int active_count = 0;
    int side_count = 0;
    for (const Zone& z : zm.allZones()) {
        if (z.active) ++active_count;
        if (z.active && z.group == "Side Panel Strip") {
            ++side_count;
            side_orders.push_back(z.sort_order);
            expect(z.grid_row >= 0, QString("side zone has grid row: %1").arg(z.zone_id));
            expect(z.grid_col >= 0, QString("side zone has grid col: %1").arg(z.zone_id));
        }
    }
    expectEq(active_count, 101, "all calibrated zones active");
    expectEq(side_count, 91, "side-panel active zone count");
    std::sort(side_orders.begin(), side_orders.end());
    for (int i = 0; i < 91; ++i) {
        expectEq(side_orders.at(static_cast<size_t>(i)), i, QString("side-panel sort order %1").arg(i));
    }

    const Zone* top_left = zm.getZone(0);
    expect(top_left != nullptr, "zone 0 exists");
    if (top_left) {
        expectEq(QString::fromStdString(top_left->group), QString("Side Panel Strip"), "zone 0 group");
        expectEq(top_left->sort_order, 0, "zone 0 sort order");
        expectEq(top_left->grid_row, 0, "zone 0 grid row");
        expectEq(top_left->grid_col, 0, "zone 0 grid col");
    }
}

void testZoneMapRoundTrip() {
    QTemporaryDir tmp;
    expect(tmp.isValid(), "create temp dir for zone map");
    const QString path = tmp.filePath("zone_map.json");

    ZoneMap zm = ZoneMap::createDefault(3);
    zm.setDiscoveryComplete(true);
    zm.setLastDiscoveredZone(2);
    zm.assignGroup({0, 1}, "Pair");
    zm.assignGroup({2}, "Solo");
    if (Zone* z = zm.getZone(0)) {
        z->active = true;
        z->r = 10;
        z->g = 20;
        z->b = 30;
        z->grid_row = 4;
        z->grid_col = 5;
    }
    expect(zm.save(path.toStdString()), "save zone map");

    ZoneMap loaded;
    expect(loaded.load(path.toStdString()), "load saved zone map");
    expectEq(loaded.zoneCount(), 3, "round-trip zone count");
    expect(loaded.discoveryComplete(), "round-trip discovery flag");
    expectEq(loaded.lastDiscoveredZone(), 2, "round-trip last discovered zone");
    expectEq(QString::fromStdString(loaded.getZone(0)->group), QString("Pair"), "round-trip group");
    expectEq(loaded.getZone(0)->grid_row, 4, "round-trip grid row");
    expectEq(loaded.groupNames().size(), static_cast<size_t>(2), "round-trip sorted groups");
}

void testProfiles() {
    ZoneMap zm = ZoneMap::createDefault(4);
    zm.assignGroup({0, 1}, "A");
    zm.assignGroup({2}, "B");
    zm.assignGroup({3}, "Unused");
    if (Zone* z = zm.getZone(0)) { z->r = 10; z->g = 20; z->b = 30; }
    if (Zone* z = zm.getZone(1)) { z->r = 30; z->g = 40; z->b = 50; }
    if (Zone* z = zm.getZone(2)) { z->r = 100; z->g = 110; z->b = 120; }

    Profile captured = Profile::captureFromZoneMap(zm, 120, "Captured");
    expectEq(captured.brightness, 100, "captured brightness clamp");
    expect(captured.perGroup.contains("A"), "captured group A");
    expectEq(captured.perGroup["A"][0], 20, "captured average red");
    expectEq(captured.perGroup["A"][1], 30, "captured average green");
    expectEq(captured.perGroup["A"][2], 40, "captured average blue");

    Profile p;
    p.name = "Runtime";
    p.perGroup.insert("A", {1, 2, 3});
    p.perGroup.insert("B", {4, 5, 6});
    const auto batches = p.expand(zm);
    std::map<std::array<int, 3>, std::vector<int>> by_color;
    for (const auto& b : batches) by_color[{b.r, b.g, b.b}] = b.zone_ids;
    expectEq(by_color[{1, 2, 3}].size(), static_cast<size_t>(2), "profile expands group A");
    expectEq(by_color[{4, 5, 6}].size(), static_cast<size_t>(1), "profile expands group B");
}

void testEvents() {
    QTemporaryDir tmp;
    expect(tmp.isValid(), "create temp dir for events");
    qputenv("ALIENWARE_RGB_CONFIG_DIR", tmp.path().toUtf8());

    QJsonObject event;
    event["id"] = "build-pass";
    event["name"] = "Build Pass";
    event["zones"] = "group:Side Panel Strip";
    event["effect"] = "circuit";
    event["duration_sec"] = 9999;
    event["rgb"] = QJsonArray{-10, 42, 999};
    writeJson(tmp.filePath("events.json"), QJsonObject{{"events", QJsonArray{event}}});

    QList<EventPreset> events;
    expect(loadEventPresets(events), "load event presets");
    expectEq(events.size(), 1, "event count");
    expectEq(events[0].duration_sec, 600, "event duration clamp");
    expectEq(events[0].rgb[0], 0, "event red clamp");
    expectEq(events[0].rgb[1], 42, "event green value");
    expectEq(events[0].rgb[2], 255, "event blue clamp");
    expect(findEventById(events, "build-pass") != nullptr, "find event by id");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    testEffects();
    testCalibratedZoneMap();
    testZoneMapRoundTrip();
    testProfiles();
    testEvents();

    if (failures != 0) {
        std::cerr << failures << " core test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "alienware rgb core tests passed\n";
    return EXIT_SUCCESS;
}
