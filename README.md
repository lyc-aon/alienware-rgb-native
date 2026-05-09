# Alienware RGB Native

Linux-native C++/Qt6 controller for the Alienware AW-ELC lighting controller.

This project is tested on:

- System: Alienware Area-51 AAT2250
- Motherboard: Alienware 02JGX1 A00
- Firmware observed during release prep: Alienware BIOS 1.10.2, 2025-10-29
- Lighting device: USB VID:PID `187c:0551`, AW-ELC
- Platform: `0x0812`, 101 total zones with a calibrated 91-zone side-panel order

It is not an official Dell or Alienware project.

## What It Does

- Qt6 desktop GUI for zone, group, profile, event, and diagnostics control
- Headless `alienware_rgb_cli` for scripts and terminals
- Calibrated `Side Panel Strip` zone order for circuit, comet, bounce, wave, and mirrored animations
- Runtime broker for optional external event producers
- Starter event presets with no private hooks or bundled audio
- Optional cached `.mp3` / `.wav` voice playback when users configure it locally
- Redacted diagnostics bundle collection
- CI build, native tests, and checked-in config validation

## Build Requirements

```bash
sudo apt install build-essential cmake jq pkg-config qt6-base-dev qt6-base-dev-tools libhidapi-dev
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

The GUI binary is `build/alienware_rgb`; the headless CLI is
`build/alienware_rgb_cli`.

## Install Or Upgrade

```bash
scripts/install-runtime
```

The installer copies the GUI, CLI, runtime broker, helper scripts, app icon, and
desktop launchers into user paths, writes a user-systemd broker unit, starts the
broker, writes a SHA256 install manifest, and runs `alienware_rgb_cli doctor`.

Existing config under `~/.config/alienware-rgb` is preserved by default. Use
`--force-config` only when intentionally replacing local calibration and event
files; replaced files are copied into `~/.config/alienware-rgb/backups`.

Preview without writing:

```bash
scripts/install-runtime --dry-run --no-enable --no-desktop --no-doctor
```

Uninstall while preserving config:

```bash
scripts/uninstall-runtime
```

Use `scripts/uninstall-runtime --purge-config` only when intentionally deleting
local calibration, event, and voice settings too.

## Runtime Config

Live config is stored under `~/.config/alienware-rgb`:

- `zone_map.json` is the calibrated 101-zone map.
- `events.json` drives GUI and CLI `fire-event` presets.
- `runtime_events.json` drives `rgb-pulse`, the broker, throttles, priorities,
  force rules, and optional voice flags.
- `voice.json` is disabled by default and only points to local cached audio if
  the user opts in.

The repository ships generic starter events only:

```bash
alienware_rgb_cli fire-event starter-circuit
alienware_rgb_cli fire-event starter-comet
alienware_rgb_cli fire-event starter-split-wave
alienware_rgb_cli fire-event starter-rainbow-cycle
alienware_rgb_cli fire-event starter-alert
```

Automation can call runtime events through:

```bash
rgb-pulse starter-circuit --force
```

To add your own automation, create a new entry in
`~/.config/alienware-rgb/runtime_events.json` and have any local hook run
`rgb-pulse <event-id>`.

## Verification

Run the live health gate:

```bash
build/alienware_rgb_cli doctor
build/alienware_rgb_cli doctor --json | jq .
```

Temporarily suppress runtime broker events while keeping manual CLI commands
available:

```bash
build/alienware_rgb_cli runtime-disable
build/alienware_rgb_cli runtime-status
build/alienware_rgb_cli runtime-enable
```

Run the repo-config gate used by CI:

```bash
scripts/verify-runtime-config build/alienware_rgb_cli
```

Run the native tests:

```bash
ctest --test-dir build --output-on-failure
```

Run an installed-runtime smoke check:

```bash
runtime-smoke --events starter-circuit,starter-comet,starter-alert
```

Collect a redacted diagnostics bundle:

```bash
scripts/collect-diagnostics
```

## Protocol Notes

- Device: USB VID:PID `187c:0551` (Alienware AW-ELC)
- Platform `0x0812` has 101 zones
- `SET_COLOR` (`0x27`) is the reliable command on this platform
- Hardware animation commands are intentionally avoided
- Max 25 zones per `SET_COLOR` packet; the controller chunks automatically
- 70 ms delay is used between send/read cycles

## Development

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
scripts/verify-runtime-config build/alienware_rgb_cli
```

CI does not require hardware. It builds both binaries and validates the
checked-in runtime config against an isolated `ALIENWARE_RGB_CONFIG_DIR`.

## License

MIT. See `LICENSE`.
