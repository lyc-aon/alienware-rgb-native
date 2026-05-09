#include "gui/main_window.h"
#include "gui/zone_list_model.h"
#include "gui/zone_accordion.h"
#include "gui/discovery_wizard.h"
#include "gui/event_dialog.h"
#include "gui/diagnostics_dialog.h"
#include "models/profile.h"
#include "models/config_paths.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QMessageBox>
#include <QApplication>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDockWidget>
#include <QToolBar>
#include <QAction>
#include <QShortcut>
#include <QKeySequence>
#include <QDir>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QIcon>

#include <algorithm>

namespace {
QString configPath() {
    return alienwareConfigPath("zone_map.json");
}

constexpr const char* kGroupBoxStyle =
    "QGroupBox { color: #94A3B8; font-size: 11px; font-weight: 700; letter-spacing: 1px; "
    "border: 1px solid #1B2636; border-radius: 6px; margin-top: 10px; padding: 16px 10px 10px 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 6px; }";

constexpr const char* kSecondaryButtonStyle =
    "QPushButton { background: #111827; color: #E5E7EB; border: 1px solid #263244; "
    "padding: 7px 10px; border-radius: 6px; font-weight: 600; }"
    "QPushButton:hover { background: #18212F; border-color: #334155; }";

constexpr const char* kPrimaryButtonStyle =
    "QPushButton { background: #0E7490; color: #F8FAFC; border: 1px solid #22D3EE; "
    "padding: 7px 14px; border-radius: 6px; font-weight: 700; }"
    "QPushButton:hover { background: #0891B2; }";

constexpr const char* kSuccessButtonStyle =
    "QPushButton { background: #16A34A; color: #F8FAFC; border: 1px solid #4ADE80; "
    "font-weight: 700; padding: 8px 10px; border-radius: 6px; }"
    "QPushButton:hover { background: #15803D; }";

constexpr const char* kInputStyle =
    "QComboBox, QLineEdit { background: #0B0F14; color: #F8FAFC; padding: 5px 8px; "
    "border: 1px solid #263244; border-radius: 5px; }"
    "QComboBox:focus, QLineEdit:focus { border-color: #22D3EE; }";

QString statusStyle(const QString& background, const QString& border = QStringLiteral("#263244")) {
    return QString("font-size: 13px; padding: 6px 12px; background: %1; color: #F8FAFC; "
                   "border: 1px solid %2; border-radius: 6px;")
        .arg(background, border);
}

// Small preset palette for the swatch row.
struct PresetColor { const char* name; int r, g, b; };
constexpr PresetColor kPresets[] = {
    {"Off",     0,   0,   0},
    {"White",   255, 255, 255},
    {"Red",     255, 0,   0},
    {"Green",   0,   200, 80},
    {"Blue",    0,   120, 255},
    {"Cyan",    0,   240, 255},
    {"Magenta", 255, 0,   200},
    {"Amber",   255, 160, 0},
};
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setWindowTitle("Alienware RGB Controller");
    setMinimumSize(1100, 720);

    // Load persisted zone map (falls back to flat default if missing/malformed).
    const QString cfg = configPath();
    qInfo() << "[main] config path =" << cfg;
    if (!zone_map_.load(cfg.toStdString())) {
        qWarning() << "[main] zone_map load failed, using default";
        zone_map_ = ZoneMap::createDefault(101);
    }

    worker_ = std::make_unique<HIDWorker>(this);
    connect(worker_.get(), &HIDWorker::connected, this, &MainWindow::onConnected);
    connect(worker_.get(), &HIDWorker::disconnected, this, &MainWindow::onDisconnected);
    connect(worker_.get(), &HIDWorker::error, this, &MainWindow::onError);

    worker_->start();

    zone_count_ = DEFAULT_ZONE_COUNT;
    pending_brightness_ = 100;

    setupUI();

    QTimer::singleShot(500, this, [this] { worker_->connectDevice(); });
}

MainWindow::~MainWindow() {
    const QString cfg = configPath();
    if (!zone_map_.save(cfg.toStdString())) {
        qWarning() << "[main] zone_map save on exit failed, path =" << cfg;
    }
    if (worker_) worker_->stop();
}

