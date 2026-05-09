#include "gui/discovery_wizard.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QPushButton>
#include <QGroupBox>
#include <QInputDialog>
#include <QTimer>

#include <algorithm>
#include <set>

namespace {

// Matches PySide6 GROUP_PRESETS (order preserved, empty sentinel excluded —
// we'll surface "(none)" explicitly).
const QStringList kGroupPresets = {
    "Front Panel Strip",
    "Alien Head",
    "CPU Cooler",
    "Interior Fans",
    "Rear Panel",
    "Top Panel",
    "Bottom Panel",
    "Motherboard",
    "GPU Area",
    "RAM Area",
};

}  // namespace

DiscoveryWizard::DiscoveryWizard(ZoneMap* zone_map, HIDWorker* worker, QWidget* parent)
    : QDialog(parent), zone_map_(zone_map), worker_(worker) {
    total_zones_ = zone_map_->zoneCount();
    current_zone_ = std::clamp(zone_map_->lastDiscoveredZone() + 1, 0, std::max(0, total_zones_ - 1));

    setWindowTitle("Zone Discovery");
    setMinimumWidth(520);
    setMinimumHeight(440);
    setModal(true);
    setStyleSheet(
        "QDialog { background: #050505; }"
        "QLabel { color: #F4F4F5; }"
        "QGroupBox { color: #A1A1AA; font-size: 11px; font-weight: 600; border: 1px solid #27272A; "
        "border-radius: 8px; margin-top: 11px; padding: 17px 10px 10px 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 8px; }"
        "QLineEdit, QComboBox { background: #161616; color: #F4F4F5; padding: 6px 9px; border: 1px solid #303033; border-radius: 8px; }"
        "QLineEdit:focus, QComboBox:focus { border-color: #5B7CFA; }"
        "QProgressBar { background: #161616; color: #A1A1AA; border: 1px solid #27272A; border-radius: 8px; padding: 1px; text-align: center; }"
        "QProgressBar::chunk { background: #A7B4FF; border-radius: 6px; }"
        "QPushButton { background: #18181B; color: #F4F4F5; border: 1px solid #303033; padding: 7px 11px; border-radius: 8px; font-weight: 500; }"
        "QPushButton:hover { background: #222225; border-color: #3F3F46; }");

    setupUI();
    refreshDisplay();
}

