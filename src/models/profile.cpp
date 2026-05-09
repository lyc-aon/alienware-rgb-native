#include "models/profile.h"
#include "models/config_paths.h"

#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDebug>

#include <algorithm>
#include <unordered_map>

QString profilesDir() {
    const QString d = alienwareConfigPath("profiles");
    QDir().mkpath(d);
    return d;
}

QString profilePath(const QString& name) {
    // Keep the file name URL-safe enough for display; we don't sanitize
    // aggressively because profile names are local-user input.
    return profilesDir() + "/" + name + ".json";
}

QStringList listProfileNames() {
    QDir d(profilesDir());
    QStringList names;
    for (const QString& f : d.entryList({"*.json"}, QDir::Files, QDir::Name)) {
        names << f.left(f.size() - 5);  // strip ".json"
    }
    return names;
}

bool Profile::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const auto o = doc.object();

    name = o.value("name").toString();
    brightness = std::clamp(o.value("brightness").toInt(100), 0, 100);

    perGroup.clear();
    const auto pg = o.value("per_group").toObject();
    for (auto it = pg.begin(); it != pg.end(); ++it) {
        const auto arr = it.value().toArray();
        if (arr.size() != 3) continue;
        std::array<int, 3> rgb = {
            std::clamp(arr[0].toInt(0), 0, 255),
            std::clamp(arr[1].toInt(0), 0, 255),
            std::clamp(arr[2].toInt(0), 0, 255),
        };
        perGroup.insert(it.key(), rgb);
    }
    return true;
}

bool Profile::saveToFile(const QString& path) const {
    QJsonObject o;
    o["name"] = name;
    o["brightness"] = brightness;

    QJsonObject pg;
    for (auto it = perGroup.begin(); it != perGroup.end(); ++it) {
        QJsonArray rgb;
        rgb.append(it.value()[0]);
        rgb.append(it.value()[1]);
        rgb.append(it.value()[2]);
        pg.insert(it.key(), rgb);
    }
    o["per_group"] = pg;

    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

Profile Profile::captureFromZoneMap(const ZoneMap& zm, int brightness, const QString& name) {
    Profile p;
    p.name = name;
    p.brightness = std::clamp(brightness, 0, 100);

    // Average r/g/b per group.
    std::unordered_map<std::string, std::array<long, 4>> sums;  // {r, g, b, n}
    for (const Zone& z : zm.allZones()) {
        if (z.group.empty()) continue;
        auto& s = sums[z.group];
        s[0] += z.r; s[1] += z.g; s[2] += z.b; s[3] += 1;
    }
    for (const auto& [grp, s] : sums) {
        const long n = s[3];
        if (n == 0) continue;
        p.perGroup.insert(QString::fromStdString(grp), {
            static_cast<int>(s[0] / n),
            static_cast<int>(s[1] / n),
            static_cast<int>(s[2] / n),
        });
    }
    return p;
}

std::vector<Profile::ColorBatch> Profile::expand(const ZoneMap& zm) const {
    // Bucket zones by (r,g,b) so each color batch is one HID call.
    std::map<std::array<int, 3>, std::vector<int>> by_color;
    for (auto it = perGroup.begin(); it != perGroup.end(); ++it) {
        const std::string grp = it.key().toStdString();
        const auto& rgb = it.value();
        for (const Zone& z : zm.allZones()) {
            if (z.group == grp) by_color[rgb].push_back(z.zone_id);
        }
    }
    std::vector<ColorBatch> out;
    out.reserve(by_color.size());
    for (auto& [rgb, ids] : by_color) {
        out.push_back({rgb[0], rgb[1], rgb[2], std::move(ids)});
    }
    return out;
}
