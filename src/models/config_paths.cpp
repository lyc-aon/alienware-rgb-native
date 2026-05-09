#include "models/config_paths.h"

#include <QDir>
#include <QStandardPaths>

QString alienwareConfigDir() {
    const QString override = qEnvironmentVariable("ALIENWARE_RGB_CONFIG_DIR").trimmed();
    const QString dir = override.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        : QDir(override).absolutePath();
    QDir().mkpath(dir);
    return dir;
}

QString alienwareConfigPath(const QString& file_name) {
    return QDir(alienwareConfigDir()).filePath(file_name);
}