void DiscoveryWizard::setupUI() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(18, 18, 18, 18);

    auto* heading = new QLabel("Zone discovery", this);
    heading->setAlignment(Qt::AlignCenter);
    heading->setStyleSheet("color: #F4F4F5; font-weight: 650; font-size: 18px;");
    root->addWidget(heading);

    auto* desc = new QLabel(
        "Each zone will light up in cyan one at a time.\n"
        "Look at your case and label what's glowing.\n"
        "Mark zones as inactive if nothing visible changes.", this);
    desc->setAlignment(Qt::AlignCenter);
    desc->setWordWrap(true);
    desc->setStyleSheet("color: #A1A1AA;");
    root->addWidget(desc);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, std::max(1, total_zones_ - 1));
    root->addWidget(progress_bar_);

    progress_label_ = new QLabel(this);
    progress_label_->setAlignment(Qt::AlignCenter);
    progress_label_->setStyleSheet("color: #A1A1AA;");
    root->addWidget(progress_label_);

    // Current zone group box
    auto* box = new QGroupBox("Current Zone", this);
    auto* bl = new QVBoxLayout(box);
    bl->setSpacing(8);

    zone_id_label_ = new QLabel(box);
    zone_id_label_->setAlignment(Qt::AlignCenter);
    zone_id_label_->setStyleSheet("font-size: 28px; font-weight: 650; color: #A7B4FF;");
    bl->addWidget(zone_id_label_);

    auto* label_row = new QHBoxLayout();
    label_row->addWidget(new QLabel("Label:", box));
    label_input_ = new QLineEdit(box);
    label_input_->setPlaceholderText("e.g. Front strip segment 5");
    label_row->addWidget(label_input_, 1);
    bl->addLayout(label_row);

    auto* group_row = new QHBoxLayout();
    group_row->addWidget(new QLabel("Group:", box));
    group_combo_ = new QComboBox(box);
    group_combo_->setEditable(true);
    populateGroupCombo();
    group_row->addWidget(group_combo_, 1);
    bl->addLayout(group_row);

    active_check_ = new QCheckBox("Zone is visible (something lit up)", box);
    active_check_->setChecked(true);
    bl->addWidget(active_check_);

    root->addWidget(box);

    // Quick actions row
    auto* quick = new QHBoxLayout();
    auto* same_btn = new QPushButton("Same as previous", this);
    same_btn->setToolTip("Copy label and group from the previous zone");
    connect(same_btn, &QPushButton::clicked, this, &DiscoveryWizard::onSameAsPrevious);
    quick->addWidget(same_btn);

    auto* retest_btn = new QPushButton("Re-test", this);
    retest_btn->setToolTip("Flash this zone again");
    connect(retest_btn, &QPushButton::clicked, this, &DiscoveryWizard::onRetest);
    quick->addWidget(retest_btn);

    auto* flash_btn = new QPushButton("Flash all off", this);
    flash_btn->setToolTip("Turn everything off briefly for contrast");
    connect(flash_btn, &QPushButton::clicked, this, &DiscoveryWizard::onFlashAllOff);
    quick->addWidget(flash_btn);

    root->addLayout(quick);

    // Navigation row
    auto* nav = new QHBoxLayout();
    prev_btn_ = new QPushButton("< Previous", this);
    connect(prev_btn_, &QPushButton::clicked, this, &DiscoveryWizard::onPrev);
    nav->addWidget(prev_btn_);

    nav->addStretch();

    auto* skip_btn = new QPushButton("Skip inactive", this);
    connect(skip_btn, &QPushButton::clicked, this, &DiscoveryWizard::onSkip);
    nav->addWidget(skip_btn);

    next_btn_ = new QPushButton("Next >", this);
    next_btn_->setStyleSheet(
        "QPushButton { background: #E8EAFF; color: #111113; border: 1px solid #FFFFFF; font-weight: 600; padding: 7px 14px; border-radius: 8px; }"
        "QPushButton:hover { background: #FFFFFF; }");
    connect(next_btn_, &QPushButton::clicked, this, &DiscoveryWizard::onNext);
    nav->addWidget(next_btn_);

    root->addLayout(nav);

    // Bottom row
    auto* bottom = new QHBoxLayout();
    auto* save_btn = new QPushButton("Save and close", this);
    connect(save_btn, &QPushButton::clicked, this, &DiscoveryWizard::onSaveAndClose);
    bottom->addWidget(save_btn);

    bottom->addStretch();

    auto* jump_btn = new QPushButton("Jump to zone", this);
    connect(jump_btn, &QPushButton::clicked, this, &DiscoveryWizard::onJumpToZone);
    bottom->addWidget(jump_btn);

    root->addLayout(bottom);
}

void DiscoveryWizard::populateGroupCombo() {
    std::set<QString> combined;
    for (const QString& p : kGroupPresets) combined.insert(p);
    for (const std::string& g : zone_map_->groupNames()) combined.insert(QString::fromStdString(g));

    group_combo_->clear();
    group_combo_->addItem("(none)", QString());
    for (const QString& g : combined) {
        group_combo_->addItem(g, g);
    }
}

