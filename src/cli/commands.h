#ifndef CLI_COMMANDS_H
#define CLI_COMMANDS_H

#include <QString>
#include <QStringList>

// Return codes: 0 on success, non-zero on failure (exit code from main).
int cmdAllOff();
int cmdAllOn(int r, int g, int b);
int cmdApplyColor(const QString& zone_spec, int r, int g, int b);
int cmdSetBrightness(int pct);
int cmdListGroups();
int cmdApplyProfile(const QString& name);
int cmdListProfiles();

// Restore every zone in a saved snapshot file to the hardware, then delete
// the file. Used by systemd-run for flash-then-restore reverts.
// Idempotent: missing snapshot file returns 0 (animator already handled it).
int cmdRestoreSnapshot(const QString& path);

// Internal: run an effect animation inline, then restore the pre-flash
// snapshot. Called by a detached subprocess spawned from state-changing
// commands with --flash-duration + --effect. Do not document in --help.
int cmdAnimateInternal(const QString& effect, const QString& zone_spec,
                       int r, int g, int b, int duration_ms,
                       const QString& snapshot_path);

// Fire a named event preset from events.json. Equivalent to
// apply-color --flash-duration <sec> --effect <name> with the preset values.
int cmdFireEvent(const QString& event_id);

// List all defined event presets.
int cmdListEvents();

// Operator gate for runtime event producers. Manual direct CLI commands still
// work; rgb-pulse and the broker drop runtime events while disabled.
int cmdRuntimeDisable();
int cmdRuntimeEnable();
int cmdRuntimeStatus();

// Read-only health check for the local install/config/runtime surface.
int cmdDoctor(bool json_output = false);

namespace cli {
    // Write a JSON snapshot of the current zone_map state to a temp file.
    // Returns the absolute path, or an empty string on failure.
    QString takeSnapshot();

    // Schedule a detached `systemd-run --user --on-active=<seconds>s
    //   alienware_rgb_cli restore-snapshot <path>`.  Returns true on success.
    bool scheduleRevert(const QString& snapshot_path, int seconds);

    // Spawn a detached `alienware_rgb_cli __animate ...` subprocess that
    // runs the effect for duration_ms and then restores the snapshot.
    // Returns true if the process was launched.
    bool spawnAnimator(const QString& effect, const QString& zone_spec,
                       int r, int g, int b, int duration_ms,
                       const QString& snapshot_path);
}

// Helpers exported for reuse / testing.
namespace cli {
    // Resolve `--zones` spec into a concrete list:
    //   "all"           → every zone
    //   "group:Name"    → every zone whose group == "Name"
    //   "1,2,3,42"      → literal list
    // Returns empty vector + sets err if invalid.
    std::vector<int> resolveZones(const QString& spec, QString* err = nullptr);

    // Brightness state (software scaling) — mirrored between GUI + CLI.
    // Stored at ~/.config/alienware-rgb/state.json.
    int  loadBrightness();          // 0-100, defaults to 100
    bool saveBrightness(int pct);

    // Scale rgb by current brightness before HID write.
    void applyBrightnessScale(int& r, int& g, int& b);
}

#endif // CLI_COMMANDS_H
