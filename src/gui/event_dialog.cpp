#include "gui/event_dialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QFrame>
#include <QMessageBox>
#include <QFileInfo>
#include <QCoreApplication>
#include <QProcess>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QUuid>

#include "models/event_preset.h"
#include "models/config_paths.h"
#include "animator/effects.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimeEdit>
#include <QGroupBox>
#include <QTime>

// ─────────────────────────────────────────────────────────── EventCard

class EventCard : public QFrame {
    Q_OBJECT
public:
    EventCard(const EventPreset& p, QWidget* parent = nullptr) : QFrame(parent), preset_(p) {
        setStyleSheet(
            "EventCard { background: #0B0F14; border: 1px solid #1B2636; border-radius: 6px; }"
            "QLineEdit, QSpinBox, QComboBox { background: #05070A; color: #F8FAFC; "
            " padding: 5px 8px; border: 1px solid #263244; border-radius: 5px; }"
            "QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #22D3EE; }"
            "QLabel { color: #E5E7EB; }"
            "QPushButton { background: #111827; color: #E5E7EB; border: 1px solid #263244; padding: 5px 10px; border-radius: 5px; }"
            "QPushButton:hover { background: #18212F; border-color: #334155; }"
        );

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(8);

        // header
        auto* hdr = new QHBoxLayout();
        arrow_ = new QLabel("▾", this); arrow_->setFixedWidth(14);
        arrow_->setStyleSheet("font-weight: bold;");
        hdr->addWidget(arrow_);

        name_edit_ = new QLineEdit(preset_.name, this);
        name_edit_->setPlaceholderText("Event name…");
        hdr->addWidget(name_edit_, 1);

        auto* preview_btn = new QPushButton("▶ Preview", this);
        preview_btn->setStyleSheet(
            "QPushButton { background: #16A34A; color: #F8FAFC; border: 1px solid #4ADE80; font-weight: 600; padding: 5px 10px; border-radius: 5px; }"
            "QPushButton:hover { background: #15803D; }");
        connect(preview_btn, &QPushButton::clicked, this, [this] { emit previewRequested(this); });
        hdr->addWidget(preview_btn);

        auto* delete_btn = new QPushButton("Delete", this);
        delete_btn->setStyleSheet(
            "QPushButton { background: #7F1D1D; border: 1px solid #F87171; color: #F8FAFC; } QPushButton:hover { background: #991B1B; }");
        connect(delete_btn, &QPushButton::clicked, this, [this] { emit deleted(this); });
        hdr->addWidget(delete_btn);

        root->addLayout(hdr);

        // summary
        summary_ = new QLabel(this);
        summary_->setStyleSheet("color: #94A3B8; font-size: 12px;");
        root->addWidget(summary_);

        // body
        body_ = new QWidget(this);
        auto* body_layout = new QVBoxLayout(body_);
        body_layout->setContentsMargins(0, 6, 0, 0);
        body_layout->setSpacing(6);

        // id row (read-only for existing, generated for new)
        auto* id_row = new QHBoxLayout();
        id_row->addWidget(new QLabel("ID:", body_));
        id_edit_ = new QLineEdit(preset_.id, body_);
        id_edit_->setPlaceholderText("kebab-case-id");
        id_edit_->setValidator(new QRegularExpressionValidator(QRegularExpression("^[a-z0-9-]+$"), body_));
        id_row->addWidget(id_edit_, 1);
        body_layout->addLayout(id_row);

        // zones row
        auto* z_row = new QHBoxLayout();
        z_row->addWidget(new QLabel("Zones:", body_));
        zones_edit_ = new QLineEdit(preset_.zones_spec, body_);
        zones_edit_->setPlaceholderText("all  |  group:<name>  |  1,2,3");
        z_row->addWidget(zones_edit_, 1);
        body_layout->addLayout(z_row);

        // effect row
        auto* e_row = new QHBoxLayout();
        e_row->addWidget(new QLabel("Effect:", body_));
        effect_combo_ = new QComboBox(body_);
        for (const QString& n : allEffectNames()) effect_combo_->addItem(n);
        const int idx = effect_combo_->findText(preset_.effect);
        if (idx >= 0) effect_combo_->setCurrentIndex(idx);
        e_row->addWidget(effect_combo_);

        e_row->addWidget(new QLabel("Duration:", body_));
        duration_spin_ = new QSpinBox(body_);
        duration_spin_->setRange(1, 600);
        duration_spin_->setSuffix(" s");
        duration_spin_->setValue(preset_.duration_sec);
        e_row->addWidget(duration_spin_);
        e_row->addStretch();
        body_layout->addLayout(e_row);

        // color row
        auto* c_row = new QHBoxLayout();
        c_row->addWidget(new QLabel("Color:", body_));
        rgb_edit_ = new QLineEdit(body_);
        rgb_edit_->setPlaceholderText("R,G,B");
        rgb_edit_->setText(QString("%1,%2,%3").arg(preset_.rgb[0]).arg(preset_.rgb[1]).arg(preset_.rgb[2]));
        rgb_edit_->setMaximumWidth(120);
        c_row->addWidget(rgb_edit_);
        swatch_ = new QLabel(body_);
        swatch_->setFixedSize(28, 20);
        updateSwatch();
        c_row->addWidget(swatch_);
        c_row->addStretch();
        body_layout->addLayout(c_row);

        root->addWidget(body_);

        connect(name_edit_, &QLineEdit::textChanged, this, &EventCard::updateSummary);
        connect(id_edit_, &QLineEdit::textChanged, this, &EventCard::updateSummary);
        connect(zones_edit_, &QLineEdit::textChanged, this, &EventCard::updateSummary);
        connect(effect_combo_, &QComboBox::currentTextChanged, this, &EventCard::updateSummary);
        connect(duration_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &EventCard::updateSummary);
        connect(rgb_edit_, &QLineEdit::textChanged, this, [this] { updateSwatch(); updateSummary(); });

        setCursor(Qt::PointingHandCursor);
        setExpanded(preset_.id.isEmpty() || preset_.name.isEmpty());
        updateSummary();
    }

