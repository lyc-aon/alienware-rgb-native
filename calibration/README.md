# Alienware RGB calibration data

This directory stores source-controlled calibration artifacts that should outlive
the mutable runtime files under `~/.config/alienware-rgb`.

## Current map

- `aw3423dwf-zone-map-20260508.json`
- Captured with camera-assisted side-panel validation on an Alienware Area-51
  AAT2250 / Alienware 02JGX1 A00 system.
- Side Panel Strip contains 91 zones with unique `sort_order` values `0..90`.
- Ordered animation code sorts grouped zones by `sort_order`, then `zone_id`.
- The top-left correction from May 8, 2026 is included: zone `0` is
  `sort_order=0`, and zone `12` is `sort_order=6`.

Install the canonical map into the live runtime config:

```bash
install -D -m 0644 calibration/aw3423dwf-zone-map-20260508.json \
  "$HOME/.config/alienware-rgb/zone_map.json"
```

Runtime saves may rewrite formatting and discard unknown top-level metadata, so
mapping provenance belongs here rather than inside the live JSON file.
