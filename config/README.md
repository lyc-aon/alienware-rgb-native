# Runtime Configuration Snapshots

These files mirror the live runtime config under `~/.config/alienware-rgb`.

- `events.json` is the GUI and CLI `fire-event` registry.
- `runtime_events.json` is the `rgb-pulse` and `rgb-event-broker` catalog for
  optional automation hooks, throttling, force rules, priorities, and voice flags.
- `voice.json` is disabled by default. If a user opts in, the broker and
  `rgb-pulse` can play cached `.mp3` or `.wav` files named after event ids.

The checked-in defaults are intentionally generic starter animations. They do
not include private local hooks, API keys, provider voice ids, or bundled audio.

Install the current repo snapshots:

```bash
install -D -m 0644 config/runtime_events.json \
  "$HOME/.config/alienware-rgb/runtime_events.json"
install -D -m 0644 config/events.json \
  "$HOME/.config/alienware-rgb/events.json"
install -D -m 0644 config/voice.json \
  "$HOME/.config/alienware-rgb/voice.json"
install -D -m 0644 calibration/aw3423dwf-zone-map-20260508.json \
  "$HOME/.config/alienware-rgb/zone_map.json"
```

Validate these snapshots without touching live config:

```bash
scripts/verify-runtime-config build/alienware_rgb_cli
```

To add a local automation event, copy an existing `starter-*` entry in
`runtime_events.json`, give it a new id, and call it with:

```bash
rgb-pulse your-event-id --force
```
