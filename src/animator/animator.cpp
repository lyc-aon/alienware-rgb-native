#include "animator/animator.h"
#include "controller/awelc_controller.h"
#include "transport/hid_lock.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QThread>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

// Write one frame under a short-lived HID lock. If busy, the frame is
// skipped (next frame retries). Lets multiple animators coexist.
void lockedWrite(AWELCController& ctl, uint8_t r, uint8_t g, uint8_t b,
                 const std::vector<int>& zone_ids) {
    HIDLock lock(500);
    if (!lock.acquired()) return;
    ctl.setColorZonesFast(r, g, b, zone_ids);
}

// Batched per-zone write: one lock acquisition, multiple color batches inside.
// batches is a map of color → zone_ids for efficient HID dispatch.
void lockedWriteBatched(AWELCController& ctl,
                        const std::map<std::array<int, 3>, std::vector<int>>& batches) {
    HIDLock lock(500);
    if (!lock.acquired()) return;
    for (const auto& [rgb, ids] : batches) {
        ctl.setColorZonesFast(
            static_cast<uint8_t>(rgb[0]),
            static_cast<uint8_t>(rgb[1]),
            static_cast<uint8_t>(rgb[2]),
            ids);
    }
}

inline void scaleColor(uint8_t base_r, uint8_t base_g, uint8_t base_b,
                       double intensity,
                       uint8_t& out_r, uint8_t& out_g, uint8_t& out_b) {
    const double i = std::clamp(intensity, 0.0, 1.0);
    out_r = static_cast<uint8_t>(std::lround(base_r * i));
    out_g = static_cast<uint8_t>(std::lround(base_g * i));
    out_b = static_cast<uint8_t>(std::lround(base_b * i));
}

inline int intensityBucket(double intensity, int bucket_count) {
    return std::clamp(
        static_cast<int>(std::round(std::clamp(intensity, 0.0, 1.0) * bucket_count)),
        0,
        bucket_count);
}

inline void addScaledColor(std::map<std::array<int, 3>, std::vector<int>>& out,
                           int zone_id,
                           uint8_t base_r,
                           uint8_t base_g,
                           uint8_t base_b,
                           double intensity,
                           int bucket_count) {
    const int bucket = intensityBucket(intensity, bucket_count);
    const double q = static_cast<double>(bucket) / static_cast<double>(bucket_count);
    uint8_t r, g, b;
    scaleColor(base_r, base_g, base_b, q, r, g, b);
    out[{r, g, b}].push_back(zone_id);
}

// Cubic smoothstep — classic ease-in/out.
inline double smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// HSV → RGB. h in [0,1], s+v in [0,1].
void hsvToRgb(double h, double s, double v, uint8_t& r, uint8_t& g, uint8_t& b) {
    double i = std::floor(h * 6.0);
    double f = h * 6.0 - i;
    double p = v * (1.0 - s);
    double q = v * (1.0 - f * s);
    double t_ = v * (1.0 - (1.0 - f) * s);
    double rf, gf, bf;
    switch (static_cast<int>(i) % 6) {
        case 0: rf = v;  gf = t_; bf = p;  break;
        case 1: rf = q;  gf = v;  bf = p;  break;
        case 2: rf = p;  gf = v;  bf = t_; break;
        case 3: rf = p;  gf = q;  bf = v;  break;
        case 4: rf = t_; gf = p;  bf = v;  break;
        case 5: rf = v;  gf = p;  bf = q;  break;
        default: rf = gf = bf = 0.0;
    }
    r = static_cast<uint8_t>(std::lround(std::clamp(rf, 0.0, 1.0) * 255));
    g = static_cast<uint8_t>(std::lround(std::clamp(gf, 0.0, 1.0) * 255));
    b = static_cast<uint8_t>(std::lround(std::clamp(bf, 0.0, 1.0) * 255));
}