// ─────────────────────────────────────────────────────────────── setupUI

void MainWindow::setupUI() {
    // ── Top toolbar (status + discover button)
    auto* toolbar = addToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setStyleSheet(
        "QToolBar { background: #05070A; border: none; border-bottom: 1px solid #18212F; padding: 6px 10px; } "
        "QToolBar QLabel { color: #E5E7EB; }"
    );

    auto* brand = new QWidget(toolbar);
    brand->setStyleSheet("background: transparent;");
    auto* brand_layout = new QHBoxLayout(brand);
    brand_layout->setContentsMargins(0, 0, 0, 0);
    brand_layout->setSpacing(9);

    auto* icon_label = new QLabel(brand);
    icon_label->setFixedSize(28, 28);
    icon_label->setPixmap(QIcon(QStringLiteral(":/icons/alienware-rgb.png")).pixmap(28, 28));
    brand_layout->addWidget(icon_label);

    auto* title = new QLabel("ALIENWARE RGB", brand);
    title->setStyleSheet("color: #22D3EE; font-size: 17px; font-weight: 700; letter-spacing: 1px; background: transparent;");
    brand_layout->addWidget(title);
    toolbar->addWidget(brand);

    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    status_label_ = new QLabel("Disconnected", toolbar);
    status_label_->setStyleSheet(statusStyle(QStringLiteral("#111827")));
    toolbar->addWidget(status_label_);

    toolbar->addSeparator();

    auto* mute_btn = new QPushButton("Mute Voice", toolbar);
    mute_btn->setCheckable(true);
    mute_btn->setStyleSheet(
        "QPushButton { background: #111827; color: #E5E7EB; border: 1px solid #263244; padding: 6px 14px; border-radius: 6px; font-weight: 600; }"
        "QPushButton:hover { background: #18212F; }"
        "QPushButton:checked { background: #7F1D1D; border-color: #F87171; color: #F8FAFC; }");
    // Read current voice state
    {
        const QString vpath = alienwareConfigPath("voice.json");
        QFile vf(vpath);
        if (vf.open(QIODevice::ReadOnly)) {
            const auto vobj = QJsonDocument::fromJson(vf.readAll()).object();
            vf.close();
            mute_btn->setChecked(!vobj.value("enabled").toBool(true));
            mute_btn->setText(mute_btn->isChecked() ? "Voice Muted" : "Mute Voice");
        }
    }
    connect(mute_btn, &QPushButton::toggled, this, [mute_btn](bool muted) {
        mute_btn->setText(muted ? "Voice Muted" : "Mute Voice");
        const QString vpath = alienwareConfigPath("voice.json");
        QFile vf(vpath);
        if (vf.open(QIODevice::ReadOnly)) {
            auto vobj = QJsonDocument::fromJson(vf.readAll()).object();
            vf.close();
            vobj["enabled"] = !muted;
            QFile wf(vpath);
            if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                wf.write(QJsonDocument(vobj).toJson(QJsonDocument::Indented));
                wf.close();
            }
        }
    });
    toolbar->addWidget(mute_btn);

    auto* events_btn = new QPushButton("Events…", toolbar);
    events_btn->setStyleSheet(kSecondaryButtonStyle);
    connect(events_btn, &QPushButton::clicked, this, &MainWindow::onOpenEvents);
    toolbar->addWidget(events_btn);

    auto* discover_btn = new QPushButton("Discover Zones…", toolbar);
    discover_btn->setStyleSheet(kPrimaryButtonStyle);
    connect(discover_btn, &QPushButton::clicked, this, &MainWindow::onOpenDiscoveryWizard);
    toolbar->addWidget(discover_btn);

    auto* discover_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
    connect(discover_shortcut, &QShortcut::activated, this, &MainWindow::onOpenDiscoveryWizard);

    auto* events_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), this);
    connect(events_shortcut, &QShortcut::activated, this, &MainWindow::onOpenEvents);

    toolbar->addSeparator();

    auto* diagnostics_btn = new QPushButton("Diagnostics…", toolbar);
    diagnostics_btn->setStyleSheet(kSecondaryButtonStyle);
    connect(diagnostics_btn, &QPushButton::clicked, this, &MainWindow::onOpenDiagnostics);
    toolbar->addWidget(diagnostics_btn);

    auto* diagnostics_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H), this);
    connect(diagnostics_shortcut, &QShortcut::activated, this, &MainWindow::onOpenDiagnostics);

    // ── Left sidebar dock
    auto* dock = new QDockWidget("Controls", this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea);
    dock->setTitleBarWidget(new QWidget(dock));  // hide dock title bar

    auto* sidebar_scroll = new QScrollArea(dock);
    sidebar_scroll->setWidgetResizable(true);
    sidebar_scroll->setFrameShape(QFrame::NoFrame);
    sidebar_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* sidebar = new QWidget(sidebar_scroll);
    sidebar->setFixedWidth(260);
    sidebar->setStyleSheet("background: #080C12; color: #E5E7EB;");
    auto* sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(12, 12, 12, 12);
    sidebar_layout->setSpacing(14);

    // ── COLOR section
    auto* color_box = new QGroupBox("COLOR", sidebar);
    color_box->setStyleSheet(kGroupBoxStyle);
    auto* color_layout = new QVBoxLayout(color_box);
    color_layout->setSpacing(8);

    auto makeSliderRow = [this, color_layout](const QString& name, QSlider*& slider, QLabel*& value_lbl, QColor tint) {
        auto* row = new QHBoxLayout();
        auto* label = new QLabel(name);
        label->setFixedWidth(16);
        label->setStyleSheet(QString("color: %1; font-weight: 700;").arg(tint.name()));
        row->addWidget(label);
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 255);
        slider->setValue(128);
        connect(slider, &QSlider::valueChanged, this, &MainWindow::onColorSliderChanged);
        row->addWidget(slider, 1);
        value_lbl = new QLabel("128");
        value_lbl->setFixedWidth(30);
        value_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value_lbl->setStyleSheet("color: #94A3B8; font-family: monospace;");
        row->addWidget(value_lbl);
        color_layout->addLayout(row);
    };
    makeSliderRow("R", r_slider_, r_value_label_, QColor(239, 68, 68));
    makeSliderRow("G", g_slider_, g_value_label_, QColor(34, 197, 94));
    makeSliderRow("B", b_slider_, b_value_label_, QColor(59, 130, 246));

    // preview + hex
    auto* preview_row = new QHBoxLayout();
    color_preview_ = new QLabel(color_box);
    color_preview_->setFixedSize(60, 32);
    color_preview_->setStyleSheet("background-color: rgb(128,128,128); border-radius: 6px; border: 1px solid #263244;");
    preview_row->addWidget(color_preview_);
    hex_input_ = new QLineEdit("#808080");
    hex_input_->setMaxLength(7);
    hex_input_->setValidator(new QRegularExpressionValidator(QRegularExpression("^#?[0-9a-fA-F]{6}$"), this));
    hex_input_->setStyleSheet(QString("%1 QLineEdit { font-family: monospace; }").arg(kInputStyle));
    connect(hex_input_, &QLineEdit::editingFinished, this, [this] {
        QString v = hex_input_->text().trimmed();
        if (!v.startsWith('#')) v.prepend('#');
        if (v.length() != 7) return;
        bool ok = false;
        const int r = v.mid(1, 2).toInt(&ok, 16); if (!ok) return;
        const int g = v.mid(3, 2).toInt(&ok, 16); if (!ok) return;
        const int b = v.mid(5, 2).toInt(&ok, 16); if (!ok) return;
        r_slider_->setValue(r); g_slider_->setValue(g); b_slider_->setValue(b);
    });
    preview_row->addWidget(hex_input_, 1);
    color_layout->addLayout(preview_row);

    // presets
    auto* presets_row = new QHBoxLayout();
    presets_row->setSpacing(4);
    for (const auto& p : kPresets) {
        auto* b = new QPushButton(color_box);
        b->setFixedSize(22, 22);
        b->setToolTip(p.name);
        b->setStyleSheet(QString(
            "QPushButton { background: rgb(%1,%2,%3); border: 1px solid #263244; border-radius: 11px; }"
            "QPushButton:hover { border: 1px solid #22D3EE; }"
        ).arg(p.r).arg(p.g).arg(p.b));
        connect(b, &QPushButton::clicked, this, [this, p] {
            r_slider_->setValue(p.r); g_slider_->setValue(p.g); b_slider_->setValue(p.b);
        });
        presets_row->addWidget(b);
    }
    color_layout->addLayout(presets_row);

    sidebar_layout->addWidget(color_box);

    // ── BRIGHTNESS
    auto* bright_box = new QGroupBox("BRIGHTNESS", sidebar);
    bright_box->setStyleSheet(color_box->styleSheet());
    auto* bright_layout = new QVBoxLayout(bright_box);
    brightness_slider_ = new QSlider(Qt::Horizontal, bright_box);
    brightness_slider_->setRange(0, 100);
    brightness_slider_->setValue(100);
    connect(brightness_slider_, &QSlider::valueChanged, this, &MainWindow::onBrightnessChanged);
    bright_layout->addWidget(brightness_slider_);
    bright_label_ = new QLabel("100%", bright_box);
    bright_label_->setStyleSheet("color: #94A3B8; font-family: monospace;");
    bright_layout->addWidget(bright_label_);
    auto* note = new QLabel("Scales RGB output at send time (platform 0x0812).", bright_box);
    note->setWordWrap(true);
    note->setStyleSheet("color: #64748B; font-size: 10px;");
    bright_layout->addWidget(note);

    sidebar_layout->addWidget(bright_box);

    // ── ACTIONS
    auto* action_box = new QGroupBox("ACTIONS", sidebar);
    action_box->setStyleSheet(color_box->styleSheet());
    auto* action_layout = new QVBoxLayout(action_box);
    auto* apply_btn = new QPushButton("Apply to Selected", action_box);
    apply_btn->setStyleSheet(kSuccessButtonStyle);
    connect(apply_btn, &QPushButton::clicked, this, [this] {
        applyColorToSelected(static_cast<uint8_t>(r_slider_->value()),
                             static_cast<uint8_t>(g_slider_->value()),
                             static_cast<uint8_t>(b_slider_->value()));
    });
    action_layout->addWidget(apply_btn);

    auto* all_on_btn = new QPushButton("All On (Apply All)", action_box);
    all_on_btn->setStyleSheet(kSecondaryButtonStyle);
    connect(all_on_btn, &QPushButton::clicked, this, [this] {
        applyColorToAll(static_cast<uint8_t>(r_slider_->value()),
                        static_cast<uint8_t>(g_slider_->value()),
                        static_cast<uint8_t>(b_slider_->value()));
    });
    action_layout->addWidget(all_on_btn);

    auto* all_off_btn = new QPushButton("All Off", action_box);
    all_off_btn->setStyleSheet(all_on_btn->styleSheet());
    connect(all_off_btn, &QPushButton::clicked, this, [this] {
        worker_->enqueue([this] { worker_->controller().allOff(zone_count_); });
        for (int i = 0; i < zone_count_; ++i) {
            if (Zone* z = zone_map_.getZone(i)) { z->r = 0; z->g = 0; z->b = 0; }
        }
        zone_model_->refresh();
        accordion_->refreshHeaders();
    });
    action_layout->addWidget(all_off_btn);

    sidebar_layout->addWidget(action_box);

    // ── GROUP ASSIGNMENT
    auto* group_box = new QGroupBox("GROUP ASSIGNMENT", sidebar);
    group_box->setStyleSheet(color_box->styleSheet());
    auto* group_layout = new QVBoxLayout(group_box);

    group_combo_ = new QComboBox(group_box);
    group_combo_->setEditable(true);
    group_combo_->setStyleSheet(kInputStyle);
    group_layout->addWidget(group_combo_);

    auto* assign_btn = new QPushButton("Assign Group", group_box);
    assign_btn->setStyleSheet(all_on_btn->styleSheet());
    connect(assign_btn, &QPushButton::clicked, this, [this] {
        const QString g = group_combo_->currentText().trimmed();
        if (g.isEmpty()) return;
        assignGroupToSelected(g);
    });
    group_layout->addWidget(assign_btn);

    auto* select_btn = new QPushButton("Select Group", group_box);
    select_btn->setStyleSheet(all_on_btn->styleSheet());
    connect(select_btn, &QPushButton::clicked, this, [this] {
        const QString g = group_combo_->currentText().trimmed();
        if (!g.isEmpty()) accordion_->selectGroup(g);
    });
    group_layout->addWidget(select_btn);

    auto* clear_btn = new QPushButton("Clear Group", group_box);
    clear_btn->setStyleSheet(all_on_btn->styleSheet());
    connect(clear_btn, &QPushButton::clicked, this, [this] { clearGroupFromSelected(); });
    group_layout->addWidget(clear_btn);

    sidebar_layout->addWidget(group_box);

    // ── PROFILES
    auto* profile_box = new QGroupBox("PROFILES", sidebar);
    profile_box->setStyleSheet(color_box->styleSheet());
    auto* profile_layout = new QVBoxLayout(profile_box);

    profile_combo_ = new QComboBox(profile_box);
    profile_combo_->setStyleSheet(kInputStyle);
    profile_layout->addWidget(profile_combo_);

    auto* save_profile_btn = new QPushButton("Save Current as Profile…", profile_box);
    save_profile_btn->setStyleSheet(all_on_btn->styleSheet());
    connect(save_profile_btn, &QPushButton::clicked, this, &MainWindow::onSaveProfile);
    profile_layout->addWidget(save_profile_btn);

    auto* load_profile_btn = new QPushButton("Load Profile", profile_box);
    load_profile_btn->setStyleSheet(all_on_btn->styleSheet());
    connect(load_profile_btn, &QPushButton::clicked, this, &MainWindow::onLoadProfile);
    profile_layout->addWidget(load_profile_btn);

    auto* delete_profile_btn = new QPushButton("Delete Profile", profile_box);
    delete_profile_btn->setStyleSheet(all_on_btn->styleSheet());
    connect(delete_profile_btn, &QPushButton::clicked, this, &MainWindow::onDeleteProfile);
    profile_layout->addWidget(delete_profile_btn);

    sidebar_layout->addWidget(profile_box);
    sidebar_layout->addStretch();

    sidebar_scroll->setWidget(sidebar);
    dock->setWidget(sidebar_scroll);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // ── CENTRAL: accordion
    zone_model_ = std::make_unique<ZoneListModel>(zone_map_);

    accordion_ = new ZoneAccordion(zone_model_.get(), &zone_map_, this);
    accordion_->setStyleSheet("background: #05070A; color: #E5E7EB;");
    setCentralWidget(accordion_);

    connect(accordion_, &ZoneAccordion::selectionChanged, this,
            [this](const std::vector<int>&) { /* badge updates inside accordion */ });

    refreshGroupCombo();
    refreshProfileCombo();
}

