#ifndef ANIMATOR_EFFECTS_H
#define ANIMATOR_EFFECTS_H

#include <QString>
#include <vector>
#include <cstdint>

// All effects share the same context. Duration is in ms. Zone ids are
// pre-resolved (group names + "all" are expanded by the caller).
struct AnimContext {
    std::vector<int> zone_ids;
    uint8_t base_r = 255;
    uint8_t base_g = 255;
    uint8_t base_b = 255;
    int duration_ms = 2000;
};

enum class EffectType {
    // Uniform-intensity effects (one color across all zones, modulated over time).
    Solid,
    Pulse,
    Breathing,
    Flicker,
    Strobe,
    Fade,
    Heartbeat,     // two quick thumps, pause, repeat
    Blink,         // single hard on → off
    TripleBlink,   // three rapid blinks, then hold bright
    SlowPulse,     // breathing at 3× slower rate
    Warmup,        // silent ramp up, hold, fade out
    ReverseFade,   // fade out from current color (no ramp-in)

    // Per-zone effects (each zone gets its own intensity/color per frame).
    Sparkle,       // random zones fire at full then decay
    Wave,          // color sweep across ordered zone indices
    Comet,         // bright head + fading trail, moves across zones
    RainbowCycle,  // HSV hue rotation per zone, cycling over time
    Circuit,       // multiple chase heads loop around ordered zones
    Bounce,        // scanner head bounces between ordered endpoints
    SplitWave,     // mirrored pulses expand from center and collapse
    Plasma,        // quantized multi-hue flow across ordered zones
};

// True if the effect needs per-zone color sampling (not uniform intensity).
bool effectIsPerZone(EffectType t);

// String <-> enum helpers so CLI flags + JSON can round-trip.
EffectType effectTypeFromString(const QString& s);
QString effectTypeToString(EffectType t);

// Every recognized effect name (for validation + GUI combo population).
QStringList allEffectNames();

#endif // ANIMATOR_EFFECTS_H
