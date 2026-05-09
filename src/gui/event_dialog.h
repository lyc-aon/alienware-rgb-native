#ifndef EVENT_DIALOG_H
#define EVENT_DIALOG_H

#include <QDialog>
#include <QList>

#include "models/event_preset.h"

class QVBoxLayout;
class QPushButton;
class EventCard;

#include <QCheckBox>
#include <QTimeEdit>

// Modal "Events" editor. Inline-card pattern. Save writes events.json.
// Live-preview button on each card invokes the CLI to actually fire the
// preset against hardware.
class EventDialog : public QDialog {
    Q_OBJECT
public:
    explicit EventDialog(QWidget* parent = nullptr);

private slots:
    void onAddEvent();
    void onSave();
    void onCardDeleted(EventCard* card);

private:
    QString cliBinaryPath() const;

    QVBoxLayout* cards_layout_ = nullptr;
    QList<EventCard*> cards_;

    // Voice controls
    QCheckBox* voice_enabled_check_ = nullptr;
    QCheckBox* voice_sched_check_ = nullptr;
    QTimeEdit* voice_start_time_ = nullptr;
    QTimeEdit* voice_end_time_ = nullptr;
};

#endif // EVENT_DIALOG_H
