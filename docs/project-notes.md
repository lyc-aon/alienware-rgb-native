# Project Notes

## Supported Hardware

This release is built around the Alienware AW-ELC USB lighting controller:

- Tested system: Alienware Area-51 AAT2250
- Tested motherboard: Alienware 02JGX1 A00
- USB lighting device: `187c:0551`
- Platform id: `0x0812`
- Zone count: 101

The checked-in calibration file maps all 101 zones and gives the 91 side-panel
strip zones a stable `sort_order` for ordered animations.

## Safety Notes

- Use `SET_COLOR` (`0x27`) for hardware writes.
- Avoid hardware animation/reset command experiments on this platform.
- The app uses software-side animation with snapshot-and-restore so effects are
  bounded and reversible.
- CI validates config and logic without hardware; real device checks happen via
  `alienware_rgb_cli doctor` on a machine with AW-ELC attached.

## Public Release Defaults

The repository defaults are intentionally generic:

- No private automation hook scripts
- No bundled audio files
- No provider API keys or private voice ids
- Starter `starter-*` light presets only

Users can add their own runtime events by editing
`~/.config/alienware-rgb/runtime_events.json` and calling `rgb-pulse <event-id>`
from any local automation.