// ───────────────────────────────────── uniform-intensity waveforms ─────
// Sample intensity at fractional time t (0..1). Returns [0..1].
double sampleIntensity(EffectType type, double t) {
    switch (type) {
        case EffectType::Solid:
            return 1.0;

        case EffectType::Pulse: {
            const double cycles = 2.0;
            return 0.5 - 0.5 * std::cos(2.0 * kPi * cycles * t);
        }

        case EffectType::Breathing: {
            const double cycles = 1.0;
            return 0.5 - 0.5 * std::cos(2.0 * kPi * cycles * t);
        }

        case EffectType::SlowPulse: {
            const double cycles = 0.33;  // 3× slower than pulse
            return 0.5 - 0.5 * std::cos(2.0 * kPi * cycles * t);
        }

        case EffectType::Flicker: {
            if (t < 0.6) {
                return 0.3 + 0.7 * QRandomGenerator::global()->generateDouble();
            }
            const double u = (t - 0.6) / 0.4;
            return 0.7 + 0.3 * u;
        }

        case EffectType::Fade: {
            if (t < 0.2) return smoothstep(t / 0.2);
            if (t < 0.8) return 1.0;
            return smoothstep((1.0 - t) / 0.2);
        }

        case EffectType::Heartbeat: {
            // Two sharp gaussian pulses per ~1s cycle: bumps at 0.05 and 0.18,
            // wide quiet at 0.22–1.0. We'll fit by the duration via the
            // outer loop; here just use cycles of 1 s of wall clock.
            // Approximate with a 4-Hz pattern: bump..bump..rest
            auto gauss = [](double x, double mu, double sigma) {
                double d = (x - mu) / sigma;
                return std::exp(-0.5 * d * d);
            };
            double cycle = std::fmod(t, 0.33333);   // 3 beats across duration
            double rel = cycle / 0.33333;
            return std::min(1.0, gauss(rel, 0.10, 0.04) + gauss(rel, 0.28, 0.05));
        }

        case EffectType::Blink: {
            return (t < 0.3) ? 1.0 : 0.0;
        }

        case EffectType::TripleBlink: {
            // 3 blinks in first 50% of duration, hold for second 50%.
            if (t >= 0.5) return 1.0;
            const double blink_cycle = std::fmod(t * 6.0, 1.0);  // 3 cycles in first half
            return blink_cycle < 0.5 ? 1.0 : 0.0;
        }

        case EffectType::Warmup: {
            if (t < 0.4) return smoothstep(t / 0.4);
            if (t < 0.8) return 1.0;
            return smoothstep((1.0 - t) / 0.2);
        }

        case EffectType::ReverseFade: {
            // Full intensity at t=0, smooth fade to 0 at t=1.
            return smoothstep(1.0 - t);
        }

        default:
            return 1.0;
    }
}

