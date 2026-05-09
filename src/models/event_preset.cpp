#include "models/event_preset.h"
#include "models/config_paths.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

#include <algorithm>

QString eventsFilePath() {
    return alienwareConfigPath("events.json");
}

QString EventPreset::summary() const {
    return QString("%1 · %2 · %3s · rgb(%4,%5,%6)")
               .arg(zones_spec, effect)
               .arg(duration_sec)
               .arg(rgb[0]).arg(rgb[1]).arg(rgb[2]);
}

bool loadEventPresets(QList<EventPreset>& out) {
    out.clear();
    QFile f(eventsFilePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    for (const QJsonValue& v : doc.object().value("events").toArray()) {
        const auto o = v.toObject();
        EventPreset p;
        p.id = o.value("id").toString();
        p.name = o.value("name").toString();
        p.zones_spec = o.value("zones").toString();
        p.effect = o.value("effect").toString("solid");
        p.duration_sec = std::clamp(o.value("duration_sec").toInt(2), 1, 600);
        const auto arr = o.value("rgb").toArray();
        if (arr.size() == 3) {
            p.rgb = {
                std::clamp(arr[0].toInt(255), 0, 255),
                std::clamp(arr[1].toInt(255), 0, 255),
                std::clamp(arr[2].toInt(255), 0, 255),
            };
        }
        if (p.id.isEmpty() || p.zones_spec.isEmpty()) continue;
        out.append(p);
    }
    return true;
}

bool saveEventPresets(const QList<EventPreset>& presets) {
    QJsonArray arr;
    for (const auto& p : presets) {
        QJsonObject o;
        o["id"] = p.id;
        o["name"] = p.name;
        o["zones"] = p.zones_spec;
        o["effect"] = p.effect;
        o["duration_sec"] = p.duration_sec;
        QJsonArray rgb;
        rgb.append(p.rgb[0]); rgb.append(p.rgb[1]); rgb.append(p.rgb[2]);
        o["rgb"] = rgb;
        arr.append(o);
    }
    QJsonObject root; root["events"] = arr;

    QSaveFile f(eventsFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

EventPreset* findEventById(QList<EventPreset>& presets, const QString& id) {
    for (auto& p : presets) if (p.id == id) return &p;
    return nullptr;
}

void seedDefaultEventsIfMissing() {
    if (QFile::exists(eventsFilePath())) return;
    QList<EventPreset> seeds = {
        {"starter-circuit",       "Starter — side circuit",
         "group:Side Panel Strip", {0, 190, 255},   "circuit",       5},
        {"starter-comet",         "Starter — comet trace",
         "group:Side Panel Strip", {40, 255, 190},  "comet",         4},
        {"starter-split-wave",    "Starter — split wave",
         "group:Side Panel Strip", {255, 205, 60},  "split-wave",    5},
        {"starter-rainbow-cycle", "Starter — rainbow cycle",
         "group:Side Panel Strip", {255, 255, 255}, "rainbow-cycle", 6},
        {"starter-plasma",        "Starter — plasma flow",
         "group:Side Panel Strip", {150, 95, 255},  "plasma",        5},
        {"starter-bounce",        "Starter — scanner bounce",
         "group:Side Panel Strip", {255, 80, 70},   "bounce",        4},
        {"starter-warmup",        "Starter — full warmup",
         "all", {0, 160, 255}, "warmup", 4},
        {"starter-alert",         "Starter — full alert",
         "all", {255, 70, 40}, "triple-blink", 3},
    };
    saveEventPresets(seeds);
}