void MainWindow::refreshProfileCombo() {
    if (!profile_combo_) return;
    const QString current = profile_combo_->currentText();
    profile_combo_->clear();
    for (const QString& n : listProfileNames()) {
        profile_combo_->addItem(n, n);
    }
    if (!current.isEmpty()) {
        const int idx = profile_combo_->findText(current);
        if (idx >= 0) profile_combo_->setCurrentIndex(idx);
    }
}

void MainWindow::onSaveProfile() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, "Save Profile",
        "Profile name (will overwrite if it already exists):",
        QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const Profile p = Profile::captureFromZoneMap(zone_map_, pending_brightness_, name);
    if (!p.saveToFile(profilePath(name))) {
        QMessageBox::warning(this, "Save Profile", "Could not write profile file.");
        return;
    }
    refreshProfileCombo();
    const int idx = profile_combo_->findText(name);
    if (idx >= 0) profile_combo_->setCurrentIndex(idx);
}

void MainWindow::onLoadProfile() {
    const QString name = profile_combo_->currentText().trimmed();
    if (name.isEmpty()) return;
    Profile p;
    if (!p.loadFromFile(profilePath(name))) {
        QMessageBox::warning(this, "Load Profile",
                             QString("Could not load profile '%1'.").arg(name));
        return;
    }

    // Apply per-group to the in-memory zone map first, then push to hardware.
    for (auto it = p.perGroup.begin(); it != p.perGroup.end(); ++it) {
        const std::string grp = it.key().toStdString();
        for (Zone& z : const_cast<std::vector<Zone>&>(zone_map_.allZones())) {
            if (z.group == grp) {
                z.r = static_cast<uint8_t>(it.value()[0]);
                z.g = static_cast<uint8_t>(it.value()[1]);
                z.b = static_cast<uint8_t>(it.value()[2]);
                zone_model_->updateZoneColor(z.zone_id, z.r, z.g, z.b);
            }
        }
    }

    pending_brightness_ = p.brightness;
    brightness_slider_->setValue(p.brightness);
    bright_label_->setText(QString("%1%").arg(p.brightness));

    accordion_->refreshHeaders();

    // Send to hardware batched by color.
    const auto batches = p.expand(zone_map_);
    worker_->enqueue([this, batches] {
        for (const auto& b : batches) {
            auto [sr, sg, sb] = effectiveRgb(
                static_cast<uint8_t>(b.r),
                static_cast<uint8_t>(b.g),
                static_cast<uint8_t>(b.b));
            worker_->controller().setColorZones(sr, sg, sb, b.zone_ids);
        }
    });
}