void DiscoveryWizard::refreshDisplay() {
    progress_bar_->setValue(current_zone_);
    progress_label_->setText(QString("Zone %1 of %2").arg(current_zone_).arg(total_zones_ - 1));
    zone_id_label_->setText(QString("Zone %1").arg(current_zone_));

    // Refresh group combo in case new groups were added meanwhile.
    populateGroupCombo();

    if (const Zone* z = zone_map_->getZone(current_zone_)) {
        label_input_->setText(QString::fromStdString(z->label));
        const QString g = QString::fromStdString(z->group);
        if (g.isEmpty()) {
            group_combo_->setCurrentIndex(0);
        } else {
            const int idx = group_combo_->findText(g);
            if (idx >= 0) group_combo_->setCurrentIndex(idx);
            else group_combo_->setEditText(g);
        }
        active_check_->setChecked(z->active);
    } else {
        label_input_->clear();
        group_combo_->setCurrentIndex(0);
        active_check_->setChecked(true);
    }

    prev_btn_->setEnabled(current_zone_ > 0);
    next_btn_->setText(current_zone_ >= total_zones_ - 1 ? QStringLiteral("Finish") : QStringLiteral("Next >"));

    lightCurrentZone();
}

void DiscoveryWizard::lightCurrentZone() {
    const int zone_id = current_zone_;
    // Queue on the HID worker so it serializes with other writes.
    worker_->enqueue([this, zone_id] {
        worker_->controller().setZoneColor(zone_id, kTestR, kTestG, kTestB);
    });
}

void DiscoveryWizard::saveCurrentInputs() {
    Zone* z = zone_map_->getZone(current_zone_);
    if (!z) return;
    z->label = label_input_->text().trimmed().toStdString();
    const QString g_text = group_combo_->currentText().trimmed();
    z->group = (g_text == QStringLiteral("(none)")) ? std::string() : g_text.toStdString();
    z->active = active_check_->isChecked();
    if (z->active) { z->r = kTestR; z->g = kTestG; z->b = kTestB; }
    else           { z->r = 0; z->g = 0; z->b = 0; }

    zone_map_->setLastDiscoveredZone(std::max(zone_map_->lastDiscoveredZone(), current_zone_));
    emit zoneDiscovered(current_zone_);
}

void DiscoveryWizard::onNext() {
    saveCurrentInputs();
    if (current_zone_ >= total_zones_ - 1) {
        zone_map_->setDiscoveryComplete(true);
        emit discoveryComplete();
        accept();
        return;
    }
    ++current_zone_;
    refreshDisplay();
}

void DiscoveryWizard::onPrev() {
    saveCurrentInputs();
    if (current_zone_ > 0) {
        --current_zone_;
        refreshDisplay();
    }
}

void DiscoveryWizard::onSkip() {
    active_check_->setChecked(false);
    label_input_->clear();
    onNext();
}

void DiscoveryWizard::onSameAsPrevious() {
    if (current_zone_ <= 0) return;
    const Zone* prev = zone_map_->getZone(current_zone_ - 1);
    if (!prev) return;
    label_input_->setText(QString::fromStdString(prev->label));
    const QString g = QString::fromStdString(prev->group);
    if (g.isEmpty()) {
        group_combo_->setCurrentIndex(0);
    } else {
        const int idx = group_combo_->findText(g);
        if (idx >= 0) group_combo_->setCurrentIndex(idx);
        else group_combo_->setEditText(g);
    }
}

void DiscoveryWizard::onRetest() { lightCurrentZone(); }

void DiscoveryWizard::onFlashAllOff() {
    worker_->enqueue([this] { worker_->controller().allOff(zone_map_->zoneCount()); });
    QTimer::singleShot(1500, this, [this] { lightCurrentZone(); });
}

void DiscoveryWizard::onSaveAndClose() {
    saveCurrentInputs();
    accept();
}

void DiscoveryWizard::onJumpToZone() {
    bool ok = false;
    const int target = QInputDialog::getInt(
        this, "Jump to Zone", "Zone ID:", current_zone_, 0, total_zones_ - 1, 1, &ok);
    if (!ok) return;
    saveCurrentInputs();
    current_zone_ = target;
    refreshDisplay();
}
