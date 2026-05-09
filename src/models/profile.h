#ifndef PROFILE_H
#define PROFILE_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <array>

#include "models/zone_map.h"

// Named reusable lighting state. Serialized at
//   ~/.config/alienware-rgb/profiles/<name>.json
//
// Schema (indented JSON):
//   {
//     "name": "Evening",
//     "brightness": 50,
//     "per_group": { "Side Panel Strip": [255, 50, 0], "Cooler": [128, 0, 255] }
//   }
//
// Per-group is enough for 99% of use cases (95 of 101 zones live in Side
// Panel Strip). If a profile references a group that no longer exists at
// apply-time, that entry is silently skipped (logged to stderr on the CLI).
struct Profile {
    QString name;
    int brightness = 100;                      // 0-100
    QMap<QString, std::array<int, 3>> perGroup;  // group name -> {r,g,b}

    // Round-trip.
    bool loadFromFile(const QString& path);
    bool saveToFile(const QString& path) const;

    // Build a Profile from the current in-memory zone map (averaged per group).
    static Profile captureFromZoneMap(const ZoneMap& zm, int brightness, const QString& name);

    // Expand into per-zone instructions against the live ZoneMap. Produces
    // pairs of (r,g,b) → list of zone ids so the caller can batch per-color.
    struct ColorBatch { int r, g, b; std::vector<int> zone_ids; };
    std::vector<ColorBatch> expand(const ZoneMap& zm) const;
};

// Helpers — directory conventions.
QString profilesDir();
QString profilePath(const QString& name);
QStringList listProfileNames();

#endif // PROFILE_H
