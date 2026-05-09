#ifndef CONFIG_PATHS_H
#define CONFIG_PATHS_H

#include <QString>

// Shared app config root. Defaults to Qt's AppConfigLocation, but tests and CI
// can set ALIENWARE_RGB_CONFIG_DIR to validate an isolated config snapshot.
QString alienwareConfigDir();
QString alienwareConfigPath(const QString& file_name);

#endif // CONFIG_PATHS_H
