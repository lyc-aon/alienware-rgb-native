#include "animator/effects.h"

#include <QStringList>

EffectType effectTypeFromString(const QString& s) {
    const QString k = s.toLower().replace('_', '-');
    if (k == "pulse")         return EffectType::Pulse;
    if (k == "breathing")     return EffectType::Breathing;
    if (k == "flicker")       return EffectType::Flicker;
    if (k == "strobe")        return EffectType::Strobe;
    if (k == "fade")          return EffectType::Fade;
    if (k == "heartbeat")     return EffectType::Heartbeat;
    if (k == "blink")         return EffectType::Blink;
    if (k == "triple-blink")  return EffectType::TripleBlink;
    if (k == "slow-pulse")    return EffectType::SlowPulse;
    if (k == "warmup")        return EffectType::Warmup;
    if (k == "reverse-fade")  return EffectType::ReverseFade;
    if (k == "sparkle")       return EffectType::Sparkle;
    if (k == "wave")          return EffectType::Wave;
    if (k == "comet")         return EffectType::Comet;
    if (k == "rainbow" || k == "rainbow-cycle")
                              return EffectType::RainbowCycle;
    if (k == "circuit")       return EffectType::Circuit;
    if (k == "bounce")        return EffectType::Bounce;
    if (k == "split-wave" || k == "splitwave")
                              return EffectType::SplitWave;
    if (k == "plasma")        return EffectType::Plasma;
    return EffectType::Solid;
}

QString effectTypeToString(EffectType t) {
    switch (t) {
        case EffectType::Solid:        return "solid";
        case EffectType::Pulse:        return "pulse";
        case EffectType::Breathing:    return "breathing";
        case EffectType::Flicker:      return "flicker";
        case EffectType::Strobe:       return "strobe";
        case EffectType::Fade:         return "fade";
        case EffectType::Heartbeat:    return "heartbeat";
        case EffectType::Blink:        return "blink";
        case EffectType::TripleBlink:  return "triple-blink";
        case EffectType::SlowPulse:    return "slow-pulse";
        case EffectType::Warmup:       return "warmup";
        case EffectType::ReverseFade:  return "reverse-fade";
        case EffectType::Sparkle:      return "sparkle";
        case EffectType::Wave:         return "wave";
        case EffectType::Comet:        return "comet";
        case EffectType::RainbowCycle: return "rainbow-cycle";
        case EffectType::Circuit:      return "circuit";
        case EffectType::Bounce:       return "bounce";
        case EffectType::SplitWave:    return "split-wave";
        case EffectType::Plasma:       return "plasma";
    }
    return "solid";
}

QStringList allEffectNames() {
    return {
        "solid", "pulse", "breathing", "flicker", "strobe", "fade",
        "heartbeat", "blink", "triple-blink", "slow-pulse", "warmup", "reverse-fade",
        "sparkle", "wave", "comet", "rainbow-cycle", "circuit", "bounce",
        "split-wave", "plasma",
    };
}

bool effectIsPerZone(EffectType t) {
    switch (t) {
        case EffectType::Sparkle:
        case EffectType::Wave:
        case EffectType::Comet:
        case EffectType::RainbowCycle:
        case EffectType::Circuit:
        case EffectType::Bounce:
        case EffectType::SplitWave:
        case EffectType::Plasma:
            return true;
        default:
            return false;
    }
}
