#include "gui/diagnostics_dialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString buttonStyle(const QString& background, const QString& border) {
    return QString(
        "QPushButton { background: %1; color: #F4F4F5; border: 1px solid %2; "
        "font-weight: 600; padding: 8px 14px; border-radius: 8px; }"
        "QPushButton:hover { background: #222225; border-color: #3F3F46; }")
        .arg(background, border);
}

QTableWidgetItem* item(const QString& text) {
    auto* i = new QTableWidgetItem(text);
    i->setFlags(i->flags() & ~Qt::ItemIsEditable);
    return i;
}

QString statusColor(const QString& status) {
    if (status == "ok") return "#7DD891";
    if (status == "warn") return "#F8C96A";
    if (status == "fail") return "#FF8A94";
    return "#A1A1AA";
}

}  // namespace

DiagnosticsDialog::DiagnosticsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Diagnostics");
    setMinimumSize(840, 620);
    setModal(false);
    setStyleSheet(
        "QDialog { background: #050505; }"
        "QLabel { color: #F4F4F5; }"
        "QTableWidget { background: #111113; color: #F4F4F5; border: 1px solid #27272A; border-radius: 8px; gridline-color: #27272A; }"
        "QHeaderView::section { background: #18181B; color: #D4D4D8; border: none; border-bottom: 1px solid #303033; padding: 7px; font-weight: 600; }"
        "QPlainTextEdit { background: #111113; color: #D4D4D8; border: 1px solid #27272A; border-radius: 8px; padding: 9px; font-family: monospace; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* top = new QHBoxLayout();
    summary_label_ = new QLabel("Health: not checked", this);
    summary_label_->setStyleSheet("color: #A1A1AA; font-weight: 600;");
    top->addWidget(summary_label_, 1);

    refresh_health_btn_ = new QPushButton("Refresh Health", this);
    refresh_health_btn_->setText("Refresh health");
    refresh_health_btn_->setStyleSheet(
        "QPushButton { background: #E8EAFF; color: #111113; border: 1px solid #FFFFFF; font-weight: 600; padding: 8px 14px; border-radius: 8px; }"
        "QPushButton:hover { background: #FFFFFF; }");
    connect(refresh_health_btn_, &QPushButton::clicked, this, &DiagnosticsDialog::refreshHealth);
    top->addWidget(refresh_health_btn_);

    runtime_status_btn_ = new QPushButton("Runtime Status", this);
    runtime_status_btn_->setText("Runtime status");
    connect(runtime_status_btn_, &QPushButton::clicked, this, &DiagnosticsDialog::refreshRuntime);
    top->addWidget(runtime_status_btn_);

    collect_bundle_btn_ = new QPushButton("Collect Bundle", this);
    collect_bundle_btn_->setText("Collect bundle");
    collect_bundle_btn_->setStyleSheet(buttonStyle("#18181B", "#303033"));
    connect(collect_bundle_btn_, &QPushButton::clicked, this, &DiagnosticsDialog::collectBundle);
    top->addWidget(collect_bundle_btn_);
    root->addLayout(top);

    checks_table_ = new QTableWidget(this);
    checks_table_->setColumnCount(3);
    checks_table_->setHorizontalHeaderLabels({"Status", "Check", "Detail"});
    checks_table_->verticalHeader()->setVisible(false);
    checks_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    checks_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    checks_table_->horizontalHeader()->setStretchLastSection(true);
    checks_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    checks_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    root->addWidget(checks_table_, 1);

    output_text_ = new QPlainTextEdit(this);
    output_text_->setReadOnly(true);
    output_text_->setMinimumHeight(170);
    root->addWidget(output_text_);

    auto* bottom = new QHBoxLayout();
    bottom->addStretch();
    auto* close_btn = new QPushButton("Close", this);
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(close_btn);
    root->addLayout(bottom);

    QTimer::singleShot(0, this, &DiagnosticsDialog::refreshHealth);
}

QString DiagnosticsDialog::cliBinaryPath() const {
    const QString app_dir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    const QString sibling = QDir(app_dir).filePath("alienware_rgb_cli");
    if (QFileInfo(sibling).isExecutable()) return sibling;

    const QString path_cli = QStandardPaths::findExecutable("alienware_rgb_cli");
    return path_cli.isEmpty() ? sibling : path_cli;
}

QString DiagnosticsDialog::diagnosticsScriptPath() const {
    const QString app_dir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    const QString sibling = QDir(app_dir).filePath("collect-diagnostics");
    if (QFileInfo(sibling).isExecutable()) return sibling;

    const QString repo_script = QDir::cleanPath(QDir(app_dir).filePath("../scripts/collect-diagnostics"));
    if (QFileInfo(repo_script).isExecutable()) return repo_script;

    const QString path_script = QStandardPaths::findExecutable("collect-diagnostics");
    return path_script.isEmpty() ? sibling : path_script;
}

void DiagnosticsDialog::refreshHealth() {
    startCommand(Task::Doctor, "doctor --json", cliBinaryPath(), {"doctor", "--json"});
}

void DiagnosticsDialog::refreshRuntime() {
    startCommand(Task::RuntimeStatus, "runtime-status", cliBinaryPath(), {"runtime-status"});
}

