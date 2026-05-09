#ifndef DIAGNOSTICS_DIALOG_H
#define DIAGNOSTICS_DIALOG_H

#include <QDialog>
#include <QProcess>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class DiagnosticsDialog : public QDialog {
    Q_OBJECT

public:
    explicit DiagnosticsDialog(QWidget* parent = nullptr);

private slots:
    void refreshHealth();
    void refreshRuntime();
    void collectBundle();
    void onProcessFinished(int exit_code, QProcess::ExitStatus exit_status);
    void onProcessError(QProcess::ProcessError error);

private:
    enum class Task {
        None,
        Doctor,
        RuntimeStatus,
        CollectBundle,
    };

    QString cliBinaryPath() const;
    QString diagnosticsScriptPath() const;
    void startCommand(Task task, const QString& title, const QString& program, const QStringList& args);
    void setBusy(bool busy, const QString& label = {});
    void renderDoctorJson(const QByteArray& json_bytes);
    void renderOutput(const QString& title, int exit_code, const QByteArray& stdout_bytes, const QByteArray& stderr_bytes);

    QLabel* summary_label_ = nullptr;
    QTableWidget* checks_table_ = nullptr;
    QPlainTextEdit* output_text_ = nullptr;
    QPushButton* refresh_health_btn_ = nullptr;
    QPushButton* runtime_status_btn_ = nullptr;
    QPushButton* collect_bundle_btn_ = nullptr;

    QProcess* process_ = nullptr;
    Task active_task_ = Task::None;
    QString active_title_;
};

#endif // DIAGNOSTICS_DIALOG_H
