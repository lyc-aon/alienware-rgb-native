# Event-Driven Light Changes

Name event presets once, then fire them from the GUI, CLI, or any local
automation hook.

## One-Liner

```bash
alienware_rgb_cli fire-event starter-circuit
```

The CLI snapshots current zone state, starts a detached animator process,
returns immediately, and restores the snapshot when the animation finishes.

## Starter Events

| Event | Zones | Effect |
|---|---|---|
| `starter-circuit` | `group:Side Panel Strip` | circuit chase |
| `starter-comet` | `group:Side Panel Strip` | comet trace |
| `starter-split-wave` | `group:Side Panel Strip` | mirrored wave |
| `starter-rainbow-cycle` | `group:Side Panel Strip` | hue cycle |
| `starter-plasma` | `group:Side Panel Strip` | multi-hue flow |
| `starter-bounce` | `group:Side Panel Strip` | scanner bounce |
| `starter-warmup` | `all` | full-system warmup |
| `starter-alert` | `all` | full-system alert blink |

## Effects

| Effect | Waveform | Good for |
|---|---|---|
| `solid` | flat color | predictable flash tests |
| `pulse` | sine pulse | subtle notifications |
| `breathing` | slow pulse | sustained status |
| `flicker` | randomized intensity | alarm/noise effects |
| `strobe` | square wave | sharp alerts |
| `fade` | smoothstep fade | smooth transitions |
| `heartbeat` | double thump | urgent prompts |
| `blink` | one hard on/off hit | short acknowledgements |
| `triple-blink` | three hits then hold | attention events |
| `slow-pulse` | slow breathing pulse | low-priority ambient status |
| `warmup` | ramp up, hold, fade | startup/wake events |
| `reverse-fade` | fade out from bright | shutdown/idle events |
| `sparkle` | per-zone spark decay | activity effects |
| `wave` | broad ordered sweep | scan previews |
| `comet` | bright head with tail | ordered-zone tracing |
| `rainbow-cycle` | rotating hue bands | ambient color flow |
| `circuit` | looping chase heads | side-panel circuit animation |
| `bounce` | scanner head between endpoints | checking ordered endpoints |
| `split-wave` | mirrored pulses | symmetrical side-panel motion |
| `plasma` | quantized sine flow | vivid ambient color |

Concurrency model: last event wins on overlapping zones. Different-zone events
can run in parallel because the animator only holds the HID lock per frame.

## GUI Event Registry

`~/.config/alienware-rgb/events.json` is seeded from `config/events.json` when
missing. Edit by hand or from `Events...` in the toolbar.

Schema:

```json
{
  "events": [
    {
      "id": "starter-circuit",
      "name": "Starter - side circuit",
      "zones": "group:Side Panel Strip",
      "rgb": [0, 190, 255],
      "effect": "circuit",
      "duration_sec": 5
    }
  ]
}
```

Fields:

- `id`: lowercase dashed identifier used by `fire-event <id>`
- `name`: display label
- `zones`: `all`, `group:<name>`, or comma-separated zone ids
- `rgb`: three integers from 0 to 255
- `effect`: one of the effect names above
- `duration_sec`: animation length before snapshot restore

## Runtime Broker

Long-running automation should call:

```bash
rgb-pulse starter-circuit --force
```

When `rgb-event-broker` is running, `rgb-pulse` enqueues a JSON request and the
broker owns queueing, coalescing, cooldowns, priority, focus skipping, hardware
dispatch, and optional voice playback. If the broker is unavailable, `rgb-pulse`
falls back to a direct CLI fire path unless `RGB_PULSE_REQUIRE_BROKER=1` is set.

Runtime presets live in `~/.config/alienware-rgb/runtime_events.json`; the repo
copy is `config/runtime_events.json`. Runtime entries can carry `priority`,
`throttle`, `force`, `voice`, `zones`, `rgb`, `effect`, and `duration`.

## Optional Voice

Voice is disabled by default and no audio files are bundled. To opt in:

1. Set `enabled` to `true` in `~/.config/alienware-rgb/voice.json`.
2. Set `output_dir` to a local directory.
3. Add phrases for the event ids you want to voice.
4. Put cached audio files at `<output_dir>/<event-id>.mp3` or
   `<output_dir>/<event-id>.wav`.
5. Set `"voice": true` on the matching runtime event.

Lights still fire when voice is disabled or missing.

## Ad-Hoc Without A Preset

```bash
alienware_rgb_cli apply-color \
  --zones group:"Side Panel Strip" \
  --rgb 0,240,255 \
  --flash-duration 3 \
  --effect comet
```

## Runtime Smoke

After installing:

```bash
runtime-smoke --events starter-circuit,starter-comet,starter-alert
```

## CLI Reference

```text
alienware_rgb_cli fire-event <id>
alienware_rgb_cli list-events
alienware_rgb_cli apply-color --zones SPEC --rgb R,G,B [--flash-duration N] [--effect NAME]
alienware_rgb_cli all-on --rgb R,G,B [--flash-duration N] [--effect NAME]
alienware_rgb_cli all-off
alienware_rgb_cli apply-profile <name> [--flash-duration N]
alienware_rgb_cli set-brightness <pct>
alienware_rgb_cli list-groups
alienware_rgb_cli list-profiles
alienware_rgb_cli runtime-disable
alienware_rgb_cli runtime-enable
alienware_rgb_cli runtime-status
alienware_rgb_cli doctor [--json]
```