// ─────────────────────────────── per-zone waveform composition ─────────
// Returns a map of color → zone ids for this frame. Quantized so HID
// writes stay bounded (max 8 unique colors per frame for RainbowCycle).
std::map<std::array<int, 3>, std::vector<int>>
samplePerZoneFrame(EffectType type, double t, const AnimContext& ctx) {
    std::map<std::array<int, 3>, std::vector<int>> out;
    const int n = static_cast<int>(ctx.zone_ids.size());
    if (n == 0) return out;

    switch (type) {
        case EffectType::Sparkle: {
            // Each zone has an independent random phase. Intensity = exp(-decay *
            // (t - phase_i) mod 1) so each zone pulses once per full cycle.
            for (int i = 0; i < n; ++i) {
                // Deterministic per-zone phase from zone index (stable across frames).
                const double phase = std::fmod((ctx.zone_ids[i] * 0.6180339887), 1.0);
                const double rel = std::fmod(t - phase + 1.0, 1.0);
                const double intensity = std::exp(-rel * 4.0);
                // Quantize to 5 buckets to bound unique colors per frame.
                const int bucket = std::clamp(static_cast<int>(std::round(intensity * 4)), 0, 4);
                const double q = bucket / 4.0;
                uint8_t r, g, b;
                scaleColor(ctx.base_r, ctx.base_g, ctx.base_b, q, r, g, b);
                out[{r, g, b}].push_back(ctx.zone_ids[i]);
            }
            return out;
        }

        case EffectType::Wave: {
            // Gaussian bump traveling across zone indices (by position in the
            // ordered zone_ids array, not by zone_id value).
            const double head = t * n;   // head zone index
            const double sigma = std::max(2.0, n * 0.08);
            for (int i = 0; i < n; ++i) {
                const double d = i - head;
                const double intensity = std::exp(-0.5 * (d / sigma) * (d / sigma));
                const int bucket = std::clamp(static_cast<int>(std::round(intensity * 5)), 0, 5);
                const double q = bucket / 5.0;
                uint8_t r, g, b;
                scaleColor(ctx.base_r, ctx.base_g, ctx.base_b, q, r, g, b);
                out[{r, g, b}].push_back(ctx.zone_ids[i]);
            }
            return out;
        }

        case EffectType::Comet: {
            // Head at t*n, trail = 6 zones back with exponential fade.
            const double head = t * n;
            const double trail_len = 6.0;
            for (int i = 0; i < n; ++i) {
                const double d = head - i;
                double intensity;
                if (d < 0) intensity = 0.0;
                else intensity = std::exp(-d / trail_len);
                const int bucket = std::clamp(static_cast<int>(std::round(intensity * 5)), 0, 5);
                const double q = bucket / 5.0;
                uint8_t r, g, b;
                scaleColor(ctx.base_r, ctx.base_g, ctx.base_b, q, r, g, b);
                out[{r, g, b}].push_back(ctx.zone_ids[i]);
            }
            return out;
        }

        case EffectType::RainbowCycle: {
            // Each zone gets its own hue, offset by zone position; hue rotates
            // over time. Quantize to 8 hue buckets for bounded HID writes.
            for (int i = 0; i < n; ++i) {
                double hue = std::fmod(static_cast<double>(i) / n + t, 1.0);
                // Quantize hue to 8 buckets.
                int bucket = static_cast<int>(std::floor(hue * 8)) % 8;
                double q_hue = bucket / 8.0;
                uint8_t r, g, b;
                hsvToRgb(q_hue, 1.0, 1.0, r, g, b);
                out[{r, g, b}].push_back(ctx.zone_ids[i]);
            }
            return out;
        }

        case EffectType::Circuit: {
            // Three chase heads loop continuously around the ordered zones.
            // This is the side-panel "circuit trace" effect: bright heads with
            // short fading tails, wrapping cleanly from last zone back to first.
            constexpr int kHeads = 3;
            constexpr double kTrailLen = 5.0;
            const double travel = t * n * 1.55;
            for (int i = 0; i < n; ++i) {
                double intensity = 0.0;
                for (int h = 0; h < kHeads; ++h) {
                    const double head = std::fmod(travel + (n * h / static_cast<double>(kHeads)), n);
                    const double d = std::fmod(head - i + n, n);
                    intensity = std::max(intensity, std::exp(-d / kTrailLen));
                }
                addScaledColor(out, ctx.zone_ids[i], ctx.base_r, ctx.base_g, ctx.base_b, intensity, 6);
            }
            return out;
        }

        case EffectType::Bounce: {
            // One hot head scans the ordered list and bounces off both ends.
            // The reflected position makes mapping mistakes near endpoints obvious.
            const double phase = std::fmod(t * 2.0, 1.0);
            const double path = (phase < 0.5) ? (phase * 2.0) : ((1.0 - phase) * 2.0);
            const double head = path * (n - 1);
            const double sigma = std::max(2.0, n * 0.045);
            for (int i = 0; i < n; ++i) {
                const double d = std::abs(i - head);
                const double intensity = std::exp(-0.5 * (d / sigma) * (d / sigma));
                addScaledColor(out, ctx.zone_ids[i], ctx.base_r, ctx.base_g, ctx.base_b, intensity, 6);
            }
            return out;
        }

        case EffectType::SplitWave: {
            // Mirrored pulses expand from the center, hit the ends, then collapse.
            // It reads well on the two-column side panel because both halves move
            // in opposite physical directions.
            const double midpoint = (n - 1) * 0.5;
            const double phase = 0.5 - 0.5 * std::cos(2.0 * kPi * 2.0 * t);
            const double radius = smoothstep(phase) * midpoint;
            const double left = midpoint - radius;
            const double right = midpoint + radius;
            const double sigma = std::max(2.0, n * 0.04);
            for (int i = 0; i < n; ++i) {
                const double dl = std::abs(i - left);
                const double dr = std::abs(i - right);
                const double intensity = std::max(
                    std::exp(-0.5 * (dl / sigma) * (dl / sigma)),
                    std::exp(-0.5 * (dr / sigma) * (dr / sigma)));
                addScaledColor(out, ctx.zone_ids[i], ctx.base_r, ctx.base_g, ctx.base_b, intensity, 6);
            }
            return out;
        }

        case EffectType::Plasma: {
            // Rotating hue bands with a sine wobble. Hue is quantized to eight
            // buckets so each frame stays bounded to at most eight HID batches.
            for (int i = 0; i < n; ++i) {
                const double pos = static_cast<double>(i) / static_cast<double>(n);
                const double wobble = std::sin(2.0 * kPi * (pos * 4.0 - t * 1.8));
                double hue = std::fmod(pos * 0.7 + t * 0.9 + wobble * 0.08 + 1.0, 1.0);
                const int bucket = static_cast<int>(std::floor(hue * 8.0)) % 8;
                hue = bucket / 8.0;
                uint8_t r, g, b;
                hsvToRgb(hue, 1.0, 1.0, r, g, b);
                out[{r, g, b}].push_back(ctx.zone_ids[i]);
            }
            return out;
        }

        default:
            return out;
    }
}

}  // namespace

