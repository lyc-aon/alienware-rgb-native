#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QSocketNotifier>
#include <QString>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#include "gui/main_window.h"

namespace {
int g_sig_fd[2] = {-1, -1};

QString appStyleSheet() {
    return QStringLiteral(R"(
        QWidget {
            background: #05070A;
            color: #E5E7EB;
            selection-background-color: #0E7490;
            selection-color: #F8FAFC;
        }
        QMainWindow, QDialog { background: #05070A; }
        QToolBar {
            background: #05070A;
            border: none;
            border-bottom: 1px solid #18212F;
            spacing: 8px;
            padding: 6px 10px;
        }
        QDockWidget {
            background: #080C12;
            border: none;
        }
        QGroupBox {
            color: #94A3B8;
            font-size: 11px;
            font-weight: 700;
            border: 1px solid #1B2636;
            border-radius: 6px;
            margin-top: 10px;
            padding: 16px 10px 10px 10px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 6px;
        }
        QLineEdit, QComboBox, QSpinBox, QTimeEdit {
            background: #0B0F14;
            color: #F8FAFC;
            border: 1px solid #263244;
            border-radius: 5px;
            padding: 5px 8px;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QTimeEdit:focus {
            border: 1px solid #22D3EE;
        }
        QPushButton {
            background: #111827;
            color: #E5E7EB;
            border: 1px solid #263244;
            border-radius: 6px;
            padding: 6px 12px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #18212F;
            border-color: #334155;
        }
        QPushButton:pressed { background: #0B0F14; }
        QCheckBox {
            color: #E5E7EB;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid #263244;
            border-radius: 3px;
            background: #05070A;
        }
        QCheckBox::indicator:hover {
            border-color: #22D3EE;
        }
        QCheckBox::indicator:checked {
            background: #0E7490;
            border-color: #22D3EE;
        }
        QSlider::groove:horizontal {
            height: 4px;
            background: #1B2636;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            width: 14px;
            margin: -5px 0;
            background: #22D3EE;
            border: 1px solid #67E8F9;
            border-radius: 7px;
        }
        QScrollArea, QScrollArea > QWidget > QWidget, QListView {
            background: transparent;
            border: none;
        }
        QScrollBar:vertical {
            background: #080C12;
            width: 10px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #263244;
            border-radius: 5px;
            min-height: 28px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QToolTip {
            background: #0B0F14;
            color: #F8FAFC;
            border: 1px solid #22D3EE;
            padding: 4px 6px;
        }
    )");
}

void applyCodexSurfaceStyle(QApplication& app) {
    app.setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#05070A"));
    palette.setColor(QPalette::WindowText, QColor("#E5E7EB"));
    palette.setColor(QPalette::Base, QColor("#0B0F14"));
    palette.setColor(QPalette::AlternateBase, QColor("#111827"));
    palette.setColor(QPalette::Text, QColor("#F8FAFC"));
    palette.setColor(QPalette::Button, QColor("#111827"));
    palette.setColor(QPalette::ButtonText, QColor("#E5E7EB"));
    palette.setColor(QPalette::Highlight, QColor("#0E7490"));
    palette.setColor(QPalette::HighlightedText, QColor("#F8FAFC"));
    app.setPalette(palette);
    app.setStyleSheet(appStyleSheet());
}

void termHandler(int) {
    char c = 1;
    const ssize_t ignored = ::write(g_sig_fd[0], &c, 1);
    (void)ignored;
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("alienware-rgb");
    app.setApplicationDisplayName("Alienware RGB");
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/alienware-rgb.png")));
    applyCodexSurfaceStyle(app);

    // Self-pipe trick for clean shutdown on SIGTERM/SIGINT so the main
    // window's destructor runs and zone_map gets persisted.
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sig_fd) == 0) {
        auto* notifier = new QSocketNotifier(g_sig_fd[1], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [&app] { app.quit(); });
        std::signal(SIGTERM, termHandler);
        std::signal(SIGINT, termHandler);
    }

    MainWindow window;
    window.setWindowIcon(app.windowIcon());
    window.show();

    return app.exec();
}