    EventPreset harvest() const {
        EventPreset p = preset_;
        p.name = name_edit_->text().trimmed();
        p.id = id_edit_->text().trimmed();
        if (p.id.isEmpty()) {
            p.id = p.name.toLower().replace(QRegularExpression("[^a-z0-9]+"), "-");
            while (p.id.startsWith('-')) p.id.remove(0, 1);
            while (p.id.endsWith('-')) p.id.chop(1);
            if (p.id.isEmpty()) p.id = "event-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        }
        p.zones_spec = zones_edit_->text().trimmed();
        p.effect = effect_combo_->currentText();
        p.duration_sec = duration_spin_->value();
        const auto parts = rgb_edit_->text().split(',');
        if (parts.size() == 3) {
            p.rgb = {
                std::clamp(parts[0].trimmed().toInt(), 0, 255),
                std::clamp(parts[1].trimmed().toInt(), 0, 255),
                std::clamp(parts[2].trimmed().toInt(), 0, 255),
            };
        }
        return p;
    }

signals:
    void deleted(EventCard* c);
    void previewRequested(EventCard* c);

protected:
    void mousePressEvent(QMouseEvent* ev) override {
        if (childAt(ev->pos()) == nullptr || childAt(ev->pos()) == arrow_ || childAt(ev->pos()) == summary_) {
            setExpanded(!expanded_);
        } else {
            QFrame::mousePressEvent(ev);
        }
    }

private:
    void setExpanded(bool exp) {
        expanded_ = exp;
        body_->setVisible(exp);
        summary_->setVisible(!exp);
        arrow_->setText(exp ? "▾" : "▸");
    }

    void updateSwatch() {
        const auto parts = rgb_edit_->text().split(',');
        int r = 128, g = 128, b = 128;
        if (parts.size() == 3) {
            r = std::clamp(parts[0].trimmed().toInt(), 0, 255);
            g = std::clamp(parts[1].trimmed().toInt(), 0, 255);
            b = std::clamp(parts[2].trimmed().toInt(), 0, 255);
        }
        swatch_->setStyleSheet(QString("background: rgb(%1,%2,%3); border: 1px solid #263244; border-radius: 4px;")
                                   .arg(r).arg(g).arg(b));
    }

    void updateSummary() {
        summary_->setText(harvest().summary());
    }

    EventPreset preset_;
    bool expanded_ = true;

    QLabel* arrow_ = nullptr;
    QLineEdit* name_edit_ = nullptr;
    QLabel* summary_ = nullptr;
    QWidget* body_ = nullptr;

    QLineEdit* id_edit_ = nullptr;
    QLineEdit* zones_edit_ = nullptr;
    QComboBox* effect_combo_ = nullptr;
    QSpinBox* duration_spin_ = nullptr;
    QLineEdit* rgb_edit_ = nullptr;
    QLabel* swatch_ = nullptr;
};

// ─────────────────────────────────────────────────────── EventDialog

EventDialog::EventDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Events");
    setMinimumSize(720, 560);
    setModal(true);
    setStyleSheet("QDialog { background: #05070A; }");

