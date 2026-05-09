#ifndef EVENT_PRESET_H
#define EVENT_PRESET_H

#include <QString>
#include <QStringList>
#include <QList>
#include <array>

#include "animator/effects.h"

// One named event. Fired by `alienware_rgb_cli fire-event <id>` or via the
// GUI's Events editor. Persisted inside events.json at
//   ~/.config/alienware-rgb/events.json
//
// Fields:
//   id           — stable dashed-lowercase id (unique within the registry)
//   name         — display label
//   zones_spec   — same parser as apply-color (`all` / `group:<name>` / id list)
//   rgb          — base color, modulated by the effect
//   effect       — one of allEffectNames()
//   duration_sec — how long the animation runs (and the flash window)
struct EventPreset {
    QString id;
    QString name;
    QString zones_spec;
    std::array<int, 3> rgb = {255, 255, 255};
    QString effect = "pulse";
    int duration_sec = 2;

    QString summary() const;
};

QString eventsFilePath();                      // ~/.config/alienware-rgb/events.json
bool loadEventPresets(QList<EventPreset>& out);
bool saveEventPresets(const QList<EventPreset>& presets);
EventPreset* findEventById(QList<EventPreset>& presets, const QString& id);

// Seed with sensible defaults the first time (if the file doesn't exist).
// Called by list-events / fire-event / the GUI on first open. Non-destructive
// if the file already exists.
void seedDefaultEventsIfMissing();

#endif // EVENT_PRESET_H
