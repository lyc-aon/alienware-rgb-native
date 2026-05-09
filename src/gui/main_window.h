#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QLineEdit>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include "protocol.h"
#include "models/zone_map.h"
#include "worker/hid_worker.h"
#include "gui/zone_list_model.h"
#include "gui/zone_filter_proxy.h"
#include "gui/zone_accordion.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onConnected(const QString& firmware, uint16_t platform, int zone_count);
    void onDisconnected(const QString& reason);
    void onError(const QString& message);
    void onBrightnessChanged(int value);
    void onColorSliderChanged(int value);
    void onOpenDiscoveryWizard();
    void onOpenEvents();
    void onOpenDiagnostics();

    void onSaveProfile();
    void onLoadProfile();
    void onDeleteProfile();

private:
    void setupUI();
    void applyColorToSelected(uint8_t r, uint8_t g, uint8_t b);
    void applyColorToAll(uint8_t r, uint8_t g, uint8_t b);
    void applyColorToHardware(uint8_t r, uint8_t g, uint8_t b, const std::vector<int>& zone_ids);
    void assignGroupToSelected(const QString& group_name);
    void clearGroupFromSelected();
    void refreshGroupCombo();
    void refreshProfileCombo();
    std::tuple<uint8_t, uint8_t, uint8_t> effectiveRgb(uint8_t r, uint8_t g, uint8_t b);

    ZoneMap zone_map_;
    std::unique_ptr<HIDWorker> worker_;
    std::unique_ptr<ZoneListModel> zone_model_;

    QLabel* status_label_ = nullptr;
    ZoneAccordion* accordion_ = nullptr;

    QSlider* r_slider_ = nullptr;
    QSlider* g_slider_ = nullptr;
    QSlider* b_slider_ = nullptr;

    QLabel* r_value_label_ = nullptr;
    QLabel* g_value_label_ = nullptr;
    QLabel* b_value_label_ = nullptr;

    QLineEdit* hex_input_ = nullptr;
    QLabel* color_preview_ = nullptr;
    QSlider* brightness_slider_ = nullptr;
    QLabel* bright_label_ = nullptr;

    QComboBox* group_combo_ = nullptr;
    QComboBox* profile_combo_ = nullptr;

    int pending_brightness_ = 100;
    int zone_count_ = 0;
};

#endif // MAIN_WINDOW_H