    seedDefaultEventsIfMissing();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    // ── VOICE CONTROLS panel
    auto* voice_box = new QGroupBox("VOICE NOTIFICATIONS", this);
    voice_box->setStyleSheet(
        "QGroupBox { color: #94A3B8; font-size: 11px; font-weight: 700; letter-spacing: 1px; "
        "border: 1px solid #1B2636; border-radius: 6px; margin-top: 10px; padding: 16px 10px 10px 10px; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 6px; }"
        "QLabel { color: #E5E7EB; } QCheckBox { color: #E5E7EB; } "
        "QTimeEdit { background: #0B0F14; color: #F8FAFC; padding: 5px 8px; border: 1px solid #263244; border-radius: 5px; }");
    auto* voice_layout = new QVBoxLayout(voice_box);
    voice_layout->setSpacing(8);

    // Load current voice config
    const QString voice_cfg_path = alienwareConfigPath("voice.json");
    QJsonObject voice_cfg;
    {
        QFile f(voice_cfg_path);
        if (f.open(QIODevice::ReadOnly)) {
            voice_cfg = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
        }
    }

    // Row 1: enabled toggle
    auto* voice_row1 = new QHBoxLayout();
    voice_enabled_check_ = new QCheckBox("Voice enabled", voice_box);
    voice_enabled_check_->setChecked(voice_cfg.value("enabled").toBool(false));
    voice_enabled_check_->setStyleSheet("QCheckBox { font-size: 13px; }");
    voice_row1->addWidget(voice_enabled_check_);
    voice_row1->addStretch();

    auto* mute_hint = new QLabel("Lights always fire. Voice respects this toggle + the schedule below.", voice_box);
    mute_hint->setStyleSheet("color: #64748B; font-size: 11px;");
    voice_row1->addWidget(mute_hint);
    voice_layout->addLayout(voice_row1);

    // Row 2: schedule
    auto* voice_row2 = new QHBoxLayout();
    voice_sched_check_ = new QCheckBox("Active hours:", voice_box);
    const auto sched = voice_cfg.value("voice_schedule").toObject();
    voice_sched_check_->setChecked(sched.value("enabled").toBool(false));
    voice_row2->addWidget(voice_sched_check_);

    voice_start_time_ = new QTimeEdit(voice_box);
    voice_start_time_->setDisplayFormat("HH:mm");
    voice_start_time_->setTime(QTime::fromString(sched.value("active_start").toString("08:00"), "HH:mm"));
    voice_row2->addWidget(voice_start_time_);

    voice_row2->addWidget(new QLabel("to", voice_box));

    voice_end_time_ = new QTimeEdit(voice_box);
    voice_end_time_->setDisplayFormat("HH:mm");
    voice_end_time_->setTime(QTime::fromString(sched.value("active_end").toString("23:00"), "HH:mm"));
    voice_row2->addWidget(voice_end_time_);

    auto* sched_hint = new QLabel("(outside this window, voice is silent, lights still fire)", voice_box);
    sched_hint->setStyleSheet("color: #64748B; font-size: 11px;");
    voice_row2->addWidget(sched_hint);
    voice_row2->addStretch();
    voice_layout->addLayout(voice_row2);

    root->addWidget(voice_box);

    // ── Top action row
    auto* top = new QHBoxLayout();
    auto* add_btn = new QPushButton("+ Add Event", this);
    add_btn->setStyleSheet(
        "QPushButton { background: #16A34A; color: #F8FAFC; border: 1px solid #4ADE80; font-weight: 700; padding: 8px 14px; border-radius: 6px; }"
        "QPushButton:hover { background: #15803D; }");
    connect(add_btn, &QPushButton::clicked, this, &EventDialog::onAddEvent);
    top->addWidget(add_btn);

    auto* hint = new QLabel(
        "Events are fired on-demand via <code>alienware_rgb_cli fire-event &lt;id&gt;</code>. "
        "Use the ▶ Preview button on each card to audition it against the hardware.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #94A3B8; font-size: 12px; padding: 0 12px;");
    top->addWidget(hint, 1);
    root->addLayout(top);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* scroll_contents = new QWidget(scroll);
    cards_layout_ = new QVBoxLayout(scroll_contents);
    cards_layout_->setContentsMargins(0, 0, 0, 0);
    cards_layout_->setSpacing(8);
    cards_layout_->addStretch();
    scroll->setWidget(scroll_contents);
    root->addWidget(scroll, 1);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch();
    auto* cancel_btn = new QPushButton("Cancel", this);
    connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addWidget(cancel_btn);
    auto* save_btn = new QPushButton("Save", this);
    save_btn->setStyleSheet(
        "QPushButton { background: #0E7490; color: #F8FAFC; border: 1px solid #22D3EE; font-weight: 700; padding: 8px 14px; border-radius: 6px; }"
        "QPushButton:hover { background: #0891B2; }");
    connect(save_btn, &QPushButton::clicked, this, &EventDialog::onSave);
    bottom->addWidget(save_btn);
    root->addLayout(bottom);

