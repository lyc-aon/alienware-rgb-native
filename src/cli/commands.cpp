#include "cli/commands.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QStringList>
#include <QProcess>
#include <QDateTime>
#include <QUuid>
#include <QCoreApplication>
#include <QCryptographicHash>

#include <algorithm>
#include <atomic>
#include <array>
#include <csignal>
#include <cstdio>
#include <map>
#include <set>
#include <thread>
#include <sys/stat.h>

#include "models/zone_map.h"
#include "models/config_paths.h"
#include "models/profile.h"
#include "models/event_preset.h"
#include "transport/hid_transport.h"
#include "transport/hid_lock.h"
#include "controller/awelc_controller.h"
#include "animator/effects.h"
#include "animator/animator.h"

namespace {

QString configDir() {
    return alienwareConfigDir();
}

QString zoneMapPath() { return configDir() + "/zone_map.json"; }
QString statePath()   { return configDir() + "/state.json"; }
QString runtimeEventsPath() { return configDir() + "/runtime_events.json"; }
QString voicePath() { return configDir() + "/voice.json"; }
QString runtimeDisableMarkerPath() {
    const QString override = qEnvironmentVariable("RGB_PULSE_DISABLE_FILE").trimmed();
    return override.isEmpty()
        ? QDir(configDir()).filePath("runtime-disabled")
        : QFileInfo(override).absoluteFilePath();
}
QString brokerStatusPath() {
    const QByteArray xdg = qgetenv("XDG_RUNTIME_DIR");
    const QString base = xdg.isEmpty() ? QString("/tmp") : QString::fromLocal8Bit(xdg);
    return base + "/rgb-event-broker.status.json";
}
QString brokerFifoPath() {
    const QByteArray override = qgetenv("RGB_BROKER_FIFO");
    if (!override.isEmpty()) return QString::fromLocal8Bit(override);
    const QByteArray xdg = qgetenv("XDG_RUNTIME_DIR");
    const QString base = xdg.isEmpty() ? QString("/tmp") : QString::fromLocal8Bit(xdg);
    return base + "/rgb-event-broker.fifo";
}

bool readJsonObjectFile(const QString& path, QJsonObject& out, QString& err) {
    QFile f(path);
    if (!f.exists()) {
        err = "missing";
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        err = "unreadable";
        return false;
    }
    QJsonParseError parse{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &parse);
    f.close();
    if (parse.error != QJsonParseError::NoError) {
        err = parse.errorString();
        return false;
    }
    if (!doc.isObject()) {
        err = "root is not an object";
        return false;
    }
    out = doc.object();
    return true;
}

bool validRgbArray(const QJsonValue& value, QString* err = nullptr) {
    const auto arr = value.toArray();
    if (arr.size() != 3) {
        if (err) *err = "rgb must have exactly 3 entries";
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!arr[i].isDouble()) {
            if (err) *err = "rgb entries must be numeric";
            return false;
        }
        const int v = arr[i].toInt(-1);
        if (v < 0 || v > 255) {
            if (err) *err = "rgb entries must be in 0..255";
            return false;
        }
    }
    return true;
}

bool knownEffect(const QString& effect) {
    return allEffectNames().contains(effect);
}

bool isFifoPath(const QString& path) {
    struct stat st {};
    if (::stat(path.toLocal8Bit().constData(), &st) != 0) return false;
    return S_ISFIFO(st.st_mode);
}

struct Doctor {
    explicit Doctor(bool json_output = false) : json(json_output) {}

    bool json = false;
    int failures = 0;
    int warnings = 0;
    QJsonArray checks;

    void ok(const QString& name, const QString& detail = {}) {
        record("ok", name, detail);
    }
    void warn(const QString& name, const QString& detail = {}) {
        ++warnings;
        record("warn", name, detail);
    }
    void fail(const QString& name, const QString& detail = {}) {
        ++failures;
        record("fail", name, detail);
    }

    void record(const QString& status, const QString& name, const QString& detail = {}) {
        QJsonObject check;
        check["status"] = status;
        check["name"] = name;
        check["detail"] = detail;
        checks.append(check);
        if (!json) {
            const QString label = status.toUpper();
            std::printf("%-5s %-24s %s\n",
                        label.toUtf8().constData(),
                        name.toUtf8().constData(),
                        detail.toUtf8().constData());
        }
    }
};

QString envOrPath(const char* name, const QString& fallback) {
    const QString value = qEnvironmentVariable(name).trimmed();
    return value.isEmpty() ? fallback : QDir(value).absolutePath();
}

QString runtimeBinDir() {
    return envOrPath("ALIENWARE_RGB_BIN_DIR", QDir::home().filePath("bin"));
}

QString runtimeDataDir() {
    const QString data_home = qEnvironmentVariable("XDG_DATA_HOME").trimmed();
    const QString fallback = data_home.isEmpty()
        ? QDir::home().filePath(".local/share/alienware-rgb")
        : QDir(data_home).filePath("alienware-rgb");
    return envOrPath("ALIENWARE_RGB_DATA_DIR", fallback);
}

QString runtimeInstallManifestPath() {
    return QDir(runtimeDataDir()).filePath("install-manifest.json");
}

QString runtimeIconPath() {
    return QDir(runtimeDataDir()).filePath("icons/alienware-rgb.png");
}

QString runtimeSystemdUserDir() {
    return envOrPath("ALIENWARE_RGB_SYSTEMD_USER_DIR", QDir::home().filePath(".config/systemd/user"));
}

bool runtimeSystemdUserDirIsLiveDefault() {
    return runtimeSystemdUserDir() == QDir(QDir::home().filePath(".config/systemd/user")).absolutePath();
}

QString runtimeApplicationsDir() {
    return envOrPath("ALIENWARE_RGB_APPLICATIONS_DIR", QDir::home().filePath(".local/share/applications"));
}

