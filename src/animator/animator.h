#ifndef ANIMATOR_H
#define ANIMATOR_H

#include "animator/effects.h"

class AWELCController;

// Runs an animation to completion on the given HID controller. BLOCKS the
// caller for `ctx.duration_ms` milliseconds. Intended to be called from a
// short-lived subprocess (the `__animate` CLI subcommand), not from the main
// GUI event loop.
//
// Respects the persisted brightness state (reads loadBrightness() via the
// same path the rest of the CLI uses).
//
// Tick rate: 30 Hz when zone_ids.size() <= 10, else 15 Hz — the AW-ELC
// can't sustain 30 Hz over all 101 zones.
//
// Returns when the duration elapses. If zone_ids is empty, returns
// immediately.
void runEffect(EffectType type, const AnimContext& ctx, AWELCController& ctl);

#endif // ANIMATOR_H