    // populate
    QList<EventPreset> presets;
    loadEventPresets(presets);
    for (const auto& p : presets) {
        auto* c = new EventCard(p, scroll_contents);
        connect(c, &EventCard::deleted, this, &EventDialog::onCardDeleted);
        connect(c, &EventCard::previewRequested, this, [this](EventCard* card) {
            // Save first so fire-event reads the latest state, then fire.
            onSave();  // silently writes events.json + keeps dialog open? No, onSave accepts().
        });
        // Preview handler: run fire-event via QProcess WITHOUT saving (use harvest values directly).
        // Actually simpler: replace the above lambda to directly invoke a one-off using harvest().
        // Restore connection below for a better preview flow:
        QObject::disconnect(c, &EventCard::previewRequested, nullptr, nullptr);
        connect(c, &EventCard::previewRequested, this, [this](EventCard* card) {
            const EventPreset hp = card->harvest();
            // Fire an ad-hoc apply-color with the current (possibly unsaved) card values.
            const QString cli = cliBinaryPath();
            if (!QFileInfo(cli).isExecutable()) {
                QMessageBox::warning(this, "Preview", "CLI not found at " + cli);
                return;
            }
            QStringList args = {
                "apply-color",
                "--zones", hp.zones_spec,
                "--rgb", QString("%1,%2,%3").arg(hp.rgb[0]).arg(hp.rgb[1]).arg(hp.rgb[2]),
                "--flash-duration", QString::number(hp.duration_sec),
                "--effect", hp.effect,
            };
            QProcess::startDetached(cli, args);
        });
        cards_layout_->insertWidget(cards_layout_->count() - 1, c);
        cards_.append(c);
    }
}

void EventDialog::onAddEvent() {
    EventPreset p;
    p.name = QString("Event %1").arg(cards_.size() + 1);
    p.zones_spec = "group:Cooler";
    p.effect = "pulse";
    p.rgb = {0, 200, 255};
    p.duration_sec = 2;
    auto* c = new EventCard(p, this);
    connect(c, &EventCard::deleted, this, &EventDialog::onCardDeleted);
    connect(c, &EventCard::previewRequested, this, [this](EventCard* card) {
        const EventPreset hp = card->harvest();
        const QString cli = cliBinaryPath();
        QStringList args = {
            "apply-color", "--zones", hp.zones_spec,
            "--rgb", QString("%1,%2,%3").arg(hp.rgb[0]).arg(hp.rgb[1]).arg(hp.rgb[2]),
            "--flash-duration", QString::number(hp.duration_sec),
            "--effect", hp.effect,
        };
        QProcess::startDetached(cli, args);
    });
    cards_layout_->insertWidget(cards_layout_->count() - 1, c);
    cards_.append(c);
}

void EventDialog::onCardDeleted(EventCard* card) {
    cards_.removeOne(card);
    card->deleteLater();
}

QString EventDialog::cliBinaryPath() const {
    const QString gui = QCoreApplication::applicationFilePath();
    return QFileInfo(gui).absolutePath() + "/alienware_rgb_cli";
}

void EventDialog::onSave() {
    // Save event presets
    QList<EventPreset> presets;
    QSet<QString> seen_ids;
    for (EventCard* c : cards_) {
        EventPreset p = c->harvest();
        if (p.name.isEmpty() || p.zones_spec.isEmpty()) continue;
        QString base = p.id;
        int suffix = 2;
        while (seen_ids.contains(p.id)) { p.id = base + "-" + QString::number(suffix++); }
        seen_ids.insert(p.id);
        presets.append(p);
    }
    if (!saveEventPresets(presets)) {
        QMessageBox::warning(this, "Save Events", "Could not write events.json.");
        return;
    }

    // Save voice settings back to voice.json
    const QString voice_cfg_path = alienwareConfigPath("voice.json");
    QFile vf(voice_cfg_path);
    if (vf.open(QIODevice::ReadOnly)) {
        QJsonObject vcfg = QJsonDocument::fromJson(vf.readAll()).object();
        vf.close();

        vcfg["enabled"] = voice_enabled_check_->isChecked();

        QJsonObject sched;
        sched["enabled"] = voice_sched_check_->isChecked();
        sched["active_start"] = voice_start_time_->time().toString("HH:mm");
        sched["active_end"] = voice_end_time_->time().toString("HH:mm");
        vcfg["voice_schedule"] = sched;

        QFile wf(voice_cfg_path);
        if (wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            wf.write(QJsonDocument(vcfg).toJson(QJsonDocument::Indented));
            wf.close();
        }
    }

    accept();
}

#include "event_dialog.moc"