void DiagnosticsDialog::collectBundle() {
    QString out_dir = qEnvironmentVariable("ALIENWARE_RGB_DIAG_DIR").trimmed();
    if (out_dir.isEmpty()) out_dir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (out_dir.isEmpty()) out_dir = QDir::homePath();
    startCommand(Task::CollectBundle, "collect-diagnostics", diagnosticsScriptPath(),
                 {"--output", out_dir, "--cli", cliBinaryPath()});
}

void DiagnosticsDialog::startCommand(Task task, const QString& title, const QString& program, const QStringList& args) {
    if (process_ && process_->state() != QProcess::NotRunning) {
        QMessageBox::information(this, "Diagnostics", "A diagnostics command is still running.");
        return;
    }
    if (!QFileInfo(program).isExecutable()) {
        QMessageBox::warning(this, "Diagnostics", "Command is not executable: " + program);
        return;
    }

    if (process_) process_->deleteLater();
    process_ = new QProcess(this);
    active_task_ = task;
    active_title_ = title;
    connect(process_, &QProcess::finished, this, &DiagnosticsDialog::onProcessFinished);
    connect(process_, &QProcess::errorOccurred, this, &DiagnosticsDialog::onProcessError);
    setBusy(true, title);
    process_->start(program, args);
}

void DiagnosticsDialog::setBusy(bool busy, const QString& label) {
    refresh_health_btn_->setEnabled(!busy);
    runtime_status_btn_->setEnabled(!busy);
    collect_bundle_btn_->setEnabled(!busy);
    if (busy) {
        summary_label_->setText("Running: " + label);
        summary_label_->setStyleSheet("color: #A7B4FF; font-weight: 600;");
    }
}

void DiagnosticsDialog::onProcessFinished(int exit_code, QProcess::ExitStatus exit_status) {
    const QByteArray stdout_bytes = process_ ? process_->readAllStandardOutput() : QByteArray();
    const QByteArray stderr_bytes = process_ ? process_->readAllStandardError() : QByteArray();
    setBusy(false);

    if (exit_status != QProcess::NormalExit) {
        renderOutput(active_title_, exit_code, stdout_bytes, stderr_bytes + "\nprocess crashed");
        summary_label_->setText("Health: command crashed");
        summary_label_->setStyleSheet("color: #FF8A94; font-weight: 600;");
    } else if (active_task_ == Task::Doctor) {
        renderDoctorJson(stdout_bytes);
        if (!stderr_bytes.trimmed().isEmpty()) output_text_->appendPlainText("\nstderr:\n" + QString::fromUtf8(stderr_bytes));
    } else {
        renderOutput(active_title_, exit_code, stdout_bytes, stderr_bytes);
        if (active_task_ == Task::CollectBundle && exit_code == 0) {
            summary_label_->setText("Bundle: created");
            summary_label_->setStyleSheet("color: #7DD891; font-weight: 600;");
        }
    }

    active_task_ = Task::None;
    active_title_.clear();
}

void DiagnosticsDialog::onProcessError(QProcess::ProcessError) {
    setBusy(false);
    const QString msg = process_ ? process_->errorString() : QString("unknown process error");
    output_text_->setPlainText(msg);
    summary_label_->setText("Health: command failed");
    summary_label_->setStyleSheet("color: #FF8A94; font-weight: 600;");
}

void DiagnosticsDialog::renderDoctorJson(const QByteArray& json_bytes) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json_bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        checks_table_->setRowCount(0);
        output_text_->setPlainText(QString::fromUtf8(json_bytes));
        summary_label_->setText("Health: invalid doctor output");
        summary_label_->setStyleSheet("color: #FF8A94; font-weight: 600;");
        return;
    }

    const QJsonObject root = doc.object();
    const bool ok = root.value("ok").toBool(false);
    const int failures = root.value("failures").toInt();
    const int warnings = root.value("warnings").toInt();
    const QJsonArray checks = root.value("checks").toArray();
    checks_table_->setRowCount(checks.size());
    for (int row = 0; row < checks.size(); ++row) {
        const QJsonObject check = checks[row].toObject();
        const QString status = check.value("status").toString();
        auto* status_item = item(status.toUpper());
        status_item->setForeground(QColor(statusColor(status)));
        checks_table_->setItem(row, 0, status_item);
        checks_table_->setItem(row, 1, item(check.value("name").toString()));
        checks_table_->setItem(row, 2, item(check.value("detail").toString()));
    }
    checks_table_->resizeRowsToContents();

    summary_label_->setText(QString("Health: %1 · %2 failure(s) · %3 warning(s)")
                                .arg(ok ? "OK" : "Attention", QString::number(failures), QString::number(warnings)));
    summary_label_->setStyleSheet(QString("color: %1; font-weight: 600;").arg(ok ? "#7DD891" : "#FF8A94"));
    output_text_->setPlainText(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)));
}

void DiagnosticsDialog::renderOutput(const QString& title, int exit_code, const QByteArray& stdout_bytes, const QByteArray& stderr_bytes) {
    QString text;
    text += "$ " + title + "\n";
    text += "[exit " + QString::number(exit_code) + "]\n\n";
    text += QString::fromUtf8(stdout_bytes);
    if (!stderr_bytes.trimmed().isEmpty()) {
        text += "\n\nstderr:\n";
        text += QString::fromUtf8(stderr_bytes);
    }
    output_text_->setPlainText(text);
    summary_label_->setText(QString("%1: exit %2").arg(title, QString::number(exit_code)));
    summary_label_->setStyleSheet(QString("color: %1; font-weight: 600;").arg(exit_code == 0 ? "#7DD891" : "#FF8A94"));
}