void MainWindow::onDeleteProfile() {
    const QString name = profile_combo_->currentText().trimmed();
    if (name.isEmpty()) return;
    const auto ret = QMessageBox::question(
        this, "Delete Profile",
        QString("Delete profile '%1'? This cannot be undone.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    QFile::remove(profilePath(name));
    refreshProfileCombo();
}

void MainWindow::refreshGroupCombo() {
    if (!group_combo_) return;
    const QString current = group_combo_->currentText();
    group_combo_->clear();
    group_combo_->addItem("(none)", QString());
    for (const std::string& g : zone_map_.groupNames()) {
        group_combo_->addItem(QString::fromStdString(g), QString::fromStdString(g));
    }
    if (!current.isEmpty()) {
        const int idx = group_combo_->findText(current);
        if (idx >= 0) group_combo_->setCurrentIndex(idx);
        else group_combo_->setCurrentText(current);
    }
}

void MainWindow::assignGroupToSelected(const QString& group_name) {
    const auto ids = accordion_->selectedZoneIds();
    if (ids.empty()) return;
    for (int id : ids) {
        if (Zone* z = zone_map_.getZone(id)) {
            z->group = group_name.toStdString();
        }
    }
    zone_model_->refresh();
    accordion_->rebuild();
    refreshGroupCombo();
}

void MainWindow::clearGroupFromSelected() {
    const auto ids = accordion_->selectedZoneIds();
    if (ids.empty()) return;
    for (int id : ids) {
        if (Zone* z = zone_map_.getZone(id)) z->group.clear();
    }
    zone_model_->refresh();
    accordion_->rebuild();
    refreshGroupCombo();
}

// ─────────────────────────────────────────────────────────── color apply

void MainWindow::onColorSliderChanged(int) {
    r_value_label_->setText(QString::number(r_slider_->value()));
    g_value_label_->setText(QString::number(g_slider_->value()));
    b_value_label_->setText(QString::number(b_slider_->value()));

    const uint8_t r = static_cast<uint8_t>(r_slider_->value());
    const uint8_t g = static_cast<uint8_t>(g_slider_->value());
    const uint8_t b = static_cast<uint8_t>(b_slider_->value());
    color_preview_->setStyleSheet(QString("background-color: rgb(%1,%2,%3); border-radius: 6px; border: 1px solid #263244;").arg(r).arg(g).arg(b));
    if (hex_input_) {
        hex_input_->setText(QString("#%1%2%3")
                                .arg(r, 2, 16, QChar('0'))
                                .arg(g, 2, 16, QChar('0'))
                                .arg(b, 2, 16, QChar('0'))
                                .toUpper());
    }
}

void MainWindow::applyColorToSelected(uint8_t r, uint8_t g, uint8_t b) {
    const auto ids = accordion_ ? accordion_->selectedZoneIds() : std::vector<int>{};
    if (ids.empty()) {
        applyColorToAll(r, g, b);
        return;
    }
    for (int id : ids) {
        if (Zone* z = zone_map_.getZone(id)) { z->r = r; z->g = g; z->b = b; }
        zone_model_->updateZoneColor(id, r, g, b);
    }
    accordion_->refreshHeaders();
    worker_->enqueue([this, r, g, b, ids] { applyColorToHardware(r, g, b, ids); });
}

void MainWindow::applyColorToAll(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < zone_count_; ++i) {
        if (Zone* z = zone_map_.getZone(i)) { z->r = r; z->g = g; z->b = b; }
    }
    zone_model_->refresh();
    if (accordion_) accordion_->refreshHeaders();
    worker_->enqueue([this, r, g, b] {
        std::vector<int> all(zone_count_);
        for (int i = 0; i < zone_count_; ++i) all[i] = i;
        applyColorToHardware(r, g, b, all);
    });
}

void MainWindow::applyColorToHardware(uint8_t r, uint8_t g, uint8_t b, const std::vector<int>& zone_ids) {
    auto [sr, sg, sb] = effectiveRgb(r, g, b);
    worker_->controller().setColorZones(sr, sg, sb, zone_ids);
}

std::tuple<uint8_t, uint8_t, uint8_t> MainWindow::effectiveRgb(uint8_t r, uint8_t g, uint8_t b) {
    if (pending_brightness_ == 100) return {r, g, b};
    const double scale = pending_brightness_ / 100.0;
    return {
        static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(r * scale)))),
        static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(g * scale)))),
        static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(b * scale)))),
    };
}

