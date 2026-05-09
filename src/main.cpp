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
            background: #050505;
            color: #F4F4F5;
            selection-background-color: #2F6BFF;
            selection-color: #FFFFFF;
        }
        QMainWindow, QDialog { background: #050505; }
        QToolBar {
            background: #050505;
            border: none;
            border-bottom: 1px solid #202020;
            spacing: 8px;
            padding: 7px 12px;
        }
        QDockWidget {
            background: #0D0D0D;
            border: none;
        }
        QGroupBox {
            color: #A1A1AA;
            font-size: 11px;
            font-weight: 600;
            border: 1px solid #27272A;
            border-radius: 8px;
            margin-top: 11px;
            padding: 17px 10px 10px 10px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 8px;
        }
        QLineEdit, QComboBox, QSpinBox, QTimeEdit {
            background: #161616;
            color: #F4F4F5;
            border: 1px solid #303033;
            border-radius: 8px;
            padding: 6px 9px;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QTimeEdit:focus {
            border: 1px solid #5B7CFA;
        }
        QPushButton {
            background: #18181B;
            color: #F4F4F5;
            border: 1px solid #303033;
            border-radius: 8px;
            padding: 6px 12px;
            font-weight: 500;
        }
        QPushButton:hover {
            background: #222225;
            border-color: #3F3F46;
        }
        QPushButton:pressed { background: #111113; }
        QCheckBox {
            color: #E4E4E7;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 14px;
            height: 14px;
            border: 1px solid #3F3F46;
            border-radius: 4px;
            background: #111113;
        }
        QCheckBox::indicator:hover {
            border-color: #5B7CFA;
        }
        QCheckBox::indicator:checked {
            background: #5B7CFA;
            border-color: #8DA2FB;
        }
        QSlider::groove:horizontal {
            height: 4px;
            background: #2A2A2D;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            width: 14px;
            margin: -5px 0;
            background: #A7B4FF;
            border: 1px solid #D9DEFF;
            border-radius: 7px;
        }
        QScrollArea, QScrollArea > QWidget > QWidget, QListView {
            background: transparent;
            border: none;
        }
        QScrollBar:vertical {
            background: #0D0D0D;
            width: 10px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3F3F46;
            border-radius: 5px;
            min-height: 28px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QToolTip {
            background: #18181B;
            color: #F4F4F5;
            border: 1px solid #3F3F46;
            padding: 4px 6px;
        }
    )");
}

void applyCodexSurfaceStyle(QApplication& app) {
    app.setStyle(QStringLiteral("Fusion"));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#050505"));
    palette.setColor(QPalette::WindowText, QColor("#F4F4F5"));
    palette.setColor(QPalette::Base, QColor("#111113"));
    palette.setColor(QPalette::AlternateBase, QColor("#18181B"));
    palette.setColor(QPalette::Text, QColor("#F4F4F5"));
    palette.setColor(QPalette::Button, QColor("#18181B"));
    palette.setColor(QPalette::ButtonText, QColor("#F4F4F5"));
    palette.setColor(QPalette::Highlight, QColor("#2F6BFF"));
    palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
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
