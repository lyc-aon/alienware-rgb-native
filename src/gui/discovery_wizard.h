#ifndef DISCOVERY_WIZARD_H
#define DISCOVERY_WIZARD_H

#include <QDialog>

#include "models/zone_map.h"
#include "worker/hid_worker.h"

class QLabel;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QProgressBar;
class QPushButton;

// Modal wizard that walks every zone 0..zone_count-1, lights each in cyan,
// and lets the user label + group + mark-visible. C++ port of the PySide6
// DiscoveryWizard at ~/dev/hardware/AlienwareRGB/gui/discovery_wizard.py.
class DiscoveryWizard : public QDialog {
    Q_OBJECT
public:
    DiscoveryWizard(ZoneMap* zone_map, HIDWorker* worker, QWidget* parent = nullptr);

signals:
    void zoneDiscovered(int zone_id);
    void discoveryComplete();

private slots:
    void onNext();
    void onPrev();
    void onSkip();
    void onSameAsPrevious();
    void onRetest();
    void onFlashAllOff();
    void onSaveAndClose();
    void onJumpToZone();

private:
    void setupUI();
    void refreshDisplay();
    void saveCurrentInputs();
    void lightCurrentZone();
    void populateGroupCombo();

    ZoneMap* zone_map_ = nullptr;
    HIDWorker* worker_ = nullptr;
    int current_zone_ = 0;
    int total_zones_ = 0;
    static constexpr uint8_t kTestR = 0;
    static constexpr uint8_t kTestG = 240;
    static constexpr uint8_t kTestB = 255;

    // UI
    QLabel* zone_id_label_ = nullptr;
    QLabel* progress_label_ = nullptr;
    QProgressBar* progress_bar_ = nullptr;
    QLineEdit* label_input_ = nullptr;
    QComboBox* group_combo_ = nullptr;
    QCheckBox* active_check_ = nullptr;
    QPushButton* prev_btn_ = nullptr;
    QPushButton* next_btn_ = nullptr;
};

#endif // DISCOVERY_WIZARD_H
