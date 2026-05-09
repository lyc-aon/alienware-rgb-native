#include "models/zone_map.h"
#include "protocol.h"
#include <algorithm>

#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>

namespace {
    // One backup written per process lifetime, first successful save.
    bool g_backup_written_this_session = false;
}

ZoneMap::ZoneMap() = default;

ZoneMap::ZoneMap(int zone_count) {
    for (int i = 0; i < zone_count; ++i) {
        zones_.emplace_back(i);
    }
}

ZoneMap ZoneMap::createDefault(int zone_count) {
    return ZoneMap(zone_count);
}

bool ZoneMap::load(const std::string& path) {
    QFile f(QString::fromStdString(path));
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonObject obj = doc.object();
    const QJsonArray arr = obj.value("zones").toArray();
    if (arr.isEmpty()) {
        return false;
    }

    zones_.clear();
    zones_.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const QJsonObject z = v.toObject();
        Zone zone(z.value("zone_id").toInt(-1));
        zone.label = z.value("label").toString().toStdString();
        zone.group = z.value("group").toString().toStdString();
        zone.image_x = z.value("image_x").toInt(-1);
        zone.image_y = z.value("image_y").toInt(-1);
        zone.grid_row = z.value("grid_row").toInt(-1);
        zone.grid_col = z.value("grid_col").toInt(-1);
        zone.sort_order = z.value("sort_order").toInt(zone.zone_id);
        zone.map_confidence = z.value("map_confidence").toDouble(0.0);
        zone.map_note = z.value("map_note").toString().toStdString();
        zone.r = static_cast<uint8_t>(z.value("r").toInt(0));
        zone.g = static_cast<uint8_t>(z.value("g").toInt(0));
        zone.b = static_cast<uint8_t>(z.value("b").toInt(0));
        zone.active = z.value("active").toBool(false);
        zones_.push_back(zone);
    }

    discovery_complete_ = obj.value("discovery_complete").toBool(false);
    last_discovered_zone_ = obj.value("last_discovered_zone").toInt(-1);
    return true;
}

bool ZoneMap::save(const std::string& path) const {
    const QString qpath = QString::fromStdString(path);

    // Ensure parent dir exists.
    QFileInfo fi(qpath);
    QDir().mkpath(fi.absolutePath());

    // Once-per-session backup of the previous file, if one exists.
    if (!g_backup_written_this_session && QFile::exists(qpath)) {
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        const QString backup_path = fi.absolutePath() + "/zone_map.backup-" + stamp + ".json";
        QFile::copy(qpath, backup_path);
        g_backup_written_this_session = true;
    }

    QJsonArray arr;
    for (const Zone& z : zones_) {
        QJsonObject o;
        o["zone_id"] = z.zone_id;
        o["label"] = QString::fromStdString(z.label);
        o["group"] = QString::fromStdString(z.group);
        o["image_x"] = z.image_x;
        o["image_y"] = z.image_y;
        o["grid_row"] = z.grid_row;
        o["grid_col"] = z.grid_col;
        o["sort_order"] = z.sort_order;
        o["map_confidence"] = z.map_confidence;
        if (!z.map_note.empty()) {
            o["map_note"] = QString::fromStdString(z.map_note);
        }
        o["r"] = static_cast<int>(z.r);
        o["g"] = static_cast<int>(z.g);
        o["b"] = static_cast<int>(z.b);
        o["active"] = z.active;
        arr.append(o);
    }

    QJsonObject root;
    root["zones"] = arr;
    root["discovery_complete"] = discovery_complete_;
    root["last_discovered_zone"] = last_discovered_zone_;

    QSaveFile out(qpath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return out.commit();  // atomic rename into place
}

Zone* ZoneMap::getZone(int zone_id) {
    if (zone_id >= 0 && zone_id < static_cast<int>(zones_.size())) {
        return &zones_[zone_id];
    }
    return nullptr;
}

const Zone* ZoneMap::getZone(int zone_id) const {
    if (zone_id >= 0 && zone_id < static_cast<int>(zones_.size())) {
        return &zones_[zone_id];
    }
    return nullptr;
}

std::vector<std::string> ZoneMap::groupNames() const {
    std::unordered_map<std::string, bool> groups;
    for (const auto& zone : zones_) {
        if (!zone.group.empty()) {
            groups[zone.group] = true;
        }
    }
    std::vector<std::string> result;
    result.reserve(groups.size());
    for (const auto& pair : groups) {
        result.push_back(pair.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<Zone*> ZoneMap::zonesInGroup(const std::string& group_name) {
    std::vector<Zone*> result;
    for (auto& zone : zones_) {
        if (zone.group == group_name) {
            result.push_back(&zone);
        }
    }
    return result;
}

void ZoneMap::assignGroup(const std::vector<int>& zone_ids, const std::string& group_name) {
    for (int id : zone_ids) {
        Zone* zone = getZone(id);
        if (zone) {
            zone->group = group_name;
        }
    }
}

void ZoneMap::clearGroup(const std::vector<int>& zone_ids) {
    for (int id : zone_ids) {
        Zone* zone = getZone(id);
        if (zone) {
            zone->group.clear();
        }
    }
}

std::vector<Zone*> ZoneMap::activeZones() {
    std::vector<Zone*> result;
    for (auto& zone : zones_) {
        if (zone.active) {
            result.push_back(&zone);
        }
    }
    return result;
}