void runEffect(EffectType type, const AnimContext& ctx, AWELCController& ctl) {
    if (ctx.zone_ids.empty() || ctx.duration_ms <= 0) return;

    // Tick rate. Fast-path writes let 60 Hz small / 30 Hz all-zones work.
    // Per-zone effects might do 5-8 HID writes per frame → throttle to 25 Hz.
    int tick_hz;
    if (effectIsPerZone(type)) {
        tick_hz = 25;
    } else {
        tick_hz = (ctx.zone_ids.size() <= 10) ? 60 : 30;
    }
    const int frame_ms = 1000 / tick_hz;

    QElapsedTimer timer;
    timer.start();

    // ── Fast-path solid: single write, sleep the whole duration.
    if (type == EffectType::Solid) {
        lockedWrite(ctl, ctx.base_r, ctx.base_g, ctx.base_b, ctx.zone_ids);
        const qint64 remaining = ctx.duration_ms - timer.elapsed();
        if (remaining > 0) QThread::msleep(static_cast<unsigned long>(remaining));
        return;
    }

    // ── Strobe wall-clock toggle.
    if (type == EffectType::Strobe) {
        bool on = true;
        const int strobe_half_ms = 60;  // ~8 Hz
        while (timer.elapsed() < ctx.duration_ms) {
            if (on) lockedWrite(ctl, ctx.base_r, ctx.base_g, ctx.base_b, ctx.zone_ids);
            else    lockedWrite(ctl, 0, 0, 0, ctx.zone_ids);
            on = !on;
            QThread::msleep(strobe_half_ms);
        }
        return;
    }

    // ── Per-zone effects.
    if (effectIsPerZone(type)) {
        while (timer.elapsed() < ctx.duration_ms) {
            const qint64 frame_start = timer.elapsed();
            const double t = static_cast<double>(frame_start) / static_cast<double>(ctx.duration_ms);
            auto batches = samplePerZoneFrame(type, t, ctx);
            lockedWriteBatched(ctl, batches);
            const qint64 elapsed_this_frame = timer.elapsed() - frame_start;
            const qint64 sleep_ms = frame_ms - elapsed_this_frame;
            if (sleep_ms > 0) QThread::msleep(static_cast<unsigned long>(sleep_ms));
        }
        return;
    }

    // ── Uniform-intensity waveform loop.
    while (timer.elapsed() < ctx.duration_ms) {
        const qint64 frame_start = timer.elapsed();
        const double t = static_cast<double>(frame_start) / static_cast<double>(ctx.duration_ms);
        const double intensity = sampleIntensity(type, t);

        uint8_t r, g, b;
        scaleColor(ctx.base_r, ctx.base_g, ctx.base_b, intensity, r, g, b);
        lockedWrite(ctl, r, g, b, ctx.zone_ids);

        const qint64 elapsed_this_frame = timer.elapsed() - frame_start;
        const qint64 sleep_ms = frame_ms - elapsed_this_frame;
        if (sleep_ms > 0) QThread::msleep(static_cast<unsigned long>(sleep_ms));
    }
}
