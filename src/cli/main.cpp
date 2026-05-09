// Alienware RGB — headless CLI.
//
// Used directly for manual control:
//   alienware_rgb_cli all-off
//   alienware_rgb_cli all-on --rgb 255,120,0
//   alienware_rgb_cli apply-color --zones group:"Alien Head" --rgb 255,0,0
//   alienware_rgb_cli set-brightness 70
//   alienware_rgb_cli list-groups
//
// Also the execution engine for event-driven runtime triggers.

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

#include "cli/commands.h"

namespace {

// Parse "r,g,b" → {r,g,b} ints (0-255). Returns false if malformed.
bool parseRgb(const QString& s, int& r, int& g, int& b) {
    const auto parts = s.split(',');
    if (parts.size() != 3) return false;
    bool ok1 = false, ok2 = false, ok3 = false;
    r = parts[0].trimmed().toInt(&ok1);
    g = parts[1].trimmed().toInt(&ok2);
    b = parts[2].trimmed().toInt(&ok3);
    return ok1 && ok2 && ok3
        && r >= 0 && r <= 255
        && g >= 0 && g <= 255
        && b >= 0 && b <= 255;
}

void printUsage() {
    std::fprintf(stderr,
        "alienware_rgb_cli — headless control for the AW-ELC lighting controller\n\n"
        "Usage:\n"
        "  alienware_rgb_cli <command> [options]\n\n"
        "Commands:\n"
        "  all-off                         turn every zone off\n"
        "  all-on --rgb R,G,B              set every zone to one color\n"
        "  apply-color --zones SPEC --rgb R,G,B\n"
        "                                  set specific zones. SPEC is one of:\n"
        "                                    all\n"
        "                                    group:<name>\n"
        "                                    <id>,<id>,<id>...\n"
        "  set-brightness <pct>            persist software-scaling brightness (0-100)\n"
        "  list-groups                     show known zone groups + counts\n"
        "  apply-profile <name>            load + apply a saved profile\n"
        "  list-profiles                   show saved profiles\n"
        "  restore-snapshot <path>         apply a snapshot file to hardware (used by flash)\n"
        "  fire-event <id>                 fire a named event preset (see events.json)\n"
        "  list-events                     list defined event presets\n"
        "  runtime-disable                 suppress runtime hook/broker light events\n"
        "  runtime-enable                  re-enable runtime hook/broker light events\n"
        "  runtime-status                  show runtime event gate and broker state\n"
        "  doctor [--json]                 validate config, mapping, runtime, voice cache\n"
        "\n"
        "Flags on apply-color / all-on:\n"
        "  --flash-duration <N>            revert to pre-flash state after N seconds\n"
        "  --effect <name>                 animation during flash window:\n"
        "                                     solid | pulse | breathing | flicker | strobe | fade\n"
        "                                     heartbeat | blink | triple-blink | slow-pulse | warmup\n"
        "                                     reverse-fade | sparkle | wave | comet | rainbow-cycle\n"
        "                                     circuit | bounce | split-wave | plasma\n\n"
        "Examples:\n"
        "  alienware_rgb_cli all-off\n"
        "  alienware_rgb_cli all-on --rgb 255,120,0\n"
        "  alienware_rgb_cli apply-color --zones group:Cooler --rgb 0,240,255\n"
        "  alienware_rgb_cli set-brightness 60\n"
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("alienware-rgb");

    const QStringList args = app.arguments();
    if (args.size() < 2) {
        printUsage();
        return 1;
    }

    const QString command = args[1];

    if (command == "-h" || command == "--help" || command == "help") {
        printUsage();
        return 0;
    }

    if (command == "all-off") {
        return cmdAllOff();
    }

    if (command == "all-on") {
        QCommandLineParser p;
        QCommandLineOption rgb("rgb", "Color as R,G,B (0-255 each)", "R,G,B");
        QCommandLineOption flash("flash-duration", "Revert to previous state after N seconds", "N");
        QCommandLineOption effect("effect", "Animation effect name", "NAME");
        p.addOption(rgb);
        p.addOption(flash);
        p.addOption(effect);
        p.process(args.mid(1));
        if (!p.isSet(rgb)) {
            std::fprintf(stderr, "all-on: --rgb is required\n"); return 1;
        }
        int r, g, b;
        if (!parseRgb(p.value(rgb), r, g, b)) {
            std::fprintf(stderr, "all-on: bad --rgb value (expected 'R,G,B' with 0-255)\n");
            return 1;
        }
        const QString effect_name = p.isSet(effect) ? p.value(effect).toLower() : "solid";
        QString snap;
        int flash_sec = 0;
        if (p.isSet(flash)) {
            bool ok = false;
            flash_sec = p.value(flash).toInt(&ok);
            if (!ok || flash_sec <= 0) {
                std::fprintf(stderr, "all-on: --flash-duration must be a positive integer\n");
                return 1;
            }
            snap = cli::takeSnapshot();
        }
        // Animated flash path: spawn animator subprocess, schedule safety-net revert, exit.
        if (flash_sec > 0 && effect_name != "solid") {
            cli::spawnAnimator(effect_name, "all", r, g, b, flash_sec * 1000, snap);
            cli::scheduleRevert(snap, flash_sec + 1);
            std::printf("all-on (animated %s): zones=all rgb=(%d,%d,%d) duration=%ds\n",
                        effect_name.toUtf8().constData(), r, g, b, flash_sec);
            return 0;
        }
        // Solid-flash / persistent-apply path.
        const int rc = cmdAllOn(r, g, b);
        if (rc == 0 && flash_sec > 0 && !snap.isEmpty()) {
            cli::scheduleRevert(snap, flash_sec);
            std::printf("  (flash scheduled: revert in %ds via %s)\n",
                        flash_sec, snap.toUtf8().constData());
        }
        return rc;
    }

    if (command == "apply-color") {
        QCommandLineParser p;
        QCommandLineOption zones("zones", "Zone spec: 'all' | 'group:<name>' | '<id>,<id>,...'", "SPEC");
        QCommandLineOption rgb("rgb", "Color as R,G,B (0-255 each)", "R,G,B");
        QCommandLineOption flash("flash-duration", "Revert to previous state after N seconds", "N");
        QCommandLineOption effect("effect", "Animation effect name", "NAME");
        p.addOption(zones);
        p.addOption(rgb);
        p.addOption(flash);
        p.addOption(effect);
        p.process(args.mid(1));
        if (!p.isSet(zones) || !p.isSet(rgb)) {
            std::fprintf(stderr, "apply-color: --zones and --rgb are required\n"); return 1;
        }
        int r, g, b;
        if (!parseRgb(p.value(rgb), r, g, b)) {
            std::fprintf(stderr, "apply-color: bad --rgb value\n"); return 1;
        }
        const QString effect_name = p.isSet(effect) ? p.value(effect).toLower() : "solid";
        QString snap;
        int flash_sec = 0;
        if (p.isSet(flash)) {
            bool ok = false;
            flash_sec = p.value(flash).toInt(&ok);
            if (!ok || flash_sec <= 0) {
                std::fprintf(stderr, "apply-color: --flash-duration must be a positive integer\n");
                return 1;
            }
            snap = cli::takeSnapshot();
        }
        if (flash_sec > 0 && effect_name != "solid") {
            cli::spawnAnimator(effect_name, p.value(zones), r, g, b, flash_sec * 1000, snap);
            cli::scheduleRevert(snap, flash_sec + 1);
            std::printf("apply-color (animated %s): zones=%s rgb=(%d,%d,%d) duration=%ds\n",
                        effect_name.toUtf8().constData(),
                        p.value(zones).toUtf8().constData(),
                        r, g, b, flash_sec);
            return 0;
        }
        const int rc = cmdApplyColor(p.value(zones), r, g, b);
        if (rc == 0 && flash_sec > 0 && !snap.isEmpty()) {
            cli::scheduleRevert(snap, flash_sec);
            std::printf("  (flash scheduled: revert in %ds via %s)\n",
                        flash_sec, snap.toUtf8().constData());
        }
        return rc;
    }

    if (command == "set-brightness") {
        if (args.size() < 3) {
            std::fprintf(stderr, "set-brightness: <pct> required\n"); return 1;
        }
        bool ok = false;
        const int pct = args[2].toInt(&ok);
        if (!ok) { std::fprintf(stderr, "set-brightness: pct must be an integer\n"); return 1; }
        return cmdSetBrightness(pct);
    }

    if (command == "list-groups") {
        return cmdListGroups();
    }

    if (command == "apply-profile") {
        // Positional <name> + optional --flash-duration N.
        QCommandLineParser p;
        QCommandLineOption flash("flash-duration", "Revert after N seconds", "N");
        p.addOption(flash);
        p.addPositionalArgument("name", "Profile name");
        p.process(args.mid(1));
        const auto positional = p.positionalArguments();
        if (positional.isEmpty()) {
            std::fprintf(stderr, "apply-profile: <name> required\n"); return 1;
        }
        const QString name = positional.first();
        QString snap;
        int flash_sec = 0;
        if (p.isSet(flash)) {
            bool ok = false;
            flash_sec = p.value(flash).toInt(&ok);
            if (!ok || flash_sec <= 0) {
                std::fprintf(stderr, "apply-profile: --flash-duration must be a positive integer\n");
                return 1;
            }
            snap = cli::takeSnapshot();
        }
        const int rc = cmdApplyProfile(name);
        if (rc == 0 && flash_sec > 0 && !snap.isEmpty()) {
            cli::scheduleRevert(snap, flash_sec);
            std::printf("  (flash scheduled: revert in %ds via %s)\n",
                        flash_sec, snap.toUtf8().constData());
        }
        return rc;
    }

    if (command == "list-profiles") {
        return cmdListProfiles();
    }

    if (command == "restore-snapshot") {
        if (args.size() < 3) {
            std::fprintf(stderr, "restore-snapshot: <path> required\n"); return 1;
        }
        return cmdRestoreSnapshot(args[2]);
    }

    if (command == "__animate") {
        // Internal subcommand spawned by spawnAnimator(). Not listed in --help.
        QCommandLineParser p;
        QCommandLineOption effect("effect", "effect name", "NAME");
        QCommandLineOption zones("zones", "zone spec", "SPEC");
        QCommandLineOption rgb("rgb", "R,G,B", "R,G,B");
        QCommandLineOption dur("duration-ms", "duration in ms", "N");
        QCommandLineOption snap("snapshot", "snapshot path", "PATH");
        p.addOption(effect);
        p.addOption(zones);
        p.addOption(rgb);
        p.addOption(dur);
        p.addOption(snap);
        p.process(args.mid(1));
        int r, g, b;
        if (!parseRgb(p.value(rgb), r, g, b)) return 1;
        const int duration = p.value(dur).toInt();
        return cmdAnimateInternal(
            p.value(effect),
            p.value(zones),
            r, g, b,
            duration,
            p.value(snap));
    }

    if (command == "fire-event") {
        if (args.size() < 3) {
            std::fprintf(stderr, "fire-event: <id> required\n"); return 1;
        }
        return cmdFireEvent(args[2]);
    }

    if (command == "list-events") {
        return cmdListEvents();
    }

    if (command == "runtime-disable") {
        return cmdRuntimeDisable();
    }

    if (command == "runtime-enable") {
        return cmdRuntimeEnable();
    }

    if (command == "runtime-status") {
        return cmdRuntimeStatus();
    }

    if (command == "doctor") {
        bool json_output = false;
        for (int i = 2; i < args.size(); ++i) {
            if (args[i] == "--json") {
                json_output = true;
            } else {
                std::fprintf(stderr, "doctor: unknown option '%s'\n", args[i].toUtf8().constData());
                return 1;
            }
        }
        return cmdDoctor(json_output);
    }

    std::fprintf(stderr, "unknown command: %s\n\n", command.toUtf8().constData());
    printUsage();
    return 1;
}