bool readTextFile(const QString& path, QString& out) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    out = QString::fromUtf8(f.readAll());
    return true;
}

QString sha256File(const QString& path, QString* err = nullptr) {
    QFile f(path);
    if (!f.exists()) {
        if (err) *err = "missing";
        return {};
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = "unreadable";
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(1024 * 1024);
        if (chunk.isEmpty() && f.error() != QFile::NoError) {
            if (err) *err = f.errorString();
            return {};
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool systemctlUserOk(const QStringList& args) {
    QProcess p;
    QStringList full_args = {"--user"};
    full_args.append(args);
    p.start("systemctl", full_args);
    if (!p.waitForFinished(2000)) {
        p.kill();
        p.waitForFinished(500);
        return false;
    }
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

bool textContainsLine(const QString& text, const QString& expected) {
    for (const QString& line : text.split('\n')) {
        if (line.trimmed() == expected) return true;
    }
    return false;
}

QStringList missingUnitHardeningLines(const QString& unit_text) {
    const QStringList required = {
        "NoNewPrivileges=true",
        "UMask=077",
        "LockPersonality=true",
        "RestrictRealtime=true",
        "RestrictSUIDSGID=true",
        "SystemCallArchitectures=native",
        "ProtectSystem=full",
        "ProtectControlGroups=true",
        "ProtectKernelTunables=true",
    };
    QStringList missing;
    for (const QString& line : required) {
        if (!textContainsLine(unit_text, line)) missing << line;
    }
    return missing;
}

void checkUnitHardening(Doctor& d, const QString& label, const QString& unit_text) {
    const QStringList missing = missingUnitHardeningLines(unit_text);
    missing.isEmpty()
        ? d.ok(label, "baseline hardening present")
        : d.fail(label, "missing " + missing.join(", "));
}

void checkInstalledRuntime(Doctor& d) {
    const QString bin_dir = runtimeBinDir();
    const QString unit_dir = runtimeSystemdUserDir();
    const QString applications_dir = runtimeApplicationsDir();

    const QStringList executable_names = {
        "alienware_rgb_cli",
        "alienware_rgb",
        "rgb-pulse",
        "rgb-event-broker",
        "runtime-smoke",
        "collect-diagnostics",
    };
    int bad_executables = 0;
    for (const QString& name : executable_names) {
        const QFileInfo fi(QDir(bin_dir).filePath(name));
        if (!fi.isFile() || !fi.isExecutable()) ++bad_executables;
    }
    bad_executables == 0
        ? d.ok("runtime executables", QString("%1 installed in %2").arg(executable_names.size()).arg(bin_dir))
        : d.fail("runtime executables", QString("%1 missing or not executable in %2").arg(bad_executables).arg(bin_dir));

    const QStringList unit_names = {
        "rgb-event-broker.service",
    };
    int missing_units = 0;
    for (const QString& name : unit_names) {
        if (!QFileInfo::exists(QDir(unit_dir).filePath(name))) ++missing_units;
    }
    missing_units == 0
        ? d.ok("runtime units", QString("%1 user units installed").arg(unit_names.size()))
        : d.fail("runtime units", QString("%1 missing in %2").arg(missing_units).arg(unit_dir));

    QString broker_unit;
    const QString broker_unit_path = QDir(unit_dir).filePath("rgb-event-broker.service");
    if (readTextFile(broker_unit_path, broker_unit)) {
        const bool paths_ok = broker_unit.contains(QDir(bin_dir).filePath("rgb-event-broker"))
                           && broker_unit.contains(QDir(bin_dir).filePath("alienware_rgb_cli"))
                           && broker_unit.contains(configDir());
        paths_ok ? d.ok("broker unit paths", "installed paths match current config")
                 : d.fail("broker unit paths", "unit does not point at installed bin/config paths");
        checkUnitHardening(d, "broker hardening", broker_unit);
    } else {
        d.fail("broker unit paths", "unreadable: " + broker_unit_path);
    }

    const QString app_desktop = QDir(applications_dir).filePath("alienware-rgb.desktop");
    QString desktop_text;
    if (!readTextFile(app_desktop, desktop_text)) {
        d.warn("desktop launcher", "missing: " + app_desktop);
    } else if (!desktop_text.contains(QDir(bin_dir).filePath("alienware_rgb"))) {
        d.warn("desktop launcher", "Exec does not point at installed GUI");
    } else if (!desktop_text.contains("Icon=" + runtimeIconPath()) || !QFileInfo::exists(runtimeIconPath())) {
        d.warn("desktop launcher", "Icon does not point at installed app icon");
    } else {
        d.ok("desktop launcher", app_desktop);
    }

    QJsonObject manifest_root;
    QString manifest_err;
    const QString manifest_path = runtimeInstallManifestPath();
    if (!readJsonObjectFile(manifest_path, manifest_root, manifest_err)) {
        d.fail("install manifest", manifest_err + ": " + manifest_path);
    } else {
        const QJsonArray files = manifest_root.value("files").toArray();
        int bad = 0;
        int checked = 0;
        for (const QJsonValue& v : files) {
            const QJsonObject o = v.toObject();
            const QString path = o.value("path").toString();
            const QString expected = o.value("sha256").toString();
            if (path.isEmpty() || expected.size() != 64) {
                ++bad;
                continue;
            }
            QString hash_err;
            const QString actual = sha256File(path, &hash_err);
            if (actual.isEmpty() || actual.compare(expected, Qt::CaseInsensitive) != 0) {
                ++bad;
                continue;
            }
            ++checked;
        }
        if (files.isEmpty()) {
            d.fail("install manifest", "files array is empty: " + manifest_path);
        } else if (bad == 0) {
            d.ok("install manifest", QString("%1 files verified").arg(checked));
        } else {
            d.fail("install manifest", QString("%1 drifted or invalid entries in %2").arg(bad).arg(manifest_path));
        }
    }

    if (!runtimeSystemdUserDirIsLiveDefault()) {
        d.ok("broker enabled", "skipped for isolated unit dir");
    } else {
        if (systemctlUserOk({"is-enabled", "--quiet", "rgb-event-broker.service"})) {
            d.ok("broker enabled", "rgb-event-broker.service");
        } else {
            d.warn("broker enabled", "rgb-event-broker.service is not enabled");
        }
    }
}

// Load the shared ZoneMap (or empty if the user hasn't run discovery yet).
bool loadZoneMap(ZoneMap& zm) {
    if (zm.load(zoneMapPath().toStdString())) return true;
    // Fall back to flat defaults so "--zones all" still works on a fresh install.
    zm = ZoneMap::createDefault(101);
    return false;
}

int zoneSortKey(const Zone& z) {
    return z.sort_order >= 0 ? z.sort_order : z.zone_id;
}

// Persist the given ZoneMap to the shared config path so subsequent
// snapshots reflect reality, and the GUI sees state on next launch.
// Failures are non-fatal (just logged); we don't roll back HID.
void persistZoneMap(const ZoneMap& zm) {
    if (!zm.save(zoneMapPath().toStdString())) {
        std::fprintf(stderr, "warning: could not persist zone_map to %s\n",
                     zoneMapPath().toUtf8().constData());
    }
}

// One-shot HID session: open device, run lambda, close. Returns exit code.
template<typename Fn>
int withHardware(Fn&& fn) {
    HIDLock lock(2000);
    if (!lock.acquired()) {
        std::fprintf(stderr, "hid lock busy at %s (another process using device)\n",
                     lock.path().c_str());
        return 2;
    }
    HIDTransport transport;
    if (!transport.open()) {
        std::fprintf(stderr, "could not open AW-ELC HID device (187c:0551) — check udev rule\n");
        return 3;
    }
    AWELCController ctl(transport);
    return fn(ctl);
}

}  // namespace

namespace cli {

std::vector<int> resolveZones(const QString& spec_in, QString* err) {
    ZoneMap zm;
    loadZoneMap(zm);
    const int count = zm.zoneCount();

    QString spec = spec_in.trimmed();
    if (spec.isEmpty() || spec.compare("all", Qt::CaseInsensitive) == 0) {
        std::vector<int> all(count);
        for (int i = 0; i < count; ++i) all[i] = i;
        return all;
    }

    if (spec.startsWith("group:", Qt::CaseInsensitive)) {
        const QString group = spec.mid(6).trimmed();
        std::vector<const Zone*> matched;
        for (const Zone& z : zm.allZones()) {
            if (QString::fromStdString(z.group).compare(group, Qt::CaseInsensitive) == 0) {
                matched.push_back(&z);
            }
        }
        std::sort(matched.begin(), matched.end(), [](const Zone* a, const Zone* b) {
            const int ak = zoneSortKey(*a);
            const int bk = zoneSortKey(*b);
            if (ak != bk) return ak < bk;
            return a->zone_id < b->zone_id;
        });
        std::vector<int> ids;
        ids.reserve(matched.size());
        for (const Zone* z : matched) ids.push_back(z->zone_id);
        if (ids.empty() && err) {
            *err = QString("no zones in group '%1'").arg(group);
        }
        return ids;
    }

    // Literal comma-separated list.
    std::vector<int> ids;
    for (const QString& part : spec.split(',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = part.trimmed().toInt(&ok);
        if (!ok || v < 0 || v >= count) {
            if (err) *err = QString("invalid zone id: '%1'").arg(part);
            return {};
        }
        ids.push_back(v);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

int loadBrightness() {
    QFile f(statePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return 100;
    QJsonParseError e{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &e);
    f.close();
    if (e.error != QJsonParseError::NoError || !doc.isObject()) return 100;
    const int pct = doc.object().value("brightness").toInt(100);
    return std::clamp(pct, 0, 100);
}

bool saveBrightness(int pct) {
    pct = std::clamp(pct, 0, 100);
    QDir().mkpath(configDir());
    QJsonObject o{{"brightness", pct}};
    QSaveFile f(statePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

void applyBrightnessScale(int& r, int& g, int& b) {
    const int pct = loadBrightness();
    if (pct == 100) return;
    const double s = pct / 100.0;
    r = std::clamp(static_cast<int>(r * s), 0, 255);
    g = std::clamp(static_cast<int>(g * s), 0, 255);
    b = std::clamp(static_cast<int>(b * s), 0, 255);
}

QString takeSnapshot() {
    ZoneMap zm;
    loadZoneMap(zm);

    QJsonArray zones;
    for (const Zone& z : zm.allZones()) {
        QJsonObject o;
        o["zone_id"] = z.zone_id;
        o["r"] = static_cast<int>(z.r);
        o["g"] = static_cast<int>(z.g);
        o["b"] = static_cast<int>(z.b);
        zones.append(o);
    }

    QJsonObject root;
    root["zones"] = zones;
    root["brightness"] = loadBrightness();
    root["taken_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
    const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QString("%1/alienware-rgb-flash-%2-%3.json")
                             .arg(tmp.isEmpty() ? "/tmp" : tmp)
                             .arg(stamp)
                             .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) return {};
    return path;
}

bool scheduleRevert(const QString& snapshot_path, int seconds) {
    if (snapshot_path.isEmpty() || seconds <= 0) return false;

    const QString self = QCoreApplication::applicationFilePath();
    const QString unit = QString("alienware-rgb-flash-%1")
                             .arg(QDateTime::currentMSecsSinceEpoch());

    QStringList args = {
        "--user",
        "--unit", unit,
        QString("--on-active=%1s").arg(seconds),
        // systemd defaults AccuracySec to 60s (battery-friendly grouping). Our
        // flashes NEED to be sharp, so force 100ms accuracy.
        "--timer-property=AccuracySec=100ms",
        "--collect",   // auto-clean-up when done
        "--quiet",
        "--",
        self,
        "restore-snapshot",
        snapshot_path,
    };
    // Fire and forget.
    return QProcess::startDetached("systemd-run", args);
}

bool spawnAnimator(const QString& effect, const QString& zone_spec,
                   int r, int g, int b, int duration_ms,
                   const QString& snapshot_path) {
    const QString self = QCoreApplication::applicationFilePath();
    QStringList args = {
        "__animate",
        "--effect", effect,
        "--zones", zone_spec,
        "--rgb", QString("%1,%2,%3").arg(r).arg(g).arg(b),
        "--duration-ms", QString::number(duration_ms),
        "--snapshot", snapshot_path,
    };
    return QProcess::startDetached(self, args);
}

}  // namespace cli

// ────────────────────────────────────────────────────────────────── commands

int cmdAllOff() {
    ZoneMap zm; loadZoneMap(zm);
    const int rc = withHardware([&](AWELCController& ctl) {
        ctl.allOff(zm.zoneCount());
        std::printf("all-off: %d zones\n", zm.zoneCount());
        return 0;
    });
    if (rc == 0) {
        for (Zone& z : const_cast<std::vector<Zone>&>(zm.allZones())) {
            z.r = 0; z.g = 0; z.b = 0;
        }
        persistZoneMap(zm);
    }
    return rc;
}

int cmdAllOn(int raw_r, int raw_g, int raw_b) {
    ZoneMap zm; loadZoneMap(zm);
    int r = raw_r, g = raw_g, b = raw_b;
    cli::applyBrightnessScale(r, g, b);
    const int rc = withHardware([&](AWELCController& ctl) {
        ctl.setAllColor(static_cast<uint8_t>(r),
                        static_cast<uint8_t>(g),
                        static_cast<uint8_t>(b),
                        zm.zoneCount());
        std::printf("all-on: %d zones, rgb=(%d,%d,%d)\n", zm.zoneCount(), r, g, b);
        return 0;
    });
    if (rc == 0) {
        // Persist the RAW (un-scaled) color so brightness changes later still scale
        // relative to the user's intent.
        for (Zone& z : const_cast<std::vector<Zone>&>(zm.allZones())) {
            z.r = static_cast<uint8_t>(raw_r);
            z.g = static_cast<uint8_t>(raw_g);
            z.b = static_cast<uint8_t>(raw_b);
        }
        persistZoneMap(zm);
    }
    return rc;
}

int cmdApplyColor(const QString& zone_spec, int raw_r, int raw_g, int raw_b) {
    QString err;
    const auto ids = cli::resolveZones(zone_spec, &err);
    if (ids.empty()) {
        std::fprintf(stderr, "no zones resolved from spec '%s'%s%s\n",
                     zone_spec.toUtf8().constData(),
                     err.isEmpty() ? "" : ": ",
                     err.toUtf8().constData());
        return 4;
    }
    int r = raw_r, g = raw_g, b = raw_b;
    cli::applyBrightnessScale(r, g, b);
    ZoneMap zm; loadZoneMap(zm);
    const int rc = withHardware([&](AWELCController& ctl) {
        ctl.setColorZones(static_cast<uint8_t>(r),
                          static_cast<uint8_t>(g),
                          static_cast<uint8_t>(b),
                          ids);
        std::printf("apply-color: %zu zones, rgb=(%d,%d,%d)\n", ids.size(), r, g, b);
        return 0;
    });
    if (rc == 0) {
        for (int id : ids) {
            if (Zone* z = zm.getZone(id)) {
                z->r = static_cast<uint8_t>(raw_r);
                z->g = static_cast<uint8_t>(raw_g);
                z->b = static_cast<uint8_t>(raw_b);
            }
        }
        persistZoneMap(zm);
    }
    return rc;
}

int cmdSetBrightness(int pct) {
    pct = std::clamp(pct, 0, 100);
    if (!cli::saveBrightness(pct)) {
        std::fprintf(stderr, "could not persist brightness state\n");
        return 5;
    }
    std::printf("brightness set to %d%% (applies on next color command)\n", pct);
    return 0;
}

int cmdListGroups() {
    ZoneMap zm; loadZoneMap(zm);
    for (const std::string& g : zm.groupNames()) {
        int count = 0;
        for (const Zone& z : zm.allZones()) if (z.group == g) ++count;
        std::printf("%-24s %d zones\n", g.c_str(), count);
    }
    return 0;
}

int cmdApplyProfile(const QString& name) {
    Profile p;
    if (!p.loadFromFile(profilePath(name))) {
        std::fprintf(stderr, "profile not found or malformed: %s\n",
                     profilePath(name).toUtf8().constData());
        return 6;
    }

    ZoneMap zm; loadZoneMap(zm);
    const auto batches = p.expand(zm);
    if (batches.empty()) {
        std::fprintf(stderr, "profile '%s' resolves to zero zones (group names drifted?)\n",
                     name.toUtf8().constData());
        return 7;
    }

    // Persist profile brightness so subsequent ad-hoc applies inherit it.
    cli::saveBrightness(p.brightness);

    return withHardware([&](AWELCController& ctl) {
        for (const auto& b : batches) {
            int r = b.r, g = b.g, bl = b.b;
            cli::applyBrightnessScale(r, g, bl);
            ctl.setColorZones(static_cast<uint8_t>(r),
                              static_cast<uint8_t>(g),
                              static_cast<uint8_t>(bl),
                              b.zone_ids);
        }
        std::printf("apply-profile '%s' — %zu color batches, brightness=%d%%\n",
                    p.name.toUtf8().constData(), batches.size(), p.brightness);
        return 0;
    });
}

int cmdRestoreSnapshot(const QString& path) {
    QFile f(path);
    if (!f.exists()) {
        // Animator already restored + cleaned up the snapshot. This is the
        // happy path for animated flashes; the systemd-run safety-net just
        // no-ops.
        std::printf("restore-snapshot: file already consumed: %s\n", path.toUtf8().constData());
        return 0;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "restore-snapshot: unreadable: %s\n",
                     path.toUtf8().constData());
        return 8;
    }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        std::fprintf(stderr, "restore-snapshot: malformed JSON at %s\n", path.toUtf8().constData());
        return 9;
    }

    const auto obj = doc.object();
    const int brightness = std::clamp(obj.value("brightness").toInt(100), 0, 100);
    cli::saveBrightness(brightness);

    // Batch zones by (r,g,b) for efficient HID writes.
    std::map<std::array<int, 3>, std::vector<int>> by_color;
    for (const QJsonValue& v : obj.value("zones").toArray()) {
        const auto z = v.toObject();
        const int id = z.value("zone_id").toInt(-1);
        if (id < 0) continue;
        const std::array<int, 3> rgb = {
            std::clamp(z.value("r").toInt(0), 0, 255),
            std::clamp(z.value("g").toInt(0), 0, 255),
            std::clamp(z.value("b").toInt(0), 0, 255),
        };
        by_color[rgb].push_back(id);
    }

    int rc = withHardware([&](AWELCController& ctl) {
        for (auto& [rgb, ids] : by_color) {
            int r = rgb[0], g = rgb[1], b = rgb[2];
            cli::applyBrightnessScale(r, g, b);
            ctl.setColorZones(static_cast<uint8_t>(r),
                              static_cast<uint8_t>(g),
                              static_cast<uint8_t>(b),
                              ids);
        }
        std::printf("restore-snapshot: %zu color batches from %s\n",
                    by_color.size(), path.toUtf8().constData());
        return 0;
    });

    // Write the snapshot state back into the shared zone_map so "current state"
    // stays consistent with what's actually on the hardware after the revert.
    if (rc == 0) {
        ZoneMap zm; loadZoneMap(zm);
        for (const QJsonValue& v : obj.value("zones").toArray()) {
            const auto z = v.toObject();
            const int id = z.value("zone_id").toInt(-1);
            if (Zone* target = zm.getZone(id)) {
                target->r = static_cast<uint8_t>(std::clamp(z.value("r").toInt(0), 0, 255));
                target->g = static_cast<uint8_t>(std::clamp(z.value("g").toInt(0), 0, 255));
                target->b = static_cast<uint8_t>(std::clamp(z.value("b").toInt(0), 0, 255));
            }
        }
        persistZoneMap(zm);
    }

    // Only delete the snapshot on success. On failure, leave it on disk so the
    // systemd-run safety-net (scheduled at duration+1s) re-fires `restore-snapshot`
    // and gets a second chance once the HID lock frees up. Stale files across
    // reboots are bounded by how many flashes you can fire in a session — /tmp
    // is cleaned on boot anyway.
    if (rc == 0) {
        QFile::remove(path);
    }
    return rc;
}

int cmdAnimateInternal(const QString& effect_name, const QString& zone_spec,
                       int r, int g, int b, int duration_ms,
                       const QString& snapshot_path) {
    QString err;
    const auto ids = cli::resolveZones(zone_spec, &err);
    if (ids.empty()) {
        std::fprintf(stderr, "__animate: no zones from '%s': %s\n",
                     zone_spec.toUtf8().constData(), err.toUtf8().constData());
        return 4;
    }

    int er = r, eg = g, eb = b;
    cli::applyBrightnessScale(er, eg, eb);

    AnimContext ctx;
    ctx.zone_ids = ids;
    ctx.base_r = static_cast<uint8_t>(er);
    ctx.base_g = static_cast<uint8_t>(eg);
    ctx.base_b = static_cast<uint8_t>(eb);
    ctx.duration_ms = duration_ms;
    const EffectType effect = effectTypeFromString(effect_name);

    // Open HID WITHOUT the animation-long lock; runEffect() acquires the
    // lock per-frame so parallel animators on different zones interleave
    // cleanly. HID device can be opened multiple times concurrently on
    // Linux — hidraw supports this; the flock serializes individual writes.
    HIDTransport transport;
    if (!transport.open()) {
        std::fprintf(stderr, "__animate: could not open AW-ELC HID\n");
        return 3;
    }
    AWELCController ctl(transport);

    // Install SIGTERM/SIGINT handler so that if this animator gets killed mid-effect
    // (GNOME session logout, parent CLI exit, user ctrl-C in debug runs, etc.), we
    // still fire a last-chance restore instead of leaving the hardware on whatever
    // frame happened to be the last HID write. The path is captured by value so
    // the signal handler needs no locking.
    static std::atomic<bool> g_signal_restore_fired{false};
    static std::string g_signal_snapshot_path;
    g_signal_snapshot_path = snapshot_path.toStdString();
    auto handler = [](int) {
        if (g_signal_restore_fired.exchange(true)) std::_Exit(0);
        if (!g_signal_snapshot_path.empty()) {
            cmdRestoreSnapshot(QString::fromStdString(g_signal_snapshot_path));
        }
        std::_Exit(0);
    };
    std::signal(SIGTERM, handler);
    std::signal(SIGINT, handler);

    runEffect(effect, ctx, ctl);

    // Final restore — on failure (typically HID lock contention with an overlapping
    // animator, or a transport glitch), retry once after a short pause. If that
    // fails too, the snapshot stays on disk and the systemd-run safety-net unit
    // (scheduled at duration+1s) re-fires restore-snapshot as a third chance.
    if (!snapshot_path.isEmpty()) {
        int rc = cmdRestoreSnapshot(snapshot_path);
        if (rc != 0 && QFile::exists(snapshot_path)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            cmdRestoreSnapshot(snapshot_path);
        }
    }
    return 0;
}

int cmdFireEvent(const QString& event_id) {
    seedDefaultEventsIfMissing();

    QList<EventPreset> presets;
    loadEventPresets(presets);
    EventPreset* p = findEventById(presets, event_id);
    if (!p) {
        std::fprintf(stderr, "fire-event: unknown event '%s' (run `list-events` to see registered ids)\n",
                     event_id.toUtf8().constData());
        return 11;
    }

    // Snapshot → spawn animator (or direct apply for solid) → schedule safety-net.
    const QString snap = cli::takeSnapshot();
    const int r = p->rgb[0], g = p->rgb[1], b = p->rgb[2];

    if (p->effect == "solid") {
        const int rc = cmdApplyColor(p->zones_spec, r, g, b);
        if (rc == 0 && !snap.isEmpty()) cli::scheduleRevert(snap, p->duration_sec);
        std::printf("fire-event '%s' (solid): zones=%s rgb=(%d,%d,%d) dur=%ds\n",
                    p->id.toUtf8().constData(), p->zones_spec.toUtf8().constData(),
                    r, g, b, p->duration_sec);
        return rc;
    }
    cli::spawnAnimator(p->effect, p->zones_spec, r, g, b, p->duration_sec * 1000, snap);
    cli::scheduleRevert(snap, p->duration_sec + 1);
    std::printf("fire-event '%s' (%s): zones=%s rgb=(%d,%d,%d) dur=%ds\n",
                p->id.toUtf8().constData(), p->effect.toUtf8().constData(),
                p->zones_spec.toUtf8().constData(), r, g, b, p->duration_sec);
    return 0;
}

int cmdListEvents() {
    seedDefaultEventsIfMissing();
    QList<EventPreset> presets;
    loadEventPresets(presets);
    if (presets.isEmpty()) { std::printf("(no events defined)\n"); return 0; }
    std::printf("%-24s %-28s %s\n", "ID", "NAME", "SUMMARY");
    std::printf("%-24s %-28s %s\n", "----", "----", "-------");
    for (const auto& p : presets) {
        std::printf("%-24s %-28s %s\n",
                    p.id.toUtf8().constData(),
                    p.name.toUtf8().constData(),
                    p.summary().toUtf8().constData());
    }
    return 0;
}

int cmdRuntimeDisable() {
    const QString marker = runtimeDisableMarkerPath();
    QDir().mkpath(QFileInfo(marker).absolutePath());

    QSaveFile f(marker);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        std::fprintf(stderr, "runtime-disable: could not write %s\n", marker.toUtf8().constData());
        return 12;
    }
    const QString body = QString("disabled_at=%1\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    f.write(body.toUtf8());
    if (!f.commit()) {
        std::fprintf(stderr, "runtime-disable: could not commit %s\n", marker.toUtf8().constData());
        return 12;
    }

    std::printf("runtime events disabled\nmarker: %s\n", marker.toUtf8().constData());
    return 0;
}

int cmdRuntimeEnable() {
    const QString marker = runtimeDisableMarkerPath();
    if (QFileInfo::exists(marker) && !QFile::remove(marker)) {
        std::fprintf(stderr, "runtime-enable: could not remove %s\n", marker.toUtf8().constData());
        return 12;
    }

    std::printf("runtime events enabled\nmarker: %s\n", marker.toUtf8().constData());
    return 0;
}

int cmdRuntimeStatus() {
    const QString marker = runtimeDisableMarkerPath();
    const bool disabled = QFileInfo::exists(marker);
    std::printf("runtime events: %s\n", disabled ? "disabled" : "enabled");
    std::printf("marker: %s\n", marker.toUtf8().constData());

    QJsonObject status_root;
    QString err;
    if (readJsonObjectFile(brokerStatusPath(), status_root, err)) {
        std::printf("broker pid: %d\n", status_root.value("pid").toInt(0));
        std::printf("broker queue_depth: %d\n", status_root.value("queue_depth").toInt(-1));
        std::printf("broker fired: %d\n", status_root.value("fired").toInt(0));
        std::printf("broker dropped: %d\n", status_root.value("dropped").toInt(0));
        if (status_root.contains("runtime_disabled")) {
            std::printf("broker sees runtime_disabled: %s\n",
                        status_root.value("runtime_disabled").toBool(false) ? "true" : "false");
        }
    } else {
        std::printf("broker status: unavailable (%s)\n", err.toUtf8().constData());
    }
    return 0;
}

int cmdDoctor(bool json_output) {
    Doctor d(json_output);

    if (!json_output) {
        std::printf("Alienware RGB doctor\n");
        std::printf("config: %s\n\n", configDir().toUtf8().constData());
    }

    // Zone map: this app relies on stable zone ids and a calibrated side-panel
    // ordering for the circuit-style animations.
    ZoneMap zm;
    bool zone_map_ok = false;
    if (!zm.load(zoneMapPath().toStdString())) {
        d.fail("zone_map", "missing or malformed: " + zoneMapPath());
    } else {
        zone_map_ok = true;
        d.ok("zone_map", QString("%1 zones").arg(zm.zoneCount()));

        bool ids_ok = true;
        for (int i = 0; i < zm.zoneCount(); ++i) {
            const Zone* z = zm.getZone(i);
            if (!z || z->zone_id != i) {
                ids_ok = false;
                break;
            }
        }
        ids_ok ? d.ok("zone ids", "contiguous and index-addressable")
               : d.fail("zone ids", "zone_id must match its array index");

        int side_count = 0;
        int side_active = 0;
        std::set<int> side_sort;
        for (const Zone& z : zm.allZones()) {
            if (z.group == "Side Panel Strip") {
                ++side_count;
                if (z.active) ++side_active;
                side_sort.insert(z.sort_order);
            }
        }
        if (side_count == 91 && side_active == 91) {
            d.ok("side panel group", "91 active zones");
        } else {
            d.fail("side panel group",
                   QString("%1 zones, %2 active; expected 91 active").arg(side_count).arg(side_active));
        }

        bool sort_ok = side_sort.size() == 91;
        int expected = 0;
        for (int v : side_sort) {
            if (v != expected++) {
                sort_ok = false;
                break;
            }
        }
        sort_ok ? d.ok("side panel order", "sort_order 0..90")
                : d.fail("side panel order", "sort_order must be unique and contiguous 0..90");

        const Zone* top_left = zm.getZone(0);
        if (top_left && top_left->group == "Side Panel Strip" && top_left->sort_order == 0) {
            d.ok("top-left anchor", "zone 0 is side-panel order 0");
        } else {
            d.fail("top-left anchor", "zone 0 should be Side Panel Strip sort_order 0");
        }
    }

    auto zoneSpecValid = [&](const QString& spec_in, QString* why) -> bool {
        if (!zone_map_ok) {
            if (why) *why = "zone_map unavailable";
            return false;
        }
        const QString spec = spec_in.trimmed();
        if (spec.isEmpty()) {
            if (why) *why = "empty zone spec";
            return false;
        }
        if (spec.compare("all", Qt::CaseInsensitive) == 0) return zm.zoneCount() > 0;
        if (spec.startsWith("group:", Qt::CaseInsensitive)) {
            const QString group = spec.mid(6).trimmed();
            int count = 0;
            for (const Zone& z : zm.allZones()) {
                if (QString::fromStdString(z.group).compare(group, Qt::CaseInsensitive) == 0) ++count;
            }
            if (count == 0 && why) *why = "empty group: " + group;
            return count > 0;
        }
        for (const QString& part : spec.split(',', Qt::SkipEmptyParts)) {
            bool ok = false;
            const int id = part.trimmed().toInt(&ok);
            if (!ok || id < 0 || id >= zm.zoneCount()) {
                if (why) *why = "invalid zone id: " + part.trimmed();
                return false;
            }
        }
        return true;
    };

    // GUI event presets.
    QJsonObject events_root;
    QString err;
    std::set<QString> gui_event_ids;
    if (!readJsonObjectFile(eventsFilePath(), events_root, err)) {
        d.fail("events.json", err + ": " + eventsFilePath());
    } else {
        const QJsonArray arr = events_root.value("events").toArray();
        if (arr.isEmpty()) {
            d.fail("events.json", "events array is empty");
        } else {
            int bad = 0;
            for (const QJsonValue& v : arr) {
                const QJsonObject o = v.toObject();
                const QString id = o.value("id").toString().trimmed();
                const QString zones = o.value("zones").toString().trimmed();
                const QString effect = o.value("effect").toString().trimmed();
                const int duration = o.value("duration_sec").toInt(0);
                QString why;
                if (id.isEmpty() || !gui_event_ids.insert(id).second) ++bad;
                if (zones.isEmpty()) ++bad;
                if (!knownEffect(effect)) ++bad;
                if (duration < 1 || duration > 600) ++bad;
                if (!validRgbArray(o.value("rgb"), &why)) ++bad;
                QString zone_err;
                if (!zoneSpecValid(zones, &zone_err)) ++bad;
            }
            bad == 0 ? d.ok("events.json", QString("%1 GUI presets valid").arg(arr.size()))
                     : d.fail("events.json", QString("%1 invalid preset fields").arg(bad));
        }
    }

    // Voice config first, so runtime voice events can be checked against it.
    QJsonObject voice_root;
    std::set<QString> phrase_ids;
    QString voice_dir;
    bool voice_enabled = false;
    if (!readJsonObjectFile(voicePath(), voice_root, err)) {
        d.warn("voice.json", err + ": " + voicePath());
    } else {
        voice_enabled = voice_root.value("enabled").toBool(false);
        voice_dir = voice_root.value("output_dir").toString();
        const QJsonObject phrases = voice_root.value("phrases").toObject();
        for (auto it = phrases.begin(); it != phrases.end(); ++it) {
            if (!it.value().toString().trimmed().isEmpty()) phrase_ids.insert(it.key());
        }
        if (voice_enabled && voice_dir.isEmpty()) {
            d.fail("voice config", "enabled but output_dir is empty");
        } else if (voice_enabled && !QFileInfo::exists(voice_dir)) {
            d.fail("voice cache dir", "missing: " + voice_dir);
        } else {
            d.ok("voice config", voice_enabled ? "enabled" : "disabled");
        }
    }

    // Runtime event catalog.
    QJsonObject runtime_root;
    if (!readJsonObjectFile(runtimeEventsPath(), runtime_root, err)) {
        d.fail("runtime_events.json", err + ": " + runtimeEventsPath());
    } else {
        const QJsonObject events = runtime_root.value("events").toObject();
        if (events.isEmpty()) {
            d.fail("runtime events", "events object is empty");
        } else {
            int bad = 0;
            int voice_missing_phrase = 0;
            int voice_missing_audio = 0;
            const std::set<QString> priorities = {"critical", "normal", "low"};
            for (auto it = events.begin(); it != events.end(); ++it) {
                const QString id = it.key();
                const QJsonObject o = it.value().toObject();
                const QString effect = o.value("effect").toString().trimmed();
                const QString priority = o.value("priority").toString("normal").trimmed();
                const int duration = o.value("duration").toInt(0);
                const int throttle = o.value("throttle").toInt(-1);
                const QString zones = o.value("zones").toString("all").trimmed();

                if (!knownEffect(effect)) ++bad;
                if (!priorities.contains(priority)) ++bad;
                if (duration < 1 || duration > 600) ++bad;
                if (throttle < 0 || throttle > 86400) ++bad;
                if (o.contains("rgb") && !validRgbArray(o.value("rgb"))) ++bad;
                QString zone_err;
                if (!zoneSpecValid(zones, &zone_err)) ++bad;

                if (o.value("voice").toBool(false)) {
                    if (!phrase_ids.contains(id)) ++voice_missing_phrase;
                    if (voice_enabled && !voice_dir.isEmpty()) {
                        const bool has_audio = QFileInfo::exists(voice_dir + "/" + id + ".wav")
                                            || QFileInfo::exists(voice_dir + "/" + id + ".mp3");
                        if (!has_audio) ++voice_missing_audio;
                    }
                }
            }
            bad == 0 ? d.ok("runtime events", QString("%1 runtime presets valid").arg(events.size()))
                     : d.fail("runtime events", QString("%1 invalid runtime fields").arg(bad));
            voice_missing_phrase == 0
                ? d.ok("voice phrases", "runtime voice events covered")
                : d.fail("voice phrases", QString("%1 voice events missing phrases").arg(voice_missing_phrase));
            if (voice_enabled) {
                voice_missing_audio == 0
                    ? d.ok("voice audio cache", "runtime voice events have cached audio")
                    : d.warn("voice audio cache", QString("%1 voice events missing cached audio").arg(voice_missing_audio));
            }
        }
    }

    // Install and broker runtime status. CI can independently disable the live
    // broker check while still validating an isolated install tree.
    if (qEnvironmentVariable("ALIENWARE_RGB_DOCTOR_INSTALL", "1") == "0") {
        d.ok("runtime install", "skipped by ALIENWARE_RGB_DOCTOR_INSTALL=0");
    } else {
        checkInstalledRuntime(d);
    }

    if (qEnvironmentVariable("ALIENWARE_RGB_DOCTOR_RUNTIME", "1") == "0") {
        d.ok("broker runtime", "skipped by ALIENWARE_RGB_DOCTOR_RUNTIME=0");
    } else {
        const QString disabled_marker = runtimeDisableMarkerPath();
        if (QFileInfo::exists(disabled_marker)) {
            d.warn("runtime enabled", "disabled marker present: " + disabled_marker);
        } else {
            d.ok("runtime enabled", "no disabled marker");
        }

        const QString fifo = brokerFifoPath();
        isFifoPath(fifo) ? d.ok("broker fifo", fifo)
                         : d.warn("broker fifo", "not present: " + fifo);

        QJsonObject status_root;
        if (!readJsonObjectFile(brokerStatusPath(), status_root, err)) {
            d.warn("broker status", err + ": " + brokerStatusPath());
        } else {
            const int pid = status_root.value("pid").toInt(0);
            const int dropped = status_root.value("dropped").toInt(0);
            const int queue_depth = status_root.value("queue_depth").toInt(-1);
            if (pid > 0 && QFileInfo::exists(QString("/proc/%1").arg(pid))) {
                d.ok("broker process", QString("pid=%1 queue_depth=%2").arg(pid).arg(queue_depth));
            } else {
                d.warn("broker process", QString("status pid %1 is not running").arg(pid));
            }
            const QString updated_text = status_root.value("updated_at").toString();
            const QDateTime updated_at = QDateTime::fromString(updated_text, Qt::ISODate);
            if (!updated_at.isValid()) {
                d.warn("broker heartbeat", "missing or invalid updated_at");
            } else {
                const qint64 age_sec = updated_at.toUTC().secsTo(QDateTime::currentDateTimeUtc());
                const int heartbeat_sec = std::clamp(status_root.value("heartbeat_sec").toInt(5), 1, 3600);
                const int stale_after_sec = std::max(30, heartbeat_sec * 4);
                if (age_sec < -10) {
                    d.warn("broker heartbeat", QString("status timestamp is %1s in the future").arg(-age_sec));
                } else if (age_sec > stale_after_sec) {
                    d.warn("broker heartbeat", QString("%1s old, expected <=%2s").arg(age_sec).arg(stale_after_sec));
                } else {
                    d.ok("broker heartbeat", QString("%1s old").arg(std::max<qint64>(0, age_sec)));
                }
            }
            dropped == 0 ? d.ok("broker dropped", "0")
                         : d.warn("broker dropped", QString::number(dropped));
        }
    }

    if (json_output) {
        QJsonObject root;
        root["ok"] = d.failures == 0;
        root["failures"] = d.failures;
        root["warnings"] = d.warnings;
        root["config_dir"] = configDir();
        root["checks"] = d.checks;
        const QByteArray encoded = QJsonDocument(root).toJson(QJsonDocument::Indented);
        std::fwrite(encoded.constData(), 1, static_cast<size_t>(encoded.size()), stdout);
    } else {
        std::printf("\nsummary: %d failure(s), %d warning(s)\n", d.failures, d.warnings);
    }
    return d.failures == 0 ? 0 : 20;
}

int cmdListProfiles() {
    const QStringList names = listProfileNames();
    if (names.isEmpty()) {
        std::printf("(no profiles saved)\n");
        return 0;
    }
    for (const QString& n : names) {
        Profile p;
        if (!p.loadFromFile(profilePath(n))) {
            std::printf("%-24s [malformed]\n", n.toUtf8().constData());
            continue;
        }
        std::printf("%-24s brightness=%3d%%  groups=%d\n",
                    n.toUtf8().constData(), p.brightness,
                    static_cast<int>(p.perGroup.size()));
    }
    return 0;
}