void MainWindow::onBrightnessChanged(int value) {
    pending_brightness_ = value;
    bright_label_->setText(QString("%1%").arg(value));
}

void MainWindow::onConnected(const QString& firmware, uint16_t platform, int zone_count) {
    zone_count_ = zone_count;
    status_label_->setText(QString("Connected · fw %1 · platform 0x%2 · %3 zones")
                               .arg(firmware)
                               .arg(platform, 4, 16, QChar('0'))
                               .arg(zone_count));
    status_label_->setStyleSheet(statusStyle(QStringLiteral("#166534"), QStringLiteral("#4ADE80")));
}

void MainWindow::onDisconnected(const QString& reason) {
    status_label_->setText(QString("Disconnected: %1").arg(reason));
    status_label_->setStyleSheet(statusStyle(QStringLiteral("#7F1D1D"), QStringLiteral("#F87171")));
}

void MainWindow::onError(const QString& message) {
    QMessageBox::critical(this, "Error", message);
}

void MainWindow::onOpenEvents() {
    EventDialog dlg(this);
    dlg.exec();
}

void MainWindow::onOpenDiagnostics() {
    auto* dlg = new DiagnosticsDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::onOpenDiscoveryWizard() {
    DiscoveryWizard dlg(&zone_map_, worker_.get(), this);

    connect(&dlg, &DiscoveryWizard::zoneDiscovered, this, [this](int zone_id) {
        zone_model_->refresh();
        // Keep the accordion's headers up to date with new colors/groups as the
        // wizard edits the map. A full rebuild is cheap (7 sections) and handles
        // group-membership changes the refresh alone wouldn't catch.
        accordion_->rebuild();
        refreshGroupCombo();
        Q_UNUSED(zone_id);
    });

    connect(&dlg, &DiscoveryWizard::discoveryComplete, this, [this] {
        zone_model_->refresh();
        accordion_->rebuild();
        refreshGroupCombo();
    });

    dlg.exec();

    // Ensure one final sync after the dialog closes via any path (X/Esc/Save).
    zone_model_->refresh();
    accordion_->rebuild();
    refreshGroupCombo();
}
